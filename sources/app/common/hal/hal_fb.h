#ifndef _HAL_FB_H
#define _HAL_FB_H

/* Framebuffer HAL — 轻量封装 /dev/fb0 */

int  hal_fb_init(void);                              /* open + mmap */
int  hal_fb_draw_rgb565(const void *rgb, int w, int h); /* 写一帧 RGB565 到显存 */
void hal_fb_clear(unsigned short color);              /* 清屏 */
void hal_fb_exit(void);                               /* munmap + close */

int  hal_fb_get_width(void);
int  hal_fb_get_height(void);
int  hal_fb_get_bpp(void);
int  hal_fb_get_line_width(void);                     /* 一行的字节数 = xres * bpp/8 */

#endif /* _HAL_FB_H */
