# current_task — 当前任务

---

## ✅ 已完成

**Phase 1~7 — 基础框架、驱动、模块全链路**
- 顶层 Makefile，交叉编译链 arm-linux-gnueabihf
- GPIO 按键驱动 + HAL + Key Manager（短按/长按/双击）
- V4L2 摄像头采集（MJPEG + YUYV fallback）
- Framebuffer 显示 + MJPEG→RGB565 转换（libjpeg-turbo）
- AVI MJPEG 录像模块（纯 V4L2 帧缓冲直写）
- Storage Manager 存储管理（照片/录像/循环覆盖）
- main.c 全功能整合

**Phase 8 — LVGL UI 结构与 SquareLine 导出**
- 已用 SquareLine Studio 设计两个页面：主界面 + 相册界面
- 主界面含：`ui_ImgPreview`(全屏预览帧)、`ui_BtnAlbum`(相册入口)、`ui_Container`内4个状态标签
- 相册界面含：`ui_imgPhotoPreview`(照片预览)、前后浏览/删除/返回按钮、`ui_lblPhotoInfo`(1/12标志)
- SquareLine 导出到 `sources/app/ui/`，生成的 UI 代码在 `sources/app/ui/app/` 目录

---

## 🏗 当前目录结构

```
sources/app/ui/
├── lvgl/                     # [待填充] LVGL 8.3.11 源码（需自己下载）
├── porting/                  # [待编写] LVGL 移植层
│   ├── lv_port_disp.c/h      # 显示驱动：用 hal_fb 输出到 /dev/fb0
│   ├── lv_port_indev.c/h     # 输入驱动：用 key_manager 生成 LVGL 输入事件
│   └── lv_port_fs.c/h        # [可选] 文件系统驱动
├── lv_conf.h                 # [待编写] LVGL 配置头文件（在 ui/ 根目录）
├── assets/                   # [可选] 字体、图标等资源文件
└── app/                      # SquareLine Studio 生成，不要手改（除了 ui_events.h）
    ├── ui.h / ui.c
    ├── ui_events.h           # [可编辑] SquareLine 保留的事件回调声明
    ├── ui_helpers.c/h
    ├── ui_comp_hook.c
    ├── CMakeLists.txt
    ├── filelist.txt
    ├── project.info
    ├── screens/
    │   ├── ui_ScreenMain.c/h     # 主界面控件定义（lvgl 对象创建）
    │   └── ui_ScreenAlbum.c/h    # 相册界面控件定义
    └── components/
        └── ui_comp_hook.c
```

---

## 🔧 具体工作清单（按文件）

### 1️⃣ 获取 LVGL 源码

**创建/下载** → `sources/app/ui/lvgl/`

| 操作 | 说明 |
|------|------|
| Ubuntu 中 cd 到项目根目录 | `cd /path/to/imx6ull-camera-terminal` |
| git submodule 添加 LVGL | `cd sources/app/ui && git submodule add -b release/v8.3 https://github.com/lvgl/lvgl.git lvgl` |
| 或者手动下载 | 从 GitHub 下载 v8.3.11 解压到 `sources/app/ui/lvgl/` |

### 2️⃣ 编写 lv_conf.h

**新建文件** → `sources/app/ui/lv_conf.h`

| 配置项 | 建议值 | 说明 |
|--------|--------|------|
| `LV_COLOR_DEPTH` | `16` | imx6ull fb 是 RGB565（SquareLine 默认 32，需改）|
| `LV_HOR_RES_MAX` | `800` | 开发板屏幕实际宽 |
| `LV_VER_RES_MAX` | `480` | 开发板屏幕实际高 |
| `LV_MEM_SIZE` | `(64U * 1024U)` | LVGL 堆大小，默认 32KB 不够 |
| `LV_USE_PERF_MONITOR` | `0` | 调试时再开 |
| `LV_FONT_MONTSERRAT_14` | `1` | 至少开一个系统字体 |
| `LV_DISP_DEF_REFR_PERIOD` | `50` | 默认30ms，调低刷新率省CPU |

### 3️⃣ 编写显示移植层

**新建文件** → `sources/app/ui/porting/lv_port_disp.c` + `lv_port_disp.h`

| 关键函数 | 作用 |
|----------|------|
| `lv_port_disp_init()` | 初始化显示驱动，注册 `disp_flush` 回调 |
| `disp_flush(disp_drv, area, color_p)` | LVGL 绘制完成后回调，将缓冲区通过 `hal_fb_draw_rgb565()` 写入 `/dev/fb0`，然后调用 `lv_disp_flush_ready()` |

