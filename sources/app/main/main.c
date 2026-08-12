/*
 * main.c - 车载终端主入口
 *
 * 整合模块:
 *   key_manager   -> 按键事件
 *   capture       -> 摄像头采集 (MJPEG)
 *   recorder      -> MJPEG/AVI 录像
 *   storage_manager -> 照片/录像文件管理 + 循环覆盖
 *   hal_fb        -> /dev/fb0 实时预览
 *   video_convert -> MJPEG -> RGB565 转换
 *   mqtt_client   -> MQTT 网络通信
 *
 * 按键功能:
 *   [拍照键] 短按 -> 拍照
 *   [录像键] 短按 -> 开始录像 | 长按 -> 停止 | 双击 -> 退出
 *
 * 线程架构说明:
 *   所有耗时操作都不在主线程和 V4L2 采集线程中执行。
 *   capture 模块内部有独立工作线程处理 MJPEG 解码和预览回调，
 *   recorder 模块内部有独立线程处理 SD 卡文件写入，
 *   main.c 的 on_preview_frame 回调现在运行在 capture 的工作线程中。
 *
 * UI 交互:
 *   实体按键（2个GPIO按键）: 拍照、录像、停止录像、退出程序
 *   屏幕虚拟按键: 相册入口、照片翻页、删除、返回（由 LVGL 触摸事件驱动）
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
#include "gps_daemon.h"
#include "video_convert.h"
#include "lvgl/lvgl.h"
#include "ui.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "ui_bridge.h"
#include "mqtt_client.h"

/* ==================== 全局状态 ==================== */
static volatile int g_running = 1;

/*
 * 预览帧回调 — 运行在 capture 模块的工作线程中（非 V4L2 采集线程）
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
    printf("预览帧操作开始\n");
    /* 静态缓冲区：避免每帧重复 malloc/free RGB565 缓冲区 */
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
     /* 如果正在录像，将 MJPEG 帧入队列到 recorder 队列（异步写 SD 卡）*/
    if (recorder_get_state() == RECORDER_RECORDING) {
       recorder_write_frame(frame->data, frame->length);
    }
    /* MJPEG 解码转 RGB565 */
    int ret = mjpeg_to_rgb565(frame->data, frame->length, rgb_buf, w, h);
    printf("mjpeg_to_rgb565的返回值 = %d\n", ret);
    if (ret < 0) {
        /* 解码失败，跳过此帧显示 */
        return;
    }

    /* 通过 LVGL的ui_bridge 异步更新 LVGL 图像控件（采集线程 → 主线程 lv_async_call）*/
    ui_bridge_update_preview(rgb_buf);

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
        gps_exit();
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

    /* 3. Framebuffer - LVGL 显示后端依赖 hal_fb */
    if (hal_fb_init() < 0) {
        fprintf(stderr, "[MAIN] fb 初始化失败，无预览\n");
    }

    /* 4. LVGL UI 初始化 */
    lv_init();
    lv_port_disp_init();	//显示移植功能 （设置屏幕参数 + 写disp_flush刷整个屏幕 
    lv_port_indev_init();	//输入移植功能  (设置类型参数 + pointer_read读触摸屏按下事件交给LVGL)
    ui_init();				//初始化UI显示
    ui_bridge_init();		//UI 桥接初始化-注册按钮事件回调

    /* 5. Recorder（只设参数，不启动）*/
    recorder_init(640, 480, 15);

    /* 6. 按键 */
    if (key_manager_init() < 0) {
        fprintf(stderr, "[MAIN] 按键初始化失败\n");
        return -1;
    }
    key_manager_register_callback(on_key_event);

    /* 7. 启动预览流 */
    if (capture_start_preview(on_preview_frame, NULL) < 0) {
        fprintf(stderr, "[MAIN] 预览启动失败\n");
    }

    /* 8. 触摸屏 */
    if (hal_touch_init("/dev/input/event0") < 0) {
        fprintf(stderr, "[MAIN] touch init failed, UI buttons will not work via touch\n");
    }

    /* 9. GPS 定位模块 */
    if (gps_init(NULL) < 0) {
        fprintf(stderr, "[MAIN] GPS 初始化失败，继续运行\n");
    }

    /* 10. MQTT 客户端 */
    if (mqtt_init("192.168.1.100", 1883, "imx6ull_cam") == 0) {
        if (mqtt_connect() == 0) {
            printf("[MAIN] MQTT 已连接\n");
        }
    } else {
        fprintf(stderr, "[MAIN] MQTT 初始化失败，继续运行\n");
    }


    return 0;
}

/* ==================== 主循环 ==================== */
int main(int argc, char **argv) {
    const char *cam_dev = "/dev/video1";

    if (argc > 1) cam_dev = argv[1];

    printf("========================================\n");
    printf("  嵌入式Linux 车载终端 v2\n");
    printf("  摄像头: %s\n", cam_dev);
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
        mqtt_loop();
        lv_tick_inc(5);      // ← 喂 LVGL 系统时钟（每 5ms）
        lv_timer_handler();
        usleep(5000);

        /* 每 100 轮打印一次 GPS 定位状态 */
        {
            static int gps_print_counter = 0;
            if (++gps_print_counter >= 100) {
                gps_print_counter = 0;
                if (gps_is_valid()) {
                    gps_data_t gps;
                    if (gps_get_data(&gps) == 0) {
                        printf("[GPS] %02d:%02d:%02d UTC, lat=%.6f lon=%.6f alt=%.1fm sats=%d spd=%.1fkn\n",
                               gps.hour, gps.minute, gps.second,
                               gps.latitude, gps.longitude,
                               gps.altitude, gps.satellites, gps.speed);
                    }
                } else {
                    printf("[GPS] 定位中...\n");
                }
            }
        }
    }

    printf("[MAIN] 正常退出\n");
    return 0;
}
