import sys
with open("sources/app/modules/storage_manager/storage_manager.c", "r", encoding="utf-8") as f:
    c = f.read()

old_func_start = "int storage_check_and_evict(void) {"
old_func_end = "void storage_exit(void)"
idx_start = c.find(old_func_start)
idx_end = c.find(old_func_end, idx_start)

new_func = """int storage_check_and_evict(void) {
    storage_stats_t stats;
    if (storage_get_stats(&stats) < 0) return -1;

    if (stats.total_bytes > stats.capacity_bytes) {
        uint64_t over = stats.total_bytes - stats.capacity_bytes;
        uint64_t to_free = over + stats.capacity_bytes / 5;
        printf("[STORAGE] capacity exceeded by %lu bytes, evicting %lu\n",
               (unsigned long)over, (unsigned long)to_free);
        storage_evict_oldest(STORAGE_TYPE_PHOTO, to_free / 2);
        storage_evict_oldest(STORAGE_TYPE_VIDEO, to_free / 2);
    }
    return 0;
}

"""

c = c[:idx_start] + new_func + c[idx_end:]

with open("sources/app/modules/storage_manager/storage_manager.c", "w", encoding="utf-8") as f:
    f.write(c)
print("fixed")
