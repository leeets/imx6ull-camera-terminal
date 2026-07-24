import sys
with open("sources/app/modules/storage_manager/storage_manager.c", "r", encoding="utf-8") as f:
    c = f.read()

lines = c.split("\n")
new_lines = []
for i, l in enumerate(lines):
    if i > 0 and l.strip() == '",' and 'capacity exceeded by %lu bytes' in lines[i-1]:
        # Skip the broken continuation line
        continue
    elif 'capacity exceeded by %lu bytes, evicting %lu' in l:
        # Rewrite properly
        new_lines.append('        printf("[STORAGE] capacity exceeded by %lu bytes, evicting %lu\\n",')
    else:
        new_lines.append(l)

c = "\n".join(new_lines)

with open("sources/app/modules/storage_manager/storage_manager.c", "w", encoding="utf-8") as f:
    f.write(c)
print("Fixed")
