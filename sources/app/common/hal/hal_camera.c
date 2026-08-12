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
static hal_camera_frame_t *g_bufs  = NULL;//摄像头帧数据

static volatile int     g_streaming = 0;
static pthread_t        g_capture_thread;
static int              g_thread_running = 0;

static hal_camera_callback_t g_frame_cb  = NULL;
static void                 *g_frame_ctx = NULL;

/* ==================== 内部辅助 static==================== */
/** 枚举 V4L2 设备支持的所有像素格式 **/
static void dump_formats(int fd) {
    struct v4l2_fmtdesc fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    while (ioctl(fd, VIDIOC_ENUM_FMT, &fmt) == 0) {//枚举出来所有格式
        printf("[HAL_CAM]   format #%d: %.4s  %s\n",
               fmt.index, (char*)&fmt.pixelformat, fmt.description);
        fmt.index++;
    }
}

/** 申请并映射 V4L2 mmap 缓冲区，拿到count个g_bufs**/
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
	//应用程序REQBUFS申请之后，才会得到资源，而不是驱动直接就有！
    if (ioctl(g_fd, VIDIOC_REQBUFS, &req) < 0) { perror("[HAL_CAM] REQBUFS"); return -1; }
    if (req.count < 2) { fprintf(stderr, "[HAL_CAM] only %d bufs\n", req.count); return -1; }

    g_nbufs = req.count;//拿到了个数
    g_bufs  = calloc(g_nbufs, sizeof(hal_camera_frame_t));
    if (!g_bufs) return -1;

    for (int i = 0; i < g_nbufs; i++) {
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(g_fd, VIDIOC_QUERYBUF, &buf) < 0) { perror("[HAL_CAM] QUERYBUF"); return -1; }
		//查询到buff信息
        g_bufs[i].index  = i;
        g_bufs[i].length = buf.length;
		//mmap映射地址到应用程序，可读可写
        g_bufs[i].data   = mmap(NULL, buf.length, PROT_READ|PROT_WRITE, MAP_SHARED, g_fd, buf.m.offset);
        if (g_bufs[i].data == MAP_FAILED) { perror("[HAL_CAM] mmap"); return -1; }
        g_bufs[i].buf = buf;
    }
    return 0;
}

/** 将所有已映射的缓冲区入队到 V4L2 驱动 **/
static int queue_all_buffers(void) {
    struct v4l2_buffer buf;
    for (int i = 0; i < g_nbufs; i++) {
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
		//VIDIOC_QBUF入队
        if (ioctl(g_fd, VIDIOC_QBUF, &buf) < 0) { perror("[HAL_CAM] QBUF"); return -1; }
    }
    return 0;
}

/** munmap释放g_nbufs个缓冲区 **/
static void free_buffers(void) {
    if (g_bufs) {
        for (int i = 0; i < g_nbufs; i++)
            if (g_bufs[i].data && g_bufs[i].data != MAP_FAILED)
                munmap(g_bufs[i].data, g_bufs[i].length);
        free(g_bufs);
        g_bufs = NULL;
    }
    g_nbufs = 0;
/* 修复REQBUFS: Device or resource busy */
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 0; req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(g_fd, VIDIOC_REQBUFS, &req) < 0)
      perror("[HAL_CAM] REQBUFS free");
    else
      printf("[HAL_CAM] REQBUFS free ok\n");

}

