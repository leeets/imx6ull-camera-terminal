#include "hal_camera.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <pthread.h>

/* ==================== 内部状态 ==================== */
static int              g_fd        = -1;
static int              g_nbufs     = 0;
static hal_camera_frame_t *g_bufs  = NULL;

static volatile int     g_streaming = 0;
static pthread_t        g_capture_thread;
static int              g_thread_running = 0;

static hal_camera_callback_t g_frame_cb  = NULL;
static void                 *g_frame_ctx = NULL;

/* ==================== 内部辅助 ==================== */
static void dump_formats(int fd) {
    struct v4l2_fmtdesc fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    while (ioctl(fd, VIDIOC_ENUM_FMT, &fmt) == 0) {
        printf("[HAL_CAM]   format #%d: %.4s  %s\n",
               fmt.index, (char*)&fmt.pixelformat, fmt.description);
        fmt.index++;
    }
}

static int request_buffers(int count) {
    struct v4l2_requestbuffers req;
    struct v4l2_buffer buf;

    if (g_bufs) {
        for (int i = 0; i < g_nbufs; i++)
            if (g_bufs[i].data && g_bufs[i].data != MAP_FAILED)
                munmap(g_bufs[i].data, g_bufs[i].length);
        free(g_bufs);
        g_bufs = NULL;
    }

    memset(&req, 0, sizeof(req));
    req.count = count;
    req.type  = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(g_fd, VIDIOC_REQBUFS, &req) < 0) { perror("[HAL_CAM] REQBUFS"); return -1; }
    if (req.count < 2) { fprintf(stderr, "[HAL_CAM] only %d bufs\n", req.count); return -1; }

    g_nbufs = req.count;
    g_bufs  = calloc(g_nbufs, sizeof(hal_camera_frame_t));
    if (!g_bufs) return -1;

    for (int i = 0; i < g_nbufs; i++) {
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(g_fd, VIDIOC_QUERYBUF, &buf) < 0) { perror("[HAL_CAM] QUERYBUF"); return -1; }

        g_bufs[i].index  = i;
        g_bufs[i].length = buf.length;
        g_bufs[i].data   = mmap(NULL, buf.length, PROT_READ|PROT_WRITE, MAP_SHARED, g_fd, buf.m.offset);
        if (g_bufs[i].data == MAP_FAILED) { perror("[HAL_CAM] mmap"); return -1; }
        g_bufs[i].buf = buf;
    }
    return 0;
}

static int queue_all_buffers(void) {
    struct v4l2_buffer buf;
    for (int i = 0; i < g_nbufs; i++) {
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(g_fd, VIDIOC_QBUF, &buf) < 0) { perror("[HAL_CAM] QBUF"); return -1; }
    }
    return 0;
}

static void free_buffers(void) {
    if (g_bufs) {
        for (int i = 0; i < g_nbufs; i++)
            if (g_bufs[i].data && g_bufs[i].data != MAP_FAILED)
                munmap(g_bufs[i].data, g_bufs[i].length);
        free(g_bufs);
        g_bufs = NULL;
    }
    g_nbufs = 0;
}

/* ==================== 采集线程 ==================== */
static void *capture_thread_func(void *arg) {
    (void)arg;
    struct v4l2_buffer buf;

    while (g_streaming) {
        fd_set fds;
        FD_ZERO(&fds); FD_SET(g_fd, &fds);
        struct timeval tv = {2, 0};
        int ret = select(g_fd + 1, &fds, NULL, NULL, &tv);
        if (ret < 0) { if (errno == EINTR) continue; break; }
        if (ret == 0) { fprintf(stderr, "[HAL_CAM] select timeout\n"); continue; }

        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (ioctl(g_fd, VIDIOC_DQBUF, &buf) < 0) { if (errno == EIO) continue; perror("[HAL_CAM] DQBUF"); break; }

        if (g_frame_cb && buf.index < (unsigned int)g_nbufs) {
            g_bufs[buf.index].buf = buf;
            g_bufs[buf.index].length = buf.bytesused;
            g_frame_cb(&g_bufs[buf.index], g_frame_ctx);
        }

        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = buf.index;
        if (ioctl(g_fd, VIDIOC_QBUF, &buf) < 0) { perror("[HAL_CAM] QBUF post"); break; }
    }
    return NULL;
}

/* ==================== 公共接口 ==================== */

