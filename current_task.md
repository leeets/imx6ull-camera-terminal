# current_task — 当前任务

> 本文件记录当前正在做什么、下一步做什么、卡点在哪。
> 随项目进展更新。

---

## Phase 2 — GPIO 按键驱动 & 应用控制（基本完成）

### 已完成
- `driver/gpio-keys/` — GPIO 按键驱动写完
- `app/common/hal/hal_key.c/h` — 按键硬件抽象层
- `app/modules/key_manager/key_manager.c/h` — 按键策略（短按拍照、长按停止录像、双击退出）
- 回调签名已修复统一

### 待做
1. 上板验证全链路：交叉编译 .ko → NFS → insmod → 跑 app
2. 验证短按/长按/双击逻辑
3. 确认 pressed 电平反转正确
4. 写 `main/main.c` 简单入口集成 key_manager 跑一圈

---

## Phase 3 — V4L2 摄像头采集（排队中）

### 前置依赖
- 确认 CSI 管脚在 DTS 中没有被 ECSPI1 / UART6 占用
- 确认内核配置中 OV5640 / CSI / V4L2 已使能
- 重构 DTS，添加 CSI + OV5640 节点

### 待做任务
- [ ] DTS：释放 CSI 管脚，添加 OV5640 子节点
- [ ] 内核配置检查 / 补充
- [ ] `camera_capture/capture.c` V4L2 采集框架
- [ ] `common/hal/hal_camera.h` 硬件抽象
- [ ] `common/ipc/` 共享内存 + 消息队列封装

---

## 近期 git 历史
```
b61d235 docs: 更新 current_task
f56373d docs: 添加 current_task.md
04d8e37 feat: 重构项目目录结构为三层架构
18f1bf2 feat: 初始化项目仓库
```
