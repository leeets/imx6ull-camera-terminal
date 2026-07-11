#include "hal_fb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

static int              g_fd_fb = -1;
static unsigned char   *g_fb_base = NULL;
static struct fb_var_screeninfo g_var;
static int              g_screen_size = 0;
static int              g_line_width = 0;
static int              g_pixel_width = 0;

int hal_fb_init(void) {
    if (g_fd_fb >= 0) return 0;

    g_fd_fb = open("/dev/fb0", O_RDWR);
    if (g_fd_fb < 0) { perror("[FB] open"); return -1; }

    if (ioctl(g_fd_fb, FBIOGET_VSCREENINFO, &g_var) < 0) {
        perror("[FB] FBIOGET_VSCREENINFO");
        close(g_fd_fb); g_fd_fb = -1;
        return -1;
    }

    g_pixel_width = g_var.bits_per_pixel / 8;
    g_line_width  = g_var.xres * g_pixel_width;
    g_screen_size = g_var.yres * g_line_width;

    g_fb_base = (unsigned char *)mmap(NULL, g_screen_size,
                                       PROT_READ | PROT_WRITE,
                                       MAP_SHARED, g_fd_fb, 0);
    if (g_fb_base == MAP_FAILED) {
        perror("[FB] mmap");
        close(g_fd_fb); g_fd_fb = -1;
        return -1;
    }

    printf("[FB] %dx%d %dbpp\n", g_var.xres, g_var.yres, g_var.bits_per_pixel);
    return 0;
}

int hal_fb_draw_rgb565(const void *rgb, int w, int h) {
    int screen_w = g_var.xres;
    int screen_h = g_var.yres;
    int src_stride = w * 2;
    int dst_stride = g_line_width;
    int offset_x, offset_y;
    int copy_w, copy_h;
    int y;

    if (!g_fb_base || !rgb) return -1;

    offset_x = (screen_w - w) / 2;
    if (offset_x < 0) offset_x = 0;
    offset_y = (screen_h - h) / 2;
    if (offset_y < 0) offset_y = 0;

    copy_w = (w > screen_w) ? screen_w : w;
    copy_h = (h > screen_h) ? screen_h : h;

    for (y = 0; y < copy_h; y++) {
        memcpy(g_fb_base + (offset_y + y) * dst_stride + offset_x * 2,
               (const unsigned char *)rgb + y * src_stride,
               copy_w * 2);
    }
    return 0;
}

void hal_fb_clear(unsigned short color) {
    int i;
    unsigned short *p = (unsigned short *)g_fb_base;
    int count = g_screen_size / 2;
    for (i = 0; i < count; i++) p[i] = color;
}

void hal_fb_exit(void) {
    if (g_fb_base) { munmap(g_fb_base, g_screen_size); g_fb_base = NULL; }
    if (g_fd_fb >= 0) { close(g_fd_fb); g_fd_fb = -1; }
}

int hal_fb_get_width(void)       { return g_var.xres; }
int hal_fb_get_height(void)      { return g_var.yres; }
int hal_fb_get_bpp(void)         { return g_var.bits_per_pixel; }
int hal_fb_get_line_width(void)  { return g_line_width; }
