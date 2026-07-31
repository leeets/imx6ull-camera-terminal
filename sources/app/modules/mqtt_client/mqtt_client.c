/**
 * mqtt_client.c - 轻量 MQTT v3.1.1 客户端实现
 *
 * 原理：
 *   纯 POSIX socket (TCP) + 手工组装 MQTT 报文。
 *   - 无任何第三方库依赖
 *   - 非阻塞 socket + 主线程轮询 mqtt_loop()
 *   - 支持 QoS 0 发布和订阅
 *   - 30 秒心跳保活
 */

/*********************
 *      INCLUDES
 *********************/
#include "mqtt_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <time.h>

/*********************
 *      DEFINES
 *********************/

/* MQTT 协议版本 */
#define MQTT_PROTOCOL_LEVEL  4   /* v3.1.1 */

/* MQTT 报文类型（固定头高4位）*/
#define MQTT_TYPE_CONNECT      0x10
#define MQTT_TYPE_CONNACK      0x20
#define MQTT_TYPE_PUBLISH      0x30
#define MQTT_TYPE_PUBACK       0x40
#define MQTT_TYPE_SUBSCRIBE    0x82
#define MQTT_TYPE_SUBACK       0x90
#define MQTT_TYPE_PINGREQ      0xC0
#define MQTT_TYPE_PINGRESP     0xD0
#define MQTT_TYPE_DISCONNECT   0xE0

/* 连接标志位 */
#define MQTT_CONN_FLAG_CLEAN   0x02
#define MQTT_CONN_FLAG_WILL    0x04
#define MQTT_CONN_FLAG_USER    0x80
#define MQTT_CONN_FLAG_PASS    0x40

/* 默认配置 */
#define MQTT_DEFAULT_KEEPALIVE 30   /* 心跳间隔秒数 */
#define MQTT_RECV_BUF_SIZE     4096
#define MQTT_MAX_SUBSCRIBERS    16	/* 最大订阅者数量 */

/**********************
 *  STRUCTURES
 **********************/

/* 订阅者条目（本client设备可以有好几个订阅信息） */
typedef struct {
    char              topic[128];
    size_t            topic_len;
    uint8_t           qos;
    mqtt_publish_cb_t cb;
    void             *user_data;
    bool              active;
} mqtt_subscriber_t;

/* MQTT 客户端实例（仅此一个设备就是client） */
typedef struct {
    /* 连接参数 */
    char      host[256];
    uint16_t  port;
    char      client_id[64];

    /* socket */
    int       sock_fd;
    mqtt_state_t state;

    /* 心跳计时 */
    time_t    last_send;   /* 上次发送数据的时间 */
    time_t    last_recv;   /* 上次接收数据的时间 */

    /* 接收缓冲区 */
    uint8_t   recv_buf[MQTT_RECV_BUF_SIZE];
    size_t    recv_len;

    /* 订阅者列表 */
    mqtt_subscriber_t subscribers[MQTT_MAX_SUBSCRIBERS];
    int       sub_count;

    /* 报文标识符（递增）*/
    uint16_t  packet_id;
} mqtt_client_t;

/**********************
 *  STATIC VARIABLES
 **********************/

static mqtt_client_t g_mqtt = {0};  //客户端初始化

/* 全局连接状态，供 ui_bridge 读取 */
volatile int g_mqtt_connected = 0;

/**********************
 *  FORWARD DECLARATIONS（为了工程规范、统一风格、防止出现先调用后声明！）
 **********************/
static int  sock_connect(const char *host, uint16_t port);
static void sock_close(void);
static int  sock_send_all(const uint8_t *data, size_t len);
static int  sock_recv_all(uint8_t *buf, size_t len, int timeout_ms);
static int  mqtt_encode_remaining_length(uint8_t *buf, uint32_t length);
static int  mqtt_decode_remaining_length(const uint8_t *buf, uint32_t *value, int *bytes);
static int  mqtt_send_connect(void);
static int  mqtt_recv_connack(void);
static int  mqtt_send_pingreq(void);
static int  mqtt_handle_publish(uint8_t *buf, size_t len);
static int  mqtt_dispatch_publish(const char *topic, size_t topic_len,
                                  const void *payload, size_t payload_len);

