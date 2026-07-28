 /*
 * gps_daemon.c - GPS 定位模块实现（daemon 守护进程：脱离终端，后台常驻）
 *
 * 架构:
 *   gps_init()       打开 UART 并启动读取线程
 *   gps_read_thread() 独立线程：循环 read() 串口数据 → 按行分割 → NMEA 解析
 *   gps_get_data()     主线程调用，原子拷贝最新数据
 *
 * 串口: /dev/ttymxc5 (UART6) 9600 8N1
 * 协议: NMEA 0183 (GPGGA + GPRMC)
 */

#include "gps_daemon.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <pthread.h>
#include <sys/types.h>

/* ==================== 配置 ==================== */
#define GPS_DEFAULT_DEV     "/dev/ttymxc5"
#define GPS_BAUDRATE        B9600
#define READ_BUF_SIZE       512         /* 单次 read 缓冲区 */
#define LINE_BUF_SIZE       256         /* 单行 NMEA 语句最大长度 */
#define MAX_LINE_CACHE      8           /* 行缓存行数 */

/* ==================== 全局状态 ==================== */
static int          g_fd = -1;                  /* 串口文件描述符 */
static pthread_t    g_thread;
static volatile int g_running = 0;

static gps_data_t   g_gps_data;                 /* 全局 最新定位数据 */
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ==================== 串口初始化 ==================== */
static int uart_open(const char *dev) {
    int fd = open(dev, O_RDWR | O_NOCTTY);		//可读可写，不可成为终端
    if (fd < 0) {
        perror("[GPS] open");
        return -1;
    }

    struct termios tio;		//配置串口行规程参数的结构体
    memset(&tio, 0, sizeof(tio));	//置零（防止校验错误）

    if (tcgetattr(fd, &tio) < 0) {		//获取tty的属性
        perror("[GPS] tcgetattr");
        close(fd);
        return -1;
    }

    cfsetospeed(&tio, GPS_BAUDRATE);	//设置输出波特率
    cfsetispeed(&tio, GPS_BAUDRATE);	//设置输入波特率

    /* 8N1 模式（8data bits/no Parity/1stop bits），设置tio参数 */
    tio.c_cflag &= ~PARENB;     /* 无校验 */
    tio.c_cflag &= ~CSTOPB;     /* 1位停止位 */
    tio.c_cflag &= ~CSIZE;
    tio.c_cflag |= CS8;         /* 8位数据 */
    tio.c_cflag |= CREAD | CLOCAL;/* 使能接收 */

    /* 关闭硬件流控 */
    tio.c_cflag &= ~CRTSCTS;

    /* 原始输入模式 */
    tio.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tio.c_iflag &= ~(IXON | IXOFF | IXANY | ICRNL | INLCR);
    tio.c_oflag &= ~OPOST;

    /* 设置 VTIME (超时 100ms) 和 VMIN */
    tio.c_cc[VTIME] = 10;      /* 1秒超时 (10 * 100ms)     仿阻塞*/
    tio.c_cc[VMIN]  = 0;       /* 非严格读取 */

    tcflush(fd, TCIOFLUSH);		//清空缓存区，清除残留的数据

    if (tcsetattr(fd, TCSANOW, &tio) < 0) {    //修改串口的属性--使写入生效
        perror("[GPS] tcsetattr");
        close(fd);
        return -1;
    }

    printf("[GPS] opened %s at 9600 8N1\n", dev);
    return fd;
}

/* ==================== NMEA 解析 ==================== */

/*
 * 提取逗号分隔字段的第 n 个数据 (0-based)
 * 返回指向该字段起始位置的指针，或 NULL
 * 用于逗号分隔地逐个解析GP语句的line的数据出来
 */
static const char* nmea_field(const char *line, int n) {
    const char *p = line;	//一行数据
    int i = 0;
    while (*p) {
        if (i == n) return p;//读取第 n 个 结束
        if (*p == ',') i++;
        p++;
    }
    return NULL;
}

/*
 * 从字符串中读取字段长度（到下一个逗号或 \0）
 */
static int field_len(const char *s) {
    int len = 0;
    while (s[len] && s[len] != ',' && s[len] != '*') len++;
    return len;
}

/* 实现两种解析语句类型，放进g_gps_data对应位置（临界资源一定要上互斥锁！） */
/*
 * 解析 GPGGA 语句line（包含的定位精度信息更详细（含海拔、差分状态等））
 * $GPGGA,064036.00,2231.52134,N,11357.37227,E,1,08,1.0,18.3,M,4.2,M,,*4B
 *          ^time   ^lat      ^N ^lon        ^E ^fix ^sat ^hdop ^alt ^M
 */
