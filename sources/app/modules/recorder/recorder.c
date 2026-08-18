/*
 * recorder.c -- AVI MJPEG 录像模块（异步写入）
 *
 * AVI 文件结构：
 *   RIFF(''AVI '')
 *      +-- LIST(''hdrl'')
 *      |   +-- avih (mainAVIHeader, 56 bytes)
 *      |   +-- LIST(''strl'')
 *      |       +-- strh (stream header, 64 bytes)
 *      |       +-- strf (BITMAPINFOHEADER, 40 bytes)
 *      +-- LIST(''movi'')
 *      |   +-- 00dc chunk x N (每帧一个)
 *      +-- idx1 (索引表)
 *
 * 策略：先写占位头，录完再回写修正总帧数和 idx1。
 *
 * 重要：idx1 偏移量在写入队列时由生产者预先计算
 * （g_next_write_offset 跟踪预期写入位置），
 * 确保即使写入是异步的，索引也是正确的。
 */

#include "recorder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <pthread.h>
#include "storage_manager.h"

/* ==================== 常量 ==================== */
#define AVI_HEADER_SIZE     56   /* mainAVIHeader */
#define STREAM_HEADER_SIZE  64   /* AVISTREAMHEADER */
#define FORMAT_SIZE         40   /* BITMAPINFOHEADER */
#define CHUNK_HEADER_SIZE   8    /* FOURCC + size */
#define IDX1_ENTRY_SIZE     16   /* 每个索引项 */

/* 录制帧环形队列大小 */
#define RECORD_QUEUE_SIZE   8

/* ==================== 录制帧队列 ==================== */
/* 在预览工作线程和 SD 卡写入线程之间解耦。
 * 队列条目中 precomputed_offset 由生产者预先计算，
 * 保证 idx1 偏移在异步写入场景中正确。 */
typedef struct {
    void    *data;                /* malloc 拷贝的 MJPEG 帧 */
    size_t   length;
    uint32_t precomputed_offset;  /* 预计算的在文件中相对 movi 的数据偏移 */
} record_queue_entry_t;

static record_queue_entry_t  g_record_queue[RECORD_QUEUE_SIZE];
static int                   g_record_head = 0;
static int                   g_record_tail = 0;
static pthread_mutex_t       g_record_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t        g_record_cond  = PTHREAD_COND_INITIALIZER;
static pthread_t             g_record_thread;
static volatile int          g_record_thread_running = 0;

/* 预计算的下一个写入位置（相对 movi，含 chunk header） */
static uint32_t              g_next_write_offset = 0;

/* ==================== 内部状态 ==================== */
static int          g_fd        = -1;
static int          g_frame_w   = 640;
static int          g_frame_h   = 480;
static int          g_fps       = 15;
static int          g_total_frames = 0;
static int          g_evict_counter = 0;
static volatile int g_recording = 0;

/* idx1 索引表缓冲区 */
static struct {
    uint32_t offset;    /* 从''movi''开始到 chunk data 的偏移 */
    uint32_t size;      /* chunk 数据大小 */
} *g_idx_entries = NULL;
static int g_idx_capacity = 0;

/* ''movi'' 在文件中的偏移（用于 idx1 计算） */
static off_t g_movi_offset = 0;

/* ==================== AVI 结构体定义 ==================== */

/* RIFF chunk */
typedef struct {
    uint32_t id;         /* FOURCC */
    uint32_t size;       /* 数据大小 */
} __attribute__((packed)) riff_chunk_t;

/* mainAVIHeader */
typedef struct {
    uint32_t us_per_frame;       /* 微秒/帧 */
    uint32_t max_bytes_per_sec;
    uint32_t padding_granularity;
    uint32_t flags;              /* AVIF_HASINDEX = 0x10 */
    uint32_t total_frames;
    uint32_t initial_frames;
    uint32_t streams;
    uint32_t suggested_buf_size;
    uint32_t width;
    uint32_t height;
    uint32_t reserved[4];
} __attribute__((packed)) avi_header_t;

