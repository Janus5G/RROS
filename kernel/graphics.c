#include "graphics.h"

#define GRAPHICS_PIXEL_RGB 0u
#define GRAPHICS_PIXEL_BGR 1u
#define GRAPHICS_MIN_WIDTH 640u
#define GRAPHICS_MIN_HEIGHT 400u
#define GRAPHICS_COLOR_DESKTOP 0x0018202au
#define GRAPHICS_COLOR_WINDOW 0x00e3e8edu
#define GRAPHICS_COLOR_TITLE 0x002f6f9fu
#define GRAPHICS_COLOR_ACTIVE_TITLE 0x004080b0u
#define GRAPHICS_COLOR_TEXT 0x00101820u
#define GRAPHICS_COLOR_CLOSE 0x00c94343u

typedef struct {
    s32 x;
    s32 y;
    u32 width;
    u32 height;
    u32 visible;
    u32 color;
} graphics_window;

typedef struct {
    s32 x;
    s32 y;
    u32 pressed;
    s32 dragging_window;
    s32 drag_x;
    s32 drag_y;
} graphics_pointer;

typedef struct {
    os_framebuffer framebuffer;
    graphics_window windows[2];
    u32 window_count;
    s32 active_window;
    graphics_pointer pointer;
} graphics_desktop;

static os_framebuffer g_guest_framebuffer;
static int g_guest_framebuffer_ready;

static const u8 g_font[26][7] = {
    {14, 17, 17, 31, 17, 17, 17}, {30, 17, 17, 30, 17, 17, 30},
    {14, 17, 16, 16, 16, 17, 14}, {30, 17, 17, 17, 17, 17, 30},
    {31, 16, 16, 30, 16, 16, 31}, {31, 16, 16, 30, 16, 16, 16},
    {14, 17, 16, 23, 17, 17, 15}, {17, 17, 17, 31, 17, 17, 17},
    {14, 4, 4, 4, 4, 4, 14}, {7, 2, 2, 2, 18, 18, 12},
    {17, 18, 20, 24, 20, 18, 17}, {16, 16, 16, 16, 16, 16, 31},
    {17, 27, 21, 21, 17, 17, 17}, {17, 25, 21, 19, 17, 17, 17},
    {14, 17, 17, 17, 17, 17, 14}, {30, 17, 17, 30, 16, 16, 16},
    {14, 17, 17, 17, 21, 18, 13}, {30, 17, 17, 30, 20, 18, 17},
    {15, 16, 16, 14, 1, 1, 30}, {31, 4, 4, 4, 4, 4, 4},
    {17, 17, 17, 17, 17, 17, 14}, {17, 17, 17, 17, 17, 10, 4},
    {17, 17, 17, 21, 21, 21, 10}, {17, 17, 10, 4, 10, 17, 17},
    {17, 17, 10, 4, 4, 4, 4}, {31, 1, 2, 4, 8, 16, 31},
};

static u32 encode_pixel(u32 color, u32 format)
{
    u32 red = (color >> 16) & 0xffu;
    u32 green = (color >> 8) & 0xffu;
    u32 blue = color & 0xffu;
    if (format == GRAPHICS_PIXEL_BGR) {
        return blue | (green << 8) | (red << 16);
    }
    return red | (green << 8) | (blue << 16);
}

static void put_pixel(
    const os_framebuffer *framebuffer,
    u32 x,
    u32 y,
    u32 color
)
{
    if (x < framebuffer->width && y < framebuffer->height) {
        usize index = (usize)y * framebuffer->pixels_per_scan_line + x;
        framebuffer->pixels[index] = encode_pixel(color, framebuffer->pixel_format);
    }
}

static u32 get_pixel(const os_framebuffer *framebuffer, u32 x, u32 y)
{
    return framebuffer->pixels[
        (usize)y * framebuffer->pixels_per_scan_line + x];
}

static void fill_rect(
    const os_framebuffer *framebuffer,
    s32 x,
    s32 y,
    u32 width,
    u32 height,
    u32 color
)
{
    for (u32 row = 0; row < height; ++row) {
        s32 screen_y = y + (s32)row;
        if (screen_y < 0 || (u32)screen_y >= framebuffer->height) {
            continue;
        }
        for (u32 column = 0; column < width; ++column) {
            s32 screen_x = x + (s32)column;
            if (screen_x >= 0 && (u32)screen_x < framebuffer->width) {
                put_pixel(
                    framebuffer, (u32)screen_x, (u32)screen_y, color);
            }
        }
    }
}

static void draw_text(
    const os_framebuffer *framebuffer,
    s32 x,
    s32 y,
    const char *text,
    u32 color
)
{
    for (; *text != '\0'; ++text, x += 6) {
        char character = *text;
        if (character >= 'a' && character <= 'z') {
            character = (char)(character - 'a' + 'A');
        }
        if (character < 'A' || character > 'Z') {
            continue;
        }
        const u8 *glyph = g_font[(u32)(character - 'A')];
        for (u32 row = 0; row < 7; ++row) {
            for (u32 column = 0; column < 5; ++column) {
                if ((glyph[row] & (1u << (4u - column))) != 0) {
                    put_pixel(
                        framebuffer,
                        (u32)(x + (s32)column),
                        (u32)(y + (s32)row),
                        color
                    );
                }
            }
        }
    }
}