这个文件会 `#include "hal_fb.h"`，直接调用现有的 `hal_fb_draw_rgb565()`。

### 4️⃣ 编写输入移植层

**新建文件** → `sources/app/ui/porting/lv_port_indev.c` + `lv_port_indev.h`

| 关键函数 | 作用 |
|----------|------|
| `lv_port_indev_init()` | 初始化输入设备（按键编码器模式）|
| `keypad_read(indev_drv, data)` | 被 LVGL 轮询，回传当前按键编码 |

推荐用 **编码器（encoder）模式**：
- 拍照键 → `LV_KEY_ENTER`（确认/拍照）
- 录像键短按 → `LV_KEY_ESC`（返回/停止录像）
- 相册中前后按钮 → `LV_KEY_LEFT` / `LV_KEY_RIGHT`（翻页）
- `lv_group` 管理焦点，`ui_BtnAlbum` 等按钮加入 group

### 5️⃣ 改写 main.c — 集成 LVGL 主循环

**修改文件** → `sources/app/main/main.c`

| 改动点 | 说明 |
|--------|------|
| 新增 `#include` | `"ui.h"`、`"lv_port_disp.h"`、`"lv_port_indev.h"` |
| `main()` 开头 | 加 `lv_init()` → `lv_port_disp_init()` → `lv_port_indev_init()` → `ui_init()` |
| 主循环 | `while(1) { key_manager_task(); lv_timer_handler(); usleep(5000); }` |
| 删除 | 移除 `hal_fb_init()`（LVGL 接管显示） |
| `on_preview_frame` | 不再直接写 fb，改为通过 `lv_async_call` 更新 LVGL 控件 |

### 6️⃣ 实现预览帧→LVGL 图像控件

**新建文件** → `sources/app/main/ui_bridge.c` + `ui_bridge.h`

在 `ui_bridge.c` 中：

| 函数 | 作用 |
|------|------|
| `update_preview_img(void *data)` | 被 `lv_async_call` 调用，用 `lv_canvas_set_px` 或 `lv_img_set_src` 更新 `ui_ImgPreview` |
| `on_preview_frame_lvgl(frame, ...)` | 在采集线程中调用，解码 MJPEG→RGB565 后 `memcpy` 到共享缓冲区，然后 `lv_async_call(update_preview_img, NULL)` |
| `ui_bridge_init()` | 初始化 canvas、事件绑定、状态定时器 |

### 7️⃣ 状态标签实时绑定

**追加到** → `sources/app/main/ui_bridge.c`

`lv_timer_create(update_status_cb, period_ms, NULL)` 注册定时器：

| 标签变量 | 数据来源 | 更新周期 |
|----------|----------|----------|
| `ui_LabelNet` | 全局变量 `g_net_status`（MQTT 更新） | 2秒 |
| `ui_LabelRecStatus` | `recorder_get_state()` | 0.5秒 |
| `ui_LabelStorage` | 新增 `storage_usage_percent()` | 5秒 |
| `ui_LabelGps` | 全局变量 `g_gps_str`（GPS 线程更新） | 2秒 |

### 8️⃣ 物理按键与 UI 按钮绑定

| 文件 | 操作 |
|------|------|
| `sources/app/ui/app/ui_events.h` | 声明回调函数签名：`void on_btn_album_clicked(lv_event_t *e)` 等 |
| `sources/app/main/ui_bridge.c` | 在 `ui_bridge_init()` 中用 `lv_obj_add_event_cb(ui_BtnAlbum, on_btn_album_clicked, LV_EVENT_CLICKED, NULL)` 绑定 |

这样即使 SquareLine 重新导出覆盖 `screens/` 目录，回调绑定也不会丢失。

### 9️⃣ 相册界面业务逻辑

**追加到** → `sources/app/main/ui_bridge.c`

| 按钮 | 回调函数 | 逻辑 |
|------|----------|------|
| `ui_btnPrev` | `on_btn_prev_clicked` | 上一张 → `storage_get_photo_path(--index)` → 加载/解码 JPEG → 显示到 `ui_imgPhotoPreview` → 更新 `ui_lblPhotoInfo` |
| `ui_btnNext` | `on_btn_next_clicked` | 下一张，同上 |
| `ui_btnDelete` | `on_btn_delete_clicked` | 删除当前照片 → `storage_delete_photo()` → 重载列表 |
| `ui_btnBack` | `on_btn_back_clicked` | `lv_scr_load(ui_ScreenMain)` 返回主界面 |