/* AVISTREAMHEADER */
typedef struct {
    uint32_t fourcc_type;        /* vids */
    uint32_t fourcc_handler;     /* MJPG */
    uint32_t flags;
    uint16_t priority;
    uint16_t language;
    uint32_t initial_frames;
    uint32_t scale;              /* 1 */
    uint32_t rate;               /* fps */
    uint32_t start;
    uint32_t length;             /* total_frames */
    uint32_t suggested_buf_size;
    uint32_t quality;
    uint32_t sample_size;
    struct {
        int16_t left;
        int16_t top;
        int16_t right;
        int16_t bottom;
    } rc_frame;
} __attribute__((packed)) stream_header_t;

/* BITMAPINFOHEADER */
typedef struct {
    uint32_t size;
    uint32_t width;
    uint32_t height;
    uint16_t planes;
    uint16_t bit_count;
    uint32_t compression;        /* MJPG FOURCC */
    uint32_t size_image;
    uint32_t x_pels_per_meter;
    uint32_t y_pels_per_meter;
    uint32_t clr_used;
    uint32_t clr_important;
} __attribute__((packed)) bitmap_info_t;

/* idx1 条目 */
typedef struct {
    uint32_t chunk_id;           /* ''00dc'' */
    uint32_t flags;              /* 0x10 = AVIIF_KEYFRAME */
    uint32_t offset;             /* ''movi'' -> chunk data 的偏移 */
    uint32_t size;               /* chunk 数据大小（不含 8 字节头） */
} __attribute__((packed)) idx1_entry_t;

/* ==================== 辅助函数 ==================== */
static uint32_t fourcc(const char *s) {
    return (uint32_t)s[0] | ((uint32_t)s[1] << 8)
         | ((uint32_t)s[2] << 16) | ((uint32_t)s[3] << 24);
}

static int write_all(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    while (len > 0) {
        ssize_t ret = write(fd, p, len);
        if (ret < 0) return -1;
        p   += ret;
        len -= ret;
    }
    return 0;
}

/* 对齐到 2 字节边界（AVI 要求） */
static size_t padded_size(size_t len) {
    return (len + 1) & ~1;
}

/* 写入 RIFF chunk header + data，自动填充对齐 */
static int write_chunk(int fd, uint32_t id, const void *data, size_t len) {
    riff_chunk_t hdr;
    hdr.id   = id;
    hdr.size = len;
    if (write_all(fd, &hdr, sizeof(hdr)) < 0) return -1;
    if (write_all(fd, data, len) < 0) return -1;
    if (len & 1) {
        char pad = 0;
        if (write(fd, &pad, 1) < 0) return -1;
    }
    return 0;
}

