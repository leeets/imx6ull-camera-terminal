#include "video_convert.h"
#include <stdint.h>	//定义各种长度的整型，跨平台。（包括BOOL类型
#include <stdlib.h>	//定义strtoull把字符串 nptr 转换成 unsigned long long。
#include <setjmp.h>	//非本地跳转：实现全局异常处理机制。深层函数遇到严重错误时，直接到最上层错误处理点。
#include <stdio.h>   // 必须先包含 stdio.h
#include <jpeglib.h>	//在包含JPEG处理库

#define CLAMP(x)  ((x) < 0 ? 0 : ((x) > 255 ? 255 : (x)))

/* YUYV 4:2:2 -> RGB565
 * 输入: YUYV 交错 (Y0 U0 Y1 V0 Y2 U1 Y3 V1 ...)
 * 输出: RGB565 (little-endian, 低字节B, 中字节G, 高字节R)
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

/* ========== libjpeg-turbo 解码错误处理 ========== */

struct jpeg_error_mgr_wrap {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buf;
};

static void jpeg_error_exit(j_common_ptr cinfo) {
    struct jpeg_error_mgr_wrap *err = (struct jpeg_error_mgr_wrap *)cinfo->err;
    (*cinfo->err->output_message)(cinfo);
    longjmp(err->setjmp_buf, 1);	//goto只能在同一个函数内跳转，setjmp+longjmp就是设置加跳转，可以在调用栈的各层之间任意跳转
}

/* MJPEG 帧解码 -> RGB565
 * 基于 libjpeg-turbo，利用 NEON 加速解码
 * 输出缓冲区必须 >= width * height * 2 字节
 */
int mjpeg_to_argb8888(const void *src, size_t src_len,
                    void *dst, int width, int height) {
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr_wrap jerr;
    unsigned char *row_rgb = NULL;
    int ret = -1;

    if (!src || !dst || src_len < 2 || width <= 0 || height <= 0)
        return -1;

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpeg_error_exit;
    if (setjmp(jerr.setjmp_buf))
        goto out;

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, (unsigned char *)src, src_len);

    jpeg_read_header(&cinfo, TRUE);
	/* 修改：为了减小延迟，640x480 → 320x240：libjpeg IDCT 快速缩放，解码量约 1/4 */
	cinfo.scale_num = 1;
    cinfo.scale_den = 2;

    /* 输出 RGB888，直出ARGB */
    cinfo.out_color_space = JCS_RGB;
    jpeg_calc_output_dimensions(&cinfo);

    jpeg_start_decompress(&cinfo);

    {
    	/* 修改：输出直接4字节/像素 */
        int row_stride = cinfo.output_width * cinfo.output_components;
        row_rgb = (unsigned char *)malloc(row_stride);
        if (!row_rgb)
            goto out_dec;

        uint32_t *argb = (uint32_t *)dst;
        int out_w = (int)cinfo.output_width;
        int min_w = (out_w < width) ? out_w : width;
        int y = 0;

        while (cinfo.output_scanline < cinfo.output_height && y < height) {
            unsigned char *rows[1] = { row_rgb };
            jpeg_read_scanlines(&cinfo, rows, 1);

            /* 逐像素 修改：直出ARGB */
            int x;
            for (x = 0; x < min_w; x++) {
                unsigned char *p = row_rgb + x * 3;
				/* B,G,R,A（小端）== (A<<24)|(R<<16)|(G<<8)|B */
                argb[y * width + x] =
                    (0xFFu << 24) | ((uint32_t)p[0] << 16) |
                    ((uint32_t)p[1] << 8) | (uint32_t)p[2];
			
                /*int r = p[0];
                int g = p[1];
                int b = p[2];
                rgb565[y * width + x] =
                    ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);*/
                    
            }
            y++;
        }
        ret = 0;
    }

out_dec:
    jpeg_finish_decompress(&cinfo);
out:
    jpeg_destroy_decompress(&cinfo);
    free(row_rgb);
    return ret;
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