static void draw_window(
    const os_framebuffer *framebuffer,
    const graphics_window *window,
    u32 active
)
{
    if (!window->visible) {
        return;
    }
    fill_rect(
        framebuffer, window->x, window->y,
        window->width, window->height, GRAPHICS_COLOR_WINDOW);
    fill_rect(
        framebuffer, window->x, window->y,
        window->width, 24,
        active ? GRAPHICS_COLOR_ACTIVE_TITLE : GRAPHICS_COLOR_TITLE);
    fill_rect(
        framebuffer,
        window->x + (s32)window->width - 20,
        window->y + 3,
        16,
        16,
        GRAPHICS_COLOR_CLOSE
    );
    draw_text(
        framebuffer, window->x + 8, window->y + 8,
        "ExperimentalOS", 0x00ffffffu);
    draw_text(
        framebuffer, window->x + 12, window->y + 40,
        "Window services ready", GRAPHICS_COLOR_TEXT);
    fill_rect(
        framebuffer, window->x + 12, window->y + 62,
        window->width - 24, 2, window->color);
}

static void render_desktop(graphics_desktop *desktop)
{
    fill_rect(
        &desktop->framebuffer, 0, 0,
        desktop->framebuffer.width, desktop->framebuffer.height,
        GRAPHICS_COLOR_DESKTOP);
    for (u32 i = 0; i < desktop->window_count; ++i) {
        if ((s32)i != desktop->active_window) {
            draw_window(&desktop->framebuffer, &desktop->windows[i], 0);
        }
    }
    if (desktop->active_window >= 0
        && (u32)desktop->active_window < desktop->window_count) {
        draw_window(
            &desktop->framebuffer,
            &desktop->windows[desktop->active_window],
            1
        );
    }
}

static int point_in_window(
    const graphics_window *window,
    s32 x,
    s32 y
)
{
    return window->visible
        && x >= window->x && y >= window->y
        && x < window->x + (s32)window->width
        && y < window->y + (s32)window->height;
}

static void pointer_event(
    graphics_desktop *desktop,
    s32 x,
    s32 y,
    u32 left_button_down
)
{
    graphics_pointer *pointer = &desktop->pointer;
    pointer->x = x;
    pointer->y = y;
    if (left_button_down && !pointer->pressed) {
        for (s32 i = (s32)desktop->window_count - 1; i >= 0; --i) {
            graphics_window *window = &desktop->windows[i];
            if (!point_in_window(window, x, y)) {
                continue;
            }
            desktop->active_window = i;
            if (y < window->y + 24
                && x >= window->x + (s32)window->width - 20) {
                window->visible = 0;
                pointer->dragging_window = -1;
            } else if (y < window->y + 24) {
                pointer->dragging_window = i;
                pointer->drag_x = x - window->x;
                pointer->drag_y = y - window->y;
            }
            break;
        }
    } else if (left_button_down && pointer->pressed
        && pointer->dragging_window >= 0) {
        graphics_window *window =
            &desktop->windows[pointer->dragging_window];
        s32 maximum_x = (s32)desktop->framebuffer.width
            - (s32)window->width;
        s32 maximum_y = (s32)desktop->framebuffer.height
            - (s32)window->height;
        window->x = x - pointer->drag_x;
        window->y = y - pointer->drag_y;
        if (window->x < 0) window->x = 0;
        if (window->y < 0) window->y = 0;
        if (window->x > maximum_x) window->x = maximum_x;
        if (window->y > maximum_y) window->y = maximum_y;
    } else if (!left_button_down) {
        pointer->dragging_window = -1;
    }
    pointer->pressed = left_button_down;
}

static int framebuffer_valid(const os_framebuffer *framebuffer)
{
    if (framebuffer == (const os_framebuffer *)0
        || framebuffer->pixels == (volatile u32 *)0
        || framebuffer->width < GRAPHICS_MIN_WIDTH
        || framebuffer->height < GRAPHICS_MIN_HEIGHT
        || framebuffer->pixels_per_scan_line < framebuffer->width
        || framebuffer->pixel_format > GRAPHICS_PIXEL_BGR) {
        return 0;
    }
    usize required_pixels = (usize)framebuffer->height
        * framebuffer->pixels_per_scan_line;
    if ((framebuffer->height != 0
            && required_pixels / framebuffer->height
                != framebuffer->pixels_per_scan_line)
        || required_pixels > framebuffer->framebuffer_size / sizeof(u32)) {
        return 0;
    }
    return 1;
}

int os_graphics_framebuffer_valid(const os_framebuffer *framebuffer)
{
    return framebuffer_valid(framebuffer);
}

