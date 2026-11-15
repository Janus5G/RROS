/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef RROS_PI5_PROBE_SUPPORT_H
#define RROS_PI5_PROBE_SUPPORT_H

#include "efi.h"

typedef struct {
    u64 conventional_pages;
    u64 mmio_pages;
    u64 lowest;
    u64 highest;
} os_pi5_probe_memory_summary;

void os_pi5_probe_output(
    efi_text_output_protocol *con,
    void (*uart_write)(const char *),
    const char *text
);
int os_pi5_probe_summarize_memory_map(
    const void *memory_map,
    usize size,
    usize descriptor_size,
    u32 descriptor_version,
    os_pi5_probe_memory_summary *summary
);

#endif
