#==============================================================================
# imx6ull-camera-terminal 顶层 Makefile
# 板卡: 韦东山 i.MX6ULL-Pro | 内核: Linux 4.9.88
# 工具链: arm-linux-gnueabihf (Linaro 6.2.1)
#==============================================================================

# ---- 路径配置（按实际环境修改）----
KERN_DIR    := /home/lalakala/100ask_imx6ull-sdk/Linux-4.9.88
TOOLCHAIN    := /home/lalakala/100ask_imx6ull-sdk/ToolChain/arm-buildroot-linux-gnueabihf_sdk-buildroot
CROSS_COMPILE := $(TOOLCHAIN)/bin/arm-buildroot-linux-gnueabihf-
CC          := $(CROSS_COMPILE)gcc
LD          := $(CROSS_COMPILE)ld
ARCH        := arm

# ---- 源码目录 ----
DRV_DIR     := sources/driver
APP_DIR     := sources/app
HAL_DIR     := $(APP_DIR)/common/hal
MOD_DIR     := $(APP_DIR)/modules
MAIN_DIR    := $(APP_DIR)/main
UI_DIR      := $(APP_DIR)/ui
LVGL_DIR    := $(UI_DIR)/lvgl
PORT_DIR    := $(UI_DIR)/porting

# ---- 输出目录 ----
BUILD_DIR   := build
OUTPUT_DIR  := $(BUILD_DIR)/target