/* 写入 AVI 头部 */
/* 修复：hdrl_size 应该是"hdrl"(4) + avih块(8+56=64) + strl列表块(8头 + "strl"(4) + strh块(8+64) + strf块(8+40) = 132)
*  共200
*/
static int write_avi_header(int fd) {
    riff_chunk_t riff;
    riff.id   = fourcc("RIFF");
    riff.size = 0;
    if (write_all(fd, &riff, sizeof(riff)) < 0) return -1;
    if (write_all(fd, "AVI ", 4) < 0) return -1;

    if (write_all(fd, "LIST", 4) < 0) return -1;
    uint32_t hdrl_size = 4 + 8 + AVI_HEADER_SIZE + 8 + 4 + 8 + STREAM_HEADER_SIZE + 8 + FORMAT_SIZE; //共200字节
    if (write_all(fd, &hdrl_size, 4) < 0) return -1;
    if (write_all(fd, "hdrl", 4) < 0) return -1;

    avi_header_t avih;
    memset(&avih, 0, sizeof(avih));
    avih.us_per_frame       = 1000000 / g_fps;
    avih.max_bytes_per_sec  = g_frame_w * g_frame_h * 2 * g_fps;
    avih.padding_granularity = 0;
    avih.flags              = 0x10;
    avih.total_frames       = 0;
    avih.initial_frames     = 0;
    avih.streams            = 1;
    avih.suggested_buf_size = g_frame_w * g_frame_h * 2;
    avih.width              = g_frame_w;
    avih.height             = g_frame_h;
    write_chunk(fd, fourcc("avih"), &avih, sizeof(avih));

    if (write_all(fd, "LIST", 4) < 0) return -1;
    uint32_t strl_size = 4 + 8 + STREAM_HEADER_SIZE + 8 + FORMAT_SIZE;
    if (write_all(fd, &strl_size, 4) < 0) return -1;
    if (write_all(fd, "strl", 4) < 0) return -1;

    stream_header_t strh;
    memset(&strh, 0, sizeof(strh));
    strh.fourcc_type    = fourcc("vids");
    strh.fourcc_handler = fourcc("MJPG");
    strh.flags          = 0;
    strh.priority       = 0;
    strh.language       = 0;
    strh.initial_frames = 0;
    strh.scale          = 1;
    strh.rate           = g_fps;
    strh.start          = 0;
    strh.length         = 0;
    strh.suggested_buf_size = g_frame_w * g_frame_h * 2;
    strh.quality        = 0;
    strh.sample_size    = 0;
    strh.rc_frame.left   = 0;
    strh.rc_frame.top    = 0;
    strh.rc_frame.right  = g_frame_w;
    strh.rc_frame.bottom = g_frame_h;
    write_chunk(fd, fourcc("strh"), &strh, sizeof(strh));

    bitmap_info_t bmp;
    memset(&bmp, 0, sizeof(bmp));
    bmp.size          = FORMAT_SIZE;
    bmp.width         = g_frame_w;
    bmp.height        = g_frame_h;
    bmp.planes        = 1;
    bmp.bit_count     = 24;
    bmp.compression   = fourcc("MJPG");
    bmp.size_image    = g_frame_w * g_frame_h * 3;
    bmp.x_pels_per_meter = 0;
    bmp.y_pels_per_meter = 0;
    bmp.clr_used      = 0;
    bmp.clr_important = 0;
    write_chunk(fd, fourcc("strf"), &bmp, sizeof(bmp));

    g_movi_offset = lseek(fd, 0, SEEK_CUR);
    if (write_all(fd, "LIST", 4) < 0) return -1;
    uint32_t movi_size_ph = 0;
    if (write_all(fd, &movi_size_ph, 4) < 0) return -1;
    if (write_all(fd, "movi", 4) < 0) return -1;

    return 0;
}

/* 回写 avih.total_frames + strh.length + RIFF size */
static int write_finalize(int fd, int total_frames) {
    off_t cur = lseek(fd, 0, SEEK_CUR);
    uint32_t v;

    v = (uint32_t)total_frames;
    lseek(fd, 48, SEEK_SET);
    write_all(fd, &v, 4);

    v = (uint32_t)total_frames;
    lseek(fd, 140, SEEK_SET);
    write_all(fd, &v, 4);

    uint32_t file_size = (uint32_t)cur - 8;
    lseek(fd, 4, SEEK_SET);
    write_all(fd, &file_size, 4);

    lseek(fd, cur, SEEK_SET);
    return 0;
}

/* 写入 idx1 索引表 */
static int write_index(int fd) {
    riff_chunk_t idx1_hdr;
    int i;

    idx1_hdr.id   = fourcc("idx1");
    idx1_hdr.size = g_total_frames * IDX1_ENTRY_SIZE;
    write_all(fd, &idx1_hdr, sizeof(idx1_hdr));

    for (i = 0; i < g_total_frames; i++) {
        idx1_entry_t entry;
        entry.chunk_id = fourcc("00dc");
        entry.flags    = 0x10;
        entry.offset   = g_idx_entries[i].offset;
        entry.size     = g_idx_entries[i].size;
        write_all(fd, &entry, sizeof(entry));
    }

    return 0;
}

