#ifndef _KEY_MANAGER_H_
#define _KEY_MANAGER_H_

/* 按键ID定义 */
#define KEY_ID_CAPTURE  110   // 拍照键
#define KEY_ID_RECORD   129   // 录像,退出键

/* 事件类型 */
typedef enum {
    KEY_EVENT_CAPTURE,         // 拍照（短按拍照按键1用）
    KEY_EVENT_RECORD_START,    // 开始录像（短按录像按键2用）
    KEY_EVENT_RECORD_STOP,     // 停止录像（长按录像按键2用）
    KEY_EVENT_EXIT,            // 退出（双击退出按键2用）
} key_event_type_t;

/* 回调函数类型：业务层注册，按键发生时调用（传入按键号、事件枚举） */
typedef void (*key_callback_t)(key_event_type_t event);

/* 接口 */
int key_manager_init(void);
void key_manager_register_callback(key_callback_t cb);
void key_manager_task(void);  // 主循环调用（子线程读阻塞）
void key_manager_exit(void);



#endif


