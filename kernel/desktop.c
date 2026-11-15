#include "desktop.h"
#include "process.h"

#ifdef EXPERIMENTALOS_DESKTOP_TEST_DIAGNOSTICS
#include <stdio.h>
#endif

#define DESKTOP_APP_COUNT 3u
#define DESKTOP_TASKBAR_HEIGHT 40u
#define DESKTOP_CODE_BASE 0x200000000ull
#define DESKTOP_ROOT_BASE 0x1000000000ull
#define DESKTOP_APP_STRIDE 0x10000ull
#define DESKTOP_PROCESS_SPACE_SIZE (3u * OS_PAGE_SIZE)
#define DESKTOP_COLOR_DESKTOP 0x0018202au
#define DESKTOP_COLOR_TASKBAR 0x00242a32u
#define DESKTOP_COLOR_LAUNCHER 0x00dce2e8u
#define DESKTOP_COLOR_WINDOW 0x00e3e8edu
#define DESKTOP_COLOR_TITLE 0x00304f6au
#define DESKTOP_COLOR_ACTIVE_TITLE 0x004080b0u
#define DESKTOP_COLOR_TEXT 0x00101820u
#define DESKTOP_COLOR_CLOSE 0x00c94343u

typedef enum {
    DESKTOP_APP_NONE = 0,
    DESKTOP_APP_CONSOLE,
    DESKTOP_APP_FILES,
    DESKTOP_APP_SETTINGS
} desktop_app_kind;

typedef struct {
    u32 window_id;
    u32 process_id;
    u32 thread_id;
    desktop_app_kind app;
    s32 x;
    s32 y;
    u32 width;
    u32 height;
    int visible;
} desktop_window;

typedef struct {
    const os_framebuffer *framebuffer;
    os_process_system processes;
    u32 root_process;
    u32 next_window_id;
    u32 window_count;
    int active_slot;
    int launcher_open;
    desktop_window windows[DESKTOP_APP_COUNT];
} classic_desktop;

typedef struct {
    u32 desktop;
    u32 taskbar;
    u32 launcher;
    u32 window;
    u32 title;
    u32 active_title;
    u32 text;
    u32 title_text;
    u32 close;
    u32 divider;
} desktop_theme;

static const desktop_theme g_classic_theme = {
    .desktop = 0x0018202au,
    .taskbar = 0x00242a32u,
    .launcher = 0x00dce2e8u,
    .window = 0x00e3e8edu,
    .title = 0x00304f6au,
    .active_title = 0x004080b0u,
    .text = 0x00101820u,
    .title_text = 0x00ffffffu,
    .close = 0x00c94343u,
    .divider = 0x00b8c2ccu,
};

static const desktop_theme g_flow_light_theme = {
    .desktop = 0x00c7d9e8u,
    .taskbar = 0x0030475au,
    .launcher = 0x00eaf0f4u,
    .window = 0x00f1f4f7u,
    .title = 0x00536e82u,
    .active_title = 0x003f83a8u,
    .text = 0x00101820u,
    .title_text = 0x00ffffffu,
    .close = 0x00c94343u,
    .divider = 0x00b2c2ccu,
};

static const desktop_theme g_flow_dark_theme = {
    .desktop = 0x00171d27u,
    .taskbar = 0x00252f3du,
    .launcher = 0x00313d4cu,
    .window = 0x00394350u,
    .title = 0x004c5d70u,
    .active_title = 0x00627fa0u,
    .text = 0x00e5edf4u,
    .title_text = 0x00ffffffu,
    .close = 0x00c94343u,
    .divider = 0x00505d6au,
};

static classic_desktop g_classic_desktop;

static int desktop_test_failure(u32 stage)
{
#ifdef EXPERIMENTALOS_DESKTOP_TEST_DIAGNOSTICS
    fprintf(stderr, "Desktop self-test failed at stage %u\n", stage);
#else
    (void)stage;
#endif
    return 0;
}

