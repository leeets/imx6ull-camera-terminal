/*
 * capture.c -- 摄像头采集模块
 *
 * 架构：
 *   V4L2 采集线程（preview_bridge）：只做 MJPEG 数据拷贝入队，快速 QBUF 归还
 *   工作线程（preview_worker_thread）：从队列取出帧，调用 g_preview_cb
 *     （g_preview_cb 中包含：MJPEG 解码 → FB 写入 → recorder_write_frame 入 recorder 队列）
 *   录制线程（recorder 内部）：从队列取出 MJPEG → write() 写到 SD 卡
 *
 * 好处：V4L2 采集线程永远不被 JPEG 解码或 SD 卡写阻塞
 */

#include <sys/mman.h>
#include "capture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

/* ==================== 预览帧环形队列 ==================== */
/* 作用：在 V4L2 采集线程和业务处理线程之间解耦
 * V4L2 线程只做入队（malloc + memcpy），快速返回
 * 业务线程取出后做 MJPEG 解码 + FB 写入 + 录制入队 */
#define PREVIEW_QUEUE_SIZE 4

typedef struct {
    void  *data;      /* malloc 拷贝的 MJPEG 帧数据 */
    size_t length;
} preview_queue_entry_t;

static preview_queue_entry_t  g_preview_queue[PREVIEW_QUEUE_SIZE];
static int                    g_preview_head = 0;  /* 生产者（V4L2 线程）写入位置 */
static int                    g_preview_tail = 0;  /* 消费者（工作线程）读取位置 */
static pthread_mutex_t        g_preview_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t         g_preview_cond  = PTHREAD_COND_INITIALIZER;
static pthread_t              g_worker_thread;
static volatile int           g_worker_running = 0;

static capture_preview_cb_t   g_preview_cb  = NULL;
static void                  *g_preview_ctx = NULL;
static int                    g_preview_started = 0;

/* V4L2 采集线程中调用 —— 只做将拿到的数据入队并通知preview，绝不阻塞 */
static void preview_bridge(const hal_camera_frame_t *frame, void *user_data) {
    (void)user_data;

    pthread_mutex_lock(&g_preview_mutex);

    int next = (g_preview_head + 1) % PREVIEW_QUEUE_SIZE;
    if (next == g_preview_tail) {
        /* 队列满了，丢帧 —— 不阻塞 V4L2 采集线程 */
        pthread_mutex_unlock(&g_preview_mutex);
        return;
    }

    /* 用entry拷贝 MJPEG 帧数据（frame就是帧数据结构体,frame->data 是 mmap 缓冲区，必须尽快归还 QBUF） */
    preview_queue_entry_t *entry = &g_preview_queue[g_preview_head];	// 放进队列！preview线程出队用。
    entry->data = malloc(frame->length);
    //printf("入队+1\n");
    if (entry->data) {
        memcpy(entry->data, frame->data, frame->length);	//entry拿到采集线程的帧数据
        entry->length = frame->length;
        g_preview_head = next;
    }

    pthread_cond_signal(&g_preview_cond);	//采集做好了，并且入队好了。通知preview线程预览
    pthread_mutex_unlock(&g_preview_mutex);
}

/* 预览视频 工作线程：从队列取出帧，调用用户的 preview_cb */
static void *preview_worker_thread(void *arg) {
    (void)arg;

    while (g_worker_running) {
        pthread_mutex_lock(&g_preview_mutex);

        while (g_preview_head == g_preview_tail && g_worker_running)	//临界资源
            pthread_cond_wait(&g_preview_cond, &g_preview_mutex);

        if (!g_worker_running) {
            pthread_mutex_unlock(&g_preview_mutex);
            break;
        }

        /* g_preview_queue出队->取出帧（transfer ownership of malloc'd buffer） */
        preview_queue_entry_t *entry = &g_preview_queue[g_preview_tail]; //拿到队列，出队调g_preview_cb即on_preview_frame预览就行
        //printf("出队+1\n");
        void  *frame_data = entry->data;
        size_t frame_len  = entry->length;
        entry->data   = NULL;
        entry->length = 0;
        g_preview_tail = (g_preview_tail + 1) % PREVIEW_QUEUE_SIZE;	//环形

        pthread_mutex_unlock(&g_preview_mutex);

        /* ---- 以下代码在独立工作线程运行，不阻塞 V4L2 采集线程 ---- */
        if (g_preview_cb && frame_data) {
            hal_camera_frame_t tmp_frame;
            memset(&tmp_frame, 0, sizeof(tmp_frame));
            tmp_frame.data   = frame_data;
            tmp_frame.length = frame_len;
            g_preview_cb(&tmp_frame, g_preview_ctx);
        }

        free(frame_data);
    }

    /* 退出前清理preview队列中残留的帧 */
    pthread_mutex_lock(&g_preview_mutex);
    while (g_preview_tail != g_preview_head) {
        free(g_preview_queue[g_preview_tail].data);
        g_preview_queue[g_preview_tail].data = NULL;
        g_preview_tail = (g_preview_tail + 1) % PREVIEW_QUEUE_SIZE;
    }
    pthread_mutex_unlock(&g_preview_mutex);

    return NULL;
}

