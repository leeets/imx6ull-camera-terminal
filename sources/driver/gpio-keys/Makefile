#==============================================================================
# imx6ull-camera-terminal — GPIO 按键驱动模块 Makefile
# ---------------------------------------------------------------------------
# 模块: gpio_key_drv.ko
# 设备: /dev/100ask_gpio_key（模块加载后自动创建）
# 板卡: 韦东山 i.MX6ULL-Pro | 内核: Linux 4.9.88
#
# 用法:
#   make                编译 gpio_key_drv.ko（默认目标，不再编译测试程序）
#   make clean          清理全部编译产物
#   make install        安装到 INSTALL_MOD_PATH 指定的根文件系统
#   make uninstall      从根文件系统中移除模块
#   make load/unload    板端 insmod / rmmod（需在板端以 root 执行）
#   make info           显示当前编译配置
#
# 所有变量均可用命令行覆盖，例如:
#   make KERN_DIR=/path/to/kernel CROSS_COMPILE=arm-linux-gnueabihf-
#==============================================================================

# ---- 内核源码目录（需先配置并编译过）----
KERN_DIR      ?= /home/lalakala/100ask_imx6ull-sdk/Linux-4.9.88

# ---- 交叉编译配置 ----
ARCH          ?= arm
CROSS_COMPILE ?= arm-linux-gnueabihf-

# ---- 安装目标根文件系统（默认项目 rootfs_overlay，可改为板端挂载的根目录）----
INSTALL_MOD_PATH ?= $(abspath $(CURDIR)/../../..)/rootfs_overlay

# ---- 模块定义：把 gpio_key_drv.c 编成 gpio_key_drv.ko ----
obj-m         := gpio_key_drv.o

# ---- 设备节点（load 后打印用）----
DEV_NODE      := /dev/100ask_gpio_key

.PHONY: all modules clean install uninstall load unload info

# 默认目标：只编译内核模块（button_test 测试程序已废弃，不再编译）
all: modules

# 编译 .ko
modules:
	@echo "=== 编译 GPIO 按键驱动 gpio_key_drv.ko ==="
	$(MAKE) -C $(KERN_DIR) M=$(CURDIR) \
		ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

# 清理：内核模块清理 + 删除所有本地编译产物
clean:
	$(MAKE) -C $(KERN_DIR) M=$(CURDIR) \
		ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean
	rm -rf .tmp_versions
	rm -f *.ko *.o *.mod.c *.mod.o *.order modules.order Module.symvers built-in.o
	rm -f .*.cmd .*.o.cmd .*.ko.cmd .*.mod.o.cmd .built-in.o.cmd

# 安装到根文件系统: $(INSTALL_MOD_PATH)/lib/modules/<release>/extra/
install: modules
	@mkdir -p $(INSTALL_MOD_PATH)
	$(MAKE) -C $(KERN_DIR) M=$(CURDIR) \
		ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) \
		INSTALL_MOD_PATH=$(INSTALL_MOD_PATH) modules_install
	@echo "=== 已安装到 $(INSTALL_MOD_PATH)/lib/modules/ ==="

uninstall:
	rm -f $(INSTALL_MOD_PATH)/lib/modules/*/extra/gpio_key_drv.ko
	@echo "=== 已从 $(INSTALL_MOD_PATH) 移除 gpio_key_drv.ko ==="

# 板端加载/卸载（需先把 gpio_key_drv.ko 拷贝到板端，并以 root 执行）
load:
	@test -f gpio_key_drv.ko || { echo "请先 make 编译，并把 gpio_key_drv.ko 拷贝到板端"; exit 1; }
	insmod gpio_key_drv.ko
	@echo "=== 已加载模块，设备节点: $(DEV_NODE) ==="

unload:
	rmmod gpio_key_drv
	@echo "=== 已卸载模块 ==="

# 显示当前配置
info:
	@echo "KERN_DIR         = $(KERN_DIR)"
	@echo "ARCH             = $(ARCH)"
	@echo "CROSS_COMPILE    = $(CROSS_COMPILE)"
	@echo "INSTALL_MOD_PATH = $(INSTALL_MOD_PATH)"