/* ==================== 采集线程 ==================== */
static void *capture_thread_func(void *arg) {
    (void)arg;
    struct v4l2_buffer buf;

    while (g_streaming) {
        fd_set fds;
        FD_ZERO(&fds); FD_SET(g_fd, &fds);
        struct timeval tv = {2, 0};
        int ret = select(g_fd + 1, &fds, NULL, NULL, &tv);//阻塞等待 V4L2 设备有数据可读（2 秒超时）
        if (ret < 0) { if (errno == EINTR) continue; break; }
        if (ret == 0) { fprintf(stderr, "[HAL_CAM] select timeout\n"); continue; }

        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (ioctl(g_fd, VIDIOC_DQBUF, &buf) < 0) { if (errno == EIO) continue; perror("[HAL_CAM] DQBUF"); break; }
		//取出已填好的帧
        //printf("出帧+1\n");
        unsigned int idx = buf.index;
        if (g_frame_cb && buf.index < (unsigned int)g_nbufs) {	
            g_bufs[buf.index].buf = buf;					// 把帧数据传给上层
            g_bufs[buf.index].length = buf.bytesused;
            g_frame_cb(&g_bufs[buf.index], g_frame_ctx);	//调用注册下来的的回调 g_frame_cb（preview_bridge）
        }

        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = idx;
		//VIDIOC_QBUF 把缓冲区重新入队 g_streaming = 0 时退出循环。
        if (ioctl(g_fd, VIDIOC_QBUF, &buf) < 0) { perror("[HAL_CAM] QBUF post"); break; }
    }
    return NULL;
}

/* ==================== 公共接口（初始化，录像，拍照） ==================== */
/* 打开 V4L2 设备并配置格式为 */
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

    dump_formats(g_fd);//枚举出所有格式
	//指定参数：
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = params->width;
    fmt.fmt.pix.height      = params->height;
    fmt.fmt.pix.pixelformat = params->pixelformat;
    fmt.fmt.pix.field       = V4L2_FIELD_ANY;
	//set_format设置要的格式。驱动会自己调整&fmt的参数，比如分辨率。
    if (ioctl(g_fd, VIDIOC_S_FMT, &fmt) < 0) { perror("[HAL_CAM] S_FMT"); goto fail; }

    printf("[HAL_CAM] format: %dx%d %.4s\n",
           fmt.fmt.pix.width, fmt.fmt.pix.height,
           (char*)&fmt.fmt.pix.pixelformat);
    return 0;

fail:
    close(g_fd); g_fd = -1;
    return -1;
}

/* main调用capture的start采集，从而会调用此函数，启动视频流采集线程--
 * 线程再调用main的回调cb实现另外的功能，都是采集线程上下文 */
int hal_camera_start(hal_camera_callback_t cb, void *user_data) {
    enum v4l2_buf_type type;
    if (g_fd < 0 || g_streaming) return -1;
    if (request_buffers(4) < 0) return -1;
    if (queue_all_buffers() < 0) return -1;

    g_frame_cb  = cb;		//这里注册的是preview_bridge(只做入队g_preview_queue)
    g_frame_ctx = user_data;
    g_streaming = 1;

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	//VIDIOC_STREAMON — 通知驱动开始采集:
    if (ioctl(g_fd, VIDIOC_STREAMON, &type) < 0) { perror("[HAL_CAM] STREAMON"); g_streaming = 0; return -1; }

	//创建线程，实现采集数据和回调用户定义的处理数据函数
    if (pthread_create(&g_capture_thread, NULL, capture_thread_func, NULL) != 0) {
        perror("[HAL_CAM] pthread_create"); g_streaming = 0; return -1;
    }
    g_thread_running = 1;
    return 0;
}
/* 单帧捕获（用于拍照，不启动线程） */
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
/*停止视频流*/
void hal_camera_stop(void) {
    printf("[HAL_CAM] stop: g_streaming=%d\n", g_streaming);
    enum v4l2_buf_type type;
    if (!g_streaming) return;
    g_streaming = 0;
    if (g_thread_running) { pthread_join(g_capture_thread, NULL); g_thread_running = 0; }
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(g_fd, VIDIOC_STREAMOFF, &type) < 0)
      perror("[HAL_CAM] STREAMOFF");
    else
      printf("[HAL_CAM] STREAMOFF ok\n");
    free_buffers();
}

void hal_camera_exit(void) {
    hal_camera_stop();
    if (g_fd >= 0) { close(g_fd); g_fd = -1; }
}
