/**
 * lv_port_indev.c - LVGL 输入移植层实现
 *
 * 仅启用触摸屏输入 (POINTER 模式)。
 * 物理按键业务逻辑由 main.c 的 on_key_event() 直接处理，
 * 不再映射为 LVGL 编码器事件。
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_port_indev.h"
#include "lvgl/lvgl.h"
#include "hal_touch.h"
#include "ui.h" 

/**********************
 *  STATIC VARIABLES
 **********************/
static lv_indev_drv_t   g_indev_pointer;		//输入驱动载体：注册回调函数和输入类型
static lv_indev_t      *g_indev_pointer_reg = NULL;	//用于保存注册成功后返回的句柄

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void pointer_read(lv_indev_drv_t *drv, lv_indev_data_t *data);

/**********************
 *  pointer_read - 触摸读取回调
 **********************/
static void pointer_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;

    hal_touch_event_t ev;
    static bool pressed = false;   /* 记住当前手指状态 */
    int ret = hal_touch_read(&ev);

    if (ret == 1) {
        printf("[TOUCH] x=%d y=%d action=%d\n", ev.x, ev.y, ev.action);
        data->point.x = ev.x;
        data->point.y = ev.y;
        if (ev.action == HAL_TOUCH_PRESS) {
            pressed = true;
            data->state = LV_INDEV_STATE_PRESSED;
        } else if (ev.action == HAL_TOUCH_RELEASE) {
            pressed = false;
            data->state = LV_INDEV_STATE_RELEASED;
        } else { /* MOVE：手指仍按着 */
            data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        }
    } else {
        /* 无新事件：保持上次状态，不要让 LVGL 误以为松开了 */
        data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    }

}

/**********************
 *  初始化
 **********************/
void lv_port_indev_init(void)
{
    /* hal_touch_init 已在 main.c 中完成，此处仅注册 LVGL POINTER 输入 */
    lv_indev_drv_init(&g_indev_pointer);
    g_indev_pointer.type = LV_INDEV_TYPE_POINTER;	//类型为POINTER
    g_indev_pointer.read_cb = pointer_read;			//注册自定义的回调函数（返回触摸数据）
    g_indev_pointer_reg = lv_indev_drv_register(&g_indev_pointer);	//注册这个drv(pointer设备)，并保存句柄

    printf("[LV_INDEV] init: pointer (touch only)\n");
}
