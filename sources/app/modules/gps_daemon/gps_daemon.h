#ifndef _GPS_DAEMON_H
#define _GPS_DAEMON_H

#include <stdint.h>
#include <stdbool.h>

/*
 * gps_daemon - GPS 定位模块
 *
 * 使用 UART6 (/dev/ttymxc5) 读取 NMEA 0183 协议数据,
 * 在独立线程中执行串口读取和 NMEA 解析,
 * 主线程通过 gps_get_data() 获取最新定位结果。
 *
 * 硬件连接:
 *   GPS TX -> UART6_RX (CSI_PIXCLK)
 *   GPS RX -> UART6_TX (CSI_MCLK) 可选
 *   串口参数: 9600 8N1, 无流控
 */

/* GPS 定位数据结构 */
typedef struct {
    double  latitude;           /* 纬度 (度)  如 22.543094 */
    double  longitude;          /* 经度 (度)  如 113.956234 */
    double  altitude;           /* 海拔 (米) */
    double  speed;              /* 地面速度 (节) */
    double  course;             /* 对地航向 (度) */
    int     hour;               /* UTC 时 */
    int     minute;             /* UTC 分 */
    int     second;             /* UTC 秒 */
    int     satellites;         /* 跟踪卫星数 */
    bool    valid;              /* 定位是否有效 (GPRMC A=有效) */
} gps_data_t;

/*
 * 初始化 GPS 模块
 * uart_dev: 串口设备路径，传 NULL 默认为 "/dev/ttymxc5"
 * 返回 0 成功，-1 失败
 */
int gps_init(const char *uart_dev);

/*
 * 获取最新 GPS 数据（线程安全，原子拷贝）
 * out: 输出当前最新定位数据
 * 返回 0 成功，-1 未初始化或无数据
 */
int gps_get_data(gps_data_t *out);

/*
 * 检查 GPS 定位是否有效（简易接口）
 * 返回 true 已定位，false 未定位
 */
bool gps_is_valid(void);

/*
 * 销毁 GPS 模块，关闭串口并回收线程
 */
void gps_exit(void);

#endif /* _GPS_DAEMON_H */
