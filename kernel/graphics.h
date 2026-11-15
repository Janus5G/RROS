#ifndef EXPERIMENTALOS_KERNEL_GRAPHICS_H
#define EXPERIMENTALOS_KERNEL_GRAPHICS_H

#include "efi.h"

typedef struct {
    volatile u32 *pixels;
    u32 width;
    u32 height;
    u32 pixels_per_scan_line;
    u32 pixel_format;
    usize framebuffer_size;
} os_framebuffer;

int os_graphics_self_test(const os_framebuffer *framebuffer);
int os_graphics_framebuffer_valid(const os_framebuffer *framebuffer);
u32 os_graphics_pixel_value(u32 color, u32 pixel_format);
u32 os_graphics_read_pixel(
    const os_framebuffer *framebuffer,
    u32 x,
    u32 y
);
void os_graphics_fill_rect(
    const os_framebuffer *framebuffer,
    s32 x,
    s32 y,
    u32 width,
    u32 height,
    u32 color
);
void os_graphics_draw_text(
    const os_framebuffer *framebuffer,
    s32 x,
    s32 y,
    const char *text,
    u32 color
);
int os_graphics_prepare_guest(efi_boot_services *boot_services);
int os_graphics_guest_framebuffer(os_framebuffer *framebuffer);
int os_graphics_guest_self_test(void);

#endif