/**********************
 *  socket套接字操作
 **********************/

static int sock_connect(const char *host, uint16_t port)
{
    int fd;
    struct sockaddr_in addr;
    struct hostent *he;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("[MQTT] socket");
        return -1;
    }

    /* 设为非阻塞（用于 mqtt_loop 中的非阻塞 recv）*/
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);  //但是

    /* 禁用 Nagle 算法，减少小包延迟 */
    int opt = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    /* DNS 解析 */
    he = gethostbyname(host);
    if (!he) {
        fprintf(stderr, "[MQTT] DNS 解析失败: %s\n", host);
        close(fd);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);

    /* 连接（由于非阻塞，第一次连接会返回 -1 EINPROGRESS）*/
    int ret = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
    if (ret < 0 && errno != EINPROGRESS) {
        perror("[MQTT] connect");
        close(fd);
        return -1;
    }

    /* 等待连接完成（最多 3 秒）*/
    fd_set wset;
    struct timeval tv;
    FD_ZERO(&wset);
    FD_SET(fd, &wset);
    tv.tv_sec  = 3;
    tv.tv_usec = 0;

    ret = select(fd + 1, NULL, &wset, NULL, &tv);
    if (ret <= 0) {
        fprintf(stderr, "[MQTT] 连接超时\n");
        close(fd);
        return -1;
    }

    /* 检查连接是否成功 */
    int err = 0;
    socklen_t errlen = sizeof(err);
    getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen);
    if (err != 0) {
        fprintf(stderr, "[MQTT] 连接失败: %s\n", strerror(err));
        close(fd);
        return -1;
    }

    printf("[MQTT] TCP 已连接 %s:%d (fd=%d)\n", host, port, fd);
    return fd;
}

static void sock_close(void)
{
    if (g_mqtt.sock_fd >= 0) {
        close(g_mqtt.sock_fd);
        g_mqtt.sock_fd = -1;
    }
}

/* 发送全部字节（阻塞模式）*/
static int sock_send_all(const uint8_t *data, size_t len)
{
    if (g_mqtt.sock_fd < 0) return -1;

    /* 临时切回阻塞模式发完整报文 */
    int flags = fcntl(g_mqtt.sock_fd, F_GETFL, 0);
    fcntl(g_mqtt.sock_fd, F_SETFL, flags & ~O_NONBLOCK);

    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(g_mqtt.sock_fd, data + sent, len - sent, 0);
        if (n <= 0) {
            fcntl(g_mqtt.sock_fd, F_SETFL, flags);
            return -1;
        }
        sent += n;
    }

    fcntl(g_mqtt.sock_fd, F_SETFL, flags);
    g_mqtt.last_send = time(NULL);
    return 0;
}

/* 非阻塞读取recieve（不等待完整长度，有多少读多少）*/
static int sock_recv_all(uint8_t *buf, size_t len, int timeout_ms)
{
    (void)timeout_ms;
    if (g_mqtt.sock_fd < 0) return -1;

    ssize_t n = recv(g_mqtt.sock_fd, buf, len, 0);
    if (n > 0) {
        g_mqtt.last_recv = time(NULL);
    }
    return (int)n;
}

/**********************
 *  MQTT 报文编码/解码
 **********************/

/* 剩余长度编码（MQTT v3.1.1 可变长度编码）编码放进buf,返回实际用了bytes */
/* MQTT 协议报文（Packet）中，固定头（Fixed Header）之后所有内容的字节数 */
/* 从这个字节之后，你还需要再读 N 个字节，才算收到一个完整的 MQTT 报文 */
static int mqtt_encode_remaining_length(uint8_t *buf, uint32_t length)
{
    int bytes = 0;
    do {
        uint8_t byte = length % 128;
        length /= 128;
        if (length > 0) byte |= 0x80;
        buf[bytes++] = byte;	//“剩余长度”的编码表达
    } while (length > 0);
    return bytes;
}