/* ==================== 录制线程 ==================== */
static void *record_thread_func(void *arg) {
    (void)arg;

    while (g_record_thread_running) {
        pthread_mutex_lock(&g_record_mutex);	//上锁，访问

        while (g_record_head == g_record_tail && g_record_thread_running)	//临界资源，被保护
            pthread_cond_wait(&g_record_cond, &g_record_mutex);

        if (!g_record_thread_running) {
            pthread_mutex_unlock(&g_record_mutex);
            break;
        }

        record_queue_entry_t *entry = &g_record_queue[g_record_tail];	//拿到队列，出队写数据到SD就行。
        void  *frame_data = entry->data;
        size_t frame_len  = entry->length;
        entry->data   = NULL;
        entry->length = 0;
        g_record_tail = (g_record_tail + 1) % RECORD_QUEUE_SIZE;	//环形

        pthread_mutex_unlock(&g_record_mutex);	//用完释放锁

        if (!frame_data) continue;

        /* ---- 录制线程：实际 write() 到 SD 卡 ---- */
        riff_chunk_t chunk;
        chunk.id   = fourcc("00dc");
        chunk.size = frame_len;
        write_all(g_fd, &chunk, sizeof(chunk));		//g_fd是recorder_start直接打开的“filename”,也就是SD卡路径
        write_all(g_fd, frame_data, frame_len);
        if (frame_len & 1) {
            char pad = 0;
            write(g_fd, &pad, 1);
        }

        free(frame_data);
    }

    /* 退出前清理g_record_queue残留帧 */
    pthread_mutex_lock(&g_record_mutex);
    while (g_record_tail != g_record_head) {
        free(g_record_queue[g_record_tail].data);
        g_record_queue[g_record_tail].data = NULL;
        g_record_tail = (g_record_tail + 1) % RECORD_QUEUE_SIZE;//环形
    }
    pthread_mutex_unlock(&g_record_mutex);

    return NULL;
}

/* ==================== 公共接口 ==================== */

int recorder_init(int width, int height, int fps) {
    if (g_recording) return -1;
    g_frame_w = width;
    g_frame_h = height;
    g_fps     = fps;
    return 0;
}

/*
*1、写AVI头
*2、启动录制线程record_thread_func，写AVI数据帧
*/
int recorder_start(const char *filename) {
    if (g_recording || g_fd >= 0) return -1;

    g_fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);	//g_fd是打开的“filename”也就是SD卡路径
    if (g_fd < 0) { perror("[REC] open"); return -1; }

    g_idx_capacity = 1024;
    g_idx_entries = calloc(g_idx_capacity, sizeof(g_idx_entries[0]));
    if (!g_idx_entries) { close(g_fd); g_fd = -1; return -1; }

    g_total_frames = 0;
    g_evict_counter = 0;
    g_record_head = 0;
    g_record_tail = 0;
    g_next_write_offset = 0;  /* movi 数据区刚起始 */

    if (write_avi_header(g_fd) < 0) {
        perror("[REC] write header");
        free(g_idx_entries); g_idx_entries = NULL;
        close(g_fd); g_fd = -1;
        return -1;
    }
	//启动录制线程
    g_record_thread_running = 1;
    if (pthread_create(&g_record_thread, NULL, record_thread_func, NULL) != 0) {
        perror("[REC] record thread create");
        g_record_thread_running = 0;
        free(g_idx_entries); g_idx_entries = NULL;
        close(g_fd); g_fd = -1;
        return -1;
    }

    g_recording = 1;
    printf("[REC] started: %s %dx%d @%dfps\n", filename, g_frame_w, g_frame_h, g_fps);
    return 0;
}

/*
 * recorder_write_frame -- 在预览工作线程中调用
 *
 * 不做实际 write()，只做：
 *   1. 计算 idx1 偏移（用 g_next_write_offset 跟踪，保护在互斥锁内）
 *   2. 不再写数据，而是数据入recorder队，让录制线程能异步写入 SD 卡
 */
