#include "graphics.h"

#include <stdio.h>
#include <string.h>

#define TEST_WIDTH 640u
#define TEST_HEIGHT 400u

static u32 g_pixels[TEST_WIDTH * TEST_HEIGHT];

int main(void)
{
    os_framebuffer framebuffer = {
        .pixels = g_pixels,
        .width = TEST_WIDTH,
        .height = TEST_HEIGHT,
        .pixels_per_scan_line = TEST_WIDTH,
        .pixel_format = 0,
        .framebuffer_size = sizeof(g_pixels),
    };
    if (!os_graphics_self_test(&framebuffer)) {
        fputs("FAIL G08-GRAPHICS: RGB framebuffer, compositor or pointer events\n",
            stderr);
        return 1;
    }
    framebuffer.pixel_format = 1;
    memset(g_pixels, 0, sizeof(g_pixels));
    if (!os_graphics_self_test(&framebuffer)) {
        fputs("FAIL G08-GRAPHICS: BGR framebuffer rendering failed\n", stderr);
        return 1;
    }
    framebuffer.framebuffer_size = sizeof(g_pixels) - sizeof(u32);
    if (os_graphics_self_test(&framebuffer)) {
        fputs("FAIL G08-GRAPHICS: undersized framebuffer was accepted\n", stderr);
        return 1;
    }
    puts("PASS G08-GRAPHICS: RGB/BGR, clipping, windows, compositor and input");
    return 0;
}
