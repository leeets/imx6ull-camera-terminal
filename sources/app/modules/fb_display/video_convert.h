#ifndef _VIDEO_CONVERT_H
#define _VIDEO_CONVERT_H

/* YUYV 转 RGB565
 * src: YUYV 数据 (width * height * 2 字节)
 * dst: 输出 RGB565 (width * height * 2 字节)
 * 返回 0 成功
 */
int yuyv_to_rgb565(const void *src, void *dst, int width, int height);

/* RGB565 转 RGB888 (可选，后续 LVGL 可能需要) */
void rgb565_to_rgb888(const unsigned short *src, unsigned char *dst, int count);

#endif /* _VIDEO_CONVERT_H */