/* 剩余长度解码 -      剩余长度解析出来整数值value */
static int mqtt_decode_remaining_length(const uint8_t *buf, uint32_t *value, int *bytes)
{
    uint32_t val = 0;
    int b = 0;
    int multiplier = 1;

    do {
        if (b >= 4) return -1;  /* 非法 */
        val += (buf[b] & 0x7F) * multiplier;	//
        multiplier *= 128;
    } while (buf[b++] & 0x80);

    *value = val;
    *bytes = b;
    return 0;
}

/* 用于写入 16 位长度前缀头（大端）*/
static inline void write_uint16(uint8_t *buf, uint16_t val)
{
    buf[0] = (uint8_t)(val >> 8);
    buf[1] = (uint8_t)(val & 0xFF);
}

/************************************************************
 *  组装并发送 CONNECT 报文（协议名 "MQTT" + 版本 + Client ID）
 ************************************************************/
static int mqtt_send_connect(void)
{
    uint8_t buf[256];
    int pos = 0;

    /* 可变头：协议名 "MQTT" + 协议级别 + 连接标志 + Keep Alive */
    /* 协议名 "MQTT" (4 字节) */
    write_uint16(buf + pos, 4);  pos += 2;
    memcpy(buf + pos, "MQTT", 4); pos += 4;

    /* 协议级别 (v3.1.1 = 4) */
    buf[pos++] = MQTT_PROTOCOL_LEVEL;

    /* 连接标志：Clean Session + 无遗嘱/用户名/密码 */
    buf[pos++] = MQTT_CONN_FLAG_CLEAN;

    /* Keep Alive (秒) */
    write_uint16(buf + pos, MQTT_DEFAULT_KEEPALIVE); pos += 2;

    /* 有效载荷：Client ID */
    size_t id_len = strlen(g_mqtt.client_id);
    write_uint16(buf + pos, (uint16_t)id_len); pos += 2;
    memcpy(buf + pos, g_mqtt.client_id, id_len); pos += (int)id_len;

    /* 组装 CONNECT 报文 */
    uint8_t packet[512];
    int pkt_pos = 0;

    /* 固定头：类型 */
    packet[pkt_pos++] = MQTT_TYPE_CONNECT;

    /* MQTT剩余长度 编码到packet */
    uint32_t remaining = pos;
    int rl_bytes = mqtt_encode_remaining_length(packet + pkt_pos, remaining);
    pkt_pos += rl_bytes;

    /* 可变头 + 有效载荷 */
    memcpy(packet + pkt_pos, buf, pos);
    pkt_pos += pos;

    printf("[MQTT] 发送 CONNECT (client_id=%s, keepalive=%ds)\n",
           g_mqtt.client_id, MQTT_DEFAULT_KEEPALIVE);

    return sock_send_all(packet, pkt_pos);
}

/* 阻塞等待并解析 CONNACK，检查 Broker 是否接受连接（返回码 0=成功） */
static int mqtt_recv_connack(void)
{
    uint8_t buf[4];
    int n;

    /* 读固定头（2 字节：拿出类型 + decode剩余长度）*/
    n = (int)recv(g_mqtt.sock_fd, buf, 2, MSG_WAITALL);	//等待接收2字节的内容（类型）再返回
    if (n != 2) return -1;

    uint32_t remaining;
    int rl_bytes;
    if (mqtt_decode_remaining_length(buf + 1, &remaining, &rl_bytes) < 0)
        return -1;

    /* 读可变头（2 字节：连接确认标志 + 返回码）*/
    n = (int)recv(g_mqtt.sock_fd, buf, 2, MSG_WAITALL);
    if (n != 2) return -1;

    uint8_t return_code = buf[1];
    if (return_code != 0) {
        fprintf(stderr, "[MQTT] CONNACK 拒绝: code=%d\n", return_code);
        return -1;
    }

    printf("[MQTT] CONNACK 已接受\n");
    return 0;
}

/**********************
 *  发送心跳包（PINGREQ），保持 MQTT 长连接不被 Broker（服务器） 踢掉
 **********************/

static int mqtt_send_pingreq(void)
{
    uint8_t packet[2] = {MQTT_TYPE_PINGREQ, 0x00};//心跳包
    printf("[MQTT] → PINGREQ\n");
    return sock_send_all(packet, 2);	//发送全部字节（心跳包）	
}

