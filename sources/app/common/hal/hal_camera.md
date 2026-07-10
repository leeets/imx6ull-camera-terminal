# hal_camera.c — 函数作用说明

V4L2 摄像头硬件抽象层。封装了 Linux V4L2 视频采集的全部 ioctl 流程，
提供 `init/start/capture_one/stop/exit` 五个公共接口。
内部管理设备 fd、mmap 缓冲区、采集线程。

---

## 内部辅助函数（static，不对外暴露）

### `dump_formats(int fd)`
枚举 V4L2 设备支持的所有像素格式，调用 `VIDIOC_ENUM_FMT`，
打印格式序号、四字符码（如 MJPG / YUYV）和描述文字。
只在初始化时调用一次，用于调试确认摄像头支持的格式。

### `request_buffers(int count)`
申请并映射 V4L2 mmap 缓冲区，步骤：
1. 如果已有缓冲区，先 `munmap` 释放旧的
2. 调用 `VIDIOC_REQBUFS` 申请 count 个缓冲区
3. 循环调用 `VIDIOC_QUERYBUF` 获取每个缓冲区的物理偏移和大小
4. `mmap` 映射到用户空间，存入全局 `g_bufs[]` 数组
返回 0 成功，-1 失败

### `queue_all_buffers(void)`
将所有已映射的缓冲区入队到 V4L2 驱动，调用 `VIDIOC_QBUF`。
必须在 `VIDIOC_STREAMON` 之前调用，驱动需要至少有一个已入队的缓冲区才能开始采集。
返回 0 成功，-1 失败

### `free_buffers(void)`
循环 `munmap` 释放所有 mmap 缓冲区，`free` 释放 `g_bufs` 数组。

---

## 采集线程

### `capture_thread_func(void *arg)`
独立线程，循环执行：
1. `select()` 阻塞等待 V4L2 设备有数据可读（2 秒超时）
2. `VIDIOC_DQBUF` 取出已填好的帧
3. 调用注册的回调 `g_frame_cb`，把帧数据传给上层
4. `VIDIOC_QBUF` 把缓冲区重新入队
`g_streaming = 0` 时退出循环。

---

## 公共接口

### `hal_camera_init(dev_path, params)`
打开 V4L2 设备并配置格式：
1. `open(dev_path)` — 打开 /dev/videoX
2. `VIDIOC_QUERYCAP` — 查询设备能力，确认支持 VIDEO_CAPTURE 和 STREAMING
3. `dump_formats` — 打印支持的格式
4. `VIDIOC_S_FMT` — 设置分辨率和像素格式（params 传入）
5. 记录实际设置的分辨率
返回 0 成功，-1 失败

### `hal_camera_start(cb, user_data)`
启动视频流采集：
1. 检查非空且未启动
2. `request_buffers(4)` — 申请 4 个 mmap 缓冲区
3. `queue_all_buffers()` — 全部入队
4. 注册帧回调 `cb` 到 `g_frame_cb`
5. `VIDIOC_STREAMON` — 通知驱动开始采集
6. `pthread_create` — 启动 `capture_thread_func` 线程
返回 0 成功，-1 失败

### `hal_camera_capture_one(frame)`
单帧捕获（用于拍照，不启动线程）：
1. 临时申请 2 个 mmap 缓冲区
2. 全部入队
3. `VIDIOC_STREAMON` 启动流
4. `VIDIOC_DQBUF` 取一帧
5. `VIDIOC_QUERYBUF` 获取该帧的 mmap 地址
6. 将数据和长度填入 `frame` 结构体，返回给调用者
7. 调用者处理后必须 `munmap(frame->data)`
8. 停止流，释放缓冲区
返回 0 成功，-1 失败

### `hal_camera_stop(void)`
停止视频流：
1. 设置 `g_streaming = 0`，采集线程退出循环
2. `pthread_join` 等待线程结束
3. `VIDIOC_STREAMOFF` 通知驱动停止
4. `free_buffers()` 释放所有 mmap 缓冲区

### `hal_camera_exit(void)`
完全关闭摄像头：
1. `hal_camera_stop()` — 停止流
2. `close(g_fd)` — 关闭设备文件

---

## 典型调用流程

```
hal_camera_init("/dev/video0", &params);
    └─ open /dev/video0
    └─ QUERYCAP → 检查能力
    └─ S_FMT → 设置 640x480 MJPEG

hal_camera_start(my_callback, NULL);
    └─ REQBUFS(4) → mmap x4
    └─ QBUF x4
    └─ STREAMON
    └─ pthread_create → capture_thread_func
                             └─ select → DQBUF → callback → QBUF (循环)

hal_camera_capture_one(&frame);
    └─ REQBUFS(2) → QBUF x2 → STREAMON → DQBUF → QUERYBUF → mmap
    └─ 返回 frame.data / frame.length
    └─ 调用者使用完后 munmap(frame.data)

hal_camera_stop();
    └─ g_streaming=0 → 线程退出 → STREAMOFF → free_buffers

hal_camera_exit();
    └─ stop() → close(fd)
```
