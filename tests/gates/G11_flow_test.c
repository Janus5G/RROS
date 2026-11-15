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
    if (!os_flow_desktop_self_test(&framebuffer, (u16)'f')) {
        fputs("FAIL G11-FLOW: dock, switching, theme or lifecycle failed\n",
            stderr);
        return 1;
    }
    if (os_flow_desktop_self_test(&framebuffer, (u16)'x')) {
        fputs("FAIL G11-FLOW: invalid activation key was accepted\n", stderr);
        return 1;
    }
    puts("PASS G11-FLOW: dock, app switching, themes and shared services");

    if (!os_classic_desktop_self_test(&framebuffer, (u16)'d')) {
        fputs("FAIL G11-CLASSIC: shared app services regressed\n", stderr);
        return 1;
    }
    puts("PASS G11-CLASSIC: shared lifecycle regression");
    return 0;
}