static void draw_app_window(
    classic_desktop *desktop,
    const desktop_window *window,
    const desktop_theme *theme,
    int active
)
{
    const os_framebuffer *framebuffer = desktop->framebuffer;
    const char *title = window->app == DESKTOP_APP_CONSOLE
        ? "Console"
        : window->app == DESKTOP_APP_FILES ? "File Manager" : "Settings";
    const char *content = window->app == DESKTOP_APP_CONSOLE
        ? "Ready"
        : window->app == DESKTOP_APP_FILES ? "Documents" : "Display";
    os_graphics_fill_rect(
        framebuffer, window->x, window->y,
        window->width, window->height, theme->window);
    os_graphics_fill_rect(
        framebuffer, window->x, window->y,
        window->width, 24,
        active ? theme->active_title : theme->title);
    os_graphics_fill_rect(
        framebuffer, window->x + (s32)window->width - 20,
        window->y + 3, 16, 16, theme->close);
    os_graphics_draw_text(
        framebuffer, window->x + 8, window->y + 8,
        title, theme->title_text);
    os_graphics_draw_text(
        framebuffer, window->x + 16, window->y + 48,
        content, theme->text);
    os_graphics_fill_rect(
        framebuffer, window->x + 12, window->y + 34,
        window->width - 24, 1, theme->divider);
}

static void render_shell(
    classic_desktop *desktop,
    const desktop_theme *theme,
    int flow
)
{
    const os_framebuffer *framebuffer = desktop->framebuffer;
    os_graphics_fill_rect(
        framebuffer, 0, 0, framebuffer->width, framebuffer->height,
        theme->desktop);

    for (u32 slot = 0; slot < DESKTOP_APP_COUNT; ++slot) {
        desktop_window *window = &desktop->windows[slot];
        if (window->visible && (int)slot != desktop->active_slot) {
            draw_app_window(desktop, window, theme, 0);
        }
    }
    if (desktop->active_slot >= 0
        && (u32)desktop->active_slot < DESKTOP_APP_COUNT) {
        desktop_window *window = &desktop->windows[desktop->active_slot];
        if (window->visible) {
            draw_app_window(desktop, window, theme, 1);
        }
    }

    if (flow) {
        s32 dock_x = ((s32)framebuffer->width - 414) / 2;
        s32 dock_y = (s32)framebuffer->height - 52;
        os_graphics_fill_rect(
            framebuffer, dock_x, dock_y, 414, 44, theme->taskbar);
        for (u32 slot = 0; slot < DESKTOP_APP_COUNT; ++slot) {
            s32 button_x = dock_x + 10 + (s32)slot * 132;
            u32 button_color = desktop->active_slot == (int)slot
                ? theme->active_title : theme->title;
            const char *label = slot == 0 ? "Console"
                : slot == 1 ? "Files" : "Settings";
            os_graphics_fill_rect(
                framebuffer, button_x, dock_y + 5, 120, 34, button_color);
            os_graphics_draw_text(
                framebuffer, button_x + 12, dock_y + 18,
                label, theme->title_text);
        }
    } else {
        s32 taskbar_y = (s32)framebuffer->height - DESKTOP_TASKBAR_HEIGHT;
        os_graphics_fill_rect(
            framebuffer, 0, taskbar_y,
            framebuffer->width, DESKTOP_TASKBAR_HEIGHT, theme->taskbar);
        os_graphics_fill_rect(
            framebuffer, 8, taskbar_y + 6, 96, 28, theme->active_title);
        os_graphics_draw_text(
            framebuffer, 18, taskbar_y + 16, "Classic", theme->title_text);
        s32 label_x = 120;
        for (u32 slot = 0; slot < DESKTOP_APP_COUNT; ++slot) {
            if (desktop->windows[slot].visible) {
                const char *label = slot == 0 ? "Console"
                    : slot == 1 ? "Files" : "Settings";
                os_graphics_draw_text(
                    framebuffer, label_x, taskbar_y + 16,
                    label, theme->title_text);
                label_x += 112;
            }
        }
    }

    if (desktop->launcher_open && !flow) {
        s32 panel_y = (s32)framebuffer->height - 224;
        os_graphics_fill_rect(
            framebuffer, 8, panel_y, 248, 216, theme->launcher);
        os_graphics_fill_rect(
            framebuffer, 8, panel_y, 248, 26, theme->active_title);
        os_graphics_draw_text(
            framebuffer, 18, panel_y + 9,
            "Applications", theme->title_text);
        os_graphics_draw_text(
            framebuffer, 22, panel_y + 52, "Console", theme->text);
        os_graphics_draw_text(
            framebuffer, 22, panel_y + 92, "File Manager", theme->text);
        os_graphics_draw_text(
            framebuffer, 22, panel_y + 132, "Settings", theme->text);
    }
}