u32 os_graphics_pixel_value(u32 color, u32 pixel_format)
{
    if (pixel_format > GRAPHICS_PIXEL_BGR) {
        return 0;
    }
    return encode_pixel(color, pixel_format);
}

u32 os_graphics_read_pixel(
    const os_framebuffer *framebuffer,
    u32 x,
    u32 y
)
{
    if (!framebuffer_valid(framebuffer)
        || x >= framebuffer->width || y >= framebuffer->height) {
        return 0;
    }
    return get_pixel(framebuffer, x, y);
}

void os_graphics_fill_rect(
    const os_framebuffer *framebuffer,
    s32 x,
    s32 y,
    u32 width,
    u32 height,
    u32 color
)
{
    if (framebuffer_valid(framebuffer)) {
        fill_rect(framebuffer, x, y, width, height, color);
    }
}

void os_graphics_draw_text(
    const os_framebuffer *framebuffer,
    s32 x,
    s32 y,
    const char *text,
    u32 color
)
{
    if (framebuffer_valid(framebuffer) && text != (const char *)0) {
        draw_text(framebuffer, x, y, text, color);
    }
}

int os_graphics_self_test(const os_framebuffer *framebuffer)
{
    if (!framebuffer_valid(framebuffer)) {
        return 0;
    }
    graphics_desktop desktop = {
        .framebuffer = *framebuffer,
        .windows = {
            {32, 40, 240, 150, 1, 0x002f9f80u},
            {110, 100, 200, 130, 1, 0x00d09030u},
        },
        .window_count = 2,
        .active_window = 1,
        .pointer = {.dragging_window = -1},
    };
    render_desktop(&desktop);
    if (get_pixel(framebuffer, 10, 10)
            != encode_pixel(GRAPHICS_COLOR_DESKTOP, framebuffer->pixel_format)
        || get_pixel(framebuffer, 120, 105)
            != encode_pixel(
                GRAPHICS_COLOR_ACTIVE_TITLE, framebuffer->pixel_format)
        || get_pixel(framebuffer, 130, 162)
            != encode_pixel(0x00d09030u, framebuffer->pixel_format)) {
        return 0;
    }

    pointer_event(&desktop, 140, 110, 1);
    pointer_event(&desktop, 160, 125, 1);
    pointer_event(&desktop, 160, 125, 0);
    if (desktop.windows[1].x != 130 || desktop.windows[1].y != 115
        || desktop.pointer.dragging_window != -1) {
        return 0;
    }
    pointer_event(&desktop, 320, 120, 1);
    pointer_event(&desktop, 320, 120, 0);
    if (desktop.windows[1].visible != 0) {
        return 0;
    }
    desktop.active_window = 0;
    render_desktop(&desktop);
    return get_pixel(framebuffer, 50, 45)
            == encode_pixel(
                GRAPHICS_COLOR_ACTIVE_TITLE, framebuffer->pixel_format)
        && get_pixel(framebuffer, 10, 10)
            == encode_pixel(GRAPHICS_COLOR_DESKTOP, framebuffer->pixel_format)
        && get_pixel(framebuffer, 190, 160)
            == encode_pixel(
                GRAPHICS_COLOR_WINDOW, framebuffer->pixel_format);
}

int os_graphics_prepare_guest(efi_boot_services *boot_services)
{
    static const efi_guid graphics_protocol_guid = {
        0x9042a9deu, 0x23dcu, 0x4a38u,
        {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a},
    };
    if (boot_services == (efi_boot_services *)0
        || boot_services->locate_protocol == (efi_locate_protocol)0) {
        return 0;
    }
    efi_graphics_output_protocol *graphics = (efi_graphics_output_protocol *)0;
    if (boot_services->locate_protocol(
            &graphics_protocol_guid, (void *)0, (void **)&graphics) != 0
        || graphics == (efi_graphics_output_protocol *)0
        || graphics->mode == (efi_graphics_output_mode *)0
        || graphics->mode->information
            == (efi_graphics_output_mode_information *)0) {
        return 0;
    }
    efi_graphics_output_mode_information *information =
        graphics->mode->information;
    os_framebuffer framebuffer = {
        .pixels = (volatile u32 *)(usize)graphics->mode->framebuffer_base,
        .width = information->horizontal_resolution,
        .height = information->vertical_resolution,
        .pixels_per_scan_line = information->pixels_per_scan_line,
        .pixel_format = information->pixel_format,
        .framebuffer_size = graphics->mode->framebuffer_size,
    };
    if (!framebuffer_valid(&framebuffer)) {
        return 0;
    }
    g_guest_framebuffer = framebuffer;
    g_guest_framebuffer_ready = 1;
    return 1;
}

int os_graphics_guest_self_test(void)
{
    return g_guest_framebuffer_ready
        && os_graphics_self_test(&g_guest_framebuffer);
}

int os_graphics_guest_framebuffer(os_framebuffer *framebuffer)
{
    if (!g_guest_framebuffer_ready
        || framebuffer == (os_framebuffer *)0) {
        return 0;
    }
    *framebuffer = g_guest_framebuffer;
    return 1;
}
