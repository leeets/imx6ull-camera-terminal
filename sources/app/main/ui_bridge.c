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
#include <setjmp.h>
#include <stdint.h>
#include <jpeglib.h>

#include <pthread.h>


/*********************
 *      DEFINES
 *********************/

/**********************
 *  STATIC VARIABLES
 **********************/
static lv_obj_t *g_preview_img = NULL;

/* 预览双缓冲：采集线程解码直写“非显示”缓冲，主线程只换指针 */
#define PREVIEW_W  320
#define PREVIEW_H  240		//预览帧改成320*240新尺寸，会减小延迟。后续的zoom、写帧等都不用改，自适应尺寸
#define PREVIEW_BYTES (PREVIEW_W * PREVIEW_H * 4)

static capture_preview_cb_t g_preview_cb = NULL;/* 回调main的on_preview_frame用 */
static lv_color_t g_preview_buf[2][PREVIEW_W * PREVIEW_H];
static pthread_mutex_t g_preview_mtx = PTHREAD_MUTEX_INITIALIZER;
static int  g_preview_show = 0;              /* 当前给 LVGL 显示的缓冲索引 */
static volatile int g_preview_pending = 0;   /* 1 = 有已写好的帧等主线程切换 */

/* 相册状态 — 通过 storage_manager 接口管理 */
static char   **g_photo_paths   = NULL;
static int      g_photo_count   = 0;
static int      g_photo_current = 0;

/* 相册预览解码（修复：LVGL 8.3 未开启 FS/JPEG 解码，文件路径无法显示照片，
 * 改用 libjpeg 解码成 32bpp 缓冲区后以 LV_IMG_SRC_VARIABLE 方式显示）。
 * 注意：LVGL 只保存图片数据指针而不拷贝，因此这两个对象必须常驻程序生命周期，
 * 不能是栈上局部变量，也不能在翻页时提前释放。 */
static lv_img_dsc_t g_photo_dsc;
static uint8_t     *g_photo_buf = NULL;
static size_t       g_photo_buf_cap = 0;

/**********************
 *  STATIC PROTOTYPES 前向声明，先声明在使用！
 **********************/
static void status_timer_cb(lv_timer_t *timer);
static void refresh_status_labels(void);
static void load_photo_list(void);
static void free_photo_list(void);
static void show_current_photo(void);
static int  decode_photo_to_dsc(const char *path);

/* 注册回调，解决 on_preview_frame 访问问题 */
void ui_bridge_set_preview_cb(capture_preview_cb_t cb)
{
    g_preview_cb = cb;
}

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
        lv_label_set_text(ui_LabelGps, "GPS: locating...");
    }

    /* 2. 存储状态 */
    {
        storage_stats_t stats;
        char buf[48];
        if (storage_get_stats(&stats) == 0) {
            uint64_t used_mb = stats.total_bytes / (1024 * 1024);
            uint64_t cap_mb  = stats.capacity_bytes / (1024 * 1024);
            snprintf(buf, sizeof(buf), "storage: %llu/%llu MB",
                     (unsigned long long)used_mb,
                     (unsigned long long)cap_mb);
        } else {
            snprintf(buf, sizeof(buf), "storage: --");
        }
        lv_label_set_text(ui_LabelStorage, buf);
    }

    /* 3. 录像状态 */
    {
        const char *rec_text;
        if (recorder_get_state() == RECORDER_RECORDING)
            rec_text = "recorder: recording";
        else
            rec_text = "recorder: idle";
        lv_label_set_text(ui_LabelRecStatus, rec_text);
    }

    /* 4. 网络状态（预留，当前显示占位）*/
    /* 4. 网络状态（读取 g_mqtt_connected）*/
    if (g_mqtt_connected) {
        lv_label_set_text(ui_LabelNet, "net: disconnected");
    } else {
        lv_label_set_text(ui_LabelNet, "net: connected");
    }
}

/**********************
 *  状态定时器回调
 **********************/
static void status_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    refresh_status_labels();
}

