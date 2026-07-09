#ifndef _HAL_KEY_H
#define _HAL_KEY_H
#include <sys/time.h> 


/*==================== 按键事件结构体 ====================*/
typedef struct {
    int key_id;    // 哪个按键（110=拍照0，129=录像1）
    int pressed;            // 1=按下, 0=释放
    struct timeval timestamp; // 事件发生的时间戳
} hal_key_event_t;

/*==================== 回调函数类型 ====================*/
/* 用于异步通知（SIGIO）或后续扩展 */
typedef void (*hal_key_callback_t)(const hal_key_event_t *ev, void *user_data);

/* 接口函数 */
int hal_key_init(void);
int hal_key_read(hal_key_event_t *ev);  // 阻塞读取
void hal_key_exit(void);



#endif

