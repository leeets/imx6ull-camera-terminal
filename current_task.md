# current_task — 当前任务

> 本文件记录当前项目状态和下一步计划。
> 随项目进展更新。

---

## 项目总览

### ✅ 已完成

**Phase 2 — 按键模块**
- `driver/gpio-keys/gpio_key_drv.c` — GPIO 按键驱动
- `common/hal/hal_key.c/h` — 按键硬件抽象层
- `modules/key_manager/key_manager.c/h` — 按键策略（短按拍照 / 长按录像停 / 双击退出）
- 全链路上板验证通过

**Phase 3 — 摄像头采集（UVC）**
- 方案从 CSI OV5640 切换为 USB 免驱摄像头（UVC）
- `common/hal/hal_camera.c/h` — V4L2 纯 HAL 层
- `modules/camera_capture/capture.c/h` — 业务层（初始化降级/预览/拍照）
- `main/main.c` — 主入口（整合 key + capture）
- 交叉编译验证通过

### 当前目录结构

```
imx6ull-camera-terminal/
├── docs/project-plan.txt
├── sources/
│   ├── kernel/                  # linux-4.9.88 + DTS
│   ├── driver/
│   │   ├── gpio-keys/           # ✅ 
│   │   └── led/                 # 空
│   └── app/
│       ├── common/hal/
│       │   ├── hal_key.c/h      # ✅
│       │   └── hal_camera.c/h   # ✅
│       ├── modules/
│       │   ├── key_manager/     # ✅
│       │   ├── camera_capture/  # ✅
│       │   └── ...              # 待写
│       ├── ui/                  # 待写
│       └── main/main.c          # ✅
├── scripts/
└── rootfs_overlay/
```

### 下一步（按优先级）

1. **Framebuffer 实时预览** — `common/hal/hal_fb.c/h` + `modules/fb_display/`
   - 打开 /dev/fb0，mmap 显存
   - 将 V4L2 采集帧（MJPEG 解码或 YUYV 转换）写入 framebuffer

2. **LVGL 仪表盘 UI** — `app/ui/`
   - 实时相机入口
   - 相册入口
   - GPS 状态信息
   - 录像状态显示

3. **录像模块** — `modules/recorder/`
4. **GPS + 4G + MQTT**

---

## 近期 git 历史
```
021d7ec chore: gitignore 排除编译产物
dbc278e refactor: 分离 HAL 层与 capture 业务层
7a11997 feat: 切换摄像头方案为 USB UVC
97d171f docs: 更新 current_task
31b1c57 feat: 摄像头 HAL + Capture 模块 + main 入口
```