static int process_has_region(
    const os_process *process,
    u64 address,
    u32 permissions
)
{
    for (usize i = 0; i < process->address_space.region_count; ++i) {
        const os_virtual_region *region = &process->address_space.regions[i];
        if (region->base == address
            && region->owner == process->id
            && region->permissions == permissions) {
            return 1;
        }
    }
    return 0;
}

static os_process *find_process(
    os_process_system *system,
    u32 process_id
)
{
    for (usize i = 0; i < OS_PROCESS_MAX_PROCESSES; ++i) {
        os_process *process = &system->processes[i];
        if (process->id == process_id
            && process->state != OS_PROCESS_FREE) {
            return process;
        }
    }
    return (os_process *)0;
}

static int process_has_thread(
    const os_process_system *system,
    u32 process_id,
    u32 thread_id
)
{
    for (usize i = 0; i < OS_PROCESS_MAX_THREADS; ++i) {
        const os_thread *thread = &system->threads[i];
        if (thread->id == thread_id
            && thread->process_id == process_id
            && (thread->state == OS_THREAD_RUNNABLE
                || thread->state == OS_THREAD_RUNNING)) {
            return 1;
        }
    }
    return 0;
}

static int cleanup_process(
    classic_desktop *desktop,
    desktop_window *window
)
{
    int was_visible = window->visible;
    if (was_visible
        && (window->app == DESKTOP_APP_NONE
            || desktop->window_count == 0)) {
        return 0;
    }
    if (!os_process_terminate(
            &desktop->processes, window->process_id, 0)) {
        return 0;
    }
    int exit_code = -1;
    if (!os_process_reap(
            &desktop->processes,
            desktop->root_process,
            window->process_id,
            &exit_code)
        || exit_code != 0) {
        return 0;
    }
    window->visible = 0;
    if (was_visible) {
        --desktop->window_count;
    }
    window->process_id = 0;
    window->thread_id = 0;
    window->window_id = 0;
    window->app = DESKTOP_APP_NONE;
    return 1;
}

static int launch_app(
    classic_desktop *desktop,
    desktop_app_kind app
)
{
    u32 slot = (u32)app - 1u;
    if (slot >= DESKTOP_APP_COUNT) {
        return 0;
    }
    desktop_window *window = &desktop->windows[slot];
    if (window->visible) {
        desktop->active_slot = (int)slot;
        return 1;
    }
    u64 base = DESKTOP_CODE_BASE + (u64)slot * DESKTOP_APP_STRIDE;
    u32 process_id = os_process_create(
        &desktop->processes,
        desktop->root_process,
        base,
        DESKTOP_PROCESS_SPACE_SIZE
    );
    if (process_id == 0) {
        return 0;
    }
    window->process_id = process_id;
    os_process *process = find_process(&desktop->processes, process_id);
    if (process == (os_process *)0) {
        (void)cleanup_process(desktop, window);
        return 0;
    }
    if (!os_virtual_reserve(
            &process->address_space, base, OS_PAGE_SIZE, process_id,
            OS_VM_READ | OS_VM_EXECUTE)
        || !os_virtual_reserve(
            &process->address_space, base + OS_PAGE_SIZE, OS_PAGE_SIZE,
            process_id, OS_VM_READ | OS_VM_WRITE)
        || !os_virtual_reserve(
            &process->address_space, base + 2u * OS_PAGE_SIZE, OS_PAGE_SIZE,
            process_id, OS_VM_READ | OS_VM_WRITE)) {
        (void)cleanup_process(desktop, window);
        return 0;
    }
    u32 thread_id = os_thread_create(&desktop->processes, process_id);
    if (thread_id == 0) {
        (void)cleanup_process(desktop, window);
        return 0;
    }

    window->window_id = desktop->next_window_id++;
    window->process_id = process_id;
    window->thread_id = thread_id;
    window->app = app;
    window->x = app == DESKTOP_APP_CONSOLE ? 32
        : app == DESKTOP_APP_FILES ? 112 : 204;
    window->y = app == DESKTOP_APP_CONSOLE ? 48
        : app == DESKTOP_APP_FILES ? 88 : 120;
    window->width = app == DESKTOP_APP_SETTINGS ? 280u : 260u;
    window->height = app == DESKTOP_APP_SETTINGS ? 200u : 180u;
    window->visible = 1;
    ++desktop->window_count;
    desktop->active_slot = (int)slot;
    if (os_scheduler_next(&desktop->processes) == 0) {
        (void)cleanup_process(desktop, window);
        desktop->active_slot = -1;
        return 0;
    }
    return 1;
}

