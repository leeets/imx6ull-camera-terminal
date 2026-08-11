# current_task — 项目完成状态（2026-07-28 更新）

---


**网络模块**

- **mqtt_client.c/h**：纯 C 轻量 MQTT v3.1.1 客户端，零外部依赖
  - TCP socket 非阻塞连接
  - 手工组装 CONNECT/PUBLISH/SUBSCRIBE/PINGREQ 报文
  - 30 秒心跳保活
  - 断线自动重连（5 秒间隔）
  - 支持 QoS 0 发布和订阅
  - 订阅回调分发机制
  - 全局状态 `g_mqtt_connected` 供 UI 标签读取

## ✅ 已完成（全部功能）

**Phase 1~7 — 基础框架、驱动、模块**

- 顶层 Makefile，交叉编译链 arm-linux-gnueabihf
- GPIO 按键驱动 + HAL + Key Manager（短按/长按/双击）
- V4L2 摄像头采集（MJPEG + YUYV fallback）
- Framebuffer 显示 + MJPEG→RGB565 转换（libjpeg-turbo）
- AVI MJPEG 录像模块（纯 V4L2 帧缓冲直写）
- Storage Manager 存储管理（照片/录像/循环覆盖）
- GPS 定位模块（gps_daemon: 独立线程+NMEA解析）
- 触摸屏 HAL（hal_touch: /dev/input 读取）
- main.c 全功能整合

**Phase 8 — LVGL UI 全链路**

- 已用 SquareLine Studio 设计两个页面：主界面 + 相册界面
- 主界面含：`ui_ImgPreview`(全屏预览帧)、`ui_BtnAlbum`(相册入口)、`ui_Container`内4个状态标签
- 相册界面含：`ui_imgPhotoPreview`(照片预览)、前后浏览/删除/返回按钮、`ui_lblPhotoInfo`(1/12标志)
- SquareLine 导出到 `sources/app/ui/`，生成的 UI 代码在 `sources/app/ui/app/` 目录
- **LVGL 8.3.11 源码**已下载到 `sources/app/ui/lvgl/`
- **lv_conf.h**已配置（RGB565 16bit, 800x480, 64KB 堆）
- **lv_port_disp.c/h**已实现（单缓冲, hal_fb 后端）
- **lv_port_indev.c/h**已实现（仅触摸 POINTER 模式，物理按键由 key_manager 直接处理）

**Phase 9 — UI 事件桥接 & 业务整合**

- **ui_bridge.c/h**：状态标签定时器（GPS/存储/录像/网络）、预览帧 lv_async_call 更新、相册导航逻辑
- **ui_events.h/c**：SquareLine 虚拟按钮回调（相册入口/返回/翻页/删除），委托给 ui_bridge
- **main.c**集成 LVGL 主循环：`lv_init()` → `lv_port_disp_init()` → `lv_port_indev_init()` → `ui_init()` → `ui_bridge_init()`
- **Makefile**已加入 LVGL 源码、porting、ui/app、ui_bridge 编译
- **storage_manager**已新增 `storage_list_photos()`、`storage_free_photo_list()`、`storage_delete_photo()` 接口
- 重构成 **V4L2 桥线程 + Worker 线程 + Recorder 写入线程 + GPS 线程** 多线程架构

**文档产出**

- `LVGL_UI.md` — LVGL 开发流程总结
- `UI_调用流程.md` — 虚拟按键/物理按键/预览帧三路调用流程
- `can_new_project_design.md` — CAN 总线项目设计文档
- `GPS_引角.md` — GPS 模块接线说明

---

## 📁 当前目录结构

### 核心源代码

