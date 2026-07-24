/*
 * storage_manager.c — 统一存储管理
 *
 * 职责：
 *   1. 照片存储 — 自动生成路径，写入 JPEG
 *   2. 录像文件管理 — 分配 AVI 路径，管理循环覆盖
 *   3. 容量限制 — 达到上限 evict 最旧文件
 *   4. 目录创建 — 自动创建 photo/ video/ 子目录
 *
 * 策略：
 *   - 照片路径: <root>/photo/IMG_YYYYMMDD_HHMMSS_<seq>.jpg
 *   - 录像路径: <root>/video/REC_YYYYMMDD_HHMMSS_<seq>.avi
 *   - 超过 capacity_bytes 时，按 mtime 升序删除最旧文件
 *   - 线程安全：当前为单线程设计，后续可加 pthread_mutex
 */

#include "storage_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <ftw.h>

/* ==================== 内部状态 ==================== */
static char      g_root[256]   = {0};
static uint64_t  g_capacity    = 512UL * 1024 * 1024;  /* 默认 512MB */
static uint32_t  g_seq_photo   = 0;
static uint32_t  g_seq_video   = 0;

/* ==================== 内部辅助 ==================== */

/* 确保目录存在，不存在则创建 */
static int ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) return 0;
        errno = ENOTDIR;
        return -1;
    }
    if (mkdir(path, 0755) < 0) {
        if (errno == EEXIST) return 0;
        perror("[STORAGE] mkdir");
        return -1;
    }
    return 0;
}

/* 生成时间戳字符串 YYYYMMDD_HHMMSS */
static void timestamp_str(char *buf, size_t sz) {
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, sz, "%Y%m%d_%H%M%S", &tm);
}

/* 根据文件类型生成路径 */
int storage_alloc_path(storage_file_type_t type, char *out, size_t out_sz) {
    char ts[32];
    timestamp_str(ts, sizeof(ts));

    if (type == STORAGE_TYPE_PHOTO) {
        snprintf(out, out_sz, "%s/photo/IMG_%s_%04u.jpg",
                 g_root, ts, g_seq_photo++);
    } else {
        snprintf(out, out_sz, "%s/video/REC_%s_%04u.avi",
                 g_root, ts, g_seq_video++);
    }
    return 0;
}

/* 获取文件大小 */
static uint64_t file_size(const char *path) {
    struct stat st;
    if (stat(path, &st) < 0) return 0;
    return (uint64_t)st.st_size;
}

/* 获取文件 mtime（秒） */
static time_t file_mtime(const char *path) {
    struct stat st;
    if (stat(path, &st) < 0) return 0;
    return st.st_mtime;
}

/* scandir 过滤：只取 .jpg 或 .avi */
static int filter_media(const struct dirent *d) {
    const char *name = d->d_name;
    if (d->d_type != DT_REG && d->d_type != DT_UNKNOWN) return 0;
    size_t len = strlen(name);
    if (len < 4) return 0;
    const char *ext = name + len - 4;
    return (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".avi") == 0);
}

/* qsort 比较：按 mtime 升序（最旧排前） */
static int cmp_mtime_asc(const void *a, const void *b) {
    const char *pa = *(const char **)a;
    const char *pb = *(const char **)b;
    time_t ta = file_mtime(pa);
    time_t tb = file_mtime(pb);
    if (ta < tb) return -1;
    if (ta > tb) return  1;
    return 0;
}

/* ==================== 存储容量计算 & 循环覆盖 ==================== */

static uint64_t calc_dir_size(const char *dir_path) {
    uint64_t total = 0;
    DIR *dir = opendir(dir_path);
    if (!dir) return 0;

    struct dirent *d;
    char path[512];
    while ((d = readdir(dir)) != NULL) {
        if (d->d_type != DT_REG && d->d_type != DT_UNKNOWN) continue;
        snprintf(path, sizeof(path), "%s/%s", dir_path, d->d_name);
        total += file_size(path);
    }
    closedir(dir);
    return total;
}

/* 删除指定目录下最旧的 N 个媒体文件，直到释放 needed_bytes */
void storage_evict_oldest(storage_file_type_t type, uint64_t needed_bytes) {
    const char *subdir;
    if (type == STORAGE_TYPE_PHOTO)
        subdir = "photo";
    else
        subdir = "video";

    char dir_path[288];
    snprintf(dir_path, sizeof(dir_path), "%s/%s", g_root, subdir);

    struct dirent **entries;
    int n = scandir(dir_path, &entries, filter_media, alphasort);
    if (n <= 0) return;

    /* 拼出完整路径 */
    char **paths = calloc(n, sizeof(char *));
    if (!paths) { free(entries); return; }
    for (int i = 0; i < n; i++) {
        paths[i] = malloc(512);
        if (paths[i])
            snprintf(paths[i], 512, "%s/%s", dir_path, entries[i]->d_name);
    }

    /* 按 mtime 升序排序 */
    qsort(paths, n, sizeof(char *), cmp_mtime_asc);

    /* 从最旧的开始删 */
    uint64_t freed = 0;
    for (int i = 0; i < n; i++) {
        if (freed >= needed_bytes) break;
        if (!paths[i]) continue;
        uint64_t sz = file_size(paths[i]);
        if (remove(paths[i]) == 0) {
            printf("[STORAGE] evicted: %s (%lu bytes)\n", paths[i], (unsigned long)sz);
            freed += sz;
        }
        free(paths[i]);
    }
    free(paths);

    /* scandir 分配的 entries 也要释放 */
    for (int i = 0; i < n; i++)
        free(entries[i]);
    free(entries);
}

