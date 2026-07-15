# current_task — 当前任务

---

## ✅ 已完成

**Phase 2 — 按键模块**
- driver/gpio-keys/, hal_key.c/h, key_manager.c/h → 全链路验证通过

**Phase 3 — 摄像头采集**
- hal_camera.c/h — V4L2 纯 HAL（init/start/capture_one/stop/exit）
- capture.c/h — 业务层（YUYV 强制模式，init/start_preview/take_photo）
- main/main.c — 主入口

**Phase 4 — Framebuffer 显示（代码完成）**
- hal_fb.c/h — 轻量 fb HAL（init/draw_rgb565/clear/exit）
- video_convert.c/h — YUYV→RGB565 转换（纯整数算法，0 外部依赖）
- 删除了旧的韦东山 DispOpr 框架（framebuffer.c / disp_manager.*）
- 默认像素格式从 MJPEG 改为 YUYV
- 交叉编译验证通过

**Phase 5 — Recorder 录像模块（代码完成）**
- recorder.c/h — AVI MJPEG 逐帧打包
  - RIFF('AVI ') → LIST('hdrl') → LIST('movi') → idx1 完整 AVI 结构
  - 帧级别写入: write_chunk('00dc') + padding
  - 录完回写: avih.total_frames, strh.length, RIFF size, idx1 索引表
  - 无外部依赖，纯 V4L2 帧缓存逐帧存盘
- 待集成到 main.c（按键触发 recorder_start/stop）

---

## 当前目录结构

```
sources/
├── common/hal/
│   ├── hal_key.c/h        # 按键 HAL
│   ├── hal_camera.c/h     # 摄像头 V4L2 HAL
│   └── hal_fb.c/h         # Framebuffer HAL
├── modules/
│   ├── key_manager/       # 按键策略
│   ├── camera_capture/    # 摄像头业务层
│   ├── fb_display/
│   │   └── video_convert.c/h  # YUYV→RGB565 转换
│   ├── recorder/          # ✅ AVI MJPEG 录像模块（已完成）
│   ├── gps_daemon/        # （待写）
│   ├── mqtt_client/       # （待写）
│   └── storage_manager/   # （待写）
├── ui/                    # LVGL（待写）
├── driver/gpio-keys/      # GPIO 按键驱动
└── main/main.c
```

## 数据流（YUYV 模式）

```
USB 摄像头 (YUYV)
  → hal_camera_start() 帧回调
  → capture.c: preview_bridge
  → fb_display: yuyv_to_rgb565()
  → hal_fb_draw_rgb565()
  → /dev/fb0 显示
```

---

## 下一步

1. **storage_manager/ — 统一管理拍照+录像存储 + 拍照编码（libjpeg-turbo）**
2. **集成 recorder 到 main.c — 按键触发录像启停**
3. **LVGL UI — 仪表盘页面**
4. **GPS + 4G + MQTT**

---

## 近期 git 历史
```
(当前未提交)
```
