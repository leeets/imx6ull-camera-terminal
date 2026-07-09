#ifndef _HAL_CAMERA_H
#include <stddef.h>
#define _HAL_CAMERA_H

#include <linux/videodev2.h>

/* 摄像头帧数据 */
typedef struct {
    void   *data;          /* 帧数据指针（mmap 映射地址） */
    size_t  length;        /* 数据长度（字节） */
    unsigned int index;    /* 缓冲区索引（用于 QBUF） */
    struct v4l2_buffer buf;/* V4L2 缓冲区元信息 */
} hal_camera_frame_t;

/* 摄像头参数 */
typedef struct {
    int   width;           /* 宽度（如 640） */
    int   height;          /* 高度（如 480） */
    unsigned int pixelformat; /* 像素格式（V4L2_PIX_FMT_MJPEG / YUYV） */
    int   fps;             /* 目标帧率 */
} hal_camera_params_t;

/* 回调：每采集到一帧时调用 */
typedef void (*hal_camera_callback_t)(const hal_camera_frame_t *frame, void *user_data);

/* 接口 */
int  hal_camera_init(const char *dev_path, hal_camera_params_t *params);
int  hal_camera_start(hal_camera_callback_t cb, void *user_data);
int  hal_camera_capture_one(hal_camera_frame_t *frame); /* 单帧捕获 */
void hal_camera_stop(void);
void hal_camera_exit(void);

#endif /* _HAL_CAMERA_H */
