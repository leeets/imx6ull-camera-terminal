#ifndef _CAPTURE_H
#define _CAPTURE_H

#include "hal_camera.h"

/* 拍照回调：业务层注册，拍照完成后收到 JPEG 数据 */
typedef void (*capture_photo_cb_t)(const void *jpeg_data, size_t length, void *user_data);

/* 预览帧回调：业务层注册，每帧预览数据 */
typedef void (*capture_preview_cb_t)(const hal_camera_frame_t *frame, void *user_data);

/* 接口 */
int  capture_init(const char *dev_path, int width, int height);
int  capture_start_preview(capture_preview_cb_t cb, void *user_data);
int  capture_take_photo(capture_photo_cb_t cb, void *user_data);
void capture_stop(void);
void capture_exit(void);

#endif /* _CAPTURE_H */
