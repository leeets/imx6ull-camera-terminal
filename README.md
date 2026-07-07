# imx6ull-camera-terminal

嵌入式 Linux 相机采集 + 车载终端项目

**板卡**: 韦东山 i.MX6ULL-Pro | **内核**: Linux 4.9.88 | **系统**: Buildroot

## 项目结构

```
imx6ull-camera-terminal/
├── docs/                  # 项目文档
├── sources/
│   ├── kernel/            # 内核配置 & 设备树
│   ├── driver/gpio-keys/  # GPIO 按键驱动 (自己写)
│   └── app/               # 应用层代码
│       ├── camera_capture # V4L2 采集
│       ├── recorder/      # 录像模块
│       ├── gps_daemon/    # GPS 解析
│       ├── mqtt_client/   # MQTT 上传
│       ├── ui/            # 仪表盘 UI
│       └── common/        # IPC、日志等公共模块
├── scripts/               # 编译/烧写/调试脚本
└── rootfs_overlay/        # 根文件系统覆盖层
```

详见 [docs/project-plan.txt](docs/project-plan.txt)。