/* 初始化：打开设备、设置格式，MJPEG 失败则降级 YUYV */
int capture_init(const char *dev_path, int width, int height) {
    hal_camera_params_t params;

    params.width       = width;
    params.height      = height;
    params.pixelformat = V4L2_PIX_FMT_MJPEG;
    params.fps         = 15;

    printf("[CAPTURE] init %s %dx%d MJPEG\n", dev_path, width, height);
    if (hal_camera_init(dev_path, &params) < 0) {
        printf("[CAPTURE] MJPEG failed, fallback YUYV\n");
        params.pixelformat = V4L2_PIX_FMT_YUYV;
        if (hal_camera_init(dev_path, &params) < 0) {
            fprintf(stderr, "[CAPTURE] init ALL failed\n");
            return -1;
        }
    }
    return 0;
}

/* 启动预览流：
 *   1. pthread_create启动预览的工作线程（处理解码 + 显示 + 录制入队）
 *   2. 启动 V4L2 采集（采集线程只负责入队） */
int capture_start_preview(capture_preview_cb_t cb, void *user_data) {
    if (g_preview_started) return 0;

    g_preview_cb  = cb;		//直接注册main给的回调函数
    g_preview_ctx = user_data;
    g_preview_head = 0;
    g_preview_tail = 0;

    /* 先启动工作线程 */
    g_worker_running = 1;
    if (pthread_create(&g_worker_thread, NULL, preview_worker_thread, NULL) != 0) {
        perror("[CAPTURE] worker thread create");
        g_worker_running = 0;
        return -1;
    }

    /* 再启动 V4L2 采集（preview_bridge 在线程中只做入队） */
    if (hal_camera_start(preview_bridge, NULL) < 0) {
        g_worker_running = 0;
        pthread_cond_signal(&g_preview_cond);	//通知线程
        pthread_join(g_worker_thread, NULL);	//回收线程
        return -1;
    }

    g_preview_started = 1;
    printf("[CAPTURE] preview started\n");
    return 0;
}

/* 拍照：暂停预览 → 单帧捕获 → 恢复预览 (这套流程不能用！)*/
/* 改成： 预览时 preview_bridge 已经把每帧 MJPEG 拷贝进环形队列了，照片直接从队列里拿最新一帧即可！ */
int capture_take_photo(capture_photo_cb_t cb, void *user_data) {
      if (!g_preview_started || !cb) return -1;

      /* 从预览环形队列取最新一帧（最多等 500ms），不停流、不碰 REQBUFS */
      void *photo = NULL;
      size_t photo_len = 0;

      for (int tries = 0; tries < 50 && !photo; tries++) {
          pthread_mutex_lock(&g_preview_mutex);
          if (g_preview_head != g_preview_tail) {
              int idx = (g_preview_head - 1 + PREVIEW_QUEUE_SIZE) % PREVIEW_QUEUE_SIZE;
              if (g_preview_queue[idx].data && g_preview_queue[idx].length) {
                  photo = malloc(g_preview_queue[idx].length);
                  if (photo) {
                      memcpy(photo, g_preview_queue[idx].data, g_preview_queue[idx].length);
                      photo_len = g_preview_queue[idx].length;
                  }
              }
          }
          pthread_mutex_unlock(&g_preview_mutex);
          if (!photo) usleep(10000);   /* 队列暂时空，等下一帧 */
      }

      if (!photo) return -1;

      cb(photo, photo_len, user_data);
      free(photo);
      return 0;
}

void capture_stop(void) {
    /* 先停工作线程再停 V4L2 */
    if (g_worker_running) {
        pthread_mutex_lock(&g_preview_mutex);
        g_worker_running = 0;
        pthread_cond_signal(&g_preview_cond);
        pthread_mutex_unlock(&g_preview_mutex);
        pthread_join(g_worker_thread, NULL);
    }
    hal_camera_stop();
    g_preview_started = 0;
}

void capture_exit(void) {
    capture_stop();
    hal_camera_exit();
}