static int handle_key(classic_desktop *desktop, u16 key)
{
    if (key == 'l') {
        desktop->launcher_open = !desktop->launcher_open;
    } else if (key == 'c' && desktop->launcher_open) {
        desktop->launcher_open = 0;
        return launch_app(desktop, DESKTOP_APP_CONSOLE);
    } else if (key == 'f' && desktop->launcher_open) {
        desktop->launcher_open = 0;
        return launch_app(desktop, DESKTOP_APP_FILES);
    } else if (key == 's' && desktop->launcher_open) {
        desktop->launcher_open = 0;
        return launch_app(desktop, DESKTOP_APP_SETTINGS);
    } else if (key == 'r' && desktop->active_slot >= 0) {
        desktop_window *window = &desktop->windows[desktop->active_slot];
        if (window->visible) {
            window->width += 40u;
            window->height += 30u;
            if (window->width > desktop->framebuffer->width - 16u) {
                window->width = desktop->framebuffer->width - 16u;
            }
            if (window->height
                > desktop->framebuffer->height - DESKTOP_TASKBAR_HEIGHT - 8u) {
                window->height = desktop->framebuffer->height
                    - DESKTOP_TASKBAR_HEIGHT - 8u;
            }
        }
    } else if (key == 'x' && desktop->active_slot >= 0) {
        desktop_window *window = &desktop->windows[desktop->active_slot];
        if (window->visible && !cleanup_process(desktop, window)) {
            return 0;
        }
        desktop->active_slot = -1;
        for (u32 slot = 0; slot < DESKTOP_APP_COUNT; ++slot) {
            if (desktop->windows[slot].visible) {
                desktop->active_slot = (int)slot;
            }
        }
    } else {
        return 0;
    }
    return 1;
}

static int handle_flow_key(
    classic_desktop *desktop,
    u16 key,
    int *dark_theme
)
{
    if (key >= '1' && key <= '3') {
        return launch_app(
            desktop, (desktop_app_kind)(key - '0'));
    }
    if (key == 't' && dark_theme != (int *)0) {
        *dark_theme = !*dark_theme;
        return 1;
    }
    if (key == 'r' || key == 'x') {
        return handle_key(desktop, key);
    }
    return 0;
}

static int initialize_desktop(
    classic_desktop *desktop,
    const os_framebuffer *framebuffer
)
{
    os_process_system_init(&desktop->processes);
    desktop->framebuffer = framebuffer;
    desktop->root_process = 0;
    desktop->next_window_id = 1;
    desktop->window_count = 0;
    desktop->active_slot = -1;
    desktop->launcher_open = 0;
    for (u32 slot = 0; slot < DESKTOP_APP_COUNT; ++slot) {
        desktop->windows[slot].window_id = 0;
        desktop->windows[slot].process_id = 0;
        desktop->windows[slot].thread_id = 0;
        desktop->windows[slot].app = DESKTOP_APP_NONE;
        desktop->windows[slot].x = 0;
        desktop->windows[slot].y = 0;
        desktop->windows[slot].width = 0;
        desktop->windows[slot].height = 0;
        desktop->windows[slot].visible = 0;
    }
    desktop->root_process = os_process_create(
        &desktop->processes, 0, DESKTOP_ROOT_BASE,
        16u * DESKTOP_PROCESS_SPACE_SIZE);
    return desktop->root_process != 0;
}

static int all_app_slots_free(const classic_desktop *desktop)
{
    for (usize i = 0; i < OS_PROCESS_MAX_PROCESSES; ++i) {
        if (desktop->processes.processes[i].state != OS_PROCESS_FREE) {
            return 0;
        }
    }
    for (usize i = 0; i < OS_PROCESS_MAX_THREADS; ++i) {
        if (desktop->processes.threads[i].state != OS_THREAD_FREE) {
            return 0;
        }
    }
    return 1;
}

