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

**Phase 6 — Storage Manager 存储管理（代码完成）**
- storage_manager.c/h — 统一管理拍照+录像存储
  - 自动生成路径: photo/IMG_*.jpg, video/REC_*.avi
  - 容量限制 + 循环覆盖（按 mtime 升序删除最旧文件）
  - 目录自动创建（photo/ video/）
  - 查询统计: photo_count, video_count, total_bytes

**Phase 7 — main.c 全功能整合（代码完成）**
- 完整主循环: init_all → key_manager_task
- 按键拍照 → storage_save_photo 存盘
- 按键录像 → recorder_start/stop + storage_alloc_path
- fb 预览 → on_preview_frame → yuyv_to_rgb565 → hal_fb_draw_rgb565
- 优雅退出: 停止录像 → 释放资源

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
│   ├── storage_manager/   # ✅ 存储管理（已完成）
│   ├── gps_daemon/        # （待写）
│   └── mqtt_client/       # （待写）
├── ui/                    # LVGL（待写）
├── driver/gpio-keys/      # GPIO 按键驱动
└── main/main.c
```

## 数据流（YUYV 模式）

```
USB 摄像头 (YUYV)
  → hal_camera_start() 帧回调
  → capture.c: preview_bridge
  → on_preview_frame (main.c)
      ├── yuyv_to_rgb565() → hal_fb_draw_rgb565() → /dev/fb0
      └── recorder_write_frame() (正在录像时)

按键事件:
  [拍照键] 短按 → capture_take_photo() → on_photo_captured()
                                        → storage_save_photo() → SD卡
  [录像键] 短按 → storage_alloc_path()  → recorder_start()
           长按 → recorder_stop()
           双击 → 全部释放 → exit
```

---

## 下一步

1. **上板验证全链路** — 插入 USB 摄像头，运行 camera_terminal 测试拍照+录像+预览
2. **LVGL UI — 仪表盘页面**
3. **GPS + 4G + MQTT**
4. **开机自启脚本 + 稳定性测试**

---

## 近期 git 历史
```
(当前未提交)
```