### 🔟 更新 storage_manager.h 暴露照片接口

**修改文件** → `sources/app/modules/storage_manager/storage_manager.h`

新增函数（让 UI 能获取照片列表）：

```c
int  storage_get_photo_count(void);
const char *storage_get_photo_path(int index);
int  storage_load_photo(const char *path, void **out_buf, size_t *out_len);
int  storage_delete_photo(const char *path);
int  storage_usage_percent(void);
```

### 1️⃣1️⃣ 更新顶层 Makefile

**修改文件** → `Makefile`

| 改动 | 说明 |
|------|------|
| `UI_DIR`, `LVGL_DIR`, `PORT_DIR` | 指向 `sources/app/ui/` 子目录 |
| `LVGL_SRCS` | 收集 `lvgl/src/*.c` 和 `lvgl/src/**/*.c` |
| `PORT_SRCS` | `porting/*.c` |
| `UI_SRCS` | `app/*.c` + `app/screens/*.c` + `app/components/*.c` |
| `UI_BRIDGE_SRCS` | `main/ui_bridge.c` |
| `CFLAGS` 增加 | `-I$(UI_DIR) -I$(LVGL_DIR) -I$(PORT_DIR)` |
| `LDFLAGS` 增加 | `-lm`（LVGL 需要数学库）|

---

## 🧵 线程架构设计

### 推荐架构：单进程多线程（不需要 IPC）

```
┌──────────────────────────────────────────────────┐
│ 主进程 camera_terminal（单进程，不需多进程/IPC） │
├──────────────────────────────────────────────────┤
│ Thread 1 (主线程):                               │
│   LVGL 绘制 + 按键处理 + 状态标签 + MQTT + GPS   │
│   运行: while(1) {                               │
│     key_manager_task();     ← 非阻塞轮询按键     │
│     lv_timer_handler();     ← LVGL 心跳+绘制     │
│     mqtt_loop();            ← MQTT 非阻塞轮询    │
│     gps_read_nb();          ← GPS 非阻塞读串口   │
│     usleep(5000);           ← 5ms 周期           │
│   }                                               │
│                                                  │
│ Thread 2 (采集线程):                             │
│   V4L2 DQBUF → 解码 → 回调                      │
│   由 hal_camera_start() 内部 pthread_create 创建  │
│                                                  │
│ Thread 3 (GPS线程, 可选):                        │
│   如果 GPS 需要独立管理/休眠唤醒，单独开线程     │
│   否则在主线程里非阻塞轮询                       │
└──────────────────────────────────────────────────┘
```

### 各模块归属

| 模块 | 所属线程 | 原因 |
|------|----------|------|
| LVGL 初始化/绘制 | **主线程** | LVGL 所有 API 必须在同一线程调用 |
| `key_manager_task()` | **主线程** | `select` 带 5ms 超时的非阻塞轮询 |
| 物理按键→UI事件 | **主线程** | 按键回调中调 `lv_async_call` |
| 状态标签更新 | **主线程** | `lv_timer_create` 注册到 LVGL 内部 |
| MQTT 心跳/接收 | **主线程** | `mosquitto_loop()` 非阻塞轮询 |
| GPS 串口读取 | **主线程** 或独立线程 | 9600bps 数据量极小，非阻塞读 |
| 相册翻页/删除 | **主线程** | 用户点击触发，LVGL 事件上下文 |
| 摄像头 V4L2 采集 | **采集线程** | 必须持续运行，15fps 不能阻塞 |
| MJPEG→RGB565 解码 | **采集线程** | 解码完再传给主线程显示 |
| 录像写 AVI | **采集线程** | MJPEG 帧直写文件，不耗时 |
| 拍照 JPEG 数据保存 | **采集线程** | `storage_save_photo` 直接存盘 |

### 线程间通信

| 场景 | 方式 |
|------|------|
| 采集线程 → UI 主线程（预览帧） | `lv_async_call(update_preview_cb, shared_buf)` |
| 按键事件 → 业务逻辑 | 直接在 `on_key_event()` 中处理，无需 IPC |
| GPS 线程 → 状态标签 | 全局 `char gps_data[128]`，主线程定时读取 |
| MQTT 接收 → 状态标签 | 全局变量 `volatile int net_connected` |

