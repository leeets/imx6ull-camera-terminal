/*判断按键事件意义*/
#include "key_manager.h"
#include "hal_key.h"
#include <stdio.h>
#include <sys/time.h>
#include <stdlib.h>

/* 每个按键的状态 */
typedef struct {
    struct timeval press_time;      // 按下时间（持续时间用）
    struct timeval last_press_time; // 上次按下时间（双击用）
    int press_count;                // 连续按下次数
    int long_press_triggered;       // 长按是否已触发（防重复）
} key_state_t;

static key_state_t state_capture = {0};  // 拍照键的状态
static key_state_t state_record = {0};   // 录像键的状态
static key_callback_t g_callback = NULL;

/* 根据key_id获取对应的状态指针，本文件使用 */
static key_state_t* get_state(int key_id) {
    if (key_id == KEY_ID_CAPTURE) return &state_capture;
    if (key_id == KEY_ID_RECORD) return &state_record;
    return NULL;
}

/* 计算时间差（毫秒）传入两个时间，本文件使用 */
static long time_diff_ms(struct timeval *t1, struct timeval *t2) {
    return (t1->tv_sec - t2->tv_sec) * 1000
         + (t1->tv_usec - t2->tv_usec) / 1000;
}

/*初始化两个按键*/
int key_manager_init(void) {
    return hal_key_init();
}
/*回调cb这个函数*/
void key_manager_register_callback(key_callback_t cb) {
    g_callback = cb;
}

/*主任务*/
void key_manager_task(void) {
    hal_key_event_t ev;//hal事件（本次事件的按键号、pressed）
    struct timeval now;//本事件时间
    key_state_t *state;//按键状态（短长双）
    long diff;//持续时间
    
    // 1. 阻塞读取按键事件
    if (hal_key_read(&ev) < 0) return;
    state = get_state(ev.key_id);
    if (!state) return;
    
    gettimeofday(&now, NULL);
	// 2. 处理：按下事件
    if (ev.pressed) {
        // 双击判断（只对录像键有效）
        if (ev.key_id == KEY_ID_RECORD) {
            diff = time_diff_ms(&now, &state->last_press_time);//两次的间隔时间
            if (diff < 200 && state->press_count == 1) {
                // 双击 → 退出
                printf("[KEY_MGR] Double click on RECORD -> EXIT\n");
                if (g_callback) g_callback(KEY_EVENT_EXIT);
                state->press_count = 0;
                return;
            }
        }
        
        //记录按下，更新两个last时间（持续、双击用）
        state->press_time = now;
        state->last_press_time = now;
        state->press_count++;
        state->long_press_triggered = 0;
    }
    // 3. 处理：释放事件
    else {
        diff = time_diff_ms(&now, &state->press_time);//本次按下持续时间
        
        if (diff >= 700) {  // 长按（按住超过700ms）
            if (!state->long_press_triggered) {
                state->long_press_triggered = 1;//防止
                
                if (ev.key_id == KEY_ID_CAPTURE) {
                    // 拍照键长按 → 也拍照（或忽略）
                    printf("[KEY_MGR] Long press CAPTURE -> CAPTURE\n");
                    if (g_callback) g_callback(KEY_EVENT_CAPTURE);
                } else if (ev.key_id == KEY_ID_RECORD) {
                    // 录像键长按 → 停止录像
                    printf("[KEY_MGR] Long press RECORD -> STOP\n");
                    if (g_callback) g_callback(KEY_EVENT_RECORD_STOP);
                }
            }
        } else if (diff >= 50) {
            // 短按（去抖后）
            if (ev.key_id == KEY_ID_CAPTURE) {
                // 拍照键短按 → 拍照
                printf("[KEY_MGR] Short press CAPTURE -> CAPTURE\n");
                if (g_callback) g_callback(KEY_EVENT_CAPTURE);
            } else if (ev.key_id == KEY_ID_RECORD) {
                // 录像键短按 → 开始录像

				// 但注意：如果是双击，已经在上面的按下事件中处理了
                // 这里需要判断是否已经触发了双击，没有双击再录像
                if (state->press_count == 1) {
                    printf("[KEY_MGR] Short press RECORD -> START\n");
                    if (g_callback) g_callback(KEY_EVENT_RECORD_START);
                }
                // 如果 press_count == 2，说明双击已经触发，不重复处理
            }
        }
		// 重置按下次数（防止双击后残留状态）
        if (ev.key_id == KEY_ID_RECORD && state->press_count >= 2) {
            state->press_count = 0;
        }
    }


}

/*退出*/
void key_manager_exit(void) {
    hal_key_exit();
}



