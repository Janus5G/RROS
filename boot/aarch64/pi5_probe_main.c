/*
 * RROS Raspberry Pi 5 hardware-probe application (isolated build).
 *
 * Read-only boot diagnostics. The probe never leaves UEFI boot services, never
 * switches the MMU, never writes interrupt-controller, PCIe or RP1 registers and
 * returns to firmware when finished. Every MMIO access is limited to the
 * firmware-configured stdout UART (a polled PL011 FR read plus DR writes).
 * Output goes to UEFI ConOut first, then to the UART selected from the
 * firmware device tree. Markers are prefixed "PI5PROBE".
 */
#include "efi.h"
#include "aarch64_mmu_runtime.h"
#include "boot_console.h"
#include "device_tree.h"
#include "pi5_probe_support.h"

#ifndef PI5_PROBE_REVISION
#define PI5_PROBE_REVISION "dev"
#endif
#define PROBE_MEMORY_MAP_BYTES (256u * 1024u)
#define PROBE_BRK_EXCEPTION_CLASS 0x3cu

volatile u32 g_arm_exception_count;
volatile u64 g_arm_exception_esr;
volatile u64 g_arm_exception_far;
volatile u64 g_arm_exception_elr;

extern u64 arm_current_el(void);
extern u64 arm_current_vbar(void);
extern u64 arm_sctlr(void);
extern void arm_install_vectors(void);
extern void arm_restore_vbar(u64 vbar);

/*
 * The shared exception vectors also reference user-mode and IRQ handlers. The
 * probe masks DAIF and installs the vectors only around one BRK at the current
 * EL, so these are unreachable; they are inert on purpose (no user mode, no GIC).
 */
void *arm_g05_exception_dispatch(void *frame)
{
    return frame;
}

void arm_g05_user_complete(void)
{
    for (;;) {
        __asm__ volatile("wfe");
    }
}

void arm_gic_irq_dispatch(void)
{
}

static u8 g_probe_memory_map[PROBE_MEMORY_MAP_BYTES] __attribute__((aligned(16)));
static os_device_tree g_probe_tree;
static efi_system_table *g_probe_system_table;
static u32 g_probe_pass;
static u32 g_probe_fail;
static u32 g_probe_uart_active;

static void probe_out(const char *text)
{
    os_pi5_probe_output(g_probe_system_table->console_out,
        g_probe_uart_active ? os_boot_console_write : 0, text);
}

static void probe_hex(u64 value)
{
    static const char digits[] = "0123456789abcdef";
    char text[19];
    text[0] = '0';
    text[1] = 'x';
    for (u32 i = 0; i < 16u; ++i) {
        text[2u + i] = digits[(value >> ((15u - i) * 4u)) & 0xfu];
    }
    text[18] = '\0';
    probe_out(text);
}

static void probe_dec(u64 value)
{
    char text[24];
    u32 pos = 23;
    text[pos] = '\0';
    do {
        text[--pos] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && pos != 0u);
    probe_out(&text[pos]);
}

static void probe_kv(const char *key, u64 value, int hex)
{
    probe_out(" ");
    probe_out(key);
    probe_out("=");
    if (hex) {
        probe_hex(value);
    } else {
        probe_dec(value);
    }
}

static void probe_check(const char *name, int ok, const char *detail)
{
    if (ok) {
        ++g_probe_pass;
    } else {
        ++g_probe_fail;
    }
    probe_out("PI5PROBE CHECK name=");
    probe_out(name);
    probe_out(ok ? " result=PASS" : " result=FAIL");
    if (detail != (const char *)0) {
        probe_out(" detail=");
        probe_out(detail);
    }
    probe_out("\r\n");
}

static u64 read_sysreg_midr(void)
{
    u64 v;
    __asm__ volatile("mrs %0, midr_el1" : "=r"(v));
    return v;
}

static u64 read_sysreg_mpidr(void)
{
    u64 v;
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(v));
    return v;
}