/**********************
 *  MQTT 报文分发（遍历订阅者列表，按 Topic 前缀匹配，命中则调用对应的回调函数）
 **********************/

static int mqtt_dispatch_publish(const char *topic, size_t topic_len,
                                  const void *payload, size_t payload_len)
{
    for (int i = 0; i < g_mqtt.sub_count; i++) {
        mqtt_subscriber_t *sub = &g_mqtt.subscribers[i];	//订阅者列表放进来
        if (!sub->active) continue;

        /* 简单前缀匹配（不支持通配符 +/#）*/
        if (topic_len >= sub->topic_len &&
            memcmp(topic, sub->topic, sub->topic_len) == 0) {	//topic相同的话：
            if (sub->cb) {
                sub->cb(topic, topic_len, payload, payload_len, sub->user_data)  //调用订阅者的回调
            }
        }
    }
    return 0;
}

//广播 （解析 PUBLISH 报文，从字节流中提取 Topic 和 Payload，然后调用分发函数）
static int mqtt_handle_publish(uint8_t *buf, size_t len)
{
    (void)len;

    uint8_t *p = buf;

    /* 跳过固定头（1 字节类型 + 剩余长度编码）*/
    p++;
    uint32_t remaining;
    int rl_bytes;
    if (mqtt_decode_remaining_length(p, &remaining, &rl_bytes) < 0)
        return -1;
    p += rl_bytes;

    uint8_t qos = (buf[0] & 0x06) >> 1;   /* 从固定头取 QoS */

    /* 主题长度（2 字节）*/
    uint16_t topic_len = (uint16_t)((p[0] << 8) | p[1]);
    p += 2;

    char *topic = (char *)p;
    p += topic_len;

    /* QoS 1 时有报文标识符 */
    if (qos > 0) {
        p += 2;  /* 跳过 packet_id */
    }

    size_t payload_len = remaining - 2 - topic_len;
    if (qos > 0) payload_len -= 2;

    printf("[MQTT] ← PUBLISH topic=%.*s (%zu bytes)\n",
           (int)topic_len, topic, payload_len);

    mqtt_dispatch_publish(topic, topic_len, p, payload_len);
    return 0;
}

//订阅者确认（解析 SUBACK 报文，打印服务器返回的订阅确认码（调试用））
static int mqtt_handle_suback(uint8_t *buf, size_t len)
{
    (void)len;
    uint8_t *p = buf + 1; /* 跳过固定头 */

    uint32_t remaining;
    int rl_bytes;
    if (mqtt_decode_remaining_length(p, &remaining, &rl_bytes) < 0)
        return -1;
    p += rl_bytes;

    uint16_t pid = (uint16_t)((p[0] << 8) | p[1]);
    uint8_t ret_code = p[2];

    printf("[MQTT] ← SUBACK pid=%u, ret_code=%d\n", pid, ret_code);
    return 0;
}

/**********************
 *  公共 API 实现
 **********************/
/* MQTT员工准备就绪 */
int mqtt_init(const char *host, uint16_t port, const char *client_id)//服务器ip+端口号
{
    memset(&g_mqtt, 0, sizeof(g_mqtt));
	/* 设置服务器ip+端口号和标志 */
    strncpy(g_mqtt.host, host, sizeof(g_mqtt.host) - 1);
    g_mqtt.port = port;
    strncpy(g_mqtt.client_id, client_id, sizeof(g_mqtt.client_id) - 1);
    g_mqtt.sock_fd = -1;
    g_mqtt.state  = MQTT_STATE_DISCONNECTED;
    g_mqtt.packet_id = 1;

    g_mqtt_connected = 0;

    printf("[MQTT] 已初始化: broker=%s:%d, client_id=%s\n",
           host, port, client_id);
    return 0;
}

/* 退出 断联+重置客户端 */
void mqtt_exit(void)
{
    mqtt_disconnect();
    memset(&g_mqtt, 0, sizeof(g_mqtt));
    g_mqtt_connected = 0;
    printf("[MQTT] 已退出\n");
}