int recorder_write_frame(const void *data, size_t len) {
    if (!g_recording || g_fd < 0) return -1;

    if (++g_evict_counter >= 30) {
        g_evict_counter = 0;
        extern int storage_check_and_evict(void);
        storage_check_and_evict();
    }

    /* 扩展 idx 缓冲区 */
    if (g_total_frames >= g_idx_capacity) {
        g_idx_capacity *= 2;
        g_idx_entries = realloc(g_idx_entries,
                                g_idx_capacity * sizeof(g_idx_entries[0]));
        if (!g_idx_entries) return -1;
    }

    pthread_mutex_lock(&g_record_mutex);

    int next = (g_record_head + 1) % RECORD_QUEUE_SIZE;
    if (next == g_record_tail) {
        /* 队列满了，丢帧 */
        pthread_mutex_unlock(&g_record_mutex);
        return -1;
    }

    /*
     * 预计算 idx1 偏移：
     * g_next_write_offset 跟踪的是相对 movi 起始位置，
     * 包括即将写入的 chunk header (8字节)。
     * 索引中记录的是 chunk data（不含 header）的偏移。
     */
    uint32_t data_offset = g_next_write_offset + CHUNK_HEADER_SIZE;
    uint32_t total_chunk_size = CHUNK_HEADER_SIZE + (uint32_t)padded_size(len);
    g_next_write_offset += total_chunk_size;

    record_queue_entry_t *entry = &g_record_queue[g_record_head];	// 入队g_record_queue，录制线程出队用！
    entry->data = malloc(len);
    if (entry->data) {
        memcpy(entry->data, data, len);
        entry->length = len;
        entry->precomputed_offset = data_offset;
        g_record_head = next;
    }

    /* 记录 idx1 条目 */
    g_idx_entries[g_total_frames].offset = data_offset;
    g_idx_entries[g_total_frames].size   = len;
    g_total_frames++;

    pthread_cond_signal(&g_record_cond);
    pthread_mutex_unlock(&g_record_mutex);

    return 0;
}

/* 
* 修复：
* movi 数据 = "movi"(4) + 所有帧
* 所以movi_size应该 = cur - g_movi_offset - 8（减掉 LIST 头 4 字节 + size 字段 4 字节）
*/
int recorder_stop(void) {
    if (!g_recording || g_fd < 0) return -1;
    g_recording = 0;

    /* 等待录制线程处理完队列 */
    if (g_record_thread_running) {
        pthread_mutex_lock(&g_record_mutex);
        g_record_thread_running = 0;
        pthread_cond_signal(&g_record_cond);
        pthread_mutex_unlock(&g_record_mutex);
        pthread_join(g_record_thread, NULL);
    }

    off_t cur = lseek(g_fd, 0, SEEK_CUR);

    uint32_t movi_size = (uint32_t)(cur - g_movi_offset - 8);
    /* 修复：movi 块布局是 "LIST"(4) + size(4) + "movi"(4) + 帧数据，
     * size 字段在 g_movi_offset+4 处；原来写到 g_movi_offset 会覆盖 "LIST"，
     * 导致解析器把下一处 "00dc" 误读成 movi 大小（1667510320）。 */
    lseek(g_fd, g_movi_offset + 4, SEEK_SET);
    write_all(g_fd, &movi_size, 4);
    lseek(g_fd, cur, SEEK_SET);

    write_index(g_fd);
    write_finalize(g_fd, g_total_frames);

    close(g_fd);
    g_fd = -1;

    free(g_idx_entries);
    g_idx_entries = NULL;
    g_idx_capacity = 0;

    printf("[REC] stopped: %d frames written\n", g_total_frames);
    return 0;
}

void recorder_exit(void) {
    if (g_recording) recorder_stop();
    if (g_fd >= 0) { close(g_fd); g_fd = -1; }
    free(g_idx_entries);
    g_idx_entries = NULL;
}

recorder_state_t recorder_get_state(void) {
    return g_recording ? RECORDER_RECORDING : RECORDER_IDLE;
}
