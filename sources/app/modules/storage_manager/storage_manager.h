/*
 * storage_manager.h — 统一存储管理
 *
 * 职责：
 *   1. 照片存储：capture_take_photo → storage_save_photo()
 *   2. 录像文件管理：recorder 创建文件前先调 storage_alloc_video()
 *   3. 循环覆盖：达到容量上限后自动删除最旧文件
 *   4. 目录创建：自动创建 photo/ video/ 子目录
 *
 * 使用方式：
 *   storage_init("/mnt/sd", 512 * 1024 * 1024);  // 根路径 + 512MB 上限
 *   char path[256];
 *   storage_alloc_path(STORAGE_TYPE_PHOTO, path, sizeof(path));
 *   // → "/mnt/sd/photo/IMG_20260718_120000.jpg"
 *   storage_save_photo(data, len);
 *   // 或
 *   storage_alloc_path(STORAGE_TYPE_VIDEO, path, sizeof(path));
 *   recorder_start(path);
 *   storage_stats(&stats);  // 查询使用情况
 *   storage_exit();
 */

#ifndef _STORAGE_MANAGER_H
#define _STORAGE_MANAGER_H

#include <stddef.h>
#include <stdint.h>

/* 文件类型 */
typedef enum {
    STORAGE_TYPE_PHOTO,   /* JPEG 照片 */
    STORAGE_TYPE_VIDEO,   /* AVI 录像 */
} storage_file_type_t;

/* 存储统计 */
typedef struct {
    uint32_t photo_count;          /* 当前照片总数 */
    uint32_t video_count;          /* 当前录像文件数 */
    uint64_t total_bytes;          /* 已用空间（字节） */
    uint64_t capacity_bytes;       /* 存储上限（字节） */
} storage_stats_t;

/*
 * 扫描 photo 目录，返回按文件名排序的完整路径列表
 * out_paths: 输出路径数组（需调用 storage_free_photo_list 释放）
 * out_count: 输出照片数量
 * 返回 0 成功，-1 失败
 */
int  storage_list_photos(char ***out_paths, int *out_count);

/*
 * 释放 storage_list_photos() 返回的路径列表
 */
void storage_free_photo_list(char **paths, int count);

/*
 * 删除指定路径的照片文件
 * path: 照片完整路径
 * 返回 0 成功，-1 失败
 */
int  storage_delete_photo(const char *path);

int  storage_init(const char *root_path, uint64_t capacity_bytes);
int  storage_alloc_path(storage_file_type_t type, char *out, size_t out_sz);
int  storage_save_photo(const void *data, size_t len);
int  storage_save_photo_with_path(const char *custom_path, const void *data, size_t len);
void storage_evict_oldest(storage_file_type_t type, uint64_t needed_bytes);
int  storage_get_stats(storage_stats_t *stats);
int  storage_check_and_evict(void);
void storage_exit(void);

#endif /* _STORAGE_MANAGER_H */
