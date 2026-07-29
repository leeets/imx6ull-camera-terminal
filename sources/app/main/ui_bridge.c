/**
 * ui_bridge.c - UI 事件桥接层实现
 *
 * 功能:
 *   1. 状态标签刷新：lv_timer_create 创建定时器，每秒刷新 GPS/网络/存储/录像状态
 *   2. 预览帧更新：通过 lv_async_call 从采集线程安全更新 ui_ImgPreview
 *   3. 相册界面：通过 storage_manager 提供的接口浏览/删除照片
 *
 * ui_bridge 中的函数都在 LVGL 主线程上下文中执行（定时器回调 / lv_async_call），
 * 因此可以直接调用 lv_* API，无需额外锁保护。
 */

/*********************
 *      INCLUDES
 *********************/
#include "mqtt_client.h"
#include "ui_bridge.h"
#include "lvgl/lvgl.h"
#include "ui.h"
#include "gps_daemon.h"
#include "storage_manager.h"
#include "recorder.h"
#include "ui_events.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*********************
 *      DEFINES
 *********************/

/**********************
 *  STATIC VARIABLES
 **********************/
static lv_obj_t *g_preview_img = NULL;

/* 预览帧拷贝缓冲区：独立于采集线程，避免野指针 */
#define PREVIEW_W  640
#define PREVIEW_H  480
static lv_color_t g_preview_buf[PREVIEW_W * PREVIEW_H];
static volatile int g_preview_pending = 0;

/* 相册状态 — 通过 storage_manager 接口管理 */
static char   **g_photo_paths   = NULL;
static int      g_photo_count   = 0;
static int      g_photo_current = 0;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void status_timer_cb(lv_timer_t *timer);
static void refresh_status_labels(void);
static void load_photo_list(void);
static void free_photo_list(void);
static void show_current_photo(void);

/* 预览帧更新（在 LVGL 主线程中执行）*/
static void async_update_preview(void *user_data);

/**********************
 *  状态标签刷新 -- 完成实际操作（都是直接调用lv_label_set_text来更新到UI上）
 **********************/
static void refresh_status_labels(void)
{
    /* 1. GPS 状态 */
    if (gps_is_valid()) {
        gps_data_t gps;
        char buf[64];
        if (gps_get_data(&gps) == 0) {
            snprintf(buf, sizeof(buf), "GPS: %.4f, %.4f",
                     gps.latitude, gps.longitude);
        } else {
            snprintf(buf, sizeof(buf), "GPS: --");
        }
        lv_label_set_text(ui_LabelGps, buf);
    } else {
        lv_label_set_text(ui_LabelGps, "GPS: 定位中...");
    }

    /* 2. 存储状态 */
    {
        storage_stats_t stats;
        char buf[48];
        if (storage_get_stats(&stats) == 0) {
            uint64_t used_mb = stats.total_bytes / (1024 * 1024);
            uint64_t cap_mb  = stats.capacity_bytes / (1024 * 1024);
            snprintf(buf, sizeof(buf), "存储: %llu/%llu MB",
                     (unsigned long long)used_mb,
                     (unsigned long long)cap_mb);
        } else {
            snprintf(buf, sizeof(buf), "存储: --");
        }
        lv_label_set_text(ui_LabelStorage, buf);
    }

    /* 3. 录像状态 */
    {
        const char *rec_text;
        if (recorder_get_state() == RECORDER_RECORDING)
            rec_text = "录像: ● 录制中";
        else
            rec_text = "录像: ○ 空闲";
        lv_label_set_text(ui_LabelRecStatus, rec_text);
    }

    /* 4. 网络状态（预留，当前显示占位）*/
    /* 4. 网络状态（读取 g_mqtt_connected）*/
    if (g_mqtt_connected) {
        lv_label_set_text(ui_LabelNet, "网络: ● 已连接");
    } else {
        lv_label_set_text(ui_LabelNet, "网络: ○ 断开");
    }

/**********************
 *  状态定时器回调
 **********************/
static void status_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    refresh_status_labels();
}

/**********************
 *  预览帧更新（直接在主线程中执行）
 **********************/
