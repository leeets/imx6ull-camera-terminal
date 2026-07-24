# 设备热插拔稳定性改造方案

## 一、背景与问题

当前系统中有三大类"设备"可能发生热插拔：

1. **摄像头** (`/dev/video1`) — V4L2 设备，有独立采集线程
2. **触摸屏** (`/dev/input/event1`) — input 子系统设备
3. **存储介质** (`/mnt/sd`) — SD 卡 / USB 存储

任何设备在运行时拔除或重新插入，若无保护机制，会导致：
- 文件描述符操作失败（读写已关闭的设备）
- 线程卡死（select/poll 等待已消失的设备）
- 内存泄漏（资源未清理）
- 进程崩溃（段错误 / SIGPIPE）

---

## 二、检测方案：inotify + open/ioctl 二级确认

### 核心思路

```
inotify 监控 /dev/ 目录
  │
  ├── IN_CREATE (设备节点出现)              ──→ open() ──→ ioctl(QUERYCAP) 确认
  │                                             失败则等下次事件重试
  │
  └── IN_DELETE (设备节点删除)              ──→ 标记设备断开 → 清理资源
```

- **一级（inotify）**：事件驱动，设备节点创建/删除时立即通知，不轮询
- **二级（open + ioctl）**：确认设备真正可用，防止"节点有了但驱动没就绪"的假阳性
- **降级（fallback）**：如果 inotify 不可用（罕见），回退到 stat() 轮询

### 为什么选 inotify 而不是其他方案？

| 方案 | 优点 | 缺点 |
|------|------|------|
| **inotify + 二级确认** ✅ | 事件驱动、轻量、无外部依赖、准确 | 需少量额外代码 |
| 纯轮询 stat() | 实现最简单 | CPU 空闲时也在跑，延迟与负载矛盾 |
| udev 规则 | 功能最全 | 依赖 udev 守护进程，Buildroot 默认无 |
| read/write 报错 | 无额外代码 | 被动检测，发现时可能已崩溃 |

---

## 三、各模块热插拔实施细则

### 3.1 摄像头热插拔

| 场景 | inotify 事件 | 二级确认 | 动作 |
|------|-------------|----------|------|
| 插入 | `/dev/video1` 被创建 | `open()` + `ioctl(VIDIOC_QUERYCAP)` | 调用 `capture_init()` + `capture_start_preview()` |
| 拔出 | `/dev/video1` 被删除 | — | 停止采集线程+工作线程，标记 `g_camera_ok=0` |
| ioctl 失败 | — | open 成功但 QUERYCAP 失败 | 等下次 inotify 事件重试（设备还在加载）|

**改造点**：
- `hal_camera.c`: 增加 `hal_camera_try_connect()` 作为二级确认函数
- `capture.c`: 监听 inotify 事件 → 尝试 reconnect
- 断开期间预览工作线程 wait，不消费队列

### 3.2 触摸屏热插拔

| 场景 | inotify 事件 | 二级确认 | 动作 |
|------|-------------|----------|------|
| 插入 | `/dev/input/event1` 被创建 | `open()` + `ioctl(EVIOCGID)` | 重新 `hal_touch_init()` |
| 拔出 | `/dev/input/event1` 被删除 | — | 标记触摸不可用，UI 禁用触摸交互 |

**改造点**：
- `hal_touch.c`: 增加 `hal_touch_try_connect()` 和连接状态查询
- `main.c`: inotify 事件触发后尝试重新 init

### 3.3 SD 卡 / 存储热插拔

SD 卡热插拔分两步：
1. **块设备出现**：`/dev/mmcblk0` 创建 → 内核自动挂载（如果 fstab 配了 auto）
2. **挂载点确认**：`stat("/mnt/sd")` 检查目录是否可用

| 场景 | inotify 事件 | 二级确认 | 动作 |
|------|-------------|----------|------|
| 插入 | `/dev/mmcblk0` 被创建 | `stat("/mnt/sd")` 确认挂载 | `storage_init()` 重新初始化 |
| 拔出 | `/dev/mmcblk0` 被删除 | — | 停止录制 → 标记存储不可用 |

**改造点**：
- `storage_manager.c`: 增加 `g_storage_mounted` 状态
- `recorder.c`: 录制线程检测 write 失败 → graceful stop（不等 inotify）

---

## 四、inotify 事件监控线程

在 `main.c` 或独立模块 `hotplug_monitor.c` 中创建事件监听线程：

