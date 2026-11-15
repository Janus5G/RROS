#include "efi.h"
#include "gfx.h"

u64 arch_timer_now(void);

/*
 * Guest self-test of the G19 pipeline. It runs the same damage-tracked and
 * full-repaint workloads inside the guest, checks the presented frames are
 * identical, and reports pixel counts plus raw counter ticks. The ticks come
 * from the guest architectural counter under emulation, so they are a relative
 * measure only and not a Raspberry Pi 5 hardware number.
 */

#define W 256u
#define H 160u

static uint32_t mem_damage[3u * W * H];
static uint32_t mem_full[3u * W * H];
static gfx_system sys_damage, sys_full;
static char report[160];
static uint32_t seed = 7u;

static uint32_t rnd(void)
{
    seed = seed * 1664525u + 1013904223u;
    return seed >> 8;
}

static void put_u64(char **p, u64 v)
{
    char t[24];
    unsigned n = 0;
    do { t[n++] = (char)('0' + (unsigned)(v % 10u)); v /= 10u; } while (v);
    while (n) *(*p)++ = t[--n];
}

static void put_s(char **p, const char *s)
{
    while (*s) *(*p)++ = *s++;
}

static gfx_rect rect(int x0, int y0, int x1, int y1)
{
    gfx_rect r;
    r.x0 = x0; r.y0 = y0; r.x1 = x1; r.y1 = y1;
    return r;
}

static void present(gfx_system *s, int full)
{
    uint64_t fence;
    if (full) gfx_system_damage_all(s, 0);
    gfx_system_render(s, 0, &fence);
    gfx_output_vblank(&s->out[0]);
}

static int same(void)
{
    const uint32_t *a = gfx_output_scanout(&sys_damage.out[0]);
    const uint32_t *b = gfx_output_scanout(&sys_full.out[0]);
    if (!a || !b) return 0;
    for (uint32_t i = 0; i < W * H; i++) if (a[i] != b[i]) return 0;
    return 1;
}

const char *os_gfx_guest_self_test(void)
{
    int ca[3], cb[3];
    u64 t0, t1, t2;
    u64 damage_pixels, full_pixels;
    char *p = report;
    gfx_surface target;
    static uint32_t tpix[32 * 32];
    gfx_cmd cmd[2];
    gfx_exec_stats st;
    int used_fallback = 0;

    gfx_system_init(&sys_damage, 0x00202020u);
    gfx_system_init(&sys_full, 0x00202020u);
    if (gfx_system_add_output(&sys_damage, 0, 0, 0, W, H, mem_damage, sizeof mem_damage / sizeof mem_damage[0]) != GFX_OK ||
        gfx_system_add_output(&sys_full, 0, 0, 0, W, H, mem_full, sizeof mem_full / sizeof mem_full[0]) != GFX_OK)
        return "output setup";
    ca[0] = gfx_layer_add(&sys_damage, rect(10, 10, 150, 100), 0x00334455u, 0); cb[0] = gfx_layer_add(&sys_full, rect(10, 10, 150, 100), 0x00334455u, 0);
    ca[1] = gfx_layer_add(&sys_damage, rect(80, 50, 240, 150), 0x00553322u, 0); cb[1] = gfx_layer_add(&sys_full, rect(80, 50, 240, 150), 0x00553322u, 0);
    ca[2] = gfx_layer_add(&sys_damage, rect(0, 0, 12, 12), 0x00FFFFFFu, 0); cb[2] = gfx_layer_add(&sys_full, rect(0, 0, 12, 12), 0x00FFFFFFu, 0);
    if (ca[0] < 0 || ca[1] < 0 || ca[2] < 0 || cb[2] < 0) return "layer setup";
    present(&sys_damage, 0); present(&sys_full, 1);
    if (!same()) return "first frame mismatch";
    for (unsigned i = 0; i < 80; i++) {
        unsigned l = rnd() % 3u;
        int x = (int)(rnd() % 240u) - 20, y = (int)(rnd() % 150u) - 10;
        gfx_layer_move(&sys_damage, ca[l], x, y);
        gfx_layer_move(&sys_full, cb[l], x, y);
        if (i % 5u == 2u) {
            uint64_t f;
            gfx_system_render(&sys_damage, 0, &f); /* superseded before vblank */
            gfx_layer_move(&sys_damage, ca[2], x / 2, y / 2);
            gfx_layer_move(&sys_full, cb[2], x / 2, y / 2);
        }
        present(&sys_damage, 0); present(&sys_full, 1);
        if (!same()) return "damage-tracked frame differs from full repaint";
    }
    sys_damage.out[0].pixels_written = 0;
    sys_full.out[0].pixels_written = 0;
    t0 = arch_timer_now();
    for (unsigned i = 0; i < 200u; i++) {
        gfx_layer_move(&sys_damage, ca[2], (int)((i * 7u) % 240u), (int)((i * 5u) % 140u));
        present(&sys_damage, 0);
    }
    t1 = arch_timer_now();
    for (unsigned i = 0; i < 200u; i++) {
        gfx_layer_move(&sys_full, cb[2], (int)((i * 7u) % 240u), (int)((i * 5u) % 140u));
        present(&sys_full, 1);
    }
    t2 = arch_timer_now();
    damage_pixels = sys_damage.out[0].pixels_written;
    full_pixels = sys_full.out[0].pixels_written;
    if (!same()) return "workload frames differ";
    if (damage_pixels * 10u >= full_pixels) return "damage tracking did not reduce pixel writes 10x";
    target.pixels = tpix; target.width = 32; target.height = 32; target.stride = 32;
    cmd[0].op = GFX_CMD_CLEAR; cmd[0].color = 5u;
    cmd[1].op = GFX_CMD_RECT; cmd[1].a[0] = 4; cmd[1].a[1] = 4; cmd[1].a[2] = 8; cmd[1].a[3] = 8; cmd[1].color = 9u;
    for (unsigned i = 0; i < 6; i++) { cmd[0].a[i] = 0; if (i > 3) cmd[1].a[i] = 0; }
    if (gfx_gpu_submit(&gfx_backend_v3d_unavailable, 0, &target, cmd, 2, &st, &used_fallback) != GFX_ERR_NO_HARDWARE) return "v3d must fail closed";
    if (gfx_gpu_submit(&gfx_backend_v3d_unavailable, &gfx_backend_software, &target, cmd, 2, &st, &used_fallback) != GFX_OK ||
        !used_fallback || tpix[5 * 32 + 5] != 9u || tpix[0] != 5u) return "software fallback";
    put_s(&p, "pixels damage="); put_u64(&p, damage_pixels);
    put_s(&p, " full="); put_u64(&p, full_pixels);
    put_s(&p, " ticks damage="); put_u64(&p, t1 - t0);
    put_s(&p, " full="); put_u64(&p, t2 - t1);
    *p = 0;
    return (const char *)0;
}

const char *os_gfx_guest_report(void)
{
    return report;
}