/* ==================== 统计查询 ==================== */

int storage_get_stats(storage_stats_t *stats) {
    if (!stats) return -1;
    memset(stats, 0, sizeof(*stats));

    char photo_dir[288], video_dir[288];
    snprintf(photo_dir, sizeof(photo_dir), "%s/photo", g_root);
    snprintf(video_dir, sizeof(video_dir), "%s/video",  g_root);

    struct dirent **entries;

    /* 统计照片 */
    int n = scandir(photo_dir, &entries, filter_media, alphasort);
    if (n >= 0) {
        stats->photo_count = n;
        for (int i = 0; i < n; i++) {
            char path[512];
            snprintf(path, sizeof(path), "%s/%s", photo_dir, entries[i]->d_name);
            stats->total_bytes += file_size(path);
            free(entries[i]);
        }
        free(entries);
    }

    /* 统计录像 */
    n = scandir(video_dir, &entries, filter_media, alphasort);
    if (n >= 0) {
        stats->video_count = n;
        for (int i = 0; i < n; i++) {
            char path[512];
            snprintf(path, sizeof(path), "%s/%s", video_dir, entries[i]->d_name);
            stats->total_bytes += file_size(path);
            free(entries[i]);
        }
        free(entries);
    }

    stats->capacity_bytes = g_capacity;
    return 0;
}

/* ==================== 公共接口 ==================== */

int storage_init(const char *root_path, uint64_t capacity_bytes) {
    if (!root_path || root_path[0] == '\0') return -1;

    strncpy(g_root, root_path, sizeof(g_root) - 1);
    g_root[sizeof(g_root) - 1] = '\0';

    /* 去掉末尾斜杠 */
    size_t len = strlen(g_root);
    while (len > 1 && g_root[len - 1] == '/')
        g_root[--len] = '\0';

    g_capacity    = capacity_bytes;
    g_seq_photo   = 0;
    g_seq_video   = 0;

    /* 创建子目录 */
    char photo_dir[288], video_dir[288];
    snprintf(photo_dir, sizeof(photo_dir), "%s/photo", g_root);
    snprintf(video_dir, sizeof(video_dir), "%s/video", g_root);

    if (ensure_dir(photo_dir) < 0) return -1;
    if (ensure_dir(video_dir)  < 0) return -1;

    printf("[STORAGE] init: root=%s, capacity=%lu bytes\n",
           g_root, (unsigned long)g_capacity);
    return 0;
}

/* 保存照片 — 自动生成路径 */
int storage_save_photo(const void *data, size_t len) {
    char path[512];
    if (storage_alloc_path(STORAGE_TYPE_PHOTO, path, sizeof(path)) < 0)
        return -1;

    return storage_save_photo_with_path(path, data, len);
}

/* 保存照片 — 指定路径（也可被外部直接调用） */
int storage_save_photo_with_path(const char *custom_path, const void *data, size_t len) {
    int fd;

    /* 检查容量，需要时 evict */
    storage_check_and_evict();
    fd = open(custom_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("[STORAGE] open photo");
        return -1;
    }

    ssize_t written = 0;
    const unsigned char *p = (const unsigned char *)data;
    while ((size_t)written < len) {
        ssize_t ret = write(fd, p + written, len - written);
        if (ret < 0) { perror("[STORAGE] write photo"); close(fd); return -1; }
        written += ret;
    }

    close(fd);
    printf("[STORAGE] photo saved: %s (%zu bytes)\n", custom_path, len);
    return 0;
}


/* ==================== 统一容量检查与混合删除 ==================== */
int storage_check_and_evict(void) {
    storage_stats_t stats;
    if (storage_get_stats(&stats) < 0) return -1;

    if (stats.total_bytes > stats.capacity_bytes) {
        uint64_t over = stats.total_bytes - stats.capacity_bytes;
        uint64_t to_free = over + stats.capacity_bytes / 5;
        printf("[STORAGE] capacity exceeded by %lu bytes, evicting %lu\n",
               (unsigned long)over, (unsigned long)to_free);
        storage_evict_oldest(STORAGE_TYPE_VIDEO, to_free);	//只释放video
    }
    return 0;
}

void storage_exit(void) {
    /* 当前无动态资源需要释放 */
    printf("[STORAGE] exit\n");
}
