#ifndef _RECORDER_H
#define _RECORDER_H

#include <stddef.h>

/* 录像状态 */
typedef enum {
    RECORDER_IDLE,		//空闲
    RECORDER_RECORDING, //录像中
} recorder_state_t;

/* AVI MJPEG 录像模块
 *
 * 使用方式：
 *   recorder_init(640, 480, 15);           // 设参数，不影响摄像头格式
 *   recorder_start("/mnt/sd/record.avi");   // 创建文件写头
 *   ... hal_camera_start(my_callback) ...    // 帧回调中调 recorder_write_frame
 *   recorder_stop();                        // 写完索引，关闭文件
 *   recorder_exit();
 */

int  recorder_init(int width, int height, int fps);
int  recorder_start(const char *filename);
int  recorder_write_frame(const void *data, size_t len);
int  recorder_stop(void);
void recorder_exit(void);

recorder_state_t recorder_get_state(void);

#endif /* _RECORDER_H */
