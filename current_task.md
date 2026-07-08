# current_task — 当前任务

> 本文件记录当前正在做什么、下一步做什么。
> 随项目进展更新。

---

## Phase 2 — GPIO 按键驱动 & 应用控制 ✅ 完成

### 已完成
- `driver/gpio-keys/gpio_key_drv.c` — GPIO 按键驱动（platform driver，中断 + 定时器去抖）
- `driver/gpio-keys/button_test.c` — 测试程序（升级版：使用 key_manager 回调）
- `common/hal/hal_key.c/h` — 按键硬件抽象层（封装 /dev/100ask_gpio_key 读写）
- `modules/key_manager/key_manager.c/h` — 按键策略（短按拍照 / 长按录像停 / 双击退出）
- 上板测试通过 ✅

---

## Phase 3 — V4L2 摄像头采集（当前）

### 待做任务（按优先级）
1. 修改 DTS：释放 CSI 管脚（ECSPI1 → 关闭，UART6 → 关闭，改为 CSI 功能）
2. DTS 添加 OV5640 子节点（挂在 I2C1 或 CSI 上）
3. 内核配置确认 / 补充（make menuconfig）
4. 编译内核 + DTB，烧录到板子
5. 编写 `camera_capture/capture.c` — V4L2 采集框架
6. 编写 `common/hal/hal_camera.h` — 摄像头 HAL 接口
7. 上板验证：`v4l2-ctl` 测试 + 自己写的采集程序

### 前置检查清单
- [ ] CSI 管脚当前被 ECSPI1 + UART6 占用 → 需释放
- [ ] 内核 .config 中 OV5640 / V4L2 / CSI 已 =m 或 =y
- [ ] DTS 中 csi 节点 status = "okay"
- [ ] I2C 总线上添加 ov5640: ov5640@3c {}

---

## 近期 git 历史
```
3600240 fix: button_test 编译添加 -I key_manager 头文件路径
bb617ab docs: 更新 current_task — Makefile 完成，待写 main.c
1d9eeb8 build: 添加顶层 Makefile
5536c70 feat: 按键 HAL + Key Manager 完成
```