/* 
* 修改为双缓冲 + 锁：增加ui_bridge_preview_begin/commit函数，更改async_update_preview更新逻辑 
* 目的：
* 消除每帧 307,200 次的 RGB565→ARGB 逐像素转换和中间缓冲拷贝，让采集线程只解码一次、主线程只换指针，降低预览延迟和 CPU 占用
*/

/* 采集线程调用begin给缓冲：返回一块“空闲”缓冲供解码直写（无拷贝），写满后再更新预览帧 commit 通知完成*/
uint8_t *ui_bridge_preview_begin(int *w, int *h)
{
    if (g_preview_pending) return NULL;      /* 上一帧还没显示，丢帧 */

    pthread_mutex_lock(&g_preview_mtx);
    uint8_t *buf = (uint8_t *)g_preview_buf[1 - g_preview_show];
    pthread_mutex_unlock(&g_preview_mtx);

    if (w) *w = PREVIEW_W;
    if (h) *h = PREVIEW_H;
    return buf;
}

void ui_bridge_preview_commit(void)
{
    g_preview_pending = 1;
    lv_async_call(async_update_preview, NULL);
}



/**********************
 *  预览帧更新（直接在主线程中执行）
 *  修改1：摄像头的帧尺寸是640x480，需要让 LVGL 缩放铺满预览帧
 *  修改2：主线程（lv_async_call 上下文）：换指针 + 无效化，无数据拷贝
 **********************/
static void async_update_preview(void *user_data)
{
    (void)user_data;

    if (!g_preview_img) {
        g_preview_img = ui_ImgPreview;
    }

    /* 数据已在回调触发前拷贝到 g_preview_buf，直接使用。
     * 修复：LVGL 只保存图像描述符指针而不拷贝，必须 static 常驻，
     * 否则函数返回后指针悬垂，预览会花屏或消失。 */
    static lv_img_dsc_t img_dsc;
    memset(&img_dsc, 0, sizeof(img_dsc));
    img_dsc.header.always_zero = 0;
    img_dsc.header.w = PREVIEW_W;
    img_dsc.header.h = PREVIEW_H;
    img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    img_dsc.data_size = PREVIEW_BYTES;  
    img_dsc.data = (const uint8_t *)g_preview_buf;

	//
	pthread_mutex_lock(&g_preview_mtx);
    g_preview_show = 1 - g_preview_show;     /* 换到刚写完的缓冲 */
    img_dsc.data = (const uint8_t *)g_preview_buf[g_preview_show];
    pthread_mutex_unlock(&g_preview_mtx);
    
    //lv_img_set_zoom(g_preview_img, 256 * LV_HOR_RES_MAX / PREVIEW_W); //缩放640×480 放大为 1024×768，铺满屏幕(增大延迟)
    lv_img_set_src(g_preview_img, &img_dsc);
    g_preview_pending = 0;
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

/* ==================== 相册照片 JPEG 解码（修复） ==================== */
/* libjpeg 错误处理：出错时 longjmp 回到解码函数统一出口释放资源，
 * 避免在错误路径上泄漏文件句柄和解压对象（遵循项目规范）。 */
typedef struct {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buf;
} ui_bridge_jpeg_err_t;

static void ui_bridge_jpeg_error_exit(j_common_ptr cinfo)
{
    ui_bridge_jpeg_err_t *err = (ui_bridge_jpeg_err_t *)cinfo->err;
    (*cinfo->err->output_message)(cinfo);
    longjmp(err->setjmp_buf, 1);
}

/* 解码 JPEG 文件到 32bpp（内存序 B,G,R,A）缓冲区并填充 g_photo_dsc。
 * 返回 0 成功，-1 失败。失败原因：文件打不开、JPEG 损坏、内存不足。 */
static int decode_photo_to_dsc(const char *path)
{
    FILE *fp = NULL;
    struct jpeg_decompress_struct cinfo;
    ui_bridge_jpeg_err_t jerr;
    unsigned char *row_rgb = NULL;
    int ret = -1;

    if (!path) return -1;

    fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "[UI_BRIDGE] 打开照片失败: %s\n", path);
        return -1;
    }

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = ui_bridge_jpeg_error_exit;
    if (setjmp(jerr.setjmp_buf))
        goto out;   /* 解码过程出错，跳过 finish，直接销毁解压对象 */

    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, fp);

    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK)
        goto out;   /* 头解析失败，未开始解压，不能调用 finish */

    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);

    {
        int w = (int)cinfo.output_width;
        int h = (int)cinfo.output_height;
        size_t need;

        if (w <= 0 || h <= 0)
            goto out_dec;

        need = (size_t)w * h * 4;   /* LV_COLOR_DEPTH=32，每像素 4 字节 */
        if (need > g_photo_buf_cap) {
            uint8_t *nb = realloc(g_photo_buf, need);
            if (!nb) goto out_dec;   /* realloc 失败时旧缓冲仍有效 */
            g_photo_buf = nb;
            g_photo_buf_cap = need;
        }

        row_rgb = malloc((size_t)w * cinfo.output_components);
        if (!row_rgb) goto out_dec;

        while (cinfo.output_scanline < cinfo.output_height) {
            unsigned char *rows[1] = { row_rgb };
            jpeg_read_scanlines(&cinfo, rows, 1);

            /* JCS_RGB 每像素 3 字节，转换为 LVGL 32bpp 的 B,G,R,A 排列 */
            uint8_t *dst = g_photo_buf + (size_t)(cinfo.output_scanline - 1) * w * 4;
            for (int x = 0; x < w; x++) {
                dst[x * 4 + 0] = row_rgb[x * 3 + 2];   /* B */
                dst[x * 4 + 1] = row_rgb[x * 3 + 1];   /* G */
                dst[x * 4 + 2] = row_rgb[x * 3 + 0];   /* R */
                dst[x * 4 + 3] = 0xFF;                 /* A */
            }
        }

        /* 填充图像描述符：数据指针指向常驻的 g_photo_buf */
        memset(&g_photo_dsc, 0, sizeof(g_photo_dsc));
        g_photo_dsc.header.always_zero = 0;
        g_photo_dsc.header.w = w;
        g_photo_dsc.header.h = h;
        g_photo_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
        g_photo_dsc.data_size = need;
        g_photo_dsc.data = g_photo_buf;

        ret = 0;
    }

