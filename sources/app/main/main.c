#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "key_manager.h"
#include "capture.h"

static volatile int g_running = 1;

void on_key_event(key_event_type_t event) {
    switch (event) {
    case KEY_EVENT_CAPTURE:
        printf("[APP] 拍照!\n");
        capture_take_photo(NULL, NULL);
        break;
    case KEY_EVENT_RECORD_START:
        printf("[APP] 开始录像\n");
        break;
    case KEY_EVENT_RECORD_STOP:
        printf("[APP] 停止录像\n");
        break;
    case KEY_EVENT_EXIT:
        printf("[APP] 退出\n");
        capture_exit();
        key_manager_exit();
        g_running = 0;
        break;
    }
}

int main(int argc, char **argv) {
    const char *cam_dev = "/dev/video0";

    if (argc > 1) cam_dev = argv[1];

    /* 初始化摄像头 */
    if (capture_init(cam_dev, 640, 480) < 0) {
        fprintf(stderr, "摄像头初始化失败，继续使用按键\n");
    }

    /* 初始化按键 */
    if (key_manager_init() < 0) {
        fprintf(stderr, "按键初始化失败\n");
        return -1;
    }
    key_manager_register_callback(on_key_event);

    printf("车载终端启动\n");
    printf("  [拍照键] 短按 → 拍照\n");
    printf("  [录像键] 短按 → 录像开始 | 长按 → 停止 | 双击 → 退出\n");

    while (g_running) {
        key_manager_task();
    }

    key_manager_exit();
    capture_exit();
    return 0;
}