```
sources/
├── driver/
│   └── gpio-keys/                  # GPIO 按键驱动模块
├── app/
│   ├── common/
│   │   ├── hal/                    # HAL 层
│   │   │   ├── hal_fb.c/h          # Framebuffer 驱动
│   │   │   ├── hal_key.c/h         # GPIO 按键 HAL
│   │   │   ├── hal_camera.c/h      # V4L2 摄像头 HAL
│   │   │   ├── hal_touch.c/h       # 触摸屏 HAL
│   │   │   └── video_convert.c/h   # MJPEG→RGB565 转换
│   │   └── ipc/                    # IPC 相关
│   ├── modules/
│   │   ├── key_manager/            # 按键管理层（短按/长按/双击）
│   │   ├── camera_capture/         # 摄像头采集 + V4L2桥 + Worker 线程
│   │   ├── fb_display/             # FB 显示封装（含 V4L2→FB 直写路径）
│   │   ├── recorder/               # AVI 录像模块
│   │   ├── storage_manager/        # 存储管理（保存/循环覆盖/列表/删除）
│   │   └── gps_daemon/             # GPS 定位（独立线程+NMEA）
│   ├── main/
│   │   ├── main.c                  # 主入口（LVGL 主循环）
│   │   ├── ui_bridge.c/h           # UI 事件桥接层
│   │   └── 说明.txt                # 桥接层设计说明
│   └── ui/
│       ├── lvgl/                   # LVGL 8.3.11 源码
│       ├── lv_conf.h               # LVGL 配置（RGB565 16bit）
│       ├── porting/
│       │   ├── lv_port_disp.c/h    # 显示移植层（hal_fb 后端）
│       │   ├── lv_port_indev.c/h   # 输入移植层（触摸 POINTER）
│       │   └── 说明.txt
│       └── app/                    # SquareLine Studio 生成
│           ├── ui.h / ui.c
│           ├── ui_events.h / ui_events.c   # 事件回调声明+实现
│           ├── ui_helpers.c/h
│           ├── screens/
│           │   ├── ui_ScreenMain.c/h       # 主界面
│           │   └── ui_ScreenAlbum.c/h      # 相册界面
│           └── components/
└── build/                          # 编译输出
    └── target/
        └── camera_terminal         # 最终可执行文件
```

### 根目录文档

| 文件 | 说明 |
|------|------|
| `README.md` | 项目说明 |
| `project-plan.md` | 开发计划与路线图 |
| `current_task.md` | **本文件** — 项目完成状态 |
| `LVGL_UI.md` | LVGL 开发流程总结 |
| `UI_调用流程.md` | UI 事件调用流程详解 |
| `can_new_project_design.md` | CAN 总线项目设计 |
| `GPS_引角.md` | GPS 模块引脚说明 |
| `Makefile` | 顶层 Makefile |

---

## 🔧 当前硬件配置

| 配置项 | 值 |
|--------|-----|
| 屏幕分辨率 | 800×480 RGB565 |
| 摄像头 | 640×480 MJPEG, 15fps, `/dev/video1` |
| 触摸屏 | `/dev/input/event0` |
| 物理按键 | GPIO 2 个（拍照/录像） |
| GPS | UART 9600bps |
| 存储
| MQTT Broker | 192.168.1.100:1883 | | `/mnt/sd`, 最大 512MB |

---

## 🧵 线程架构（当前实际）

```
┌──────────────────────────────────────────────────┐
│  主线程 (main)                                    │
│  lv_init / lv_port_disp_init / ui_init           │  ← 初始化
│  while(1) {                                      │
│    key_manager_task();  // 5ms 超时轮询按键      │  ← 非阻塞
│    lv_timer_handler();  // LVGL 心跳/动画/定时器 │
│    usleep(5000);                                 │
│  }                                                │
├──────────────────────────────────────────────────┤
│  GPS 线程 (gps_daemon)                           │
│  while(1) { read(uart); parse_nmea(); }          │  ← 阻塞读 UART
├──────────────────────────────────────────────────┤
│  V4L2 桥线程 (capture)                           │
│  while(1) { DQBUF → memcpy → QBUF }             │  ← 只入队不解码
├──────────────────────────────────────────────────┤
│  Worker 线程 (capture)                           │
│  dequeue → mjpeg_to_rgb565                       │  ← 解码+预览+录像
│          → lv_async_call / recorder_write_frame  │
├──────────────────────────────────────────────────┤
│  录像写入线程 (recorder)                         │
│  队列接收 MJPEG → write() → SD 卡               │  ← 不阻塞采集
└──────────────────────────────────────────────────┘
```