int hal_camera_init(const char *dev_path, hal_camera_params_t *params) {
    struct v4l2_capability cap;
    struct v4l2_format fmt;

    if (g_fd >= 0) return 0;

    g_fd = open(dev_path, O_RDWR);
    if (g_fd < 0) { perror("[HAL_CAM] open"); return -1; }

    if (ioctl(g_fd, VIDIOC_QUERYCAP, &cap) < 0) { perror("[HAL_CAM] QUERYCAP"); goto fail; }
    printf("[HAL_CAM] driver: %s, card: %s\n", cap.driver, cap.card);

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) { fprintf(stderr, "[HAL_CAM] not capture\n"); goto fail; }
    if (!(cap.capabilities & V4L2_CAP_STREAMING)) { fprintf(stderr, "[HAL_CAM] no streaming\n"); goto fail; }

    dump_formats(g_fd);

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = params->width;
    fmt.fmt.pix.height      = params->height;
    fmt.fmt.pix.pixelformat = params->pixelformat;
    fmt.fmt.pix.field       = V4L2_FIELD_ANY;

    if (ioctl(g_fd, VIDIOC_S_FMT, &fmt) < 0) { perror("[HAL_CAM] S_FMT"); goto fail; }

    printf("[HAL_CAM] format: %dx%d %.4s\n",
           fmt.fmt.pix.width, fmt.fmt.pix.height,
           (char*)&fmt.fmt.pix.pixelformat);
    return 0;

fail:
    close(g_fd); g_fd = -1;
    return -1;
}

int hal_camera_start(hal_camera_callback_t cb, void *user_data) {
    enum v4l2_buf_type type;
    if (g_fd < 0 || g_streaming) return -1;
    if (request_buffers(4) < 0) return -1;
    if (queue_all_buffers() < 0) return -1;

    g_frame_cb  = cb;
    g_frame_ctx = user_data;
    g_streaming = 1;

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(g_fd, VIDIOC_STREAMON, &type) < 0) { perror("[HAL_CAM] STREAMON"); g_streaming = 0; return -1; }

    if (pthread_create(&g_capture_thread, NULL, capture_thread_func, NULL) != 0) {
        perror("[HAL_CAM] pthread_create"); g_streaming = 0; return -1;
    }
    g_thread_running = 1;
    return 0;
}

int hal_camera_capture_one(hal_camera_frame_t *frame) {
    struct v4l2_requestbuffers req;
    struct v4l2_buffer buf;
    enum v4l2_buf_type type;
    int ret = -1;

    if (g_fd < 0) return -1;

    memset(&req, 0, sizeof(req));
    req.count = 2; req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(g_fd, VIDIOC_REQBUFS, &req) < 0) { perror("[HAL_CAM] REQBUFS"); return -1; }

    for (int i = 0; i < 2; i++) {
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; buf.memory = V4L2_MEMORY_MMAP; buf.index = i;
        if (ioctl(g_fd, VIDIOC_QBUF, &buf) < 0) { perror("[HAL_CAM] QBUF"); goto out; }
    }
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(g_fd, VIDIOC_STREAMON, &type) < 0) { perror("[HAL_CAM] STREAMON"); goto out; }

    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; buf.memory = V4L2_MEMORY_MMAP;
    if (ioctl(g_fd, VIDIOC_DQBUF, &buf) < 0) { perror("[HAL_CAM] DQBUF"); goto out_off; }

    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; buf.memory = V4L2_MEMORY_MMAP; buf.index = 0;
    if (ioctl(g_fd, VIDIOC_QUERYBUF, &buf) < 0) { perror("[HAL_CAM] QUERYBUF"); goto out_off; }

    frame->index  = 0;
    frame->length = buf.bytesused;
    frame->data   = mmap(NULL, buf.length, PROT_READ|PROT_WRITE, MAP_SHARED, g_fd, buf.m.offset);
    if (frame->data == MAP_FAILED) { perror("[HAL_CAM] mmap"); goto out_off; }
    frame->buf    = buf;
    ret = 0;

out_off:
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(g_fd, VIDIOC_STREAMOFF, &type);
out:
    memset(&req, 0, sizeof(req));
    req.count = 0; req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; req.memory = V4L2_MEMORY_MMAP;
    ioctl(g_fd, VIDIOC_REQBUFS, &req);
    return ret;
}

void hal_camera_stop(void) {
    enum v4l2_buf_type type;
    if (!g_streaming) return;
    g_streaming = 0;
    if (g_thread_running) { pthread_join(g_capture_thread, NULL); g_thread_running = 0; }
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(g_fd, VIDIOC_STREAMOFF, &type);
    free_buffers();
}

void hal_camera_exit(void) {
    hal_camera_stop();
    if (g_fd >= 0) { close(g_fd); g_fd = -1; }
}
