#ifndef _HAL_TOUCH_H
#define _HAL_TOUCH_H

#include <stdint.h>
#include <stdbool.h>

/*
 * hal_touch - 触摸屏硬件抽象层
 *
 * 读取 /dev/input/eventX 触摸事件，提供坐标+按下/抬起状态
 * 供 lv_port_indev.c 作为 LVGL 触摸输入驱动调用
 */

/* 触摸事件类型 */
typedef enum {
    HAL_TOUCH_NONE = 0,
    HAL_TOUCH_PRESS,       /* 按下 */
    HAL_TOUCH_RELEASE,     /* 抬起 */
    HAL_TOUCH_MOVE,        /* 滑动 */
} hal_touch_action_t;

/* 触摸事件数据结构 */
typedef struct {
    hal_touch_action_t action;   /* 事件类型 */
    uint16_t x;                  /* X 坐标（像素）*/
    uint16_t y;                  /* Y 坐标（像素）*/
    uint8_t  pressure;           /* 压力值（0-255）*/
} hal_touch_event_t;

/*
 * 打开触摸屏设备
 * dev_path: 设备节点路径，如 "/dev/input/event1"
 * 返回 0 成功，-1 失败
 */
int hal_touch_init(const char *dev_path);

/*
 * 读取一个触摸事件（阻塞/非阻塞取决于 fd 模式）
 * out: 输出触摸事件结构体
 * 返回 1 成功，0 无事件（非阻塞模式），-1 错误
 */
int hal_touch_read(hal_touch_event_t *out);

/*
 * 设置非阻塞模式
 */
void hal_touch_set_nonblock(bool enable);

/*
 * 获取触摸屏分辨率（用于坐标归一化）
 */
int hal_touch_get_resolution(uint16_t *width, uint16_t *height);

/*
 * 释放触摸屏资源
 */
void hal_touch_exit(void);

#endif /* _HAL_TOUCH_H */