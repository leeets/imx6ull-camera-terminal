# current_task — 当前任务

> 本文件记录当前正在做什么、下一步做什么、卡点在哪。
> 随项目进展更新。

---

## Phase 2 — GPIO 按键驱动 & 应用控制（基本完成）

### 已完成
- `driver/gpio-keys/` — GPIO 按键驱动写完
- `app/common/hal/hal_key.c/h` — 按键硬件抽象层
- `app/modules/key_manager/key_manager.c/h` — 按键策略
- `Makefile` — 顶层构建系统（drivers/apps/button_test/deploy）

### 下一步
1. 写 `main/main.c` 简单入口，拉通 key_manager 回调
2. `make apps` 验证编译通过
3. 上板验证全链路

---

## Phase 3 — V4L2 摄像头采集（排队中）

### 待做任务
- [ ] DTS：释放 CSI 管脚（ECSPI1 + UART6 → CSI）
- [ ] 内核配置确认：OV5640 / CSI / V4L2
- [ ] `camera_capture/capture.c` V4L2 采集
- [ ] `common/hal/hal_camera.h`
- [ ] `common/ipc/` 共享内存 + 消息队列

---

## 近期 git 历史
```
1d9eeb8 build: 添加顶层 Makefile
5536c70 feat: 按键 HAL + Key Manager 完成
b61d235 docs: 更新 current_task
f56373d docs: 添加 current_task.md
```