```c
static void *hotplug_monitor_thread(void *arg) {
    int inotify_fd = inotify_init1(IN_NONBLOCK);
    int wd = inotify_add_watch(inotify_fd, "/dev", IN_CREATE | IN_DELETE);

    char buf[4096];
    while (g_running) {
        ssize_t len = read(inotify_fd, buf, sizeof(buf));
        if (len < 0) { /* EAGAIN → sleep 重试 */ continue; }

        for (char *p = buf; p < buf + len; ) {
            struct inotify_event *ev = (struct inotify_event *)p;
            if (ev->mask & IN_CREATE) {
                if (strcmp(ev->name, "video1") == 0)
                    try_reconnect_camera();
                else if (strcmp(ev->name, "event1") == 0)
                    try_reconnect_touch();
                else if (strncmp(ev->name, "mmcblk", 6) == 0)
                    try_reconnect_storage();
            } else if (ev->mask & IN_DELETE) {
                /* 设备拔出，标记断开 */
                if (strcmp(ev->name, "video1") == 0)
                    notify_camera_disconnected();
            }
            p += sizeof(struct inotify_event) + ev->len;
        }
    }
    close(inotify_fd);
    return NULL;
}
```

**注意事项**：
- `/dev` 目录下 inotify 事件很频繁，必须按文件名 过滤
- inotify 队列有大小限制（`/proc/sys/fs/inotify/max_queued_events`），嵌入式需调大
- 二级确认是同步的，但重连操作应放在工作线程而不是 inotify 线程中做

---

## 五、状态管理与重连逻辑

### 统一状态定义

```c
typedef enum {
    DEVICE_DISCONNECTED,   /* 设备不在 /dev 中 */
    DEVICE_ATTACHING,      /* 节点出现，正在进行二级确认 */
    DEVICE_CONNECTED,      /* open + ioctl 确认成功，正常运行 */
    DEVICE_ERROR,          /* 运行中 ioctl 失败，等待重连 */
} device_status_t;
```

### 重连参数

每个设备的最多重试次数和间隔由 `reconnect_config_t` 控制：

```c
typedef struct {
    int         max_retries;      /* 最大重试次数，-1 表示持续重试 */
    int         retry_interval_ms;/* 每次重试间隔 */
    device_status_t current;
} reconnect_state_t;
```

---

## 六、跨模块影响

| 模块 | 断开时行为 | 重连时行为 |
|------|-----------|-----------|
| capture | 工作线程 wait，V4L2 线程退出 | 重新 init + start |
| recorder | write 失败 → graceful stop | 不自动恢复（用户手动按录像键）|
| storage | 标记不可用，UI 提示"请插入 SD 卡"| storage_init 重新初始化 |
| hal_fb | 不需要处理（FB 设备不会热插拔）| — |
| key_manager | 不需要处理（GPIO 按键不涉及热插拔）| — |

---

## 七、实现优先级建议

| 优先级 | 模块 | inotify 部分 | 二级确认部分 | 工作量 |
|--------|------|-------------|-------------|--------|
| P0 | hotplug_monitor 框架 | inotify 线程 + 事件分发框架 | — | 1~2 天 |
| P0 | STORAGE 热插拔 | mmcblk 事件 | stat 挂载点 | 1 天 |
| P1 | CAMERA 热插拔 | video 事件 | open + VIDIOC_QUERYCAP | 2~3 天 |
| P2 | TOUCH 热插拔 | event 事件 | open + EVIOCGID | 1 天 |
| P3 | 重连参数调优 | — | 重试间隔/次数配置 | 1 天 |

---

## 八、注意事项与边界情况

1. **`/dev` 目录事件风暴**：`inotify_add_watch` 建议只监控具体设备文件的父路径，或收到事件后按名称过滤，避免每次所有 `/dev` 变化都触发
2. **inotify 队列溢出**：嵌入式系统上默认 `max_queued_events` 可能只有 16384，但仍建议调大到 65536
3. **摄像头驱动加载延迟**：`/dev/video1` 出现后可能需要几毫秒到几百毫秒驱动才完全就绪，二级确认的 `ioctl` 超时保护很重要
4. **SD 卡分区 vs 整卡**：`/dev/mmcblk0` 是整卡，`/dev/mmcblk0p1` 是分区，建议同时监控两者，或用 `stat()` 确认挂载点为准
5. **inotify 的 fallback**：如果 inotify 不可用（例如运行在容器中），可以回退到定时 `stat()` 轮询，代码结构兼容两种模式