out_dec:
    jpeg_finish_decompress(&cinfo);
out:
    jpeg_destroy_decompress(&cinfo);
    if (row_rgb) free(row_rgb);
    if (fp) fclose(fp);
    return ret;
}

/* 显示当前照片 */
static void show_current_photo(void)
{
    //lv_obj_add_flag(ui_imgPhotoPreview, LV_OBJ_FLAG_HIDDEN);   /* 临时测试：隐藏照片控件 */
    
    if (g_photo_count <= 0 || !g_photo_paths || !g_photo_paths[g_photo_current]) {
        lv_img_set_src(ui_imgPhotoPreview, NULL);
        lv_label_set_text(ui_lblPhotoInfo, "no photos");
        return;
    }

    /* 修复：LVGL 8.3 无法直接加载 JPEG 文件路径（FS/SJPG 均未开启），
     * 改为 libjpeg 解码后以图像描述符方式显示，翻页时重新解码当前照片 */
    if (decode_photo_to_dsc(g_photo_paths[g_photo_current]) == 0) {
        lv_img_set_src(ui_imgPhotoPreview, &g_photo_dsc);
    } else {
        lv_img_set_src(ui_imgPhotoPreview, NULL);
        lv_label_set_text(ui_lblPhotoInfo, "loading failed");
        return;
    }

    /* 更新照片信息标签  */
    char info[32];
    snprintf(info, sizeof(info), "%d / %d", g_photo_current + 1, g_photo_count);
    lv_label_set_text(ui_lblPhotoInfo, info);

    /* 修复：相册活动时直接局部重绘照片区会花屏（全屏重绘正常），
       * 强制整屏失效，让 LVGL 走和"重新进入相册"相同的完整重绘路径 */
      lv_obj_invalidate(ui_ScreenAlbum);
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
    capture_stop();          /* 进相册：停止预览采集（capture.c 已有，幂等，安全） */
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

    if (g_preview_cb)
          capture_start_preview(g_preview_cb, NULL);   /* 出相册：恢复预览 */
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
