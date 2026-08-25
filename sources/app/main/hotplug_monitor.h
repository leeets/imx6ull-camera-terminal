#ifndef _HOTPLUG_MONITOR_H
#define _HOTPLUG_MONITOR_H

#include <stdint.h>

/*
 * 设备热插拔监控模块
 *
 * 基于 inotify 监听 /dev 目录的设备节点创建/删除事件，并用
 * open() + ioctl() 二级确认设备真正可用后执行重连（摄像头/触摸屏/SD卡）。
 * inotify 不可用时回退到 stat() 定时轮询。
 *
 * 线程模型：模块内部创建独立监控线程，重连动作在监控线程内串行执行，
 * 事件由 inotify 内核队列缓存，不丢失。
 */

/*
 * 启动热插拔监控
 * camera_dev / touch_dev: 设备节点路径，如 "/dev/video1"、"/dev/input/event1"
 * camera_w / camera_h:    摄像头期望分辨率（重连时用）
 * storage_root:           存储挂载点，如 "/mnt/sd"（二级确认的目标）
 * storage_capacity:       存储容量上限（重连时用）
 * 返回 0 成功，-1 失败
 */
int hotplug_monitor_init(const char *camera_dev, int camera_w, int camera_h,
                         const char *touch_dev,
                         const char *storage_root, uint64_t storage_capacity);

/* 停止监控线程并回收资源 */
void hotplug_monitor_exit(void);

#endif /* _HOTPLUG_MONITOR_H */
