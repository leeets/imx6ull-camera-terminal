#include "hal_key.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <signal.h>

/*==================== 内部私有定义 ====================*/
#define KEY_DEVICE_PATH     "/dev/100ask_gpio_key"   // 驱动节点,提供两个按键

static int fd = -1;

/* 初始化函数 */
int hal_key_init(void) {
	fd = open(KEY_DEVICE_PATH, O_RDWR);	
	if(fd < 0)
	{
		perror("open file failed");
        return -1;
	}
	return 0;
}

/* 读函数 */
int hal_key_read(hal_key_event_t *ev){
	int raw;//临时
    
    if (fd < 0) 
		return -1;
    
    // 阻塞读取
    if (read(fd, &raw, 4) != 4) {
        return -1;
    }
    
    // 解析：高位是按键GPIO号，低位是电平
    ev->key_id = (raw >> 8) & 0xFF;
    ev->pressed = !(raw & 0x1);  // 低电平有效，要反转
    
    return 0;
}

/* 退出函数 */
void hal_key_exit(void) {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}
