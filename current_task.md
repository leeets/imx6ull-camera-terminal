# current_task — 当前任务

> 本文件记录当前正在做什么、下一步做什么、卡点在哪。
> 随项目进展更新。

---

## Phase 2 — GPIO 按键驱动 & 应用控制

### 已完成
- gpio_key_drv.c + Makefile 写完
- button_test.c 写完（短按拍照 / 长按录像 / 双击退出）
- compatible 字符串已改为 `"gpio-keys"`，与板子 DTS 匹配

### 状态
- 驱动代码写完了，compatible 匹配了 ✅
- 现在在做下一步：测试前把 .ko 放到板子上，或者直接走应用层封装

### 下一步
1. 打开 `common/hal/hal_key.h` 和 `common/hal/hal_key.c`
   — 把按键操作封装成 HAL 接口，上层统一读按键而不是直接 open/read /dev 节点
2. 打开 `modules/key_manager/key_manager.h` 和 `key_manager.c`
   — 把按键策略（短按拍照、长按录像、双击退出）从测试程序迁移到正式模块
3. 在 PC 上用 gcc 编译验证逻辑（hack GPIO 返回值的方式 mock 测试）
4. 上板测试：交叉编译驱动 .ko + app → NFS → insmod → 跑 app
5. commit：驱动 HAL + Key Manager + 测试结果

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
