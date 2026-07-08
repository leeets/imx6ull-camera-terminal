# current_task — 当前任务

> 本文件记录当前正在做什么、下一步做什么、卡点在哪。
> 随项目进展更新。

---

## Phase 2 — GPIO 按键驱动 & 应用控制（进行中）

### 已完成
- gpio_key_drv.c + Makefile 写完
- button_test.c 写完（短按拍照 / 长按录像 / 双击退出）

### 卡点
- 驱动 compatible = "gpio_keys"，板子 DTS 里是 "gpio-keys"
  → 需要统一，建议驱动里改成 "gpio-keys" 与 DTS 一致

### 下一步
1. 改驱动 compatible 字符串 `"gpio_keys"` → `"gpio-keys"`（匹配 DTS）
2. 交叉编译驱动 `.ko` 和测试程序
3. NFS 到板子，`insmod` 验证，跑 `button_test`
4. 验证短按/长按/双击全部跑通
5. commit：`gpio_key_drv.c` + `button_test.c` + `Makefile` + `README.md`
6. ⬅ 提交后回来更新本文件

---

## Phase 3 — V4L2 摄像头采集（排队中）

### 前置依赖
- G 确认 CSI 管脚在 DTS 中没有被 ECSPI1 / UART6 占用
- G 确认内核配置（`make menuconfig`）中 OV5640 / CSI / V4L2 已使能
- G 重构 DTS，添加 CSI + OV5640 节点

### 待做任务
- [ ] DTS：释放 CSI 管脚，添加 OV5640 子节点
- [ ] 内核配置检查 / 补充
- [ ] `camera_capture/capture.c` V4L2 采集框架
- [ ] `common/hal/hal_camera.h` 硬件抽象
- [ ] `common/ipc/` 共享内存 + 消息队列封装
