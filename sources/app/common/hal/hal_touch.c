/*
 * hal_touch.c - 触摸屏硬件抽象层
 *
 * 通过 input_event 结构体读取 /dev/input/eventX 触摸事件。
 * 支持 EV_ABS（USB 电容屏）和 EV_KEY（电阻屏）。
 */

#include "hal_touch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/input.h>

/* 私有全局变量 */
static int g_touch_fd = -1;
/* 默认分辨率，可通过 ioctl 或校准更新 */
static uint16_t g_screen_width  = 800;
static uint16_t g_screen_height = 480;
static bool g_nonblock = false;

/* 缓存的坐标（用于 EV_SYN 同步事件）*/
static uint16_t g_last_x = 0;
static uint16_t g_last_y = 0;

int hal_touch_init(const char *dev_path)
{
    if (!dev_path) {
        fprintf(stderr, "[HAL_TOUCH] 设备路径为 NULL\n");//检查路径
        return -1;
    }

    g_touch_fd = open(dev_path, O_RDWR | O_NONBLOCK);//打开设备，读写 非阻塞
    if (g_touch_fd < 0) {
        fprintf(stderr, "[HAL_TOUCH] 打开 %s 失败: %s\n",
                dev_path, strerror(errno));
        return -1;
    }

    printf("[HAL_TOUCH] 已打开 %s (fd=%d)\n", dev_path, g_touch_fd);
    return 0;
}

//读一次事件，之后被lv_port_indev集成调用，来读取触摸
int hal_touch_read(hal_touch_event_t *out)
{
    struct input_event ev;//临时 读事件值（所有的输入设备都是这样的数据格式，Linux input 子系统的核心设计）
    ssize_t n;
    static int touch_state = 0; /* 0=抬起, 1=按下 */

    if (!out || g_touch_fd < 0)
        return -1;

    memset(out, 0, sizeof(*out));//内存设置，清空event结构体
    out->action = HAL_TOUCH_NONE;

    n = read(g_touch_fd, &ev, sizeof(ev));	//读到ev--非阻塞的，无数据会直接返回-1，一定要判断返回值，及时打断程序
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;   /* 非阻塞模式下无事件 */
        fprintf(stderr, "[HAL_TOUCH] 读取错误: %s\n", strerror(errno));
        return -1;
    }

    if (n != sizeof(ev))
        return 0;   /* 不完整事件，跳过 */

    /* 解析 input_event内容 */
    switch (ev.type) {
    case EV_ABS:
        /* USB 电容触摸屏 */
        switch (ev.code) {
        case ABS_X:
            g_last_x = (uint16_t)(ev.value * g_screen_width / 32767);
            break;
        case ABS_Y:
            g_last_y = (uint16_t)(ev.value * g_screen_height / 32767);
            break;
        case ABS_PRESSURE:
            if (ev.value > 0 && !touch_state) {		//按下（状态没有但事件有）
                touch_state = 1;						//更新按下状态1
                out->action = HAL_TOUCH_PRESS;			//类型是按下
            } else if (ev.value == 0 && touch_state) {		//释放（状态有但事件没有）
                touch_state = 0;						//更新按下状态0
                out->action = HAL_TOUCH_RELEASE;
            } else {
                out->action = HAL_TOUCH_MOVE;
            }
            break;
        }
        break;

    case EV_KEY:
        /* 电阻触摸屏 */
        if (ev.code == BTN_TOUCH) {
            if (ev.value == 1 && !touch_state) {
                touch_state = 1;
                out->action = HAL_TOUCH_PRESS;
            } else if (ev.value == 0 && touch_state) {
                touch_state = 0;
                out->action = HAL_TOUCH_RELEASE;
            }
        }
        break;

    case EV_SYN:
        /* 同步事件 - 报告累积的坐标 */
        if (touch_state) {
            out->x = g_last_x;
            out->y = g_last_y;
            if (out->action == HAL_TOUCH_NONE)
                out->action = HAL_TOUCH_MOVE;
        }
        break;

    default:
        return 0;   /* 忽略其他事件类型 */
    }

    out->x = g_last_x;
    out->y = g_last_y;		//更新坐标

    return (out->action != HAL_TOUCH_NONE) ? 1 : 0;//类型不是NONE就返回1
}

/*切换触摸屏的"读取模式" 阻塞与否*/
void hal_touch_set_nonblock(bool enable)
{
    if (g_touch_fd < 0) return;

    int flags = fcntl(g_touch_fd, F_GETFL, 0);
    if (flags < 0) return;

    if (enable)
        fcntl(g_touch_fd, F_SETFL, flags | O_NONBLOCK);
    else
        fcntl(g_touch_fd, F_SETFL, flags & ~O_NONBLOCK);

    g_nonblock = enable;
}

/*查询屏幕分辨率 本模块设置分辨率 */
int hal_touch_get_resolution(uint16_t *width, uint16_t *height)
{
    if (!width || !height) return -1;
    *width  = g_screen_width;
    *height = g_screen_height;
    return 0;
}

/*释放资源*/
void hal_touch_exit(void)
{
    if (g_touch_fd >= 0) {
        close(g_touch_fd);
        g_touch_fd = -1;
        printf("[HAL_TOUCH] 已关闭\n");
    }
}