static u64 read_sysreg_mmfr0(void)
{
    u64 v;
    __asm__ volatile("mrs %0, id_aa64mmfr0_el1" : "=r"(v));
    return v;
}

static u64 read_sysreg_pfr0(void)
{
    u64 v;
    __asm__ volatile("mrs %0, id_aa64pfr0_el1" : "=r"(v));
    return v;
}

static u64 read_cntfrq(void)
{
    u64 v;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
    return v;
}

static u64 read_cntpct(void)
{
    u64 v;
    __asm__ volatile("isb\n\tmrs %0, cntpct_el0" : "=r"(v) : : "memory");
    return v;
}

static int guid_equal(const efi_guid *a, const efi_guid *b)
{
    if (a->data1 != b->data1 || a->data2 != b->data2 || a->data3 != b->data3) {
        return 0;
    }
    for (u32 i = 0; i < 8u; ++i) {
        if (a->data4[i] != b->data4[i]) {
            return 0;
        }
    }
    return 1;
}

static void probe_utf16(const u16 *text)
{
    char buffer[48];
    usize n = 0;
    if (text == (const u16 *)0) {
        probe_out("(null)");
        return;
    }
    while (text[n] != 0 && n < sizeof(buffer) - 1u) {
        buffer[n] = text[n] < 0x80u ? (char)text[n] : '?';
        ++n;
    }
    buffer[n] = '\0';
    probe_out(buffer);
}

static void stage_handoff(void)
{
    probe_out("PI5PROBE BEGIN revision=" PI5_PROBE_REVISION " scope=read-only boot-services\r\n");
    probe_out("PI5PROBE HANDOFF firmware_vendor=");
    probe_utf16((const u16 *)g_probe_system_table->firmware_vendor);
    probe_kv("firmware_revision", g_probe_system_table->firmware_revision, 1);
    probe_kv("config_tables", g_probe_system_table->number_of_table_entries, 0);
    probe_out("\r\n");

    static const efi_guid acpi20 = {
        0x8868e871u, 0xe4f1, 0x11d3,
        {0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81}};
    static const efi_guid fdt = {
        0xb1b621d5u, 0xf19c, 0x41a5,
        {0x83, 0x0b, 0xd9, 0x15, 0x2c, 0x69, 0xaa, 0xe0}};
    int have_acpi = 0;
    int have_fdt = 0;
    const efi_configuration_table *tables =
        (const efi_configuration_table *)g_probe_system_table->configuration_table;
    for (usize i = 0; tables != (const efi_configuration_table *)0
         && i < g_probe_system_table->number_of_table_entries && i < 256u; ++i) {
        have_acpi |= guid_equal(&tables[i].vendor_guid, &acpi20);
        have_fdt |= guid_equal(&tables[i].vendor_guid, &fdt);
    }
    probe_out("PI5PROBE CONFIG-TABLES");
    probe_kv("acpi", (u64)have_acpi, 0);
    probe_kv("fdt", (u64)have_fdt, 0);
    probe_out("\r\n");
    probe_check("handoff-uefi-system-table", g_probe_system_table->boot_services != 0, (const char *)0);
    probe_check("config-table-fdt-present", have_fdt,
        have_fdt ? (const char *)0
                 : "no FDT table: select Device Tree under Device Manager > Raspberry Pi Configuration");
}

static void stage_cpu(void)
{
    u64 el = arm_current_el();
    u64 mmfr0 = read_sysreg_mmfr0();
    u64 pfr0 = read_sysreg_pfr0();
    probe_out("PI5PROBE CPU");
    probe_kv("current_el", el, 0);
    probe_kv("midr", read_sysreg_midr(), 1);
    probe_kv("mpidr", read_sysreg_mpidr(), 1);
    probe_kv("id_aa64mmfr0", mmfr0, 1);
    probe_kv("id_aa64pfr0", pfr0, 1);
    probe_kv("sctlr", arm_sctlr(), 1);
    probe_kv("vbar", arm_current_vbar(), 1);
    probe_out("\r\n");
    probe_check("cpu-exception-level-1-or-2", el == 1u || el == 2u, (const char *)0);
    probe_check("cpu-firmware-mmu-enabled", (arm_sctlr() & 1u) != 0u, (const char *)0);
    /* ID_AA64MMFR0.PARange must be able to cover the BCM2712 peripheral window. */
    probe_check("cpu-physical-address-range", (mmfr0 & 0xfu) >= 2u, (const char *)0);
}

