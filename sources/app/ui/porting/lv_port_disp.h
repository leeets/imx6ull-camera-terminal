#ifndef _LV_PORT_DISP_H
#define _LV_PORT_DISP_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * lv_port_disp.h - LVGL 显示移植层
 *
 * 使用 hal_fb 驱动 /dev/fb0 作为 LVGL 显示后端。
 * 800x480 RGB565 全屏显示。
 */

/*********************
 * 初始化
 *********************/
void lv_port_disp_init(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _LV_PORT_DISP_H */
