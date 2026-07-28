/**
 * ui_events.c - SquareLine 虚拟按钮事件回调实现
 *
 * 所有回调函数的签名由 ui_events.h 声明，
 * 实际业务逻辑委托给 ui_bridge.c 的接口。
 *
 * SquareLine Studio 不会覆盖这个文件（ui_events.h 声明 / .h 被覆盖，.c 保留）。
 */
#include "ui_events.h"
#include "ui_bridge.h"

/* 接口：触摸屏识别到相应的触摸事件，就调用on函数 - 接着才会调用ui_bridge中的具体实现 */
void on_btnAlbum_clicked(lv_event_t * e)		
{
    (void)e;
    printf("[UI_EVENTS] Album button clicked, switching to album screen\n");
    ui_bridge_show_album();
}

void on_btnBack_clicked(lv_event_t * e)
{
    (void)e;
    printf("[UI_EVENTS] Back button clicked, returning to main screen\n");
    ui_bridge_show_main();
}

void on_btnPrev_clicked(lv_event_t * e)
{
    (void)e;
    printf("[UI_EVENTS] Prev button clicked\n");
    ui_bridge_album_prev();
}

void on_btnNext_clicked(lv_event_t * e)
{
    (void)e;
    printf("[UI_EVENTS] Next button clicked\n");
    ui_bridge_album_next();
}

void on_btnDelete_clicked(lv_event_t * e)
{
    (void)e;
    printf("[UI_EVENTS] Delete button clicked\n");
    ui_bridge_album_delete();
}