static void parse_gga(const char *line) {
    const char *f;
    char buf[32];
    int len;

    /* 纬度: 格式 ddmm.mmmmm */
    f = nmea_field(line, 2);
    if (!f || *f == ',') return;
    len = field_len(f);
    if (len <= 0 || len >= (int)sizeof(buf)) return;
    memcpy(buf, f, len); buf[len] = '\0';

    double lat_raw = atof(buf);
    int lat_deg = (int)(lat_raw / 100.0);
    double lat_min = lat_raw - lat_deg * 100.0;
    double latitude = lat_deg + lat_min / 60.0;

    /* 南北纬 */
    f = nmea_field(line, 3);
    if (f && *f == 'S') latitude = -latitude;

    /* 经度: 格式 dddmm.mmmmm */
    f = nmea_field(line, 4);
    if (!f || *f == ',') return;
    len = field_len(f);
    if (len <= 0 || len >= (int)sizeof(buf)) return;
    memcpy(buf, f, len); buf[len] = '\0';

    double lon_raw = atof(buf);
    int lon_deg = (int)(lon_raw / 100.0);
    double lon_min = lon_raw - lon_deg * 100.0;
    double longitude = lon_deg + lon_min / 60.0;

    /* 东西经 */
    f = nmea_field(line, 5);
    if (f && *f == 'W') longitude = -longitude;

    /* 定位状态: 0=无效, 1=GPS, 2=DGPS */
    f = nmea_field(line, 6);
    int fix = (f && *f != ',') ? atoi(f) : 0;

    /* 卫星数 */
    f = nmea_field(line, 7);
    int sats = (f && *f != ',') ? atoi(f) : 0;

    /* 海拔 */
    f = nmea_field(line, 9);
    double alt = (f && *f != ',') ? atof(f) : 0.0;

    /* UTC 时间: hhmmss.ss */
    f = nmea_field(line, 1);
    if (f && *f != ',') {
        len = field_len(f);
        if (len >= 6) {
            memcpy(buf, f, 6); buf[6] = '\0';
        }
    }

    pthread_mutex_lock(&g_mutex);
    g_gps_data.latitude  = latitude;
    g_gps_data.longitude = longitude;
    g_gps_data.altitude  = alt;
    g_gps_data.satellites = sats;
    if (fix >= 1) g_gps_data.valid = true;
    pthread_mutex_unlock(&g_mutex);
}

/*
 * 解析 GPRMC 语句（包含的数据更全面（含速度和方位角））
 * $GPRMC,064036.00,A,2231.52134,N,11357.37227,E,0.08,77.4,180724,,,A*7A
 *          ^time      ^st ^lat      ^N ^lon        ^E ^spd  ^crs ^date
 */
static void parse_rmc(const char *line) {
    const char *f;
    char buf[32];
    int len;

    /* 定位状态: A=有效, V=无效 */
    f = nmea_field(line, 2);
    bool valid = (f && *f == 'A');

    /* === 用局部变量计算所有字段，末尾统一加锁写入 === */
    double lat = 0.0, lon = 0.0, spd = 0.0, crs = 0.0;
    int h = 0, m = 0, s = 0;

    /* 纬度 */
    f = nmea_field(line, 3);
    if (f && *f != ',') {
        len = field_len(f);
        if (len > 0 && len < (int)sizeof(buf)) {
            memcpy(buf, f, len); buf[len] = '\0';
            double raw = atof(buf);
            int deg = (int)(raw / 100.0);
            lat = deg + (raw - deg * 100.0) / 60.0;
        }
    }

    f = nmea_field(line, 4);
    if (f && *f == 'S') lat = -lat;

    /* 经度 */
    f = nmea_field(line, 5);
    if (f && *f != ',') {
        len = field_len(f);
        if (len > 0 && len < (int)sizeof(buf)) {
            memcpy(buf, f, len); buf[len] = '\0';
            double raw = atof(buf);
            int deg = (int)(raw / 100.0);
            lon = deg + (raw - deg * 100.0) / 60.0;
        }
    }

    f = nmea_field(line, 6);
    if (f && *f == 'W') lon = -lon;

    /* 地面速度 (节) */
    f = nmea_field(line, 7);
    if (f && *f != ',') spd = atof(f);

    /* 对地航向 (度) */
    f = nmea_field(line, 8);
    if (f && *f != ',') crs = atof(f);

    /* UTC 时间 */
    f = nmea_field(line, 1);
    if (f && *f != ',') {
        len = field_len(f);
        if (len >= 6) {
            memcpy(buf, f, 6); buf[6] = '\0';
            char tmp[4];
            memcpy(tmp, buf, 2); tmp[2] = '\0'; h = atoi(tmp);
            memcpy(tmp, buf+2, 2); tmp[2] = '\0'; m = atoi(tmp);
            memcpy(tmp, buf+4, 2); tmp[2] = '\0'; s = atoi(tmp);
        }
    }

    /* 单次加锁写入所有字段，避免主线程读到半写状态 */
    pthread_mutex_lock(&g_mutex);
    g_gps_data.valid     = valid;
    g_gps_data.latitude  = lat;
    g_gps_data.longitude = lon;
    g_gps_data.speed     = spd;
    g_gps_data.course    = crs;
    g_gps_data.hour      = h;
    g_gps_data.minute    = m;
    g_gps_data.second    = s;
    pthread_mutex_unlock(&g_mutex);
}


