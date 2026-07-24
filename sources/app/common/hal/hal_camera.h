#ifndef _HAL_CAMERA_H
#define _HAL_CAMERA_H

#include <stddef.h>
#include <linux/videodev2.h>

/* 摄像头 帧数据 */
typedef struct {
    void   *data;           /* 帧数据指针（mmap 映射地址） */
    size_t  length;         /* 数据长度（字节） */
    unsigned int index;     /* 缓冲区索引（用于 QBUF） */
    struct v4l2_buffer buf; /* V4L2 缓冲区元信息 */
} hal_camera_frame_t;

/* 摄像头参数 */
typedef struct {
    int   width;
    int   height;
    unsigned int pixelformat; /* V4L2_PIX_FMT_MJPEG / YUYV */
    int   fps;
} hal_camera_params_t;

/* 每帧回调 */
typedef void (*hal_camera_callback_t)(const hal_camera_frame_t *frame, void *user_data);

int  hal_camera_init(const char *dev_path, hal_camera_params_t *params);
int  hal_camera_start(hal_camera_callback_t cb, void *user_data);
int  hal_camera_capture_one(hal_camera_frame_t *frame);
void hal_camera_stop(void);
void hal_camera_exit(void);

#endif /* _HAL_CAMERA_H */
