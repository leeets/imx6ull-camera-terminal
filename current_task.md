# current_task — 当前任务

> 本文件记录当前项目状态和下一步计划。
> 随项目进展更新。

---

## 当前项目结构

```
imx6ull-camera-terminal/
├── docs/
│   └── project-plan.txt              # 项目规划书
├── sources/
│   ├── kernel/                       # 内核配置 + 设备树（独立管理）
│   │   ├── .config                   # 内核配置
│   │   └── 100ask_imx6ull-14x14.dts  # 板级设备树（待改 CSI）
│   ├── driver/                       # 自己写的驱动
│   │   └── gpio-keys/                # ✅ GPIO 按键驱动（已测通）
│   │       ├── gpio_key_drv.c        #   驱动源码
│   │       ├── Makefile              #   驱动 Makefile
│   │       └── *.ko / *.o            #   编译产物（板子上验证通过）
│   ├── app/
│   │   ├── common/
│   │   │   ├── hal/hal_key.c/h       # ✅ 按键硬件抽象层
│   │   │   ├── ipc/.gitkeep          #   IPC 封装（待写）
│   │   │   └── utils/.gitkeep        #   工具函数（待写）
│   │   ├── modules/
│   │   │   ├── key_manager/          # ✅ 按键策略模块
│   │   │   │   ├── key_manager.c/h   #   （短按/长按/双击）
│   │   │   │   └── button_test.c     #   ✅ 带回调的测试入口（已测通）
│   │   │   ├── camera_capture/.gitkeep  # 摄像头采集（待写）
│   │   │   ├── recorder/.gitkeep        # 录像模块（待写）
│   │   │   ├── gps_daemon/.gitkeep      # GPS 解析（待写）
│   │   │   ├── mqtt_client/.gitkeep     # MQTT 上传（待写）
│   │   │   └── storage_manager/.gitkeep # 存储管理（待写）
│   │   ├── ui/.gitkeep               # UI 显示（待写）
│   │   └── main/.gitkeep             # 主入口（待写）
│   ├── driver/led/.gitkeep           # LED 驱动（待扩展）
│   └── driver/buzzer/.gitkeep        # 蜂鸣器驱动（待扩展）
├── scripts/                          # 脚本目录（空）
├── rootfs_overlay/                   # 根文件系统覆盖层（空）
├── Makefile                          # 顶层构建
├── README.md
├── current_task.md                   # 本文件
```

## Phase 2 — 按键模块 ✅ 完全完成

| 模块 | 状态 |
|------|------|
| `gpio_key_drv.c` — 内核驱动 | ✅ 上板验证通过 |
| `hal_key.c/h` — 硬件抽象层 | ✅ 上板验证通过 |
| `key_manager.c/h` — 按键策略 | ✅ 上板验证通过 |
| `button_test.c` — 测试入口 | ✅ 上板验证通过 |

## Phase 3 — V4L2 摄像头采集（下一步）

### 待做任务（按顺序）
1. DTS：释放 CSI 管脚，关闭 ECSPI1 + UART6
2. DTS：添加 OV5640 节点（I2C 地址 0x3c）
3. 内核配置确认（make menuconfig）
4. 编译内核 + DTB，烧录
5. 编写 `camera_capture/capture.c` — V4L2 采集
6. 编写 `common/hal/hal_camera.h` — 摄像头 HAL

### 需要确认
- 摄像头接的是 CSI 排线口吗？
- 板子上 OV5640 用哪个 I2C 总线？I2C1 (UART4引脚) 还是 I2C2 (UART5引脚)？
- 需要板子原理图确认 CSI 管脚实际连线

---

## 近期 git 历史
```
4db5668 docs: 更新 current_task — Phase2 完成，进入 Phase3
3600240 fix: button_test 编译添加 -I key_manager 头文件路径
bb617ab docs: 更新 current_task — Makefile 完成，待写 main.c
1d9eeb8 build: 添加顶层 Makefile
5536c70 feat: 按键 HAL + Key Manager 完成
```
