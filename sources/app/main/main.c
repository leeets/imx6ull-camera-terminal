/*
 * main.c - 车载终端主入口
 *
 * 整合模块:
 *   key_manager   -> 按键事件
 *   capture       -> 摄像头采集(MJPEG)
 *   recorder      -> MJPEG/AVI 录像
 *   storage_manager -> 照片/录像文件管理 + 循环覆盖
 *   hal_fb        -> /dev/fb0 实时预览
 *   video_convert -> MJPEG -> RGB565 转换
 *
 * 按键功能:
 *   [拍照键] 短按 -> 拍照
 *   [录像键] 短按 -> 开始录像 | 长按 -> 停止 | 双击 -> 退出
 *
 * 线程架构说明：
 *   所有耗时操作都不在主线程和 V4L2 采集线程中执行。
 *   capture 模块内部有独立工作线程处理 MJPEG 解码和预览回调，
 *   recorder 模块内部有独立线程处理 SD 卡文件写入，
 *   main.c 的 on_preview_frame 回调现在运行在 capture 的工作线程中。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "key_manager.h"
#include "capture.h"
#include "recorder.h"
#include "storage_manager.h"
#include "hal_fb.h"
#include "hal_touch.h"
#include "video_convert.h"

/* ==================== 全局状态 ==================== */
static volatile int g_running = 1;

/*
 * 预览帧回调 —— 运行在 capture 模块的工作线程中（非 V4L2 采集线程）
 *
 * 因为 MJPEG 解码和 FB 写入都比较耗时，所以不能放在 V4L2 的 DQBUF 回调里。
 * capture 内部有帧队列 + 工作线程，V4L2 线程只做入队，
 * 解码 + FB 写入 + 录制入队都在独立工作线程中执行。
 *
 * frame->data 是 malloc 拷贝的独立缓冲区，用完即 free，
 * 与 V4L2 的 mmap 缓冲区无关联，可以安全持有和使用。
 */
static void on_preview_frame(const hal_camera_frame_t *frame, void *user_data) {
    (void)user_data;

    /* 静态缓存区：避免每帧重复 malloc/free RGB565 缓冲区 */
    static void *rgb_buf = NULL;
    static int rgb_buf_size = 0;

    int w = 640, h = 480;
    int need = w * h * 2;

    if (!rgb_buf || rgb_buf_size < need) {
        free(rgb_buf);
        rgb_buf = malloc(need);
        rgb_buf_size = need;
        if (!rgb_buf) return;
    }

    /* MJPEG 解码转 RGB565 */
    if (mjpeg_to_rgb565(frame->data, frame->length, rgb_buf, w, h) < 0) {
        /* 解码失败，跳过此帧显示 */
        return;
    }

    /* 写入 FrameBuffer 显示 */
    hal_fb_draw_rgb565(rgb_buf, w, h);

    /* 如果正在录像，将 MJPEG 帧入队到 recorder 队列（异步写入 SD 卡） */
    if (recorder_get_state() == RECORDER_RECORDING) {
        recorder_write_frame(frame->data, frame->length);
    }
}

/* 拍照回调 - capture_take_photo 完成后调用 */
static void on_photo_captured(const void *jpeg_data, size_t length, void *user_data) {
    (void)user_data;
    printf("[MAIN] photo captured: %zu bytes\n", length);

    if (storage_save_photo(jpeg_data, length) < 0) {
        fprintf(stderr, "[MAIN] save photo failed\n");
    }
}

/* ==================== 按键事件处理 ==================== */
void on_key_event(key_event_type_t event) {
    static int recording = 0;
    static char video_path[512];
    static int photo_pending = 0;

    switch (event) {
    case KEY_EVENT_CAPTURE:
        printf("[MAIN] 拍照\n");
        capture_take_photo(on_photo_captured, NULL);
        break;

    case KEY_EVENT_RECORD_START:
        if (!recording) {
            if (storage_alloc_path(STORAGE_TYPE_VIDEO, video_path, sizeof(video_path)) < 0) {
                fprintf(stderr, "[MAIN] alloc video path failed\n");
                break;
            }

            printf("[MAIN] 开始录像 -> %s\n", video_path);
            if (recorder_start(video_path) < 0) {
                fprintf(stderr, "[MAIN] recorder_start failed\n");
                break;
            }
            recording = 1;
        }
        break;

    case KEY_EVENT_RECORD_STOP:
        if (recording) {
            printf("[MAIN] 停止录像\n");
            recorder_stop();
            recording = 0;
        }
        break;

    case KEY_EVENT_EXIT:
        printf("[MAIN] 退出\n");
        if (recording) {
            recorder_stop();
            recording = 0;
        }
        capture_exit();
        hal_fb_exit();
        key_manager_exit();
        storage_exit();
        g_running = 0;
        break;
    }
}

/* ==================== 初始化 ==================== */
static int init_all(const char *cam_dev) {
    /* 1. 存储 */
    if (storage_init("/mnt/sd", 512UL * 1024 * 1024) < 0) {
        fprintf(stderr, "[MAIN] storage_init failed, try /tmp\n");
        storage_init("/tmp", 32UL * 1024 * 1024);
    }

    /* 2. 摄像头 */
    if (capture_init(cam_dev, 640, 480) < 0) {
        fprintf(stderr, "[MAIN] 摄像头初始化失败\n");
        return -1;
    }

    /* 3. Framebuffer */
    if (hal_fb_init() < 0) {
        fprintf(stderr, "[MAIN] fb 初始化失败，无预览\n");
    }

    /* 4. Recorder（只设参数，不启动） */
    recorder_init(640, 480, 15);

    /* 5. 按键 */
    if (key_manager_init() < 0) {
        fprintf(stderr, "[MAIN] 按键初始化失败\n");
        return -1;
    }
    key_manager_register_callback(on_key_event);

    /* 6. 启动预览流 */
    if (capture_start_preview(on_preview_frame, NULL) < 0) {
        fprintf(stderr, "[MAIN] 预览启动失败\n");
    }

    /* 7. 触摸屏 */
    if (hal_touch_init("/dev/input/event1") < 0) {
        fprintf(stderr, "[MAIN] touch init failed, UI buttons will not work via touch\n");
    }

    return 0;
}

/* ==================== 主循环 ==================== */
int main(int argc, char **argv) {
    const char *cam_dev = "/dev/video1";

    if (argc > 1) cam_dev = argv[1];

    printf("========================================\n");
    printf("  嵌入式Linux 车载终端 v2\n");
    printf("  摄像头 %s\n", cam_dev);
    printf("========================================\n");

    if (init_all(cam_dev) < 0) {
        fprintf(stderr, "[MAIN] 初始化失败\n");
        return -1;
    }

    printf("系统就绪\n");
    printf("  [拍照键] 短按 -> 拍照并存储到SD卡\n");
    printf("  [录像键] 短按 -> 开始录像 | 长按 -> 停止 | 双击 -> 退出\n");

    while (g_running) {
        key_manager_task();
    }

    printf("[MAIN] 正常退出\n");
    return 0;
}