---

## 📋 按文件汇总

| 操作 | 文件路径 |
|------|----------|
| 🆕 下载/子模块 | `sources/app/ui/lvgl/`（LVGL 8.3.11 源码） |
| 🆕 新建 | `sources/app/ui/lv_conf.h` |
| 🆕 新建 | `sources/app/ui/porting/lv_port_disp.c/h` |
| 🆕 新建 | `sources/app/ui/porting/lv_port_indev.c/h` |
| 🖊 修改 | `sources/app/main/main.c`（集成 LVGL 主循环） |
| 🆕 新建 | `sources/app/main/ui_bridge.c/h`（UI 事件回调 + 状态刷新 + 相册逻辑） |
| 🖊 编辑 | `sources/app/ui/app/ui_events.h`（声明事件回调签名） |
| 🖊 修改 | `sources/app/modules/storage_manager/storage_manager.h`（新增照片列表接口） |
| 🖊 修改 | `Makefile`（加入 LVGL/UI 编译） |

---

## ⚠️ 注意事项

1. **SquareLine 导出会覆盖 `app/`**：所有手改代码写在 `ui_bridge.c`，不要改 `screens/` 下文件
2. **颜色深度**：SquareLine 默认 `LV_COLOR_DEPTH=32`，imx6ull 是 RGB565(16bit)，需要在 SquareLine 项目设置改为 RGB565 或手动改 `lv_conf.h` 和删除 `ui.c` 中的 `#error`
3. **LVGL 内存**：`LV_MEM_SIZE` 至少 64KB，双缓冲额外需要 800×480×2 ≈ 768KB
4. **预览帧拷贝**：采集线程解码后的 RGB565 缓冲区（614KB/帧）需要用 `memcpy` 到 LVGL 缓冲区，建议用双缓冲避免撕裂
5. **LVGL 是单线程的**：所有 `lv_*` API 必须在主线程调用，从采集线程更新 UI 必须用 `lv_async_call`
6. **CPU 紧张**：不录像约 75%、录像约 80%，imx6ull 单核能跑但接近满载。可降低 LVGL 刷新率（`LV_DISP_DEF_REFR_PERIOD=50`）


---

## ✋ 新模块：触摸屏 HAL (hal_touch)

**已添加** → `sources/app/common/hal/hal_touch.h` + `hal_touch.c`

| 函数 | 作用 |
|------|------|
| `hal_touch_init(dev_path)` | 打开 `/dev/input/eventX`，返回 fd |
| `hal_touch_read(&ev)` | 读取触摸事件（坐标+按下/抬起/移动） |
| `hal_touch_set_nonblock(bool)` | 设置非阻塞/阻塞模式 |
| `hal_touch_get_resolution(w,h)` | 获取屏幕分辨率 |
| `hal_touch_exit()` | 关闭触摸屏 |

**为什么需要触摸屏 HAL：**

SquareLine 按钮（`ui_BtnAlbum`、`ui_btnPrev`、`ui_btnNext` 等）默认等待触摸（或鼠标）点击事件。没有触摸输入驱动，LVGL 的按钮无法被触发。

**触摸输入与物理按键双轨并存：**

```
用户交互方式：
├── 触摸屏 (Touch)       → lv_port_indev.c (touch 模式)   → 直接点 UI 按钮
└── 物理按键 (Key GPIO)  → key_manager → lv_port_indev.c (encoder 模式) → ENTER/ESC 导航
```

**lv_port_indev.c 中二选一或同时开启：**

| 模式 | 初始化函数 | 数据来源 |
|------|-----------|----------|
| `LV_INDEV_TYPE_POINTER` | 调用 `hal_touch_read()` | 触摸坐标，LVGL 自动检测点击命中 |
| `LV_INDEV_TYPE_ENCODER` | 调用 `key_manager`+ `hal_key` | 物理按键映射为 ENTER/ESC/PREV/NEXT |

> ⚠️ 触摸屏设备节点可能是 `/dev/input/event1` 或 `/dev/input/event0`，
> 具体取决于 kernel 的注册顺序。在板子上 `ls /dev/input/` 确认后再配置。
> 如果触摸屏需要校准，可以在 `hal_touch_init` 后调用 `tslib` 或手动校准（5点校准）。

**文件已更新但无需修改 Makefile** — 因为 `HAL_SRCS := $(wildcard $(HAL_DIR)/*.c)` 已经自动包含 `hal_touch.c`。

