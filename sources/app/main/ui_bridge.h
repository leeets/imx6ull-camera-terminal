/*
 * ui_bridge.h - UI 事件桥接层
 *
 * 职责:
 *   1. 状态标签刷新定时器（GPS/网络/存储/录像）
 *   2. 预览帧更新（从采集线程通过 lv_async_call 调用）
 *   3. 相册界面导航（上一张/下一张/删除/返回）
 *
 * 使用方式:
 *   在 main.c 中调用 ui_bridge_init() 启动状态刷新定时器。
 */

#ifndef _UI_BRIDGE_H
#define _UI_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "capture.h"          /* capture_preview_cb_t 类型、提供capture_stop等函数 */

/*********************
 * 初始化
 *********************/
void ui_bridge_init(void);

/* 把on_preview_frame回调注册进来（解决 on_preview_frame 访问问题） */
void ui_bridge_set_preview_cb(capture_preview_cb_t cb);

/*********************
 * 实现无拷贝 更新预览帧
 *********************/
uint8_t *ui_bridge_preview_begin(int *w, int *h);
void ui_bridge_preview_commit(void);


/*********************
 * 预览帧更新回调（可在非主线程安全调用）
 *
 * rgb565: RGB565 像素数据 (800*480*2 字节)
 * 内部通过 lv_async_call 切换到主线程更新 UI
 *********************/
void ui_bridge_update_preview(const void *rgb565);


/*********************
 * 相册导航接口
 *********************/
void ui_bridge_album_prev(void);
void ui_bridge_album_next(void);
void ui_bridge_album_delete(void);
void ui_bridge_album_back(void);

/* 切换到相册界面 */
void ui_bridge_show_album(void);

/* 切换到主界面 */
void ui_bridge_show_main(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _UI_BRIDGE_H */
