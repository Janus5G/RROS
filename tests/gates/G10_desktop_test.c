#include "desktop.h"

#include <stdio.h>
#include <string.h>

#define TEST_WIDTH 640u
#define TEST_HEIGHT 480u

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
    memset(g_pixels, 0, sizeof(g_pixels));
    if (!os_classic_desktop_self_test(&framebuffer, (u16)'d')) {
        fputs("FAIL G10-DESKTOP: launch, resize, close or cleanup failed\n",
            stderr);
        return 1;
    }
    if (os_graphics_read_pixel(
            &framebuffer, 2, TEST_HEIGHT - 2u)
        != os_graphics_pixel_value(0x00242a32u, framebuffer.pixel_format)) {
        fputs("FAIL G10-DESKTOP: taskbar framebuffer output missing\n", stderr);
        return 1;
    }
    if (os_classic_desktop_self_test(&framebuffer, (u16)'x')) {
        fputs("FAIL G10-DESKTOP: invalid activation key was accepted\n", stderr);
        return 1;
    }
    puts("PASS G10-DESKTOP: shell rendering, app lifecycle and invalid input");
    return 0;
}