/* TCP通了，MQTT身份认证通过 */
int mqtt_connect(void)
{
    if (g_mqtt.state == MQTT_STATE_CONNECTED) return 0;

    g_mqtt.state = MQTT_STATE_CONNECTING;

    /* 1. TCP 连接 */
    int fd = sock_connect(g_mqtt.host, g_mqtt.port);
    if (fd < 0) {
        g_mqtt.state = MQTT_STATE_DISCONNECTED;
        return -1;
    }
    g_mqtt.sock_fd = fd;

    /* 2. mqtt发送 CONNECT */
    if (mqtt_send_connect() < 0) {
        sock_close();
        g_mqtt.state = MQTT_STATE_DISCONNECTED;
        return -1;
    }

    /* 3. 等待 CONNACK确认 */
    if (mqtt_recv_connack() < 0) {
        sock_close();
        g_mqtt.state = MQTT_STATE_DISCONNECTED;
        return -1;
    }

    g_mqtt.state  = MQTT_STATE_CONNECTED;
    g_mqtt_connected = 1;
    g_mqtt.last_send = time(NULL);
    g_mqtt.last_recv = time(NULL);

    printf("[MQTT] 已连接到 Broker\n");
    return 0;
}

void mqtt_disconnect(void)
{
    if (g_mqtt.state == MQTT_STATE_DISCONNECTED) return;

    /* 发送 DISCONNECT 报文，更新状态 */
    uint8_t packet[2] = {MQTT_TYPE_DISCONNECT, 0x00};
    sock_send_all(packet, 2);

    g_mqtt.state  = MQTT_STATE_DISCONNECTED;
    g_mqtt_connected = 0;
    sock_close();

    printf("[MQTT] 已断开\n");
}

mqtt_state_t mqtt_get_state(void)
{
    return g_mqtt.state;
}

/* 作为发布者向某个topic发送一个数据：数据publish上传成功 */
int mqtt_publish(const char *topic, const void *payload,
                 size_t payload_len, uint8_t qos, bool retain)
{
    if (g_mqtt.state != MQTT_STATE_CONNECTED) return -1;

    size_t topic_len = strlen(topic);

    /* 构造 PUBLISH 报文 */
    uint8_t header_type = MQTT_TYPE_PUBLISH;
    if (retain) header_type |= 0x01;
    header_type |= (qos << 1);

    uint32_t remaining = 2 + topic_len + payload_len;  /* QoS 0 无 packet_id */
    if (qos > 0) remaining += 2;

    uint8_t rl_buf[4];
    int rl_bytes = mqtt_encode_remaining_length(rl_buf, remaining);

    uint8_t *packet = malloc(1 + rl_bytes + remaining);
    if (!packet) return -1;

    int pos = 0;
    packet[pos++] = header_type;
    memcpy(packet + pos, rl_buf, rl_bytes); pos += rl_bytes;

    write_uint16(packet + pos, topic_len); pos += 2;
    memcpy(packet + pos, topic, topic_len); pos += (int)topic_len;

    if (qos > 0) {
        write_uint16(packet + pos, g_mqtt.packet_id++); pos += 2;
    }

    memcpy(packet + pos, payload, payload_len); pos += (int)payload_len;

    printf("[MQTT] → PUBLISH topic=%s (%zu bytes, qos=%d)\n",
           topic, payload_len, qos);

    int ret = sock_send_all(packet, pos);
    free(packet);
    return ret;
}

