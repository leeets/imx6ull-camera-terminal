#ifndef _VIDEO_CONVERT_H
#define _VIDEO_CONVERT_H

#include <stddef.h>

/* YUYV 转 RGB565（摄像头为 YUYV 源时使用）
 * src: YUYV 数据 (width * height * 2 字节)
 * dst: 输出 RGB565 (width * height * 2 字节)
 * 返回 0 成功
 */
int yuyv_to_rgb565(const void *src, void *dst, int width, int height);

/* MJPEG 帧解码并转 RGB565（摄像头为 MJPEG 源时使用）
 * src:      MJPEG 帧数据（完整 JPEG 编码的一帧）
 * src_len:  数据长度
 * dst:      输出 RGB565 缓冲区 (width * height * 2 字节)
 * width:    目标宽度（解码后缩放至此宽度）
 * height:   目标高度
 * 返回 0 成功，-1 解码失败
 */
int mjpeg_to_rgb565(const void *src, size_t src_len,
                    void *dst, int width, int height);

/* RGB565 转 RGB888 (可选，后续 LVGL 可能需要) */
void rgb565_to_rgb888(const unsigned short *src, unsigned char *dst, int count);

#endif /* _VIDEO_CONVERT_H */
