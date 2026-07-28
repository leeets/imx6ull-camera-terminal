#ifndef _LV_PORT_INDEV_H
#define _LV_PORT_INDEV_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * lv_port_indev.h - LVGL 输入移植层
 *
 * 仅提供触摸屏 (POINTER) 输入方式。
 * 物理按键由 main.c 通过 key_manager 回调直接处理，
 * 不再映射为 LVGL 编码器事件。
 */

/*********************
 * 初始化
 *********************/
void lv_port_indev_init(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /* _LV_PORT_INDEV_H */
