#include "boot_console.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void check(const char *name, int condition)
{
    printf("%s %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition) {
        ++failures;
    }
}

typedef struct {
    uint8_t data[4096];
    size_t size;
    char strings[512];
    size_t strings_size;
} fdt_builder;

static void put32(fdt_builder *b, uint32_t v)
{
    b->data[b->size++] = (uint8_t)(v >> 24);
    b->data[b->size++] = (uint8_t)(v >> 16);
    b->data[b->size++] = (uint8_t)(v >> 8);
    b->data[b->size++] = (uint8_t)v;
}

static void pad4(fdt_builder *b)
{
    while (b->size & 3u) {
        b->data[b->size++] = 0;
    }
}

static void begin(fdt_builder *b, const char *name)
{
    put32(b, 1);
    size_t n = strlen(name) + 1u;
    memcpy(b->data + b->size, name, n);
    b->size += n;
    pad4(b);
}

static void end(fdt_builder *b)
{
    put32(b, 2);
}

static void prop_string(fdt_builder *b, const char *name, const char *value)
{
    size_t off = b->strings_size;
    size_t n = strlen(name) + 1u;
    memcpy(b->strings + off, name, n);
    b->strings_size += n;
    size_t len = strlen(value) + 1u;
    put32(b, 3);
    put32(b, (uint32_t)len);
    put32(b, (uint32_t)off);
    memcpy(b->data + b->size, value, len);
    b->size += len;
    pad4(b);
}