int os_classic_desktop_self_test(
    const os_framebuffer *framebuffer,
    u16 activation_key
)
{
    if (!os_graphics_framebuffer_valid(framebuffer)
        || framebuffer->height < 420u) {
        return desktop_test_failure(1);
    }
    if (activation_key != 'd') {
        return 0;
    }
    classic_desktop *desktop = &g_classic_desktop;
    if (!initialize_desktop(desktop, framebuffer)) {
        return desktop_test_failure(2);
    }

    render_shell(desktop, &g_classic_theme, 0);
    if (os_graphics_read_pixel(
            framebuffer, 2, framebuffer->height - 2u)
        != os_graphics_pixel_value(
            DESKTOP_COLOR_TASKBAR, framebuffer->pixel_format)) {
        return desktop_test_failure(3);
    }
    if (!handle_key(desktop, 'l')) {
        return desktop_test_failure(4);
    }
    render_shell(desktop, &g_classic_theme, 0);
    if (os_graphics_read_pixel(
            framebuffer, 12, framebuffer->height - 190u)
        != os_graphics_pixel_value(
            DESKTOP_COLOR_LAUNCHER, framebuffer->pixel_format)) {
        return desktop_test_failure(5);
    }

    os_process *console_process = (os_process *)0;
    if (!handle_key(desktop, 'c')
        || !desktop->windows[0].visible
        || (console_process = find_process(
            &desktop->processes, desktop->windows[0].process_id))
            == (os_process *)0
        || console_process->live_threads != 1u
        || !process_has_thread(
            &desktop->processes,
            desktop->windows[0].process_id,
            desktop->windows[0].thread_id)
        || !process_has_region(
            console_process, DESKTOP_CODE_BASE,
            OS_VM_READ | OS_VM_EXECUTE)) {
        return desktop_test_failure(6);
    }
    render_shell(desktop, &g_classic_theme, 0);
    if (!handle_key(desktop, 'l')
        || !handle_key(desktop, 'f')
        || !handle_key(desktop, 'l')
        || !handle_key(desktop, 's')
        || desktop->window_count != 3u
        || desktop->active_slot != (int)DESKTOP_APP_SETTINGS - 1) {
        return desktop_test_failure(7);
    }
    os_process *settings_process = find_process(
        &desktop->processes, desktop->windows[2].process_id);
    if (settings_process == (os_process *)0
        || settings_process->live_threads != 1u
        || !process_has_thread(
            &desktop->processes,
            desktop->windows[2].process_id,
            desktop->windows[2].thread_id)
        || !process_has_region(
            settings_process, DESKTOP_CODE_BASE + 2u * DESKTOP_APP_STRIDE,
            OS_VM_READ | OS_VM_EXECUTE)) {
        return desktop_test_failure(8);
    }
    if (!handle_key(desktop, 'r')
        || desktop->windows[2].width != 320u
        || desktop->windows[2].height != 230u) {
        return desktop_test_failure(9);
    }
    render_shell(desktop, &g_classic_theme, 0);
    if (os_graphics_read_pixel(
            framebuffer,
            (u32)desktop->windows[2].x + 4u,
            (u32)desktop->windows[2].y + 4u)
        != os_graphics_pixel_value(
            DESKTOP_COLOR_ACTIVE_TITLE, framebuffer->pixel_format)) {
        return desktop_test_failure(10);
    }

    if (!handle_key(desktop, 'x')) {
        return desktop_test_failure(111);
    }
    if (desktop->windows[2].visible) {
        return desktop_test_failure(112);
    }
    if (!handle_key(desktop, 'x')) {
        return desktop_test_failure(113);
    }
    if (desktop->windows[1].visible) {
        return desktop_test_failure(114);
    }
    if (!handle_key(desktop, 'x')) {
        return desktop_test_failure(115);
    }
    if (desktop->windows[0].visible) {
        return desktop_test_failure(116);
    }
    if (desktop->window_count != 0) {
        return desktop_test_failure(117);
    }
    render_shell(desktop, &g_classic_theme, 0);
    if (!os_process_terminate(
            &desktop->processes, desktop->root_process, 0)) {
        return desktop_test_failure(12);
    }
    int root_exit_code = -1;
    if (!os_process_reap(
            &desktop->processes, 0, desktop->root_process, &root_exit_code)
        || root_exit_code != 0
        || !all_app_slots_free(desktop)) {
        return desktop_test_failure(13);
    }
    return 1;
}

