/*
 * hal_touch.c - 触摸屏硬件抽象层
 *
 * 通过 input_event 结构体读取 /dev/input/eventX 触摸事件。
 * 支持 EV_ABS（USB 电容屏）和 EV_KEY（电阻屏）。
 * 修复：hal_touch.c 加 MT 协议 B 的解析，坐标范围用 EVIOCGABS 从驱动动态读（不要硬编码 32767），同时把缩放改成 1024×600
 */

#include "hal_touch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/input.h>

/* 文件顶部静态变量区加 */
static int g_abs_x_min = 0, g_abs_x_max = 32767;
static int g_abs_y_min = 0, g_abs_y_max = 32767;

/* 私有全局变量 */
static int g_touch_fd = -1;
/* 默认分辨率，可通过 ioctl 或校准更新 */
static uint16_t g_screen_width  = 1024;
static uint16_t g_screen_height = 600;
static bool g_nonblock = false;

/* 缓存的坐标（用于 EV_SYN 同步事件）*/
static uint16_t g_last_x = 0;
static uint16_t g_last_y = 0;

int hal_touch_init(const char *dev_path)
{
    if (!dev_path) {
        fprintf(stderr, "[HAL_TOUCH] 设备路径为 NULL\n");//检查路径
        return -1;
    }

    g_touch_fd = open(dev_path, O_RDWR | O_NONBLOCK);//打开设备，读写 非阻塞
    if (g_touch_fd < 0) {
        fprintf(stderr, "[HAL_TOUCH] 打开 %s 失败: %s\n",
                dev_path, strerror(errno));
        return -1;
    }
    printf("[HAL_TOUCH] 已打开 %s (fd=%d)\n", dev_path, g_touch_fd);
    /*Open成功后加入：*/
    struct input_absinfo ai;
    if (ioctl(g_touch_fd, EVIOCGABS(ABS_MT_POSITION_X), &ai) == 0) {
        g_abs_x_min = ai.minimum; g_abs_x_max = ai.maximum;
    }
    if (ioctl(g_touch_fd, EVIOCGABS(ABS_MT_POSITION_Y), &ai) == 0) {
        g_abs_y_min = ai.minimum; g_abs_y_max = ai.maximum;
    }
    printf("[HAL_TOUCH] abs x[%d,%d] y[%d,%d]\n", g_abs_x_min, g_abs_x_max, g_abs_y_min, g_abs_y_max);

    return 0;
}

//读一次事件，之后被lv_port_indev集成调用，来读取触摸
int hal_touch_read(hal_touch_event_t *out)
{
    struct input_event ev;//临时 读事件值（所有的输入设备都是这样的数据格式，Linux input 子系统的核心设计）
    ssize_t n;
    static int touch_state = 0; /* 0=抬起, 1=按下 */

    if (!out || g_touch_fd < 0)
        return -1;

    memset(out, 0, sizeof(*out));//内存设置，清空event结构体
    out->action = HAL_TOUCH_NONE;

    n = read(g_touch_fd, &ev, sizeof(ev));	//读到ev--非阻塞的，无数据会直接返回-1，一定要判断返回值，及时打断程序
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;   /* 非阻塞模式下无事件 */
        fprintf(stderr, "[HAL_TOUCH] 读取错误: %s\n", strerror(errno));
        return -1;
    }

    if (n != sizeof(ev))
        return 0;   /* 不完整事件，跳过 */

  /* 解析 input_event内容（修复：保留老协议，加 MT B） */
  switch (ev.type) {
  /* 电容触摸屏 */
  case EV_ABS:
      switch (ev.code) {
      case ABS_X:
      case ABS_MT_POSITION_X:
      {
          int range = g_abs_x_max - g_abs_x_min;
          if (range <= 0) range = 1;
          int v = (ev.value - g_abs_x_min) * g_screen_width / range;
          if (v < 0) v = 0;
          if (v >= (int)g_screen_width) v = g_screen_width - 1;
          g_last_x = (uint16_t)v;
          break;
      }
      case ABS_Y:
      case ABS_MT_POSITION_Y:
      {
          int range = g_abs_y_max - g_abs_y_min;
          if (range <= 0) range = 1;
          int v = (ev.value - g_abs_y_min) * g_screen_height / range;
          if (v < 0) v = 0;
          if (v >= (int)g_screen_height) v = g_screen_height - 1;
          g_last_y = (uint16_t)v;
          break;
      }
      case ABS_MT_TRACKING_ID:
          /* 协议B：>=0 按下，<0 抬起 */
          if (ev.value >= 0 && !touch_state) {
              touch_state = 1;
              out->action = HAL_TOUCH_PRESS;
          } else if (ev.value < 0 && touch_state) {
              touch_state = 0;
              out->action = HAL_TOUCH_RELEASE;
          }
          break;
      case ABS_PRESSURE:
          /* 老协议兜底 */
          if (ev.value > 0 && !touch_state) {
              touch_state = 1;
              out->action = HAL_TOUCH_PRESS;
          } else if (ev.value == 0 && touch_state) {
              touch_state = 0;
              out->action = HAL_TOUCH_RELEASE;
          }
          break;
      default:
          break;
      }
      break;
  /* 电阻触摸屏 */
  case EV_KEY:
      if (ev.code == BTN_TOUCH) {
          if (ev.value == 1 && !touch_state) {
              touch_state = 1;
              out->action = HAL_TOUCH_PRESS;
          } else if (ev.value == 0 && touch_state) {
              touch_state = 0;
              out->action = HAL_TOUCH_RELEASE;
          }
      }
      break;
    /* 同步事件--标记一次完整事件的结束、批量传输优化 */
  case EV_SYN:
      out->x = g_last_x;
      out->y = g_last_y;
      /* 手指按下且本帧没有按下/抬起事件，才算滑动 */
      if (touch_state && out->action == HAL_TOUCH_NONE)
          out->action = HAL_TOUCH_MOVE;
      break;

  default:
      return 0;
  }
    out->x = g_last_x;
    out->y = g_last_y;		//更新坐标

    return (out->action != HAL_TOUCH_NONE) ? 1 : 0;//类型不是NONE就返回1
}

/*切换触摸屏的"读取模式" 阻塞与否*/
void hal_touch_set_nonblock(bool enable)
{
    if (g_touch_fd < 0) return;

    int flags = fcntl(g_touch_fd, F_GETFL, 0);
    if (flags < 0) return;

    if (enable)
        fcntl(g_touch_fd, F_SETFL, flags | O_NONBLOCK);
    else
        fcntl(g_touch_fd, F_SETFL, flags & ~O_NONBLOCK);

    g_nonblock = enable;
}

/*查询屏幕分辨率 本模块设置分辨率 */
int hal_touch_get_resolution(uint16_t *width, uint16_t *height)
{
    if (!width || !height) return -1;
    *width  = g_screen_width;
    *height = g_screen_height;
    return 0;
}

/*释放资源*/
void hal_touch_exit(void)
{
    if (g_touch_fd >= 0) {
        close(g_touch_fd);
        g_touch_fd = -1;
        printf("[HAL_TOUCH] 已关闭\n");
    }
}