static void async_update_preview(void *user_data)
{
    (void)user_data;

    if (!g_preview_img) {
        g_preview_img = ui_ImgPreview;
    }

    /* 数据已在回调触发前拷贝到 g_preview_buf，直接使用 */
    lv_img_dsc_t img_dsc;
    memset(&img_dsc, 0, sizeof(img_dsc));
    img_dsc.header.always_zero = 0;
    img_dsc.header.w = PREVIEW_W;
    img_dsc.header.h = PREVIEW_H;
    img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    img_dsc.data_size = PREVIEW_W * PREVIEW_H * 2;
    img_dsc.data = (const uint8_t *)g_preview_buf;

    lv_img_set_src(g_preview_img, &img_dsc);
    g_preview_pending = 0;
}

/* 被main的on_preview_frame() 回调接口，实现用LVGL来实现帧预览： */
void ui_bridge_update_preview(const void *rgb565)
{
    /* 上次更新尚未完成则跳过，防积压 */
    if (g_preview_pending) return;

    /* 立即拷贝到自有缓冲区（采集线程中执行，但拷贝是原子的）*/
    memcpy(g_preview_buf, rgb565, PREVIEW_W * PREVIEW_H * 2);
    g_preview_pending = 1;

    /* 通过 lv_async_call 切换到主线程更新 ui_ImgPreview */
    lv_async_call(async_update_preview, NULL);
}

/**********************
 *  相册功能 — 通过 storage_manager API
 **********************/

/* 从 storage_manager 加载照片列表 */
static void load_photo_list(void)
{
    free_photo_list();

    if (storage_list_photos(&g_photo_paths, &g_photo_count) < 0) {
        g_photo_paths = NULL;
        g_photo_count = 0;
    }
    g_photo_current = 0;
}

static void free_photo_list(void)
{
    storage_free_photo_list(g_photo_paths, g_photo_count);
    g_photo_paths = NULL;
    g_photo_count = 0;
}

/* 显示当前照片 */
static void show_current_photo(void)
{
    if (g_photo_count <= 0 || !g_photo_paths || !g_photo_paths[g_photo_current]) {
        lv_img_set_src(ui_imgPhotoPreview, NULL);
        lv_label_set_text(ui_lblPhotoInfo, "无照片");
        return;
    }

    /* 从文件加载 JPEG 并设置到图像控件 */
    lv_img_set_src(ui_imgPhotoPreview, g_photo_paths[g_photo_current]);

    /* 更新照片信息标签 */
    char info[32];
    snprintf(info, sizeof(info), "%d / %d", g_photo_current + 1, g_photo_count);
    lv_label_set_text(ui_lblPhotoInfo, info);
}

void ui_bridge_album_prev(void)
{
    if (g_photo_count <= 0) return;
    g_photo_current = (g_photo_current - 1 + g_photo_count) % g_photo_count;
    show_current_photo();
}

void ui_bridge_album_next(void)
{
    if (g_photo_count <= 0) return;
    g_photo_current = (g_photo_current + 1) % g_photo_count;
    show_current_photo();
}

/* 调用storage的函数进行实际删除 */
void ui_bridge_album_delete(void)
{
    if (g_photo_count <= 0 || !g_photo_paths[g_photo_current]) return;

    /* 调用 storage_manager 删除照片 */
    if (storage_delete_photo(g_photo_paths[g_photo_current]) == 0) {
        /* 重新加载照片列表 */
        load_photo_list();
        show_current_photo();
    }
}

void ui_bridge_album_back(void)
{
    ui_bridge_show_main();
}

void ui_bridge_show_album(void)
{
    load_photo_list();
    show_current_photo();
    _ui_screen_change(&ui_ScreenAlbum, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                      ui_ScreenAlbum_screen_init);
}

void ui_bridge_show_main(void)
{
    /* 回到主界面时释放照片列表内存 */
    free_photo_list();
    _ui_screen_change(&ui_ScreenMain, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                      ui_ScreenMain_screen_init);
}

/**********************
 *  UI 桥接初始化
 **********************/
void ui_bridge_init(void)
{
    /* 创建状态刷新定时器，每秒回调一次status_timer_cb从而刷新状态 */
    lv_timer_create(status_timer_cb, 1000, NULL);

    /* 绑定 SquareLine 按钮事件回调 */
    lv_obj_add_event_cb(ui_BtnAlbum, on_btnAlbum_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_btnBack, on_btnBack_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_btnPrev, on_btnPrev_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_btnNext, on_btnNext_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_btnDelete, on_btnDelete_clicked, LV_EVENT_CLICKED, NULL);

    printf("[UI_BRIDGE] init: status timer created\n");
}
