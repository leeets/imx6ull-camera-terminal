#include <sys/mman.h>
#include "capture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static capture_preview_cb_t g_preview_cb  = NULL;
static void                *g_preview_ctx = NULL;
static int                  g_preview_started = 0;

/* 预览回调桥接：hal_camera 的帧回调 → 业务层的 preview_cb */
static void preview_bridge(const hal_camera_frame_t *frame, void *user_data) {
    (void)user_data;
    if (g_preview_cb)
        g_preview_cb(frame, g_preview_ctx);
}

/* 初始化：打开设备、设置格式，MJPEG 失败则降级 YUYV */
int capture_init(const char *dev_path, int width, int height) {
    hal_camera_params_t params;

    params.width       = width;
    params.height      = height;
    params.pixelformat = V4L2_PIX_FMT_MJPEG;
    params.fps         = 15;

    printf("[CAPTURE] init %s %dx%d MJPEG\n", dev_path, width, height);
    if (hal_camera_init(dev_path, &params) < 0) {
        printf("[CAPTURE] MJPEG failed, fallback YUYV\n");
        params.pixelformat = V4L2_PIX_FMT_YUYV;
        if (hal_camera_init(dev_path, &params) < 0) {
            fprintf(stderr, "[CAPTURE] init ALL failed\n");
            return -1;
        }
    }
    return 0;
}

/* 启动预览流 */
int capture_start_preview(capture_preview_cb_t cb, void *user_data) {
    if (g_preview_started) return 0;
    g_preview_cb  = cb;
    g_preview_ctx = user_data;
    if (hal_camera_start(preview_bridge, NULL) < 0)
        return -1;
    g_preview_started = 1;
    printf("[CAPTURE] preview started\n");
    return 0;
}

/* 拍照：停流 → 单帧 → 恢复流（如果之前是预览状态） */
int capture_take_photo(capture_photo_cb_t cb, void *user_data) {
    hal_camera_frame_t frame;
    int was_previewing = g_preview_started;

    if (was_previewing) {
        hal_camera_stop();
        g_preview_started = 0;
    }

    int ret = hal_camera_capture_one(&frame);
    if (ret == 0 && cb) {
        cb(frame.data, frame.length, user_data);
        munmap(frame.data, frame.length);
    }

    if (was_previewing) {
        hal_camera_start(preview_bridge, NULL);
        g_preview_started = 1;
    }
    return ret;
}

void capture_stop(void) {
    hal_camera_stop();
    g_preview_started = 0;
}

void capture_exit(void) {
    hal_camera_stop();
    hal_camera_exit();
    g_preview_started = 0;
}