int os_flow_desktop_self_test(
    const os_framebuffer *framebuffer,
    u16 activation_key
)
{
    if (!os_graphics_framebuffer_valid(framebuffer)
        || framebuffer->height < 420u) {
        return desktop_test_failure(201);
    }
    if (activation_key != 'f') {
        return 0;
    }
    classic_desktop *desktop = &g_classic_desktop;
    if (!initialize_desktop(desktop, framebuffer)) {
        return desktop_test_failure(202);
    }
    int dark_theme = 0;
    render_shell(desktop, &g_flow_light_theme, 1);
    s32 dock_x = ((s32)framebuffer->width - 414) / 2;
    s32 dock_y = (s32)framebuffer->height - 52;
    if (os_graphics_read_pixel(framebuffer, 2, 2)
            != os_graphics_pixel_value(
                g_flow_light_theme.desktop, framebuffer->pixel_format)
        || os_graphics_read_pixel(
            framebuffer, (u32)dock_x + 2u, (u32)dock_y + 2u)
            != os_graphics_pixel_value(
                g_flow_light_theme.taskbar, framebuffer->pixel_format)) {
        return desktop_test_failure(203);
    }
    if (!handle_flow_key(desktop, '1', &dark_theme)
        || !handle_flow_key(desktop, '2', &dark_theme)
        || !handle_flow_key(desktop, '3', &dark_theme)
        || desktop->window_count != DESKTOP_APP_COUNT
        || desktop->active_slot != (int)DESKTOP_APP_SETTINGS - 1) {
        return desktop_test_failure(204);
    }
    u32 files_process_id = desktop->windows[1].process_id;
    u32 files_window_id = desktop->windows[1].window_id;
    if (!handle_flow_key(desktop, '2', &dark_theme)
        || desktop->active_slot != (int)DESKTOP_APP_FILES - 1
        || desktop->windows[1].process_id != files_process_id
        || desktop->windows[1].window_id != files_window_id
        || !handle_flow_key(desktop, '3', &dark_theme)
        || !handle_flow_key(desktop, '2', &dark_theme)
        || desktop->active_slot != (int)DESKTOP_APP_FILES - 1
        || desktop->window_count != DESKTOP_APP_COUNT) {
        return desktop_test_failure(205);
    }
    if (!handle_flow_key(desktop, 't', &dark_theme) || !dark_theme) {
        return desktop_test_failure(206);
    }
    render_shell(desktop, &g_flow_dark_theme, 1);
    if (os_graphics_read_pixel(framebuffer, 2, 2)
            != os_graphics_pixel_value(
                g_flow_dark_theme.desktop, framebuffer->pixel_format)
        || os_graphics_read_pixel(
            framebuffer, (u32)dock_x + 2u, (u32)dock_y + 2u)
            != os_graphics_pixel_value(
                g_flow_dark_theme.taskbar, framebuffer->pixel_format)
        || os_graphics_read_pixel(
            framebuffer, (u32)dock_x + 10u + 132u + 2u,
            (u32)dock_y + 7u)
            != os_graphics_pixel_value(
                g_flow_dark_theme.active_title, framebuffer->pixel_format)) {
        return desktop_test_failure(207);
    }
    if (!handle_flow_key(desktop, 'r', &dark_theme)
        || desktop->windows[1].width != 300u
        || desktop->windows[1].height != 210u
        || handle_flow_key(desktop, 'z', &dark_theme)) {
        return desktop_test_failure(208);
    }
    render_shell(desktop, &g_flow_dark_theme, 1);
    if (os_graphics_read_pixel(
            framebuffer,
            (u32)desktop->windows[1].x + 4u,
            (u32)desktop->windows[1].y + 4u)
        != os_graphics_pixel_value(
            g_flow_dark_theme.active_title, framebuffer->pixel_format)) {
        return desktop_test_failure(209);
    }
    if (!handle_flow_key(desktop, 'x', &dark_theme)
        || desktop->windows[1].visible
        || desktop->window_count != 2u
        || !handle_flow_key(desktop, 'x', &dark_theme)
        || desktop->windows[2].visible
        || !handle_flow_key(desktop, 'x', &dark_theme)
        || desktop->windows[0].visible
        || desktop->window_count != 0) {
        return desktop_test_failure(210);
    }
    render_shell(desktop, &g_flow_dark_theme, 1);
    if (!os_process_terminate(
            &desktop->processes, desktop->root_process, 0)) {
        return desktop_test_failure(211);
    }
    int root_exit_code = -1;
    if (!os_process_reap(
            &desktop->processes, 0, desktop->root_process, &root_exit_code)
        || root_exit_code != 0
        || !all_app_slots_free(desktop)) {
        return desktop_test_failure(212);
    }
    return 1;
}
