/*
 * recorder.c — AVI MJPEG 录像模块
 *
 * AVI 文件结构：
 *   RIFF('AVI ')
 *     ├── LIST('hdrl')
 *     │   ├── avih (mainAVIHeader, 56 bytes)
 *     │   └── LIST('strl')
 *     │       ├── strh (stream header, 64 bytes)
 *     │       └── strf (BITMAPINFOHEADER, 40 bytes)
 *     ├── LIST('movi')
 *     │   └── 00dc chunk x N (每帧一个)
 *     └── idx1 (索引表)
 *
 * 策略：先写占位头，录完再回写修正总帧数和 idx1。
 */

#include "recorder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

/* ==================== 常数 ==================== */
#define AVI_HEADER_SIZE     56   /* mainAVIHeader */
#define STREAM_HEADER_SIZE  64   /* AVISTREAMHEADER */
#define FORMAT_SIZE         40   /* BITMAPINFOHEADER */
#define CHUNK_HEADER_SIZE   8    /* FOURCC + size */
#define IDX1_ENTRY_SIZE     16   /* 每个索引项 */

/* ==================== 内部状态 ==================== */
static int          g_fd        = -1;
static int          g_frame_w   = 640;
static int          g_frame_h   = 480;
static int          g_fps       = 15;
static int          g_total_frames = 0;
static volatile int g_recording = 0;

/* idx1 索引表缓冲区 */
static struct {
    uint32_t offset;    /* 从 'movi' 开始到 chunk 的偏移 */
    uint32_t size;      /* chunk 数据大小 */
} *g_idx_entries = NULL;
static int g_idx_capacity = 0;

/* 'movi' 在文件中的偏移（用于 idx1 计算） */
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
    uint32_t chunk_id;           /* '00dc' */
    uint32_t flags;              /* 0x10 = AVIIF_KEYFRAME */
    uint32_t offset;             /* 'movi' → chunk data 的偏移 */
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
    if (len > 0 && write_all(fd, data, len) < 0) return -1;
    /* padding byte */
    if (len & 1) {
        char pad = 0;
        if (write(fd, &pad, 1) < 0) return -1;
    }
    return 0;
}

/* ==================== AVI 头写入 ==================== */
static int write_avi_header(int fd) {
    riff_chunk_t riff, list_hdrl, list_strl;
    avi_header_t avih;
    stream_header_t strh;
    bitmap_info_t strf;
    off_t riff_size_pos, movi_pos;

    /* ===== RIFF('AVI ') ===== */
    /* 先写 RIFF header，size 占位，最后回写 */
    riff.id = fourcc("RIFF");
    riff.size = 0;  /* 占位 */
    if (write_all(fd, &riff, sizeof(riff)) < 0) return -1;
    riff_size_pos = lseek(fd, 0, SEEK_CUR) - 4;  /* 记录 size 位置 */

    if (write_all(fd, "AVI ", 4) < 0) return -1;

    /* ===== LIST('hdrl') ===== */
    list_hdrl.id   = fourcc("LIST");
    list_hdrl.size = 0;  /* 占位 */
    if (write_all(fd, &list_hdrl, sizeof(list_hdrl)) < 0) return -1;
    /* 记录 hdrl size 位置 */
    off_t hdrl_size_pos = lseek(fd, 0, SEEK_CUR) - 4;

    if (write_all(fd, "hdrl", 4) < 0) return -1;

    /* avih */
    memset(&avih, 0, sizeof(avih));
    avih.us_per_frame      = 1000000 / g_fps;
    avih.max_bytes_per_sec = g_frame_w * g_frame_h * 2 * g_fps;
    avih.flags             = 0x10;  /* AVIF_HASINDEX */
    avih.total_frames      = 0;     /* 占位，录完回写 */
    avih.streams           = 1;
    avih.suggested_buf_size = g_frame_w * g_frame_h * 2;
    avih.width             = g_frame_w;
    avih.height            = g_frame_h;
    if (write_chunk(fd, fourcc("avih"), &avih, sizeof(avih)) < 0) return -1;

    /* ===== LIST('strl') ===== */
    list_strl.id   = fourcc("LIST");
    list_strl.size = 0;  /* 占位 */
    if (write_all(fd, &list_strl, sizeof(list_strl)) < 0) return -1;
    off_t strl_size_pos = lseek(fd, 0, SEEK_CUR) - 4;

    if (write_all(fd, "strl", 4) < 0) return -1;

    /* strh */
    memset(&strh, 0, sizeof(strh));
    strh.fourcc_type    = fourcc("vids");
    strh.fourcc_handler = fourcc("MJPG");
    strh.scale          = 1;
    strh.rate           = g_fps;
    strh.length         = 0;  /* 占位 */
    strh.suggested_buf_size = g_frame_w * g_frame_h * 2;
    strh.rc_frame.right  = g_frame_w;
    strh.rc_frame.bottom = g_frame_h;
    if (write_chunk(fd, fourcc("strh"), &strh, sizeof(strh)) < 0) return -1;

    /* strf (BITMAPINFOHEADER) */
    memset(&strf, 0, sizeof(strf));
    strf.size        = sizeof(strf);
    strf.width       = g_frame_w;
    strf.height      = g_frame_h;
    strf.planes      = 1;
    strf.bit_count   = 24;
    strf.compression = fourcc("MJPG");
    strf.size_image  = g_frame_w * g_frame_h * 2;
    if (write_chunk(fd, fourcc("strf"), &strf, sizeof(strf)) < 0) return -1;

    /* 回写 strl size */
    off_t cur = lseek(fd, 0, SEEK_CUR);
    uint32_t strl_size = (uint32_t)(cur - strl_size_pos - 4);
    lseek(fd, strl_size_pos, SEEK_SET);
    write_all(fd, &strl_size, 4);
    lseek(fd, cur, SEEK_SET);

    /* 回写 hdrl size */
    uint32_t hdrl_size = (uint32_t)(cur - hdrl_size_pos - 4);
    lseek(fd, hdrl_size_pos, SEEK_SET);
    write_all(fd, &hdrl_size, 4);
    lseek(fd, cur, SEEK_SET);

    /* ===== LIST('movi') ===== */
    riff_chunk_t movi_list;
    movi_list.id   = fourcc("LIST");
    movi_list.size = 0;  /* 占位 */
    if (write_all(fd, &movi_list, sizeof(movi_list)) < 0) return -1;
    g_movi_offset = lseek(fd, 0, SEEK_CUR) - 4;  /* 记录 movi LIST 的 size 位置 */

    if (write_all(fd, "movi", 4) < 0) return -1;

    return 0;
}