/* Returns a complete blob in `out` (caller buffer >= 8192) and its size. */
static size_t finish(fdt_builder *b, uint8_t *out)
{
    put32(b, 9);
    size_t struct_off = 56u; /* 40-byte header + one empty memory-reservation entry */
    size_t strings_off = struct_off + b->size;
    size_t total = strings_off + b->strings_size;
    memset(out, 0, total);
    uint32_t h[10] = {0xd00dfeedu, (uint32_t)total, (uint32_t)struct_off,
        (uint32_t)strings_off, 40, 17, 16, 0, (uint32_t)b->strings_size,
        (uint32_t)b->size};
    for (int i = 0; i < 10; ++i) {
        out[i * 4] = (uint8_t)(h[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(h[i] >> 8);
        out[i * 4 + 3] = (uint8_t)h[i];
    }
    memcpy(out + struct_off, b->data, b->size);
    memcpy(out + strings_off, b->strings, b->strings_size);
    return total;
}

static size_t make_blob(uint8_t *out, const char *stdout_path, const char *alias_name,
    const char *alias_value)
{
    fdt_builder *b = calloc(1, sizeof(*b));
    begin(b, "");
    if (stdout_path != NULL) {
        begin(b, "chosen");
        prop_string(b, "stdout-path", stdout_path);
        end(b);
    }
    if (alias_name != NULL) {
        begin(b, "aliases");
        prop_string(b, alias_name, alias_value);
        end(b);
    }
    end(b);
    size_t total = finish(b, out);
    free(b);
    return total;
}

static void add_node(os_device_tree *tree, const char *name, const char *compat,
    int enabled, uint64_t address, uint64_t size, int translated)
{
    os_device_tree_node *n = &tree->nodes[tree->node_count++];
    memset(n, 0, sizeof(*n));
    strcpy(n->name, name);
    strcpy(n->compatible, compat);
    strcpy(n->compatible_list, compat);
    n->compatible_list_size = (uint16_t)(strlen(compat) + 1u);
    n->enabled = (uint8_t)enabled;
    n->region_count = 1;
    n->regions[0].address = address;
    n->regions[0].size = size;
    n->regions[0].translated = (uint8_t)translated;
}

static os_device_tree *new_tree(void)
{
    return calloc(1, sizeof(os_device_tree));
}

static int resolves(const uint8_t *blob, size_t size, const os_device_tree *t,
    os_boot_console_info *info)
{
    return os_boot_console_resolve_stdout(blob, size, t, info);
}

static void prop_cells(fdt_builder *b, const char *name, size_t count, const uint32_t *cells)
{
    size_t off = b->strings_size;
    size_t n = strlen(name) + 1u;
    memcpy(b->strings + off, name, n);
    b->strings_size += n;
    put32(b, 3);
    put32(b, (uint32_t)(count * 4u));
    put32(b, (uint32_t)off);
    for (size_t i = 0; i < count; ++i) {
        put32(b, cells[i]);
    }
}

/* Cell-count properties placed AFTER ranges/reg (as QEMU emits them) must still apply. */
static void test_cell_properties_after_ranges(void)
{
    fdt_builder *b = calloc(1, sizeof(*b));
    const uint32_t one[1] = {1};
    const uint32_t ranges[4] = {0x0, 0x0, 0x10000000u, 0x10000u};
    const uint32_t reg[2] = {0x100u, 0x20u};
    begin(b, "");
    begin(b, "bus@0");
    prop_cells(b, "ranges", 4, ranges);
    prop_cells(b, "#address-cells", 1, one);
    prop_cells(b, "#size-cells", 1, one);
    begin(b, "serial@100");
    prop_string(b, "compatible", "arm,pl011");
    prop_cells(b, "reg", 2, reg);
    end(b);
    end(b);
    end(b);
    uint8_t *blob = calloc(1, 8192);
    size_t total = finish(b, blob);
    os_device_tree *tree = new_tree();
    int ok = os_device_tree_parse(blob, total, tree);
    check("cell counts declared after ranges are applied (QEMU property order)", ok && tree->node_count == 3u);
    if (ok) {
        const os_device_tree_node *serial = &tree->nodes[2];
        check("child register window translates through the late-declared ranges",
            serial->region_count == 1u && serial->regions[0].translated
            && serial->regions[0].address == 0x10000100ull && serial->regions[0].size == 0x20u);
    } else {
        check("child register window translates through the late-declared ranges", 0);
    }
    free(tree);
    free(blob);
    free(b);
}

int main(int argc, char **argv)
{
    static uint8_t blob[8192];
    os_boot_console_info info;
    os_device_tree *t = new_tree();
    add_node(t, "serial@7d001000", "arm,pl011\0arm,primecell", 1, 0x107d001000ull, 0x200, 1);
    add_node(t, "serial@7d002000", "arm,pl011", 1, 0x107d002000ull, 0x200, 1);

    size_t n = make_blob(blob, "serial10:115200n8", "serial10", "/soc/serial@7d001000");
    check("alias stdout-path resolves to the single enabled PL011",
        resolves(blob, n, t, &info) && info.base == 0x107d001000ull
        && info.kind == OS_BOOT_CONSOLE_FDT_PL011 && strcmp(info.node_name, "serial@7d001000") == 0);

    n = make_blob(blob, "/soc/serial@7d002000:115200", NULL, NULL);
    check("direct path with options resolves", resolves(blob, n, t, &info) && info.base == 0x107d002000ull);

    n = make_blob(blob, NULL, "serial10", "/soc/serial@7d001000");
    check("missing /chosen is rejected without a guess", !resolves(blob, n, t, &info) && info.kind == 0);

    n = make_blob(blob, "serial99:115200n8", "serial10", "/soc/serial@7d001000");
    check("unknown alias rejected", !resolves(blob, n, t, &info));

    n = make_blob(blob, "/soc/serial@dead0000", NULL, NULL);
    check("path naming an unknown node rejected", !resolves(blob, n, t, &info));

    n = make_blob(blob, "serial10", "serial10", "relative/serial@7d001000");
    check("non-absolute alias target rejected", !resolves(blob, n, t, &info));

    n = make_blob(blob, "/", NULL, NULL);
    check("root path rejected", !resolves(blob, n, t, &info));

    t->nodes[0].enabled = 0;
    n = make_blob(blob, "/soc/serial@7d001000", NULL, NULL);
    check("disabled node rejected", !resolves(blob, n, t, &info));
    t->nodes[0].enabled = 1;

    t->nodes[0].regions[0].translated = 0;
    check("untranslated register window rejected", !resolves(blob, n, t, &info));
    t->nodes[0].regions[0].translated = 1;

    t->nodes[0].regions[0].size = 0x10;
    check("register window smaller than the PL011 data/flag registers rejected", !resolves(blob, n, t, &info));
    t->nodes[0].regions[0].size = 0x200;

    t->nodes[0].regions[0].address = 0x107d001002ull;
    check("unaligned base rejected", !resolves(blob, n, t, &info));
    t->nodes[0].regions[0].address = 0x107d001000ull;

    strcpy(t->nodes[0].compatible_list, "ns16550a");
    t->nodes[0].compatible_list_size = 9;
    strcpy(t->nodes[0].compatible, "ns16550a");
    check("non-PL011 compatible rejected", !resolves(blob, n, t, &info));
    strcpy(t->nodes[0].compatible, "arm,pl011");
    strcpy(t->nodes[0].compatible_list, "arm,pl011");
    t->nodes[0].compatible_list_size = 10;
    check("restored node resolves again", resolves(blob, n, t, &info));

    strcpy(t->nodes[1].name, "serial@7d001000");
    check("duplicate node names are ambiguous and rejected", !resolves(blob, n, t, &info));
    strcpy(t->nodes[1].name, "serial@7d002000");

    uint8_t bad[8192];
    memcpy(bad, blob, n);
    bad[0] ^= 0xff;
    check("bad magic rejected", !resolves(bad, n, t, &info));
    memcpy(bad, blob, n);
    check("truncated blob rejected", !resolves(bad, 48, t, &info));
    check("header-only size rejected", !resolves(bad, 8, t, &info));
    check("NULL blob rejected", !resolves(NULL, 0, t, &info));
    memcpy(bad, blob, n);
    bad[56 + 4 + 4] = 0x7f; /* node name never terminates inside the structure block */
    for (size_t i = 68; i < 76 && i < n; ++i) {
        bad[i] = 'x';
    }
    (void)resolves(bad, n, t, &info);
    check("corrupt structure block never crashes", 1);
    test_cell_properties_after_ranges();

    /* The console must stay silent and not touch memory when nothing was selected. */
    os_boot_console_disable();
    os_boot_console_write("must not be written anywhere");
    check("write on a disabled console is a no-op", os_boot_console_get()->kind == OS_BOOT_CONSOLE_NONE);

    /* Drive a fake PL011 register file (FR.TXFF clear) through the discovered path. */
    static uint32_t regs[64];
    memset(regs, 0, sizeof(regs));
    t->nodes[0].regions[0].address = (uint64_t)(uintptr_t)regs;
    n = make_blob(blob, "/soc/serial@7d001000", NULL, NULL);
    check("select_fdt adopts the UART without programming it",
        os_boot_console_select_fdt(blob, n, t) && regs[12] == 0 && regs[9] == 0 && regs[11] == 0);
    os_boot_console_write("AB");
    check("adopted console writes to the data register", (regs[0] & 0xff) == 'B');
    regs[6] = 1u << 5;
    os_boot_console_write("C");
    check("a stuck transmitter does not hang or write", (regs[0] & 0xff) == 'B');
    os_boot_console_disable();
    check("failed selection leaves the console disabled",
        !os_boot_console_select_fdt(blob, 0, t) && os_boot_console_get()->kind == OS_BOOT_CONSOLE_NONE);

    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb");
        if (f == NULL) {
            printf("FAIL cannot open %s\n", argv[1]);
            return 1;
        }
        fseek(f, 0, SEEK_END);
        long len = ftell(f);
        fseek(f, 0, SEEK_SET);
        uint8_t *real = malloc((size_t)len);
        if (fread(real, 1, (size_t)len, f) != (size_t)len) {
            return 1;
        }
        fclose(f);
        os_device_tree *rt = new_tree();
        int parsed = os_device_tree_parse(real, (size_t)len, rt);
        printf("INFO real DTB %s: %ld bytes, parse=%d nodes=%zu\n", argv[1], len, parsed, (size_t)rt->node_count);
        int ok = parsed && os_boot_console_resolve_stdout(real, (size_t)len, rt, &info);
        printf("INFO real DTB stdout-path='%s' node='%s' base=0x%llx span=0x%llx\n",
            info.stdout_path, info.node_name, (unsigned long long)info.base, (unsigned long long)info.span);
        check("real Raspberry Pi 5 firmware DTB resolves a PL011 console", ok);
        free(rt);
        free(real);
    }
    free(t);
    printf("boot_console failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
