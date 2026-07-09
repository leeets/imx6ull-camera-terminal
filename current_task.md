# current_task — 当前任务

> 本文件记录当前项目状态和下一步计划。
> 随项目进展更新。

---

## Phase 3 — V4L2 摄像头采集 ✅ 代码完成

### 已完成
- DTS：释放 CSI 管脚（ECSPI1 disabled，UART6 keep，CSI 管脚全部改为 CSI 功能）
- DTS：添加 OV5640 节点（I2C2 子节点，地址 0x3c，compatible = "ovti,ov5640"）
- DTS：使能 &csi 节点，连接到 OV5640 endpoint
- 内核配置：OV5640/V4L2/CSI 全部 =m 或 =y
- 编译内核 + DTB 烧录到板子
- `common/hal/hal_camera.h` — 摄像头 HAL 接口
- `modules/camera_capture/capture.h/c` — Capture 模块
- `main/main.c` — 主入口（整合按键 + 摄像头）
- 交叉编译验证通过 ✅

### 待验证（上板）
- [ ] `v4l2-ctl --list-devices` 确认 /dev/videoX
- [ ] `v4l2-ctl -d /dev/video0 --list-formats` 确认格式
- [ ] `v4l2-ctl -d /dev/video0 --set-fmt-video=width=640,height=480,pixelformat=MJPG --stream-mmap --stream-to=/tmp/test.jpg --stream-count=1`
- [ ] 全链路：驱动 insmod → app 启动 → 按键拍照 → 按键录像
- [ ] MJPEG 预览帧率测试

## Phase 4 — 录像 & 存储（下一步）
- [ ] `recorder/recorder.c` — MJPEG 逐帧打包
- [ ] `storage_manager/storage.c` — 循环覆盖
- [ ] `common/ipc/` — 共享内存 + 消息队列

---

## 近期 git 历史
```
31b1c57 feat: 摄像头 HAL + Capture 模块 + main 入口
1345c1e docs: 更新 current_task — 反映实际目录状态，Phase2 确认完成
4db5668 docs: 更新 current_task — Phase2 完成，进入 Phase3
3600240 fix: button_test 编译添加 -I key_manager 头文件路径
bb617ab docs: 更新 current_task — Makefile 完成，待写 main.c
```
