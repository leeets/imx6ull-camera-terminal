/**
 * lv_conf.h - LVGL 8.3.11 配置
 * imx6ull 屏幕: 800x480 RGB565 16bit
 */

/* clang-format off */
#if 1 /* 启用此配置文件 */
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* 颜色深度: imx6ull fb 为 RGB565 */
#define LV_COLOR_DEPTH 32

/* 交换 R/G 通道: imx6ull 无此需求 */
#define LV_COLOR_16_SWAP 0

/* 屏幕分辨率 */
#define LV_HOR_RES_MAX 1024
#define LV_VER_RES_MAX 600

/* LVGL 堆大小: 至少 64KB，有双缓冲时需要更多 */
#define LV_MEM_SIZE (256 * 1024U)

/* 显示刷新周期 (ms): 调低以节省 CPU */
#define LV_DISP_DEF_REFR_PERIOD 50

/* 默认开启 Montserrat 14 字体 */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_16 1

/* 关闭性能监视器（调试时可开启） */
#define LV_USE_PERF_MONITOR 0

/* 关闭不必要的功能以节省内存 */
#define LV_USE_ANIMATION 1
#define LV_USE_GROUP 1        /* 按键导航需要 group 支持 */
#define LV_USE_OBJ_ID 0

/* 日志等级: 0=关闭, 1=error, 2=warn, 3=info, 4=trace */
#define LV_USE_LOG 0

/* 内存分配器: 使用标准 libc */
#define LV_MEM_CUSTOM 0

/* GPU 加速: 无 */
#define LV_USE_GPU_ARM2D 0
#define LV_USE_GPU_STM32_DMA2D 0
#define LV_USE_GPU_NXP_PXP 0
#define LV_USE_GPU_NXP_VG_LITE 0
#define LV_USE_GPU_SWM341 0

/* 文件系统: 暂时不需要 */
#define LV_USE_FS_STDIO 0
#define LV_USE_FS_POSIX 0

/* 默认字体 */
#define LV_TICK_CUSTOM 0

#endif /*LV_CONF_H*/
#endif /*1 启用配置*/