# ---- 应用源码 ----
HAL_SRCS    := $(wildcard $(HAL_DIR)/*.c)
MOD_SRCS    := $(wildcard $(MOD_DIR)/key_manager/*.c) \
             $(wildcard $(MOD_DIR)/camera_capture/*.c) \
             $(wildcard $(MOD_DIR)/fb_display/*.c) \
             $(wildcard $(MOD_DIR)/recorder/*.c) \
             $(wildcard $(MOD_DIR)/mqtt_client/*.c) \
             $(wildcard $(MOD_DIR)/storage_manager/*.c) \
             $(wildcard $(MOD_DIR)/gps_daemon/*.c)
MAIN_SRCS   := $(wildcard $(MAIN_DIR)/*.c)

# ---- LVGL 源码 ----
LVGL_CORE   := $(wildcard $(LVGL_DIR)/src/core/*.c)
LVGL_DRAW   := $(wildcard $(LVGL_DIR)/src/draw/*.c)
LVGL_DRAWSW := $(wildcard $(LVGL_DIR)/src/draw/sw/*.c)
LVGL_EXTRA  := $(wildcard $(LVGL_DIR)/src/extra/*.c)
LVGL_EXTLAY := $(wildcard $(LVGL_DIR)/src/extra/layouts/*/*.c)
LVGL_EXTHME := $(wildcard $(LVGL_DIR)/src/extra/themes/*/*.c)
LVGL_EXTWGT := $(wildcard $(LVGL_DIR)/src/extra/widgets/*/*.c)
LVGL_FONT   := $(wildcard $(LVGL_DIR)/src/font/*.c)
LVGL_HAL    := $(wildcard $(LVGL_DIR)/src/hal/*.c)
LVGL_MISC   := $(wildcard $(LVGL_DIR)/src/misc/*.c)
LVGL_WIDGET := $(wildcard $(LVGL_DIR)/src/widgets/*.c)

LVGL_SRC    := $(LVGL_CORE) $(LVGL_DRAW) $(LVGL_DRAWSW) $(LVGL_EXTRA) $(LVGL_EXTLAY) $(LVGL_EXTHME) $(LVGL_EXTWGT) $(LVGL_FONT) $(LVGL_HAL) $(LVGL_MISC) $(LVGL_WIDGET)

# ---- SquareLine UI / porting / bridge ----
UI_APP_SRCS := $(wildcard $(UI_DIR)/app/*.c) \
               $(wildcard $(UI_DIR)/app/screens/*.c) \
               $(wildcard $(UI_DIR)/app/components/*.c)
PORT_SRCS   := $(wildcard $(PORT_DIR)/*.c)
UI_BRIDGE_SRCS := $(wildcard $(MAIN_DIR)/ui_bridge.c)

APP_SRCS    := $(HAL_SRCS) $(MOD_SRCS) $(MAIN_SRCS) $(LVGL_SRC) $(UI_APP_SRCS) $(PORT_SRCS) $(UI_BRIDGE_SRCS)
# ---- 编译器标志 ----
CFLAGS      := -Wall \
	-I$(HAL_DIR) \
	-I$(MAIN_DIR) \
	-I$(MOD_DIR)/key_manager \
	-I$(MOD_DIR)/gps_daemon \
	-I$(MOD_DIR)/camera_capture \
	-I$(MOD_DIR)/fb_display \
	-I$(MOD_DIR)/recorder \
	-I$(MOD_DIR)/storage_manager \
	-I$(MOD_DIR)/mqtt_client \
	-I$(APP_DIR)/common/ipc -I$(APP_DIR)/common/utils -I$(UI_DIR) -I$(LVGL_DIR) -I$(PORT_DIR) -I$(UI_DIR)/app
LDFLAGS     := -lpthread -lrt -ljpeg -lm

# ---- 目标文件 ----
APP_TARGET  := $(OUTPUT_DIR)/camera_terminal
APP_OBJS    := $(patsubst $(HAL_DIR)/%.c, $(BUILD_DIR)/$(HAL_DIR)/%.o, $(HAL_SRCS))
APP_OBJS    += $(patsubst $(MOD_DIR)/key_manager/%.c, $(BUILD_DIR)/$(MOD_DIR)/key_manager/%.o, $(wildcard $(MOD_DIR)/key_manager/*.c))
APP_OBJS    += $(patsubst $(MOD_DIR)/gps_daemon/%.c, $(BUILD_DIR)/$(MOD_DIR)/gps_daemon/%.o, $(wildcard $(MOD_DIR)/gps_daemon/*.c))
APP_OBJS    += $(patsubst $(MOD_DIR)/camera_capture/%.c, $(BUILD_DIR)/$(MOD_DIR)/camera_capture/%.o, $(wildcard $(MOD_DIR)/camera_capture/*.c))
APP_OBJS    += $(patsubst $(MOD_DIR)/fb_display/%.c, $(BUILD_DIR)/$(MOD_DIR)/fb_display/%.o, $(wildcard $(MOD_DIR)/fb_display/*.c))
APP_OBJS    += $(patsubst $(MOD_DIR)/recorder/%.c, $(BUILD_DIR)/$(MOD_DIR)/recorder/%.o, $(wildcard $(MOD_DIR)/recorder/*.c))
APP_OBJS    += $(patsubst $(MOD_DIR)/storage_manager/%.c, $(BUILD_DIR)/$(MOD_DIR)/storage_manager/%.o, $(wildcard $(MOD_DIR)/storage_manager/*.c))
APP_OBJS    += $(patsubst $(MAIN_DIR)/%.c, $(BUILD_DIR)/$(MAIN_DIR)/%.o, $(MAIN_SRCS))
APP_OBJS    += $(patsubst $(LVGL_DIR)/src/%.c, $(BUILD_DIR)/$(LVGL_DIR)/src/%.o, $(LVGL_SRC))
APP_OBJS    += $(patsubst $(UI_DIR)/app/%.c, $(BUILD_DIR)/$(UI_DIR)/app/%.o, $(wildcard $(UI_DIR)/app/*.c))
APP_OBJS    += $(patsubst $(UI_DIR)/app/screens/%.c, $(BUILD_DIR)/$(UI_DIR)/app/screens/%.o, $(wildcard $(UI_DIR)/app/screens/*.c))
APP_OBJS    += $(patsubst $(UI_DIR)/app/components/%.c, $(BUILD_DIR)/$(UI_DIR)/app/components/%.o, $(wildcard $(UI_DIR)/app/components/*.c))
APP_OBJS    += $(patsubst $(PORT_DIR)/%.c, $(BUILD_DIR)/$(PORT_DIR)/%.o, $(PORT_SRCS))
APP_OBJS    += $(patsubst $(MAIN_DIR)/ui_bridge.c, $(BUILD_DIR)/$(MAIN_DIR)/ui_bridge.o, $(wildcard $(MAIN_DIR)/ui_bridge.c))



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

# 编译每个 .c -> .o
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# 链接
$(APP_TARGET): $(APP_OBJS)
	@mkdir -p $(OUTPUT_DIR)
	$(CC) -o $@ $^ $(LDFLAGS)
	@echo "=== 应用编译完成: $@ ==="


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
	@echo "  工具链 $(shell $(CC) --version | head -1)"
	@echo "  内核: $(KERN_DIR)"
	@echo "=========================================="




