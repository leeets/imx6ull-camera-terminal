import sys
# Read full storage_manager.c
with open("sources/app/modules/storage_manager/storage_manager.c", "r", encoding="utf-8") as f:
    content = f.read()

# 1. Add storage_check_and_evict implementation before storage_exit
exit_idx = content.find("void storage_exit(void)")

new_func = '''
/* ==================== \u7edf\u4e00\u5bb9\u91cf\u68c0\u67e5\u4e0e\u6df7\u5408\u5220\u9664 ==================== */
int storage_check_and_evict(void) {
    storage_stats_t stats;
    if (storage_get_stats(&stats) < 0) return -1;
    
    if (stats.total_bytes > stats.capacity_bytes) {
        uint64_t over = stats.total_bytes - stats.capacity_bytes;
        /* \u591a\u5220 20% \u907f\u514d\u9891\u7e41\u89e6\u53d1 */
        uint64_t to_free = over + stats.capacity_bytes / 5;
        printf("[STORAGE] capacity exceeded by %lu bytes, evicting %lu\\n",
               (unsigned long)over, (unsigned long)to_free);
        storage_evict_oldest(STORAGE_TYPE_PHOTO, to_free / 2);
        storage_evict_oldest(STORAGE_TYPE_VIDEO, to_free / 2);
    }
    return 0;
}

'''

content = content[:exit_idx] + new_func + content[exit_idx:]

# 2. Replace evict logic in storage_save_photo_with_path
old_block = "    /* \u68c0\u67e5\u5bb9\u91cf\uff0c\u9700\u8981\u65f6 evict */"
new_block = old_block + "\n    storage_check_and_evict();"
# Remove the old detailed evict lines
content = content.replace(
    "    /* \u68c0\u67e5\u5bb9\u91cf\uff0c\u9700\u8981\u65f6 evict */",
    "    /* \u68c0\u67e5\u5bb9\u91cf\uff0c\u9700\u8981\u65f6 evict */\n    storage_check_and_evict();"
)

# Remove the old verbose evict block (between the check comment and the fd = open line)
# Find the pattern: after the comment, remove lines until "fd = open"
lines = content.split("\n")
new_lines = []
skip_until_open = False
for l in lines:
    if skip_until_open:
        if "fd = open" in l:
            skip_until_open = False
            new_lines.append(l)
        continue
    if l.strip().startswith("storage_stats_t stats") or l.strip().startswith("if (stats.total_bytes"):
        continue
    if l.strip().startswith("uint64_t over =") or l.strip().startswith("uint64_t to_free ="):
        continue
    if l.strip().startswith("printf") and "capacity exceeded" in l:
        continue
    if l.strip().startswith("storage_evict_oldest(STORAGE_TYPE_PHOTO"):
        continue
    if l.strip() == "    }":
        # Check if this closes the capacity check, before fd = open
        # Look ahead: if next significant line is fd = open, skip this too
        pass
    new_lines.append(l)

content = "\n".join(new_lines)

with open("sources/app/modules/storage_manager/storage_manager.c", "w", encoding="utf-8") as f:
    f.write(content)
print("ok")
