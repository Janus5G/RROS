#include "boot_console.h"

#define FDT_MAGIC 0xd00dfeedu
#define FDT_BEGIN_NODE 1u
#define FDT_END_NODE 2u
#define FDT_PROP 3u
#define FDT_NOP 4u
#define FDT_END 9u
#define FDT_HEADER_SIZE 40u
#define FDT_MAX_DEPTH 32u

#define PL011_DR 0x00u
#define PL011_FR 0x18u
#define PL011_FR_TXFF (1u << 5)
#define PL011_CR 0x30u
#define TX_SPIN_LIMIT 1000000u

static os_boot_console_info g_console;
static volatile u32 *g_uart;

static u32 be32(const u8 *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

static int str_equal(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static usize str_length(const char *s)
{
    usize n = 0;
    while (s[n] != '\0') {
        ++n;
    }
    return n;
}

static int fdt_header_valid(const u8 *fdt, usize size, u32 *struct_off,
    u32 *struct_size, u32 *strings_off, u32 *strings_size)
{
    if (fdt == (const u8 *)0 || size < FDT_HEADER_SIZE
        || size > OS_DEVICE_TREE_MAX_BLOB_SIZE || be32(fdt) != FDT_MAGIC) {
        return 0;
    }
    *struct_off = be32(fdt + 8);
    *strings_off = be32(fdt + 12);
    *strings_size = be32(fdt + 32);
    *struct_size = be32(fdt + 36);
    if (*struct_off > size || *struct_size > size - *struct_off
        || *strings_off > size || *strings_size > size - *strings_off
        || (*struct_off & 3u) != 0u) {
        return 0;
    }
    return 1;
}

/* Copy the string property `prop` of the depth-1 node named `node` into out. */
static int fdt_get_root_child_string(
    const u8 *fdt, usize size, const char *node, const char *prop,
    char *out, usize out_size)
{
    u32 struct_off, struct_size, strings_off, strings_size;
    if (!fdt_header_valid(fdt, size, &struct_off, &struct_size,
            &strings_off, &strings_size) || out_size == 0u) {
        return 0;
    }
    const u8 *cursor = fdt + struct_off;
    const u8 *end = cursor + struct_size;
    u32 depth = 0;
    int in_target = 0;
    while (end - cursor >= 4) {
        u32 token = be32(cursor);
        cursor += 4;
        if (token == FDT_BEGIN_NODE) {
            usize name_len = 0;
            while (cursor + name_len < end && cursor[name_len] != '\0') {
                ++name_len;
            }
            if (cursor + name_len >= end) {
                return 0;
            }
            if (depth == 1u) {
                in_target = str_equal((const char *)cursor, node);
            }
            ++depth;
            if (depth > FDT_MAX_DEPTH) {
                return 0;
            }
            usize advance = (name_len + 1u + 3u) & ~(usize)3u;
            if ((usize)(end - cursor) < advance) {
                return 0;
            }
            cursor += advance;
        } else if (token == FDT_END_NODE) {
            if (depth == 0u) {
                return 0;
            }
            --depth;
            if (depth == 1u) {
                in_target = 0;
            }
        } else if (token == FDT_PROP) {
            if (end - cursor < 8) {
                return 0;
            }
            u32 length = be32(cursor);
            u32 name_off = be32(cursor + 4);
            cursor += 8;
            usize advance = ((usize)length + 3u) & ~(usize)3u;
            if (length > (u32)(end - cursor)
                || advance > (usize)(end - cursor)
                || name_off >= strings_size) {
                return 0;
            }
            if (depth == 2u && in_target) {
                const char *name = (const char *)(fdt + strings_off + name_off);
                usize max_name = strings_size - name_off;
                usize n = 0;
                while (n < max_name && name[n] != '\0') {
                    ++n;
                }
                if (n == max_name) {
                    return 0;
                }
                if (str_equal(name, prop)) {
                    if (length == 0u || length > out_size
                        || cursor[length - 1u] != '\0') {
                        return 0;
                    }
                    for (u32 i = 0; i < length; ++i) {
                        out[i] = (char)cursor[i];
                    }
                    return 1;
                }
            }
            cursor += advance;
        } else if (token == FDT_NOP) {
            continue;
        } else {
            return 0;
        }
    }
    return 0;
}

static int copy_bounded(char *dst, usize dst_size, const char *src, usize len)
{
    if (len + 1u > dst_size) {
        return 0;
    }
    for (usize i = 0; i < len; ++i) {
        dst[i] = src[i];
    }
    dst[len] = '\0';
    return 1;
}

int os_boot_console_resolve_stdout(
    const void *fdt_blob,
    usize fdt_size,
    const os_device_tree *tree,
    os_boot_console_info *info
)
{
    if (info == (os_boot_console_info *)0) {
        return 0;
    }
    info->kind = OS_BOOT_CONSOLE_NONE;
    info->base = 0;
    info->span = 0;
    info->stdout_path[0] = '\0';
    info->node_name[0] = '\0';
    if (tree == (const os_device_tree *)0
        || tree->node_count == 0u
        || tree->node_count > OS_DEVICE_TREE_MAX_NODES) {
        return 0;
    }
    const u8 *fdt = (const u8 *)fdt_blob;
    char raw[OS_BOOT_CONSOLE_PATH_MAX];
    if (!fdt_get_root_child_string(
            fdt, fdt_size, "chosen", "stdout-path", raw, sizeof(raw))
        && !fdt_get_root_child_string(
            fdt, fdt_size, "chosen", "linux,stdout-path", raw, sizeof(raw))) {
        return 0;
    }
    usize len = 0;
    while (raw[len] != '\0' && raw[len] != ':') {
        ++len;
    }
    raw[len] = '\0';
    if (len == 0u) {
        return 0;
    }
    char path[OS_BOOT_CONSOLE_PATH_MAX];
    if (raw[0] == '/') {
        if (!copy_bounded(path, sizeof(path), raw, len)) {
            return 0;
        }
    } else if (!fdt_get_root_child_string(
            fdt, fdt_size, "aliases", raw, path, sizeof(path))
        || path[0] != '/') {
        return 0;
    }
    if (!copy_bounded(info->stdout_path, sizeof(info->stdout_path),
            path, str_length(path))) {
        return 0;
    }
    const char *leaf = path;
    for (const char *p = path; *p != '\0'; ++p) {
        if (*p == '/') {
            leaf = p + 1;
        }
    }
    if (*leaf == '\0') {
        return 0;
    }
    const os_device_tree_node *match = (const os_device_tree_node *)0;
    usize matches = 0;
    for (usize i = 0; i < tree->node_count; ++i) {
        if (str_equal(tree->nodes[i].name, leaf)) {
            match = &tree->nodes[i];
            ++matches;
        }
    }
    if (matches != 1u || !match->enabled
        || !os_device_tree_node_has_compatible(match, "arm,pl011")
        || match->region_count == 0u) {
        return 0;
    }
    const os_device_tree_region *region = &match->regions[0];
    if (!region->translated || region->size < OS_BOOT_CONSOLE_MIN_REGISTER_SPAN
        || (region->address & 3u) != 0u || region->address >= (1ull << 48)
        || region->size > (1ull << 20)) {
        return 0;
    }
    info->kind = OS_BOOT_CONSOLE_FDT_PL011;
    info->base = region->address;
    info->span = region->size;
    (void)copy_bounded(info->node_name, sizeof(info->node_name),
        match->name, str_length(match->name));
    return 1;
}

void os_boot_console_disable(void)
{
    g_uart = (volatile u32 *)0;
    g_console.kind = OS_BOOT_CONSOLE_NONE;
    g_console.base = 0;
    g_console.span = 0;
    g_console.stdout_path[0] = '\0';
    g_console.node_name[0] = '\0';
}

void os_boot_console_select_qemu_virt(void)
{
    os_boot_console_disable();
    g_console.kind = OS_BOOT_CONSOLE_QEMU_VIRT;
    g_console.base = OS_BOOT_CONSOLE_QEMU_VIRT_PL011_BASE;
    g_console.span = 0x1000u;
    g_uart = (volatile u32 *)(usize)g_console.base;
    g_uart[PL011_CR / 4u] = 0;
    g_uart[9] = 13;
    g_uart[10] = 1;
    g_uart[11] = 0x70;
    g_uart[PL011_CR / 4u] = 0x301;
}

int os_boot_console_select_fdt(
    const void *fdt,
    usize fdt_size,
    const os_device_tree *tree
)
{
    os_boot_console_info info;
    os_boot_console_disable();
    if (!os_boot_console_resolve_stdout(fdt, fdt_size, tree, &info)) {
        return 0;
    }
    g_console = info;
    g_uart = (volatile u32 *)(usize)info.base;
    return 1;
}

const os_boot_console_info *os_boot_console_get(void)
{
    return &g_console;
}

void os_boot_console_write(const char *text)
{
    if (g_uart == (volatile u32 *)0 || text == (const char *)0) {
        return;
    }
    while (*text != '\0') {
        u32 spins = 0;
        while ((g_uart[PL011_FR / 4u] & PL011_FR_TXFF) != 0u
            && spins < TX_SPIN_LIMIT) {
            ++spins;
        }
        if (spins == TX_SPIN_LIMIT) {
            return;
        }
        g_uart[PL011_DR / 4u] = (u8)*text++;
    }
}
