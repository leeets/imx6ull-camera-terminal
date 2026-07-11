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
│   ├── recorder/          # （待写）
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

1. **上板验证全链路** — 插入 USB 摄像头，运行 camera_terminal
2. **recorder/ — MJPEG 逐帧打包 AVI**
3. **storage_manager/ — 统一管理拍照+录像存储**
4. **LVGL UI — 仪表盘页面**
5. **GPS + 4G + MQTT**

---

## 近期 git 历史
```
(当前未提交)
```
