#ifndef EXPERIMENTALOS_KERNEL_BOOT_CONSOLE_H
#define EXPERIMENTALOS_KERNEL_BOOT_CONSOLE_H

#include "device_tree.h"
#include "efi.h"

/* PL011 base of the QEMU "virt" machine. Valid ONLY on that virtual platform. */
#define OS_BOOT_CONSOLE_QEMU_VIRT_PL011_BASE 0x09000000ull
#define OS_BOOT_CONSOLE_MIN_REGISTER_SPAN 0x20u
#define OS_BOOT_CONSOLE_PATH_MAX 160u

#define OS_BOOT_CONSOLE_NONE 0u
#define OS_BOOT_CONSOLE_QEMU_VIRT 1u
#define OS_BOOT_CONSOLE_FDT_PL011 2u

typedef struct {
    u32 kind;
    u64 base;
    u64 span;
    char stdout_path[OS_BOOT_CONSOLE_PATH_MAX];
    char node_name[OS_DEVICE_TREE_NAME_MAX];
} os_boot_console_info;

/* Firmware-independent QEMU virt console: programs the PL011 (legacy behaviour). */
void os_boot_console_select_qemu_virt(void);

/*
 * Resolve /chosen stdout-path (directly or through /aliases) to exactly one
 * enabled "arm,pl011" node of the parsed tree with a translated register
 * window. Pure function: performs no MMIO. Returns 1 on success.
 */
int os_boot_console_resolve_stdout(
    const void *fdt,
    usize fdt_size,
    const os_device_tree *tree,
    os_boot_console_info *info
);

/*
 * Adopt the firmware-configured UART named by stdout-path. The UART is NOT
 * reprogrammed (clock and baud are owned by firmware). On failure the console
 * stays disabled and no MMIO is ever issued.
 */
int os_boot_console_select_fdt(
    const void *fdt,
    usize fdt_size,
    const os_device_tree *tree
);

void os_boot_console_disable(void);
const os_boot_console_info *os_boot_console_get(void);
void os_boot_console_write(const char *text);

#endif