static void stage_exception(void)
{
    u64 previous_daif = 0;
    __asm__ volatile("mrs %0, daif\n\tmsr daifset, #0xf" : "=r"(previous_daif) : : "memory");
    u64 previous_vbar = arm_current_vbar();
    g_arm_exception_count = 0;
    arm_install_vectors();
    __asm__ volatile("brk #0" : : : "x16", "x17", "memory");
    arm_restore_vbar(previous_vbar);
    __asm__ volatile("msr daif, %0" : : "r"(previous_daif) : "memory");
    probe_out("PI5PROBE EXCEPTION");
    probe_kv("count", g_arm_exception_count, 0);
    probe_kv("esr", g_arm_exception_esr, 1);
    probe_kv("elr", g_arm_exception_elr, 1);
    probe_kv("vbar_restored", (u64)(arm_current_vbar() == previous_vbar), 0);
    probe_out("\r\n");
    probe_check("exception-brk-vector-entered",
        g_arm_exception_count == 1u
        && ((g_arm_exception_esr >> 26) & 0x3fu) == PROBE_BRK_EXCEPTION_CLASS
        && g_arm_exception_elr != 0u,
        (const char *)0);
    probe_check("exception-vbar-restored", arm_current_vbar() == previous_vbar, (const char *)0);
}

static void stage_memory_map(void)
{
    usize size = sizeof(g_probe_memory_map);
    usize map_key = 0;
    usize descriptor_size = 0;
    u32 descriptor_version = 0;
    efi_status status = g_probe_system_table->boot_services->get_memory_map(
        &size, g_probe_memory_map, &map_key, &descriptor_size, &descriptor_version);
    probe_out("PI5PROBE MEMMAP");
    probe_kv("status", status, 1);
    probe_kv("bytes", size, 0);
    probe_kv("descriptor_size", descriptor_size, 0);
    if (status != 0 || descriptor_size < sizeof(efi_memory_descriptor)
        || descriptor_size > 256u || size > sizeof(g_probe_memory_map)) {
        probe_out("\r\n");
        probe_check("memmap-get", 0, "GetMemoryMap failed or returned unusable layout");
        return;
    }
    os_pi5_probe_memory_summary summary;
    if (!os_pi5_probe_summarize_memory_map(
            g_probe_memory_map, size, descriptor_size, descriptor_version, &summary)) {
        probe_out("\r\n");
        probe_check("memmap-validates-for-mmu-handoff", 0,
                    "production validator rejected descriptor layout or ranges");
        return;
    }
    probe_kv("descriptors", size / descriptor_size, 0);
    probe_kv("conventional_pages", summary.conventional_pages, 0);
    probe_kv("mmio_pages", summary.mmio_pages, 0);
    probe_kv("lowest", summary.lowest, 1);
    probe_kv("highest", summary.highest, 1);
    probe_out("\r\n");
    probe_check("memmap-validates-for-mmu-handoff",
        1,
        (const char *)0);
    probe_check("memmap-conventional-memory-present", summary.conventional_pages > 256u, (const char *)0);
}

static const void *g_probe_fdt;
static usize g_probe_fdt_size;
static int g_probe_tree_ok;

