/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pi5_probe_support.h"
#include "aarch64_mmu_runtime.h"

void os_pi5_probe_output(
    efi_text_output_protocol *con,
    void (*uart_write)(const char *),
    const char *text
)
{
    const char *cursor = text;
    if (con != (efi_text_output_protocol *)0 && con->output_string != 0) {
        u16 chunk[96];
        usize used = 0;
        while (*cursor != '\0') {
            chunk[used++] = (u16)(u8)*cursor++;
            if (used == 94u || *cursor == '\0') {
                chunk[used] = 0;
                (void)con->output_string(con, chunk);
                used = 0;
            }
        }
    }
    if (uart_write != 0) {
        uart_write(text);
    }
}

int os_pi5_probe_summarize_memory_map(
    const void *memory_map,
    usize size,
    usize descriptor_size,
    u32 descriptor_version,
    os_pi5_probe_memory_summary *summary
)
{
    if (summary == (os_pi5_probe_memory_summary *)0) {
        return 0;
    }
    *summary = (os_pi5_probe_memory_summary){0};
    if (!os_aarch64_mmu_validate_efi_memory_map(
            memory_map, size, descriptor_size, descriptor_version)) {
        return 0;
    }
    summary->lowest = ~(u64)0;
    for (usize index = 0; index < size / descriptor_size; ++index) {
        const efi_memory_descriptor *d =
            (const efi_memory_descriptor *)((const u8 *)memory_map + index * descriptor_size);
        if (d->type == 7u) {
            summary->conventional_pages += d->number_of_pages;
        } else if (d->type == 11u || d->type == 12u) {
            summary->mmio_pages += d->number_of_pages;
        }
        if (d->physical_start < summary->lowest) {
            summary->lowest = d->physical_start;
        }
        u64 top = d->physical_start + d->number_of_pages * 4096u;
        if (top > summary->highest) {
            summary->highest = top;
        }
    }
    return 1;
}