/* 对“topic”感兴趣，作为订阅者，设备进入接收服务器推送来的消息的状态 */
int mqtt_subscribe(const char *topic, uint8_t qos,
                   mqtt_publish_cb_t cb, void *user_data)
{
    if (g_mqtt.sub_count >= MQTT_MAX_SUBSCRIBERS) {
        fprintf(stderr, "[MQTT] 订阅者列表已满\n");
        return -1;
    }

    /* 保存订阅信息 */
    mqtt_subscriber_t *sub = &g_mqtt.subscribers[g_mqtt.sub_count++];
    sub->topic_len = strlen(topic);
    if (sub->topic_len >= sizeof(sub->topic)) sub->topic_len = sizeof(sub->topic) - 1;
    memcpy(sub->topic, topic, sub->topic_len);
    sub->topic[sub->topic_len] = '\0';
    sub->qos       = qos;
    sub->cb        = cb;
    sub->user_data = user_data;
    sub->active    = true;

    /* 发送 SUBSCRIBE 报文 */
    uint16_t pid = g_mqtt.packet_id++;

    uint32_t remaining = 2 + 2 + sub->topic_len + 1;  /* pid + topic_len + topic + qos */
    uint8_t rl_buf[4];
    int rl_bytes = mqtt_encode_remaining_length(rl_buf, remaining);

    uint8_t packet[256];
    int pos = 0;
    packet[pos++] = MQTT_TYPE_SUBSCRIBE;
    memcpy(packet + pos, rl_buf, rl_bytes); pos += rl_bytes;

    write_uint16(packet + pos, pid); pos += 2;
    write_uint16(packet + pos, (uint16_t)sub->topic_len); pos += 2;
    memcpy(packet + pos, topic, sub->topic_len); pos += (int)sub->topic_len;
    packet[pos++] = qos;

    printf("[MQTT] → SUBSCRIBE topic=%s (pid=%u)\n", topic, pid);
    return sock_send_all(packet, pos);
}

/* MQTT客户端动作, 在主线程中被循环调用 */
int mqtt_loop(void)
{
    if (g_mqtt.state != MQTT_STATE_CONNECTED) {
        /* 尝试自动重连（每 5 秒重试一次）*/
        static time_t last_retry = 0;
        time_t now = time(NULL);
        if (now - last_retry >= 5) {
            last_retry = now;
            printf("[MQTT] 尝试重连...\n");
            mqtt_connect();
        }
        return -1;
    }

    /* 1. 非阻塞接收数据 */
    int n = sock_recv_all(g_mqtt.recv_buf + g_mqtt.recv_len,
                          MQTT_RECV_BUF_SIZE - g_mqtt.recv_len, 0);	//用封装好的
    if (n > 0) {
        g_mqtt.recv_len += n;

        /* 尝试解析一个完整的报文 */
        while (g_mqtt.recv_len >= 2) {
            uint8_t type = g_mqtt.recv_buf[0];
            uint32_t remaining;
            int rl_bytes;

            if (mqtt_decode_remaining_length(g_mqtt.recv_buf + 1,
                                             &remaining, &rl_bytes) < 0)
                break;

            size_t pkt_len = 1 + rl_bytes + remaining;
            if (g_mqtt.recv_len < pkt_len) break;  /* 报文还不完整 */

            /* 按类型处理 */
            switch (type & 0xF0) {
            case MQTT_TYPE_PUBLISH:
                mqtt_handle_publish(g_mqtt.recv_buf, pkt_len);	//封装好的，广播分发内容（Topic 和 Payload）
                break;
            case MQTT_TYPE_SUBACK:
                mqtt_handle_suback(g_mqtt.recv_buf, pkt_len);	//封装好的，服务器给的订阅确认
                break;
            case MQTT_TYPE_PINGRESP:
                printf("[MQTT] ← PINGRESP\n");
                break;
            case MQTT_TYPE_CONNACK:
                /* 重连后收到，忽略 */
                break;
            default:
                printf("[MQTT] ← 未知报文 type=0x%02x\n", type);
                break;
            }

            /* 从缓冲区移除已处理的报文 */
            size_t consumed = pkt_len;
            if (g_mqtt.recv_len > consumed) {
                memmove(g_mqtt.recv_buf, g_mqtt.recv_buf + consumed,
                        g_mqtt.recv_len - consumed);
            }
            g_mqtt.recv_len -= consumed;
        }
    } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        /* 连接断开 */
        printf("[MQTT] 连接断开\n");
        sock_close();
        g_mqtt.state  = MQTT_STATE_DISCONNECTED;	//更新状态
        g_mqtt_connected = 0;
        return -1;
    }

    /* 2. 心跳检查（每隔 keepalive/2 秒发送 PINGREQ）*/
    time_t now = time(NULL);
    if (now - g_mqtt.last_send >= MQTT_DEFAULT_KEEPALIVE / 2) {
        mqtt_send_pingreq();		//封装好的
    }

    return 0;
}