static void stage_device_tree(void)
{
    g_probe_fdt_size = 0;
    int found = os_device_tree_find_uefi_blob(
        g_probe_system_table, &g_probe_fdt, &g_probe_fdt_size);
    probe_out("PI5PROBE DT");
    probe_kv("blob_found", (u64)found, 0);
    probe_kv("blob_bytes", g_probe_fdt_size, 0);
    probe_kv("node_capacity", OS_DEVICE_TREE_MAX_NODES, 0);
    if (!found) {
        probe_out("\r\n");
        probe_check("dt-blob-found", 0, "no FDT configuration table");
        return;
    }
    g_probe_tree_ok = os_device_tree_parse(g_probe_fdt, g_probe_fdt_size, &g_probe_tree);
    probe_kv("parse", (u64)g_probe_tree_ok, 0);
    probe_kv("nodes", g_probe_tree_ok ? g_probe_tree.node_count : 0u, 0);
    if (g_probe_tree_ok && g_probe_tree.node_count != 0) {
        probe_out(" root_compatible=");
        probe_out(g_probe_tree.nodes[0].compatible);
    }
    probe_out("\r\n");
    probe_check("dt-blob-found", 1, (const char *)0);
    probe_check("dt-parses-with-production-parser", g_probe_tree_ok,
        g_probe_tree_ok ? (const char *)0 : "os_device_tree_parse rejected the firmware FDT");
}

static void probe_region(const os_device_tree_node *node)
{
    for (u32 r = 0; r < node->region_count && r < 4u; ++r) {
        probe_out(" reg");
        probe_dec(r);
        probe_out("=");
        probe_hex(node->regions[r].address);
        probe_out("+");
        probe_hex(node->regions[r].size);
    }
}

static void stage_uart(void)
{
    probe_out("PI5PROBE UART-SELECT begin\r\n");
    if (!g_probe_tree_ok) {
        probe_out("PI5PROBE UART source=none reason=no-parsed-device-tree\r\n");
        probe_check("uart-stdout-resolved", 0, "no parsed DT; output continues on UEFI ConOut only");
        return;
    }
    os_boot_console_info info;
    int resolved = os_boot_console_resolve_stdout(
        g_probe_fdt, g_probe_fdt_size, &g_probe_tree, &info);
    probe_out("PI5PROBE UART");
    probe_kv("resolved", (u64)resolved, 0);
    if (resolved) {
        probe_out(" stdout_path=");
        probe_out(info.stdout_path);
        probe_out(" node=");
        probe_out(info.node_name);
        probe_kv("base", info.base, 1);
        probe_kv("span", info.span, 1);
    }
    probe_out("\r\n");
    probe_check("uart-stdout-resolved", resolved, (const char *)0);
    if (!resolved) {
        return;
    }
    probe_out("PI5PROBE UART-ENABLE about-to-write-firmware-configured-pl011 (last marker if the board hangs)\r\n");
    if (os_boot_console_select_fdt(g_probe_fdt, g_probe_fdt_size, &g_probe_tree)) {
        g_probe_uart_active = 1;
        probe_out("PI5PROBE UART-ACTIVE source=fdt-pl011 firmware-configured, not reprogrammed\r\n");
        probe_check("uart-fdt-console-active", 1, (const char *)0);
    } else {
        probe_check("uart-fdt-console-active", 0, "boot console refused the resolved node");
    }
}