/* 回写 avih.total_frames、strh.length、RIFF size */
static int write_finalize(int fd, int total_frames) {
    uint32_t v;
    off_t cur = lseek(fd, 0, SEEK_CUR);

    /* 回写 avih.total_frames（文件偏移 48） */
    v = (uint32_t)total_frames;
    lseek(fd, 48, SEEK_SET);
    write_all(fd, &v, 4);

    /* 回写 strh.length（文件偏移 140） */
    v = (uint32_t)total_frames;
    lseek(fd, 140, SEEK_SET);
    write_all(fd, &v, 4);

    /* RIFF size = 文件总大小 - 8 */
    uint32_t file_size = (uint32_t)cur - 8;
    lseek(fd, 4, SEEK_SET);
    write_all(fd, &file_size, 4);

    lseek(fd, cur, SEEK_SET);
    return 0;
}

/* 写入 idx1 索引表 */
static int write_index(int fd) {
    off_t cur = lseek(fd, 0, SEEK_CUR);
    riff_chunk_t idx1_hdr;
    int i;

    idx1_hdr.id   = fourcc("idx1");
    idx1_hdr.size = g_total_frames * IDX1_ENTRY_SIZE;
    write_all(fd, &idx1_hdr, sizeof(idx1_hdr));

    for (i = 0; i < g_total_frames; i++) {
        idx1_entry_t entry;
        entry.chunk_id = fourcc("00dc");
        entry.flags    = 0x10;  /* AVIIF_KEYFRAME (MJPEG 每帧都是关键帧) */
        entry.offset   = g_idx_entries[i].offset;
        entry.size     = g_idx_entries[i].size;
        write_all(fd, &entry, sizeof(entry));
    }

    return 0;
}

/* ==================== 公共接口 ==================== */

int recorder_init(int width, int height, int fps) {
    if (g_recording) return -1;
    g_frame_w = width;
    g_frame_h = height;
    g_fps     = fps;
    return 0;
}

int recorder_start(const char *filename) {
    if (g_recording || g_fd >= 0) return -1;

    g_fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (g_fd < 0) { perror("[REC] open"); return -1; }

    /* 分配 idx1 索引缓冲区（初始 1024 帧） */
    g_idx_capacity = 1024;
    g_idx_entries = calloc(g_idx_capacity, sizeof(g_idx_entries[0]));
    if (!g_idx_entries) { close(g_fd); g_fd = -1; return -1; }

    g_total_frames = 0;

    if (write_avi_header(g_fd) < 0) {
        perror("[REC] write header");
        free(g_idx_entries); g_idx_entries = NULL;
        close(g_fd); g_fd = -1;
        return -1;
    }

    g_recording = 1;
    printf("[REC] started: %s %dx%d @%dfps\n", filename, g_frame_w, g_frame_h, g_fps);
    return 0;
}

int recorder_write_frame(const void *data, size_t len) {
    riff_chunk_t chunk;
    off_t chunk_data_offset;

    if (!g_recording || g_fd < 0) return -1;

    /* 扩展 idx 缓冲区 */
    if (g_total_frames >= g_idx_capacity) {
        g_idx_capacity *= 2;
        g_idx_entries = realloc(g_idx_entries,
                                g_idx_capacity * sizeof(g_idx_entries[0]));
        if (!g_idx_entries) return -1;
    }

    /* 'movi' 开始到这次 chunk 数据开头的偏移 */
    chunk_data_offset = (off_t)(lseek(g_fd, 0, SEEK_CUR) - g_movi_offset - 4);

    /* 写 chunk header：'00dc' + 数据大小 */
    chunk.id   = fourcc("00dc");
    chunk.size = len;
    if (write_all(g_fd, &chunk, sizeof(chunk)) < 0) return -1;

    /* 写帧数据 */
    if (write_all(g_fd, data, len) < 0) return -1;

    /* 对齐到 2 字节 */
    if (len & 1) {
        char pad = 0;
        if (write(g_fd, &pad, 1) < 0) return -1;
    }

    /* 记录索引条目 */
    g_idx_entries[g_total_frames].offset = chunk_data_offset + CHUNK_HEADER_SIZE;
    g_idx_entries[g_total_frames].size   = len;
    g_total_frames++;

    return 0;
}

int recorder_stop(void) {
    if (!g_recording || g_fd < 0) return -1;
    g_recording = 0;

    off_t cur = lseek(g_fd, 0, SEEK_CUR);

    /* 回写 movi LIST size */
    uint32_t movi_size = (uint32_t)(cur - g_movi_offset - 4);
    lseek(g_fd, g_movi_offset, SEEK_SET);
    write_all(g_fd, &movi_size, 4);
    lseek(g_fd, cur, SEEK_SET);

    /* 写入 idx1 */
    write_index(g_fd);

    /* 回写 avih total_frames + strh length + RIFF size */
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
