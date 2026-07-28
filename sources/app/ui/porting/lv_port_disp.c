/**
 * lv_port_disp.c - LVGL 显示移植层实现
 *
 * 架构:
 *   使用单缓冲区模式 + 全屏刷新。
 *   disp_flush() 回调中调用 hal_fb_draw_rgb565() 写入 /dev/fb0。
 *
 * 注意:
 *   由于 imx6ull 没有 GPU，单缓冲是合理选择。
 *   如需防撕裂可改为双缓冲（LVGL 内部双缓冲 + memcpy 到 fb）。
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_port_disp.h"
#include "lvgl/lvgl.h"
#include "hal_fb.h"

/*********************
 *      DEFINES
 *********************/
#define DISP_BUF_SIZE (LV_HOR_RES_MAX * LV_VER_RES_MAX)

/**********************
 *  STATIC VARIABLES
 **********************/
static lv_disp_drv_t      g_disp_drv;
static lv_disp_t         *g_disp        = NULL;
static lv_color_t         g_disp_buf[DISP_BUF_SIZE];
static lv_disp_draw_buf_t g_draw_buf;   /* 全局缓冲区描述符，避免复合字面量生命周期问题 */

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p);

/**********************
 *  初始化
 **********************/
void lv_port_disp_init(void)
{
    /* hal_fb_init() 由外部 main.c 调用，此处不再重复初始化 */
    printf("[LV_DISP] init: %dx%d RGB565\n", LV_HOR_RES_MAX, LV_VER_RES_MAX);

    /* 初始化绘制缓冲区描述符（全局变量，生命周期与程序一致）*/
    lv_disp_draw_buf_init(&g_draw_buf, g_disp_buf, NULL, DISP_BUF_SIZE);

    lv_disp_drv_init(&g_disp_drv);
    g_disp_drv.hor_res   = LV_HOR_RES_MAX;
    g_disp_drv.ver_res   = LV_VER_RES_MAX;
    g_disp_drv.flush_cb  = disp_flush;
    g_disp_drv.draw_buf  = &g_draw_buf;

    g_disp = lv_disp_drv_register(&g_disp_drv);
}

/**********************
 *  disp_flush - LVGL 绘制完成后的回调
 **********************/
static void disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    (void)drv;
    (void)area;
    (void)color_p;

    /* 将 LVGL 缓冲区内容整帧写入 framebuffer */
    hal_fb_draw_rgb565(g_disp_buf, LV_HOR_RES_MAX, LV_VER_RES_MAX);

    /* 通知 LVGL 刷新完成 */
    lv_disp_flush_ready(drv);
}
