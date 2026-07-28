# imx6ull-camera-terminal

嵌入式 Linux 车载终端 / 相机采集系统

**板卡**: 韦东山 i.MX6ULL-Pro | **内核**: Linux 4.9.88 | **工具链**: arm-linux-gnueabihf (Linaro 6.2.1) | **UI**: LVGL 8.3.11

---

## 1. 需求分析

### 背景

基于 i.MX6ULL 单核 ARM Cortex-A7 平台，构建一个具备实时相机预览、拍照存储、视频录制、GPS 定位和图形界面交互的车载终端原型系统。

### 核心需求

- **相机预览**: V4L2 采集 MJPEG 流 → 软解码 → LVGL 全屏显示（15fps）
- **拍照**: GPIO 按键触发 → JPEG 保存到 SD 卡
- **录像**: GPIO 按键启停 → MJPEG 帧直写 AVI 文件
- **照片浏览**: 触摸屏操作 → 相册界面翻页/删除
- **GPS 定位**: UART 读取 NMEA 0183 → 独立线程解析 → 主线程读取
- **存储管理**: 自动创建目录、容量统计、循环覆盖
- **图形界面**: LVGL 双页面（主界面 + 相册），物理按键+触摸双输入

### 硬件约束

- 单核 800MHz Cortex-A7，无 GPU
- 256MB DDR3，无硬件视频编解码器
- 800×480 RGB565 Framebuffer
- 两个 GPIO 物理按键 + 电阻触摸屏

---

## 2. 方案设计

### 整体架构

```
┌──────────────────────────────────────────────────┐
│                主线程 (Main Loop)                  │
│  key_manager_task → lv_timer_handler → usleep    │
│  ┌────────────┬────────────┬───────────────────┐  │
│  │ LVGL 绘制  │ 状态刷新   │ MQTT 非阻塞轮询   │  │
│  │ 按键轮询   │ 相册导航   │ GPS 定时读取      │  │
│  └────────────┴────────────┴───────────────────┘  │
├──────────────────────────────────────────────────┤
│  V4L2 桥线程: DQBUF → 入队 → QBUF (只入队不处理)  │
├──────────────────────────────────────────────────┤
│  Worker 线程: 出队 → mjpeg_to_rgb565()          │
│            → lv_async_call(更新预览)            │
│            → recorder_write_frame() (可选)      │
├──────────────────────────────────────────────────┤
│  Recorder 线程: 接收 MJPEG 帧 → write() → SD 卡  │
├──────────────────────────────────────────────────┤
│  GPS 线程: UART 阻塞读取 → NMEA 解析             │
└──────────────────────────────────────────────────┘
```

### 模块划分

| 模块 | 职责 | 线程 |
|------|------|------|
| `key_manager` | GPIO 按键去抖 + 长按/双击策略 | 主线程 |
| `capture` | V4L2 采集 + 帧队列 + MJPEG 解码 | 桥线程 + Worker 线程 |
| `recorder` | AVI 文件写入（MJPEG 帧直写） | 独立线程 |
| `storage_manager` | 照片/录像路径分配、容量统计、循环覆盖 | 主线程 |
| `gps_daemon` | UART 读取 NMEA 0183 解析 | 独立线程 |
| `lv_port_disp` | LVGL → hal_fb → /dev/fb0 显示后端 | 主线程 |
| `lv_port_indev` | 触摸坐标上报 + 按键编码器 | 主线程轮询 |
| `ui_bridge` | 状态标签、预览帧、相册导航桥接 | 主线程 + lv_async_call |

### 线程间通信

| 场景 | 方式 |
|------|------|
| V4L2 桥线程 → Worker（原始 MJPEG 帧） | 环形队列 + 条件变量 |
| Worker → 主线程（预览 RGB565） | `lv_async_call` |
| GPS 线程 → 主线程 | 互斥锁保护全局变量 |
| 录像帧入队 | 线程安全队列，非阻塞写 |

---

## 3. 项目实现

### 目录结构

```
imx6ull-camera-terminal/
├── sources/
│   ├── driver/gpio-keys/          # GPIO 按键内核驱动
│   ├── app/
│   │   ├── common/hal/            # 硬件抽象层 (fb/camera/key/touch)
│   │   ├── modules/
│   │   │   ├── key_manager/       # 按键策略 (短按/长按/双击)
│   │   │   ├── camera_capture/    # V4L2 采集 + MJPEG 解码
│   │   │   ├── recorder/          # AVI MJPEG 录像
│   │   │   ├── storage_manager/   # 存储管理 + 循环覆盖
│   │   │   ├── gps_daemon/        # GPS NMEA 解析
│   │   │   └── mqtt_client/       # MQTT 上传 (预留)
│   │   ├── ui/
│   │   │   ├── lvgl/              # LVGL 8.3.11 源码
│   │   │   ├── porting/           # 显示/输入移植层
│   │   │   ├── app/               # SquareLine Studio 生成
│   │   │   └── lv_conf.h          # LVGL 配置
│   │   └── main/
│   │       ├── main.c             # 主入口 + 业务整合
│   │       ├── ui_bridge.c/h      # UI 事件桥接层
│   │       └── ui_events.c        # 按钮回调实现
│   └── kernel/
├── scripts/                       # 编译/烧写脚本
├── rootfs_overlay/                # 根文件系统覆盖层
├── Makefile                       # 顶层编译
└── docs/
```

### 关键技术点

- **MJPEG 软解码 Pipeline**: V4L2 采集 MJPEG → libjpeg-turbo 解码 → RGB565 直接写入 LVGL 缓冲区，避免额外的颜色空间转换
- **双缓冲防撕裂**: Worker 线程解码到独立缓冲区，`memcpy` 到 LVGL 缓冲区，`lv_async_call` 异步切到主线程显示
- **LVGL 单线程约束**: 所有 `lv_*` API 仅在主线程调用，跨线程更新使用 `lv_async_call`
- **存储循环覆盖**: 按 mtime 升序删除最旧文件，优先淘汰录像（体积大）
- **按键去抖/策略**: 内核驱动+用户态双重去抖，通过 timer 实现短按(200ms)/长按(800ms)/双击识别

### 编译与使用

```bash
# 编译内核驱动
make drivers

# 编译应用
make apps

# 输出文件
build/target/camera_terminal    # 主程序

# 板端运行（默认摄像头 /dev/video1）
./camera_terminal [/dev/videoX]
```

---

## 4. 后续改进与设想

### 短期（计划中）

- **外设热插拔**: udev 规则自动检测摄像头插入/拔出，无需重启
- **开机自启**: Systemd unit / init script 守护进程
- **MQTT 数据上传**: 将 GPS + 照片缩略图上传到云端
- **录音功能**: 通过 WM8960 音频编解码器录制音频

### 中期（可扩展）

- **硬件编解码加速**: 借助 i.MX6ULL 的 PXP 2D 加速引擎做图像缩放/旋转
- **双摄切换**: 支持前后摄像头通过 GPIO 切换
- **碰撞检测 + 自动录像**: 加速度传感器触发紧急录像段
- **配置持久化**: JSON 配置文件保存分辨率、录像时长等参数

### 长期（展望）

- **远程 OTA 升级**: swupdate + 双分区 AB 升级
- **4G 远程监控**: EC20/EC200 模块集成，实时视频流上传
- **AI 辅助**: 移植 TensorFlow Lite Micro 到 ARM 端做前车检测/车道偏离预警
- **功耗优化**: CPU 调频策略、LCD 背光 PWM 自动调节
