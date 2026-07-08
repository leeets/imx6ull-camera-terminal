# imx6ull-camera-terminal

嵌入式 Linux 相机采集 + 车载终端项目

**板卡**: 韦东山 i.MX6ULL-Pro | **内核**: Linux 4.9.88 | **系统**: Buildroot

## 项目结构

```
imx6ull-camera-terminal/
├── docs/                    # 文档
├── sources/
│   ├── kernel/              # 内核源码 + 设备树
│   ├── driver/              # 自己写的驱动
│   │   ├── gpio-keys/       # GPIO 按键驱动
│   │   ├── led/             # 状态指示灯驱动（待扩展）
│   │   └── buzzer/          # 蜂鸣器驱动（待扩展）
│   └── app/                 # 应用层
│       ├── common/          # HAL + IPC + 工具函数
│       │   ├── hal/         # 硬件抽象层接口
│       │   ├── ipc/         # 进程间通信封装
│       │   └── utils/       # 日志、配置、时间工具
│       ├── modules/         # 业务模块
│       │   ├── key_manager/       # 按键管理（长短按+双击策略）
│       │   ├── camera_capture/    # 摄像头采集
│       │   ├── recorder/          # 录像模块
│       │   ├── gps_daemon/        # GPS 解析
│       │   ├── mqtt_client/       # MQTT 上传
│       │   └── storage_manager/   # 存储管理
│       ├── ui/              # UI 显示（LVGL/Qt）
│       └── main/            # 主程序入口
├── scripts/                 # 编译/烧写脚本
├── rootfs_overlay/          # 根文件系统覆盖层
└── README.md
```

详见 [docs/project-plan.txt](docs/project-plan.txt)。
