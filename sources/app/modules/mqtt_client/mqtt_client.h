/**
 * mqtt_client.h - 轻量 MQTT v3.1.1 客户端（零依赖，纯 POSIX socket + 手工报文）
 */

#ifndef _MQTT_CLIENT_H
#define _MQTT_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== 回调类型 ==================== */
typedef void (*mqtt_publish_cb_t)(const char *topic, size_t topic_len,
                                  const void *payload, size_t payload_len,
                                  void *user_data);

/* ==================== 状态 ==================== */
typedef enum {
    MQTT_STATE_DISCONNECTED = 0,
    MQTT_STATE_CONNECTING,
    MQTT_STATE_CONNECTED,
} mqtt_state_t;

/* ==================== 初始化 & 销毁 ==================== */
int mqtt_init(const char *host, uint16_t port, const char *client_id);
void mqtt_exit(void);

/* ==================== 连接管理 ==================== */
int  mqtt_connect(void);
void mqtt_disconnect(void);
mqtt_state_t mqtt_get_state(void);

/* ==================== 收发 ==================== */
int mqtt_publish(const char *topic, const void *payload,
                 size_t payload_len, uint8_t qos, bool retain);
int mqtt_subscribe(const char *topic, uint8_t qos,
                   mqtt_publish_cb_t cb, void *user_data);

/* ==================== 主循环集成 ==================== */
int mqtt_loop(void);

/* ==================== 全局状态（给 ui_bridge 用）==================== */
extern volatile int g_mqtt_connected;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _MQTT_CLIENT_H */