### 各模块线程归属

| 模块 | 线程 |
|------|------|
| LVGL 初始化/绘制 | 主线程 |
| `key_manager_task()` / 物理按键→UI | 主线程 |
| 状态标签更新（lv_timer） | 主线程（LVGL 内部） |
| 相册翻页/删除 | 主线程 |
| 摄像头 V4L2 采集 | V4L2 桥线程 |
| MJPEG→RGB565 解码 | Worker 线程 |
| 拍照 JPEG 保存 | Worker 线程 |
| 录像写 AVI | Recorder 线程 |
| GPS 串口读取+NMEA 解析 | GPS 独立线程 |

---

## ⚠️ 已知问题与改进方向

1. **SquareLine 导出会覆盖 `app/`**：所有手改代码在 `ui_bridge.c` 和 `ui_events.c`，不要改 `screens/` 下文件
2. **颜色深度**：SquareLine 默认 `LV_COLOR_DEPTH=32`，已改为 `16`（RGB565），`lv_conf.h` 已配置
3. **LVGL 内存**：`LV_MEM_SIZE` 64KB，双缓冲额外需 800×480×2 ≈ 768KB
4. **CPU 负载**：不录像~75%，录像~80%，imx6ull 单核接近满载。可降低 LVGL 刷新率（`LV_DISP_DEF_REFR_PERIOD=50`）
5. **触摸屏设备节点**：当前硬编码 `/dev/input/event1`，部分板子可能是 `event0`，建议启动时自动检测
6. **预览帧撕裂**：当前单缓冲模式，解码后的 RGB565 通过 memcpy 到 LVGL 缓冲区，偶有撕裂。如需防撕裂可加双缓冲
7. **无网络模块**：MQTT/网络状态标签当前显示占位 "网络: --"，尚未集成网络协议栈
8. **CAN 总线**：`can_new_project_design.md` 为设计文档，尚未实现

---

## 🔍 编译 + 上板差距盘点（2026-08-06 补充）

**总体结论：目前还不能直接编译+上板。jpeglib（libjpeg）确实缺失，但不是唯一缺口 —— 还有顶层 Makefile 路径错误、4 处代码错误、以及上板运行环境（库文件/脚本）未准备。**

### 1. jpeglib / libjpeg 缺失（编译链路硬缺口，已实测）

- `sources/app/modules/fb_display/video_convert.c:6` 包含 `<jpeglib.h>`，Makefile 链接 `-ljpeg`。
- 工具链 sysroot（`ToolChain/gcc-linaro-6.2.1-2016.11-.../arm-linux-gnueabihf/libc/usr/{include,lib}`）内既无 `jpeglib.h` 也无 `libjpeg.*`；宿主机 `/usr/include` 同样没有；全盘未找到可用头文件/库。
- 实际执行 `make apps`，链接阶段报 `ld: cannot find -ljpeg`。
- 源码包已就位但未构建：`/home/lalakala/100ask_imx6ull-sdk/Buildroot_2020.02.x/dl/jpeg-turbo/libjpeg-turbo-2.0.4.tar.gz` + `package/jpeg-turbo`，Buildroot 从未跑过（无 `output/` 目录）。需要交叉编译 libjpeg-turbo 并安装进 sysroot（含 `jpeglib.h`、`libjpeg.so/.a`），上板时还要把 `libjpeg.so.x` 一起拷到板端。
- 补充：本项目只用到 JPEG **解码**（MJPEG→RGB565，`jpeg_mem_src` 属 libjpeg-turbo API）；拍照/录像都是把摄像头直接输出的 MJPEG 帧落盘，**不需要 JPEG 编码**。
- 替代方案：仓库里已有 `fb_display/tjpgd.c`（Tiny JPEG 解码器，纯 C 零依赖），可替换 libjpeg 直接免掉该外部依赖。

