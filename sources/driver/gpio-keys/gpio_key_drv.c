#include <linux/module.h>
#include <linux/poll.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/tty.h>
#include <linux/kmod.h>
#include <linux/gfp.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/fcntl.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <asm/current.h>


struct gpio_key{
	int gpio;
	struct gpio_desc *gpiod;
	int flag;
	int irq;
	int last_val;
	int irq_enabled;
	struct timer_list key_timer;
	struct tasklet_struct tasklet;
	struct work_struct work;//工作队列
} ;

static struct gpio_key *gpio_keys_100ask;//一些按键（2个）

/* 主设备号                                                                 */
static int major = 0;
static struct class *gpio_key_class;

/* 环形缓冲区 按键数据 */
#define BUF_LEN 128
static int g_keys[BUF_LEN];
static int r, w;

struct fasync_struct *button_fasync;
/*
*fd，
*/

#define NEXT_POS(x) ((x+1) % BUF_LEN)//环形next
//环形缓冲区API：
static int is_key_buf_empty(void)
{
	return (r == w);
}

static int is_key_buf_full(void)
{
	return (r == NEXT_POS(w));
}

static void put_key(int key)
{
	if (!is_key_buf_full())
	{
		g_keys[w] = key;
		w = NEXT_POS(w);
	}
}

static int get_key(void)
{
	int key = 0;
	if (!is_key_buf_empty())
	{
		key = g_keys[r];
		r = NEXT_POS(r);
	}
	return key;
}


static DECLARE_WAIT_QUEUE_HEAD(gpio_key_wait);//队列头

/*定时器函数，完成得到按键值，唤醒读函数的wait并异步通知*/
static void key_timer_expire(unsigned long data)
{
	struct gpio_key *gpio_key = (struct gpio_key *)data;
	int val;
	int key;
	// 使用isr中保存的值，而不是重新读取
	val = gpio_key->last_val;

	printk("key_timer_expire key %d %d\n", gpio_key->gpio, val);
	key = (gpio_key->gpio << 8) | val;
	put_key(key);
	wake_up_interruptible(&gpio_key_wait);
	kill_fasync(&button_fasync, SIGIO, POLL_IN);
}

static void key_tasklet_func(unsigned long data)
{
	/* data ==> gpio */
	struct gpio_key *gpio_key = (struct gpio_key *)data;
	int val;

	val = gpiod_get_value(gpio_key->gpiod);


	printk("key_tasklet_func key %d %d\n", gpio_key->gpio, val);
}

/*工作队列的函数，根据gpio_key的work地址得到结构体地址并拿到其中的按键值*/
static void key_work_func(struct work_struct *work)
{
	struct gpio_key *gpio_key = container_of(work, struct gpio_key, work);
	int val;

	val = gpiod_get_value(gpio_key->gpiod);

	printk("key_work_func: the process is %s pid %d\n",current->comm, current->pid);	
	printk("key_work_func key %d %d\n", gpio_key->gpio, val);
}

/* 实现对应的open/read/write等函数，填入file_operations结构体                   */
static ssize_t gpio_key_drv_read (struct file *file, char __user *buf, size_t size, loff_t *offset)
{
	//printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
	int err;
	int key;

	if (is_key_buf_empty() && (file->f_flags & O_NONBLOCK))
		return -EAGAIN;
	
	wait_event_interruptible(gpio_key_wait, !is_key_buf_empty());//阻塞，等待唤醒
	key = get_key();
	err = copy_to_user(buf, &key, 4);
	
	return 4;
}

static unsigned int gpio_key_drv_poll(struct file *fp, poll_table * wait)
{
	printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
	poll_wait(fp, &gpio_key_wait, wait);
	return is_key_buf_empty() ? 0 : POLLIN | POLLRDNORM;//0或者poll
}

static int gpio_key_drv_fasync(int fd, struct file *file, int on)
{
	if (fasync_helper(fd, file, on, &button_fasync) >= 0)
		return 0;
	else
		return -EIO;
}


/* 定义自己的file_operations结构体                 */
static struct file_operations gpio_key_drv = {
	.owner	 = THIS_MODULE,
	.read    = gpio_key_drv_read,
	.poll    = gpio_key_drv_poll,
	.fasync  = gpio_key_drv_fasync,
};

//中断函数：mod_timer设置超时时间，schedule_work调度工作work（即用即弃）
static irqreturn_t gpio_key_isr(int irq, void *dev_id)
{
	struct gpio_key *gpio_key = dev_id;
	int val;//保存按键值
	gpio_key->last_val = gpiod_get_value(gpio_key->gpiod);//先读出来按键值
	
	//printk("gpio_key_isr key %d irq happened\n", gpio_key->gpio);
	tasklet_schedule(&gpio_key->tasklet);
	mod_timer(&gpio_key->key_timer, jiffies + HZ/50);
	schedule_work(&gpio_key->work);
	return IRQ_HANDLED;
}