/* 最上层的解析行的封装函数，直接用。
 * 解析一行 NMEA（标准协议，包括$GPGGA和$GPRMC两种具体语句类型） 语句
 */
static void parse_nmea_line(const char *line) {
    if (line[0] != '$') return;

    if (strncmp(line, "$GPGGA", 6) == 0)	//前六个字符就是 “$ + 类型标识符”
        parse_gga(line);
    else if (strncmp(line, "$GPRMC", 6) == 0)
        parse_rmc(line);
    /* 其他语句如 GPGSV / GPGSA 可后续扩展 */
}

/* ==================== GPS读取线程 ==================== */
static void *gps_read_thread(void *arg) {
    (void)arg;

    char read_buf[READ_BUF_SIZE];
    /* 行缓存：缓存半个行的情况 */
    char line_cache[LINE_BUF_SIZE];
    int  line_pos = 0;

    printf("[GPS] reader thread started\n");

    while (g_running) {
        int n = read(g_fd, read_buf, sizeof(read_buf) - 1);		//一次read到数据到buf（最多512）
        if (n < 0) {
            if (errno == EAGAIN || errno == EINTR)
                continue;
            perror("[GPS] read");
            break;
        }
        if (n == 0) {
            /* 超时无数据 */
            usleep(10000);
            continue;
        }

        read_buf[n] = '\0';

        /* 对read_buf逐字节处理，按 \n 分割行 */
        for (int i = 0; i < n; i++) {
            char c = read_buf[i];	

            if (c == '\n' || c == '\r') {	//读到行尾：
                if (line_pos > 0) {
                    line_cache[line_pos] = '\0';
                    parse_nmea_line(line_cache);	//这一行字符串到行尾了，parse
                    line_pos = 0;
                }
                /* 跳过连续的 \r\n */
            } else {						//读到数值：
                if (line_pos < LINE_BUF_SIZE - 1) {
                    line_cache[line_pos++] = c;	//逐个放进cache里
                } else {
                    /* 行过长，丢弃 */
                    line_pos = 0;
                }
            }
        }
    }

    printf("[GPS] reader thread exiting\n");
    return NULL;
}

/* ==================== 对外接口 ==================== */
int gps_init(const char *uart_dev) {
    if (g_fd >= 0) {
        printf("[GPS] already initialized\n");
        return 0;
    }

    if (!uart_dev) uart_dev = GPS_DEFAULT_DEV;

    g_fd = uart_open(uart_dev);
    if (g_fd < 0) return -1;

    /* 初始化默认数据 */
    memset(&g_gps_data, 0, sizeof(g_gps_data));
    g_gps_data.valid = false;

    /* 启动读取线程 */
    g_running = 1;
    if (pthread_create(&g_thread, NULL, gps_read_thread, NULL) != 0) {
        perror("[GPS] pthread_create");
        g_running = 0;
        close(g_fd);
        g_fd = -1;
        return -1;
    }

    printf("[GPS] init OK\n");
    return 0;
}

int gps_get_data(gps_data_t *out) {		//拿最新的数据出来
    if (!out) return -1;
    if (g_fd < 0) return -1;

    pthread_mutex_lock(&g_mutex);
    memcpy(out, &g_gps_data, sizeof(gps_data_t));
    pthread_mutex_unlock(&g_mutex);

    return 0;
}

bool gps_is_valid(void) {			//一个gps_data_t的有效标志位
    bool v;
    pthread_mutex_lock(&g_mutex);
    v = g_gps_data.valid;
    pthread_mutex_unlock(&g_mutex);
    return v;
}

void gps_exit(void) {		//释放资源（回收线程）
    if (g_fd < 0) return;

    g_running = 0;
    pthread_join(g_thread, NULL);

    close(g_fd);
    g_fd = -1;

    printf("[GPS] exited\n");
}