static void stage_interrupt_and_timer(void)
{
    int gic_found = 0;
    for (usize i = 0; g_probe_tree_ok && i < g_probe_tree.node_count; ++i) {
        const os_device_tree_node *n = &g_probe_tree.nodes[i];
        if (!n->enabled) {
            continue;
        }
        if (os_device_tree_node_has_compatible(n, "arm,gic-400")
            || os_device_tree_node_has_compatible(n, "arm,cortex-a15-gic")
            || os_device_tree_node_has_compatible(n, "arm,gic-v3")) {
            probe_out("PI5PROBE GIC node=");
            probe_out(n->name);
            probe_out(" compatible=");
            probe_out(n->compatible);
            probe_region(n);
            probe_out(" mmio_access=none\r\n");
            gic_found = 1;
            break;
        }
    }
    probe_out("PI5PROBE GIC-CPU-IF");
    probe_kv("id_aa64pfr0_gic", (read_sysreg_pfr0() >> 24) & 0xfu, 0);
    probe_out("\r\n");
    probe_check("gic-controller-described-in-dt", gic_found,
        "DT description only; no GIC register access is performed by the probe");

    u64 freq = read_cntfrq();
    u64 first = read_cntpct();
    (void)g_probe_system_table->boot_services->stall(100000u);
    u64 second = read_cntpct();
    u64 delta = second - first;
    probe_out("PI5PROBE TIMER");
    probe_kv("cntfrq", freq, 0);
    probe_kv("ticks_per_100ms", delta, 0);
    probe_out("\r\n");
    probe_check("timer-frequency-reported", freq >= 1000000u && freq <= 1000000000u, (const char *)0);
    probe_check("timer-counter-advances-with-firmware-stall",
        freq != 0u && delta >= freq / 20u && delta <= freq / 2u, (const char *)0);
}

static void stage_pcie_rp1(void)
{
    u32 pcie = 0;
    u32 rp1 = 0;
    for (usize i = 0; g_probe_tree_ok && i < g_probe_tree.node_count; ++i) {
        const os_device_tree_node *n = &g_probe_tree.nodes[i];
        int is_pcie = os_device_tree_node_has_compatible(n, "brcm,bcm2712-pcie")
            || os_device_tree_node_has_compatible(n, "pci-host-ecam-generic");
        int is_rp1 = os_device_tree_node_has_compatible(n, "raspberrypi,rp1")
            || (n->name[0] == 'r' && n->name[1] == 'p' && n->name[2] == '1' && n->name[3] == '\0');
        if (!is_pcie && !is_rp1) {
            continue;
        }
        probe_out(is_pcie ? "PI5PROBE PCIE node=" : "PI5PROBE RP1 node=");
        probe_out(n->name);
        probe_kv("enabled", n->enabled, 0);
        probe_out(" compatible=");
        probe_out(n->compatible);
        probe_region(n);
        probe_out(" mmio_access=none\r\n");
        pcie += (u32)(is_pcie && n->enabled);
        rp1 += (u32)(is_rp1);
    }
    probe_out("PI5PROBE PCIE-SUMMARY");
    probe_kv("enabled_controllers", pcie, 0);
    probe_kv("rp1_nodes", rp1, 0);
    probe_out(" ecam_or_config_access=none\r\n");
    /* Informational on generic virtual platforms; the Pi 5 evaluator requires both. */
    probe_check("platform-pcie-controller-enabled-in-dt", pcie != 0u, "informational on non-Pi5 platforms");
    probe_check("platform-rp1-described-in-dt", rp1 != 0u, "informational on non-Pi5 platforms");
}

efi_status efi_main(void *image_handle, efi_system_table *system_table)
{
    (void)image_handle;
    if (system_table == (efi_system_table *)0
        || system_table->boot_services == (efi_boot_services *)0
        || system_table->boot_services->get_memory_map == 0
        || system_table->boot_services->stall == 0) {
        return 1u;
    }
    g_probe_system_table = system_table;
    stage_handoff();
    stage_cpu();
    stage_exception();
    stage_memory_map();
    stage_device_tree();
    stage_uart();
    stage_interrupt_and_timer();
    stage_pcie_rp1();
    probe_out("PI5PROBE END");
    probe_kv("pass", g_probe_pass, 0);
    probe_kv("fail", g_probe_fail, 0);
    probe_out(" result=");
    probe_out(g_probe_fail == 0u ? "DIAGNOSTICS-COMPLETE" : "DIAGNOSTICS-FAILURES");
    probe_out(" physical_claims=none\r\n");
    (void)system_table->boot_services->stall(3000000u);
    return 0;
}