/* 1. 从platform_device获得GPIO
 * 2. gpio=>irq
 * 3. request_irq
 */
static int gpio_key_probe(struct platform_device *pdev)
{
	printk("gpio_key_probe: ENTER!!!\n");
	int err;
	struct device_node *node = pdev->dev.of_node;//设备节点
	int count;
	int i;
	enum of_gpio_flags flag;
		
	printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);

	count = of_gpio_count(node);//设备个数
	if (!count)
	{
		printk("%s %s line %d, there isn't any gpio available\n", __FILE__, __FUNCTION__, __LINE__);
		return -1;
	}
	//申请内核空间内存，count个struct gpio_key大小
	gpio_keys_100ask = kzalloc(sizeof(struct gpio_key) * count, GFP_KERNEL);
	for (i = 0; i < count; i++)//逐设备进行初始化内容：
	{		
		gpio_keys_100ask[i].gpio = of_get_gpio_flags(node, i, &flag);
		if (gpio_keys_100ask[i].gpio < 0)
		{
			printk("%s %s line %d, of_get_gpio_flags fail\n", __FILE__, __FUNCTION__, __LINE__);
			return -1;
		}
		gpio_keys_100ask[i].gpiod = gpio_to_desc(gpio_keys_100ask[i].gpio);
		gpio_keys_100ask[i].last_val = gpiod_get_value(gpio_keys_100ask[i].gpiod);
		gpio_keys_100ask[i].flag = flag & OF_GPIO_ACTIVE_LOW;
		gpio_keys_100ask[i].irq  = gpio_to_irq(gpio_keys_100ask[i].gpio);

		//setup_timer(&gpio_keys_100ask[i].key_timer, key_timer_expire, &gpio_keys_100ask[i]);
		setup_timer(&gpio_keys_100ask[i].key_timer, key_timer_expire, (unsigned long)&gpio_keys_100ask[i]);//初始
		gpio_keys_100ask[i].key_timer.expires = ~0;

		tasklet_init(&gpio_keys_100ask[i].tasklet, key_tasklet_func, (unsigned long)&gpio_keys_100ask[i]);
		//work初始化：
		INIT_WORK(&gpio_keys_100ask[i].work, key_work_func);
	}

	for (i = 0; i < count; i++)//对每个设备申请中断，函数一样
	{
		err = request_irq(gpio_keys_100ask[i].irq, gpio_key_isr, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "100ask_gpio_key", &gpio_keys_100ask[i]);
	}

	/* 注册file_operations，在板子自动创建设备节点 	*/
	major = register_chrdev(0, "100ask_gpio_key", &gpio_key_drv);  /* /dev/gpio_key */

	gpio_key_class = class_create(THIS_MODULE, "100ask_gpio_key_class");
	if (IS_ERR(gpio_key_class)) {
		printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
		unregister_chrdev(major, "100ask_gpio_key");
		return PTR_ERR(gpio_key_class);
	}

	device_create(gpio_key_class, NULL, MKDEV(major, 0), NULL, "100ask_gpio_key"); /* /dev/100ask_gpio_key */
        
    return 0;
    
}

static int gpio_key_remove(struct platform_device *pdev)
{/*反过来做，销毁设备，类，注销驱动，irq/timer/tasklet销毁，释放struct内存*/
	//int err;
	struct device_node *node = pdev->dev.of_node;
	int count;
	int i;

	device_destroy(gpio_key_class, MKDEV(major, 0));
	class_destroy(gpio_key_class);
	unregister_chrdev(major, "100ask_gpio_key");

	count = of_gpio_count(node);
	for (i = 0; i < count; i++)
	{
		free_irq(gpio_keys_100ask[i].irq, &gpio_keys_100ask[i]);
		del_timer(&gpio_keys_100ask[i].key_timer);
		tasklet_kill(&gpio_keys_100ask[i].tasklet);
	}
	kfree(gpio_keys_100ask);
    return 0;
}


static const struct of_device_id ask100_keys[] = {
    { .compatible = "gpio-keys" },
    { },
};

/* 1. 定义platform_driver */
static struct platform_driver gpio_keys_driver = {
    .probe      = gpio_key_probe,
    .remove     = gpio_key_remove,
    .driver     = {
        .name   = "100ask_gpio_key",
        .of_match_table = ask100_keys,
    },
};

/* 2. 在入口函数注册platform_driver */
static int __init gpio_key_init(void)
{
    int err;
    
	printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
	
    err = platform_driver_register(&gpio_keys_driver); 
	
	return err;
}

/* 3. 有入口函数就应该有出口函数：卸载驱动程序时，就会去调用这个出口函数
 *     卸载platform_driver
 */
static void __exit gpio_key_exit(void)
{
	printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);

    platform_driver_unregister(&gpio_keys_driver);
}


/* 7. 其他完善：提供设备信息，自动创建设备节点                                     */

module_init(gpio_key_init);
module_exit(gpio_key_exit);

MODULE_LICENSE("GPL");


