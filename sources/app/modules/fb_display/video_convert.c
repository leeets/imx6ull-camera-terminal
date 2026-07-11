#include "video_convert.h"
#include <stdint.h>

#define CLAMP(x)  ((x) < 0 ? 0 : ((x) > 255 ? 255 : (x)))

/* YUYV 4:2:2 → RGB565
 * 输入: YUYV 交错 (Y0 U0 Y1 V0 Y2 U1 Y3 V1 ...)
 * 输出: RGB565 (little-endian, 低5位R, 中6位G, 高5位B)
 * 每个 YUYV 4 字节输出 2 个 RGB565 像素
 */
int yuyv_to_rgb565(const void *src, void *dst, int width, int height) {
    const unsigned char *yuyv = (const unsigned char *)src;
    unsigned short *rgb  = (unsigned short *)dst;
    int total = width * height / 2;  /* 每 2 像素一组 */
    int i;

    for (i = 0; i < total; i++) {
        int y0 = yuyv[0];
        int u  = yuyv[1] - 128;
        int y1 = yuyv[2];
        int v  = yuyv[3] - 128;

        /* Y0 */
        int r = CLAMP(y0 + 1.402f * v);
        int g = CLAMP(y0 - 0.344f * u - 0.714f * v);
        int b = CLAMP(y0 + 1.772f * u);
        rgb[0] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);

        /* Y1 */
        r = CLAMP(y1 + 1.402f * v);
        g = CLAMP(y1 - 0.344f * u - 0.714f * v);
        b = CLAMP(y1 + 1.772f * u);
        rgb[1] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);

        yuyv += 4;
        rgb  += 2;
    }
    return 0;
}

void rgb565_to_rgb888(const unsigned short *src, unsigned char *dst, int count) {
    int i;
    for (i = 0; i < count; i++) {
        unsigned short p = src[i];
        *dst++ = ((p >> 8) & 0xf8) | ((p >> 13) & 0x07);    /* R */
        *dst++ = ((p >> 3) & 0xfc) | ((p >>  9) & 0x03);    /* G */
        *dst++ = ((p << 3) & 0xf8) | ((p >>  2) & 0x07);    /* B */
    }
}
