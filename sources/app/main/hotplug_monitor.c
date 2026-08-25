/*
 * hotplug_monitor.c - 设备热插拔监控（inotify + 二级确认）
 *
 * 一级（inotify）：监听 /dev 目录 IN_CREATE / IN_DELETE，事件驱动不轮询；
 * 二级（open + ioctl）：节点出现后驱动可能未就绪，用 open() 配合
 *      VIDIOC_QUERYCAP / EVIOCGID / stat() 确认设备真正可用再重连；
 * 降级（fallback）：inotify 不可用时回退 stat() 定时轮询，保证功能可用。
 *
 * 线程安全：重连动作只在监控线程串行执行；摄像头 start/stop 由 capture.c
 * 内部互斥锁保护，与主线程（相册进出）并发调用不冲突。
 */

#include "hotplug_monitor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/inotify.h>
#include <sys/stat.h>

#include "capture.h"
#include "hal_touch.h"
#include "storage_manager.h"
#include "recorder.h"

/* /dev 下事件很频繁，必须按设备名过滤 */
#define HOTPLUG_WATCH_DIR      "/dev"
#define HOTPLUG_RETRY_MS       100      /* 二级确认失败后的重试间隔 */
#define HOTPLUG_RETRY_MAX      30       /* 最大重试次数（约 3 秒） */
#define HOTPLUG_POLL_MS        200      /* inotify 不可用时的轮询周期 */

/* 设备当前状态（监控线程读写） */
static volatile int g_camera_present;
static volatile int g_touch_present;
static volatile int g_storage_present;

/* 设备配置（init 时固化，重连时使用） */
static char     g_camera_dev[64];
static char     g_camera_base[32];
static int      g_camera_w;
static int      g_camera_h;
static char     g_touch_dev[64];
static char     g_touch_base[32];
static char     g_storage_root[128];
static uint64_t g_storage_capacity;

static volatile int g_monitor_running;
static pthread_t    g_monitor_thread;

/* ==================== 摄像头 ==================== */

/* 二级确认通过后重连采集：停旧流 → 关旧 fd → init → 恢复预览 */
static void on_camera_created(void) {
    int retried;

    if (g_camera_present)
        return;   /* 已有可用采集，忽略重复事件 */

    /* 节点出现后驱动可能仍在加载，open + QUERYCAP 确认，带重试 */
    for (retried = 0; retried < HOTPLUG_RETRY_MAX; retried++) {
        if (hal_camera_try_connect(g_camera_dev) == 0)
            break;
        usleep(HOTPLUG_RETRY_MS * 1000);
    }
    if (retried >= HOTPLUG_RETRY_MAX) {
        fprintf(stderr, "[HOTPLUG] 摄像头确认失败，等待下次事件\n");
        return;
    }

    printf("[HOTPLUG] 摄像头 %s 已就绪，重连采集\n", g_camera_dev);
    if (capture_restart_preview(g_camera_dev, g_camera_w, g_camera_h) == 0)
        g_camera_present = 1;
}

static void on_camera_deleted(void) {
    if (!g_camera_present)
        return;
    printf("[HOTPLUG] 摄像头 %s 已拔出，停止采集\n", g_camera_dev);
    capture_stop();   /* 幂等：停工作线程 + V4L2 流 */
    g_camera_present = 0;
}

/* ==================== 触摸屏 ==================== */

static void on_touch_created(void) {
    int retried;

    if (g_touch_present)
        return;

    for (retried = 0; retried < HOTPLUG_RETRY_MAX; retried++) {
        if (hal_touch_try_connect(g_touch_dev) == 0)
            break;
        usleep(HOTPLUG_RETRY_MS * 1000);
    }
    if (retried >= HOTPLUG_RETRY_MAX)
        return;

    printf("[HOTPLUG] 触摸屏 %s 已就绪，重新打开\n", g_touch_dev);
    if (hal_touch_init(g_touch_dev) == 0)
        g_touch_present = 1;
}

static void on_touch_deleted(void) {
    if (!g_touch_present)
        return;
    printf("[HOTPLUG] 触摸屏 %s 已拔出\n", g_touch_dev);
    hal_touch_exit();   /* 关闭 fd，lv_port_indev 读失败会保持状态不误触发 */
    g_touch_present = 0;
}

/* ==================== SD 卡 / 存储 ==================== */

/* 存储二级确认：stat 配置的挂载点。
 * 注意：main 在 SD 缺失时会回退 storage 到 /tmp，storage_manager 内部
 * g_root 可能是 /tmp，这里必须按配置的 g_storage_root（如 /mnt/sd）判断，
 * 否则插入 SD 时会被误判为“已可用”而不触发重连。 */
static int storage_root_available(void) {
    struct stat st;

    if (stat(g_storage_root, &st) < 0)
        return -1;

    return S_ISDIR(st.st_mode) ? 0 : -1;
}

static void on_storage_created(void) {
    int retried;

    if (g_storage_present)
        return;

    /* 块设备节点出现后内核挂载可能滞后，以挂载点 stat 为准 */
    for (retried = 0; retried < HOTPLUG_RETRY_MAX; retried++) {
        if (storage_root_available() == 0)
            break;
        usleep(HOTPLUG_RETRY_MS * 1000);
    }
    if (retried >= HOTPLUG_RETRY_MAX)
        return;

    printf("[HOTPLUG] 存储挂载点 %s 可用，重新初始化\n", g_storage_root);
    if (storage_init(g_storage_root, g_storage_capacity) == 0)
        g_storage_present = 1;
}