### 2. 顶层 Makefile 路径错误（编译第一步就卡在这）

- `APP_DIR := app`、`DRV_DIR := driver` 等路径全部少了 `sources/` 前缀。
- 从项目根目录 `make apps`：`$(wildcard app/...)` 全部为空 → 0 个 .o，直接执行空链接并报 `-ljpeg`；`make drivers` 同样找不到 `driver/` 目录。
- 修复：Makefile 内路径统一加 `sources/` 前缀（临时绕过可 `cd sources && make -f ../Makefile apps`，但应修 Makefile）。

### 3. 修完依赖后仍会编译失败的代码问题（已逐个实测）

| 文件位置 | 问题 |
| --- | --- |
| `sources/app/ui/app/ui_events.h:14` | `#include "../ui.h"` 路径错误，应为 `"ui.h"`（同目录）。导致 ui.c、screens/*.c、ui_helpers.c、ui_events.c、ui_bridge.c 全部 fatal error |
| `sources/app/common/hal/hal_camera.c:30` | 行尾多出游离 token：`fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;pixelformat` |
| `sources/app/modules/mqtt_client/mqtt_client.c:418` | `sub->cb(...)` 调用后缺分号 |
| 顶层 Makefile CFLAGS | 缺 `-I$(MOD_DIR)/mqtt_client`，`ui_bridge.c` 的 `#include "mqtt_client.h"` 找不到头 |
| `sources/app/main/ui_bridge.c:113` | `refresh_status_labels()` 缺函数收尾 `}`（全文件 `{` 28 个 / `}` 27 个），后续函数被解析成 GNU 嵌套函数，能编过但必须补上 |

### 4. 上板运行环境缺口

- `build/` 不存在：应用层从未成功编译过（仅 `sources/driver/gpio-keys/gpio_key_drv.ko` 是 2026-07-07 编译的旧产物）。
- `rootfs_overlay/`、`scripts/`、`tools/` 全部为空：没有 libjpeg.so.x、没有 insmod/启动脚本、没有 udev 规则、没有部署/烧写脚本。
- 上板最小动作：交叉编译出 `camera_terminal` → 连同 `libjpeg.so.x`、`gpio_key_drv.ko` 拷到板端 → insmod → 确认 `/dev/video1`、`/dev/input/event0`、SD 卡挂载点 `/mnt/sd` 存在后运行。

### 5. 文档与现状不一致的地方（顺带发现）

- MQTT：`mqtt_client.c/h` 已实现（733 行）并集成进 main.c（`mqtt_init`/`mqtt_connect`/`mqtt_loop`），ui_bridge 已在读 `g_mqtt_connected`。本文件“已知问题”第 7 条“无网络模块”已过期。
- 触摸屏：main.c 实际用 `/dev/input/event0`，本文件第 5 条写的 event1 已过期。
- 相册 JPEG 预览：`ui_bridge.c` 用 `lv_img_set_src(ui_imgPhotoPreview, 文件路径)` 直接加载 JPEG，但 lv_conf.h 中 `LV_USE_FS_STDIO/POSIX` 均为 0，也未开 `LV_USE_SJPG` —— LVGL 8.3 没有可用的 JPEG 文件解码链路，相册预览上板后大概率不显示。需要接解码（仓库自带 sjpg/tjpgd）或读文件解码后转 `lv_img_dsc_t` 再上板验证。

### 6. 建议的最小修复顺序

1. Makefile：路径加 `sources/` 前缀 + CFLAGS 补 mqtt_client 的 include 目录。
2. jpeglib：交叉编译 libjpeg-turbo 2.0.4 装进 sysroot（或改接 tjpgd.c 去掉 `-ljpeg`）。
3. 修 4 处代码错误：ui_events.h include、hal_camera.c、mqtt_client.c、ui_bridge.c 大括号。
4. 准备上板环境：libjpeg.so.x、驱动 insmod、部署脚本。
5. 相册预览接 JPEG 解码后再上板验证。
