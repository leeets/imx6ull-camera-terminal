#==============================================================================
# imx6ull-camera-terminal 顶层 Makefile
# 板卡: 韦东山 i.MX6ULL-Pro | 内核: Linux 4.9.88
# 工具链: arm-linux-gnueabihf (Linaro 6.2.1)
#==============================================================================

# ---- 路径配置（按实际环境修改） ----
KERN_DIR    := /home/lalakala/100ask_imx6ull-sdk/Linux-4.9.88
TOOLCHAIN   := /home/lalakala/100ask_imx6ull-sdk/ToolChain/gcc-linaro-6.2.1-2016.11-x86_64_arm-linux-gnueabihf
CROSS_COMPILE := $(TOOLCHAIN)/bin/arm-linux-gnueabihf-
CC          := $(CROSS_COMPILE)gcc
LD          := $(CROSS_COMPILE)ld
ARCH        := arm

# ---- 源码目录 ----
DRV_DIR     := driver
APP_DIR     := app
HAL_DIR     := $(APP_DIR)/common/hal
MOD_DIR     := $(APP_DIR)/modules
MAIN_DIR    := $(APP_DIR)/main
UI_DIR      := $(APP_DIR)/ui

# ---- 输出目录 ----
BUILD_DIR   := build
OUTPUT_DIR  := $(BUILD_DIR)/target

# ---- 应用源码 ----
HAL_SRCS    := $(wildcard $(HAL_DIR)/*.c)
MOD_SRCS    := $(wildcard $(MOD_DIR)/key_manager/*.c) \
             $(wildcard $(MOD_DIR)/camera_capture/*.c) \
             $(wildcard $(MOD_DIR)/fb_display/*.c) \
             $(wildcard $(MOD_DIR)/recorder/*.c) \
             $(wildcard $(MOD_DIR)/storage_manager/*.c)
MAIN_SRCS   := $(wildcard $(MAIN_DIR)/*.c)
APP_SRCS    := $(HAL_SRCS) $(MOD_SRCS) $(MAIN_SRCS)
# ---- 编译器标志 ----
CFLAGS      := -Wall \
	-I$(HAL_DIR) \
	-I$(MOD_DIR)/key_manager \
	-I$(MOD_DIR)/camera_capture \
	-I$(MOD_DIR)/fb_display \
	-I$(MOD_DIR)/recorder \
	-I$(MOD_DIR)/storage_manager \
	-I$(APP_DIR)/common/ipc -I$(APP_DIR)/common/utils
LDFLAGS     := -lpthread -lrt

# ---- 目标文件 ----
APP_TARGET  := $(OUTPUT_DIR)/camera_terminal
APP_OBJS    := $(patsubst $(HAL_DIR)/%.c, $(BUILD_DIR)/$(HAL_DIR)/%.o, $(HAL_SRCS))
APP_OBJS    += $(patsubst $(MOD_DIR)/key_manager/%.c, $(BUILD_DIR)/$(MOD_DIR)/key_manager/%.o, $(wildcard $(MOD_DIR)/key_manager/*.c))
APP_OBJS    += $(patsubst $(MOD_DIR)/camera_capture/%.c, $(BUILD_DIR)/$(MOD_DIR)/camera_capture/%.o, $(wildcard $(MOD_DIR)/camera_capture/*.c))
APP_OBJS    += $(patsubst $(MOD_DIR)/fb_display/%.c, $(BUILD_DIR)/$(MOD_DIR)/fb_display/%.o, $(wildcard $(MOD_DIR)/fb_display/*.c))
APP_OBJS    += $(patsubst $(MOD_DIR)/recorder/%.c, $(BUILD_DIR)/$(MOD_DIR)/recorder/%.o, $(wildcard $(MOD_DIR)/recorder/*.c))
APP_OBJS    += $(patsubst $(MOD_DIR)/storage_manager/%.c, $(BUILD_DIR)/$(MOD_DIR)/storage_manager/%.o, $(wildcard $(MOD_DIR)/storage_manager/*.c))
APP_OBJS    += $(patsubst $(MAIN_DIR)/%.c, $(BUILD_DIR)/$(MAIN_DIR)/%.o, $(MAIN_SRCS))



# ---- 默认目标 ----
.PHONY: all
all: drivers apps

#==============================================================================
# 1. 编译内核驱动
#==============================================================================
.PHONY: drivers
drivers:
	@echo "=== 编译驱动 ==="
	$(MAKE) -C $(DRV_DIR)/gpio-keys KERN_DIR=$(KERN_DIR) CROSS_COMPILE=$(CROSS_COMPILE) ARCH=$(ARCH)

#==============================================================================
# 2. 编译应用层
#==============================================================================
.PHONY: apps
apps: $(APP_TARGET)

# 编译每个 .c → .o
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# 链接
$(APP_TARGET): $(APP_OBJS)
	@mkdir -p $(OUTPUT_DIR)
	$(CC) -o $@ $^ $(LDFLAGS)
	@echo "=== 应用编译完成: $@ ==="

#==============================================================================
# 3. 单独编译按键测试程序
#==============================================================================
.PHONY: button_test
button_test:
	@mkdir -p $(OUTPUT_DIR)
	$(CC) $(CFLAGS) -I$(HAL_DIR) -I$(MOD_DIR) \
		-o $(OUTPUT_DIR)/button_test \
		$(HAL_DIR)/hal_key.c \
		$(MOD_DIR)/key_manager/key_manager.c \
		$(MOD_DIR)/key_manager/button_test.c
	@echo "=== 测试程序编译完成: $(OUTPUT_DIR)/button_test ==="

#==============================================================================
# 5. 清理
#==============================================================================
.PHONY: clean
clean:
	$(MAKE) -C $(DRV_DIR)/gpio-keys clean
	rm -rf $(BUILD_DIR)

.PHONY: distclean
distclean: clean
	rm -f $(DRV_DIR)/gpio-keys/*.ko $(DRV_DIR)/gpio-keys/*.o $(DRV_DIR)/gpio-keys/modules.order $(DRV_DIR)/gpio-keys/Module.symvers $(DRV_DIR)/gpio-keys/button_test

#==============================================================================
# 6. 查看当前状态
#==============================================================================
.PHONY: info
info:
	@echo "=========================================="
	@echo "  imx6ull-camera-terminal"
	@echo "  工具链: $(shell $(CC) --version | head -1)"
	@echo "  内核: $(KERN_DIR)"
	@echo "=========================================="