static void on_storage_deleted(void) {
    if (!g_storage_present)
        return;
    printf("[HOTPLUG] 存储已拔出\n");
    /* 正在录像时先优雅停止（写索引、关文件），避免写到消失的设备 */
    if (recorder_get_state() == RECORDER_RECORDING)
        recorder_stop();
    g_storage_present = 0;
}

/* ==================== 事件分发 ==================== */

static void handle_device_event(const struct inotify_event *ev) {
    if (ev->len <= 0)
        return;   /* 无文件名的系统事件（如队列溢出），跳过 */

    if (strcmp(ev->name, g_camera_base) == 0) {
        if (ev->mask & IN_CREATE)      on_camera_created();
        else if (ev->mask & IN_DELETE) on_camera_deleted();
    } else if (strcmp(ev->name, g_touch_base) == 0) {
        if (ev->mask & IN_CREATE)      on_touch_created();
        else if (ev->mask & IN_DELETE) on_touch_deleted();
    } else if (strncmp(ev->name, "mmcblk", 6) == 0 ||
               strncmp(ev->name, "sd", 2) == 0) {
        /* 整卡 mmcblk0 与分区 mmcblk0p1 都触发；USB 存储为 sdX */
        if (ev->mask & IN_CREATE)      on_storage_created();
        else if (ev->mask & IN_DELETE) on_storage_deleted();
    }
}

/* inotify 不可用时的降级：stat() 轮询，按状态变化调用同样的处理函数 */
static void hotplug_poll_once(void) {
    int present;

    present = (hal_camera_try_connect(g_camera_dev) == 0);
    if (present && !g_camera_present)      on_camera_created();
    else if (!present && g_camera_present) on_camera_deleted();

    present = (hal_touch_try_connect(g_touch_dev) == 0);
    if (present && !g_touch_present)       on_touch_created();
    else if (!present && g_touch_present)  on_touch_deleted();

    present = (storage_root_available() == 0);
    if (present && !g_storage_present)     on_storage_created();
    else if (!present && g_storage_present) on_storage_deleted();
}

/* ==================== 监控线程 ==================== */

static void *hotplug_monitor_thread(void *arg) {
    (void)arg;
    int ifd;
    int wd = -1;

    ifd = inotify_init1(IN_NONBLOCK);
    if (ifd >= 0) {
        wd = inotify_add_watch(ifd, HOTPLUG_WATCH_DIR, IN_CREATE | IN_DELETE);
        if (wd < 0) {
            perror("[HOTPLUG] inotify_add_watch");
            close(ifd);
            ifd = -1;
        }
    } else {
        perror("[HOTPLUG] inotify_init1，回退 stat 轮询");
    }

    while (g_monitor_running) {
        if (ifd < 0) {
            hotplug_poll_once();
            usleep(HOTPLUG_POLL_MS * 1000);
            continue;
        }

        char buf[4096];
        ssize_t len = read(ifd, buf, sizeof(buf));
        if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                usleep(20000);
                continue;
            }
            perror("[HOTPLUG] inotify read");
            break;
        }

        /* 一次 read 可能包含多个事件，逐个解析 */
        for (char *p = buf; p < buf + len; ) {
            const struct inotify_event *ev = (const struct inotify_event *)p;
            handle_device_event(ev);
            p += sizeof(struct inotify_event) + ev->len;
        }
    }

    if (wd >= 0) inotify_rm_watch(ifd, wd);
    if (ifd >= 0) close(ifd);
    return NULL;
}

/* ==================== 公共接口 ==================== */

int hotplug_monitor_init(const char *camera_dev, int camera_w, int camera_h,
                         const char *touch_dev,
                         const char *storage_root, uint64_t storage_capacity) {
    const char *base;

    if (!camera_dev || !touch_dev || !storage_root)
        return -1;

    snprintf(g_camera_dev, sizeof(g_camera_dev), "%s", camera_dev);
    g_camera_w = camera_w;
    g_camera_h = camera_h;
    snprintf(g_touch_dev, sizeof(g_touch_dev), "%s", touch_dev);
    snprintf(g_storage_root, sizeof(g_storage_root), "%s", storage_root);
    g_storage_capacity = storage_capacity;

    /* 只匹配设备文件名，避免 /dev 下其他节点变化误触发 */
    base = strrchr(camera_dev, '/');
    snprintf(g_camera_base, sizeof(g_camera_base), "%s", base ? base + 1 : camera_dev);
    base = strrchr(touch_dev, '/');
    snprintf(g_touch_base, sizeof(g_touch_base), "%s", base ? base + 1 : touch_dev);

    /* 开机时设备可能已就绪，先同步一次当前状态 */
    g_camera_present  = (hal_camera_try_connect(g_camera_dev) == 0);
    g_touch_present   = (hal_touch_try_connect(g_touch_dev) == 0);
    g_storage_present = (storage_root_available() == 0);

    g_monitor_running = 1;
    if (pthread_create(&g_monitor_thread, NULL, hotplug_monitor_thread, NULL) != 0) {
        perror("[HOTPLUG] thread create");
        g_monitor_running = 0;
        return -1;
    }

    printf("[HOTPLUG] monitor started: camera=%s touch=%s storage=%s\n",
           g_camera_base, g_touch_base, g_storage_root);
    return 0;
}

void hotplug_monitor_exit(void) {
    if (!g_monitor_running)
        return;
    g_monitor_running = 0;
    pthread_join(g_monitor_thread, NULL);
    printf("[HOTPLUG] monitor stopped\n");
}
