# current_task — 当前任务

> 本文件记录当前项目状态和下一步计划。
> 随项目进展更新。

---

## 当前项目状态

### Phase 2 — 按键模块 ✅ 完成
- `driver/gpio-keys/` — GPIO 按键驱动（insmod 验证通过）
- `common/hal/hal_key.c/h` — 按键硬件抽象层
- `modules/key_manager/key_manager.c/h` — 按键策略（短按/长按/双击）
- 全链路上板测试通过 ✅

### Phase 3 — 摄像头采集 ✅ 代码完成

#### 变更记录
- CSI 接口 OV5640 方案 → USB 免驱摄像头（UVC）
- 原因：OV5640 依赖 CSI 管脚、I2C GPIO 控制，硬件验证复杂且板子上 probe 报错 -22
- UVC 方案优势：即插即用、内核自带驱动、V4L2 接口完全一致

#### DTS
- ECSPI1 → disabled（释放 CSI 管脚）
- CSI pinmux → CSI 功能（已配好，保留不动）
- OV5640 节点 → 已添加 rst/pwdn/mclk/csi_id 属性（弃用，保留为 CSI 方案备选）
- UVC 摄像头不需要任何 DTS 修改

#### 应用层
- `common/hal/hal_camera.h` — V4L2 HAL 接口（与 OV5640/UVC 通用）
- `modules/camera_capture/capture.c/h` — Capture 业务模块（与 OV5640 时代完全兼容，V4L2 接口没变）
- `main/main.c` — 主入口
- 交叉编译验证通过 ✅

### 下一步

1. **上板验证 UVC 摄像头**
   - 插入 USB 摄像头 → `lsusb`、`dmesg` 确认 uvcvideo 加载
   - `ls /dev/video*` 确认设备节点
   - `v4l2-ctl -d /dev/video0 --list-formats` 确认格式，优先 MJPEG

2. **Phase 4 — 录像 & 存储**
   - `recorder/recorder.c` — MJPEG 逐帧打包 AVI
   - `storage_manager/storage.c` — 循环覆盖
   - `common/ipc/` — 共享内存 + 消息队列

3. **Phase 5 — GPS + 4G + MQTT**
   - `gps_daemon/` — minmea NMEA 解析
   - `mqtt_client/` — MQTT 上报
   - 4G 拨号脚本

---

## 近期 git 历史
```
97d171f docs: 更新 current_task — Phase3 代码完成
31b1c57 feat: 摄像头 HAL + Capture 模块 + main 入口
1345c1e docs: 反映实际目录状态，Phase2 确认完成
4db5668 docs: Phase2 完成，进入 Phase3
```
