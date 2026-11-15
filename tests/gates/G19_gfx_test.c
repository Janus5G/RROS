#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "gfx.h"

static int failures;
#define CHECK(c, name) do { if (c) printf("PASS %s\n", name); else { printf("FAIL %s\n", name); failures++; } } while (0)

static uint32_t rng_state = 12345u;
static uint32_t rnd(void) { rng_state = rng_state * 1664525u + 1013904223u; return rng_state >> 8; }

static gfx_rect R(int x0, int y0, int x1, int y1) { gfx_rect r = { x0, y0, x1, y1 }; return r; }

static void test_damage(void)
{
    gfx_damage d = { 0 };
    gfx_damage_clear(&d);
    CHECK(gfx_damage_add(&d, R(10, 10, 20, 20), 100, 100) == GFX_OK && d.count == 1 && gfx_damage_area(&d) == 100, "G19-DAMAGE-ADD-AND-AREA");
    gfx_damage_add(&d, R(12, 12, 18, 18), 100, 100);
    CHECK(d.count == 1 && gfx_damage_area(&d) == 100, "G19-DAMAGE-CONTAINED-RECT-IGNORED");
    gfx_damage_add(&d, R(15, 15, 30, 30), 100, 100);
    CHECK(d.count == 1 && d.r[0].x0 == 10 && d.r[0].x1 == 30 && d.r[0].y1 == 30, "G19-DAMAGE-OVERLAPPING-RECTS-MERGE-CONSERVATIVELY");
    gfx_damage_clear(&d);
    gfx_damage_add(&d, R(0, 0, 10, 10), 100, 100);
    gfx_damage_add(&d, R(10, 0, 20, 10), 100, 100);
    CHECK(d.count == 1 && gfx_damage_area(&d) == 200, "G19-DAMAGE-ADJACENT-RECTS-MERGE");
    gfx_damage_clear(&d);
    gfx_damage_add(&d, R(0, 0, 5, 5), 100, 100);
    gfx_damage_add(&d, R(50, 50, 55, 55), 100, 100);
    CHECK(d.count == 2 && gfx_damage_area(&d) == 50, "G19-DAMAGE-DISJOINT-RECTS-KEPT-SEPARATE");
    gfx_damage_clear(&d);
    for (int i = 0; i < 20; i++) gfx_damage_add(&d, R(i * 4, i * 4, i * 4 + 2, i * 4 + 2), 200, 200);
    CHECK(d.count <= GFX_MAX_RECTS && d.count >= 1, "G19-DAMAGE-LIST-BOUNDED-BY-BOUNDING-BOX-FALLBACK");
    {
        int covered = 1;
        for (int i = 0; i < 20; i++) {
            int ok = 0;
            for (unsigned k = 0; k < d.count; k++)
                if (d.r[k].x0 <= i * 4 && d.r[k].y0 <= i * 4 && d.r[k].x1 >= i * 4 + 2 && d.r[k].y1 >= i * 4 + 2) ok = 1;
            if (!ok) covered = 0;
        }
        CHECK(covered, "G19-DAMAGE-NEVER-LOSES-COVERAGE-WHEN-MERGING");
    }
    gfx_damage_clear(&d);
    CHECK(gfx_damage_add(&d, R(-50, -50, 5, 5), 100, 100) == GFX_OK && d.r[0].x0 == 0 && d.r[0].x1 == 5, "G19-DAMAGE-CLIPPED-TO-SURFACE");
    gfx_damage_clear(&d);
    gfx_damage_add(&d, R(200, 200, 300, 300), 100, 100);
    gfx_damage_add(&d, R(5, 5, 5, 9), 100, 100);
    gfx_damage_add(&d, R(9, 9, 2, 2), 100, 100);
    gfx_damage_add(&d, R(-2147483647 - 1, -2147483647 - 1, 2147483647, 2147483647), 64, 64);
    CHECK(d.count == 1 && gfx_damage_area(&d) == 64u * 64u, "G19-DAMAGE-EMPTY-INVERTED-AND-EXTREME-RECTS-SAFE");
    CHECK(gfx_damage_add(0, R(0, 0, 1, 1), 10, 10) == GFX_ERR_ARGUMENT && gfx_damage_add(&d, R(0, 0, 1, 1), 0, 10) == GFX_ERR_ARGUMENT, "G19-DAMAGE-ARGUMENTS-VALIDATED");
}

static uint32_t pool_mem[3 * 64 * 48];

static void test_pool(void)
{
    gfx_output o;
    gfx_frame f, g;
    uint64_t fence;
    CHECK(gfx_output_init(&o, 64, 48, pool_mem, sizeof pool_mem / sizeof pool_mem[0]) == GFX_OK, "G19-POOL-INIT");
    CHECK(gfx_output_init(&o, 8, 48, pool_mem, 100000) == GFX_ERR_ARGUMENT && gfx_output_init(&o, 5000, 48, pool_mem, 100000) == GFX_ERR_ARGUMENT &&
          gfx_output_init(&o, 64, 48, pool_mem, 3 * 64 * 48 - 1) == GFX_ERR_RANGE && gfx_output_init(&o, 64, 48, 0, 100000) == GFX_ERR_ARGUMENT,
          "G19-POOL-INIT-REJECTS-BAD-GEOMETRY-AND-SHORT-MEMORY");
    gfx_output_init(&o, 64, 48, pool_mem, sizeof pool_mem / sizeof pool_mem[0]);
    CHECK(gfx_output_begin(&o, &f) == GFX_OK && f.full && f.seq == 1 && gfx_damage_area(&f.repaint) == 64u * 48u, "G19-POOL-FIRST-FRAME-IS-FULL-REPAINT");
    CHECK(gfx_output_begin(&o, &g) == GFX_ERR_STATE, "G19-POOL-ONE-FRAME-IN-FLIGHT-PER-OUTPUT");
    CHECK(gfx_output_frame_pixels(&o, &f) == pool_mem || gfx_output_frame_pixels(&o, &f) != 0, "G19-POOL-FRAME-POINTER-ONLY-WHILE-DRAWING");
    CHECK(gfx_output_submit(&o, &f, &fence) == GFX_OK && fence == 1 && !gfx_output_fence_signaled(&o, 1) && gfx_output_frame_pixels(&o, &f) == 0, "G19-SYNC-SUBMIT-IS-NOT-VISIBLE-BEFORE-VBLANK");
    CHECK(gfx_output_scanout(&o) == 0, "G19-SYNC-NO-SCANOUT-BEFORE-FIRST-FLIP");
    CHECK(gfx_output_vblank(&o) == 1 && gfx_output_fence_signaled(&o, 1) && gfx_output_scanout(&o) != 0, "G19-SYNC-VBLANK-FLIPS-AND-SIGNALS-FENCE");
    CHECK(gfx_output_vblank(&o) == 0 && o.repeats == 1 && o.flips == 1, "G19-SYNC-VBLANK-WITHOUT-NEW-FRAME-REPEATS-SCANOUT");
    CHECK(!gfx_output_fence_signaled(&o, 99) && gfx_output_fence_signaled(&o, 0), "G19-SYNC-FUTURE-FENCE-NOT-SIGNALED");
    gfx_output_begin(&o, &f);
    CHECK(f.full && o.full_repaints == 2, "G19-POOL-SECOND-BUFFER-FIRST-USE-NEEDS-FULL-REPAINT");
    gfx_output_submit(&o, &f, &fence);
    gfx_output_begin(&o, &f);
    CHECK(gfx_output_submit(&o, &f, &fence) == GFX_OK && o.dropped == 1, "G19-SYNC-UNPRESENTED-FRAME-IS-SUPERSEDED-AND-COUNTED");
    CHECK(gfx_output_fence_signaled(&o, 2) == 0, "G19-SYNC-SUPERSEDED-FENCE-WAITS-FOR-LATER-FLIP");
    gfx_output_vblank(&o);
    CHECK(gfx_output_fence_signaled(&o, 2) && gfx_output_fence_signaled(&o, fence), "G19-SYNC-SUPERSEDED-FENCE-SIGNALS-WITH-LATER-FRAME");
    {
        gfx_frame bad = f;
        bad.seq = 77;
        gfx_output_begin(&o, &g);
        CHECK(gfx_output_submit(&o, &bad, &fence) == GFX_ERR_STATE && gfx_output_submit(&o, 0, &fence) == GFX_ERR_ARGUMENT, "G19-POOL-SUBMIT-VALIDATES-FRAME-TOKEN");
        gfx_output_abort(&o, &g);
    }
    CHECK(gfx_output_abort(&o, &g) == GFX_ERR_STATE, "G19-POOL-ABORT-ONLY-WHILE-DRAWING");
    CHECK(o.acquires >= 4, "G19-POOL-REUSES-FIXED-BUFFERS-WITHOUT-ALLOCATION");
}

/* a scene mirrored into a damage-tracked system (A) and a full-repaint reference (B) */
static uint32_t memA[2][3 * 160 * 120], memB[2][3 * 160 * 120];
static uint32_t sprite[20 * 20];

static int same_frame(gfx_system *a, gfx_system *b, unsigned id)
{
    const uint32_t *pa = gfx_output_scanout(&a->out[id]), *pb = gfx_output_scanout(&b->out[id]);
    size_t n = (size_t)a->out[id].width * a->out[id].height;
    if (!pa || !pb) return 0;
    return memcmp(pa, pb, n * sizeof(uint32_t)) == 0;
}

static void present(gfx_system *s, unsigned id, int full)
{
    uint64_t fence;
    if (full) gfx_system_damage_all(s, id);
    gfx_system_render(s, id, &fence);
    gfx_output_vblank(&s->out[id]);
}

static void test_scene_equivalence(void)
{
    gfx_system A, B;
    int ids[5];
    int all_equal = 1, frames = 0;
    uint64_t pix_damage, pix_full;
    for (int i = 0; i < 400; i++) sprite[i] = 0xFF000000u | (uint32_t)(i * 2654435761u);
    gfx_system_init(&A, 0x00101010u);
    gfx_system_init(&B, 0x00101010u);
    CHECK(gfx_system_add_output(&A, 0, 0, 0, 160, 120, memA[0], sizeof memA[0] / sizeof memA[0][0]) == GFX_OK &&
          gfx_system_add_output(&B, 0, 0, 0, 160, 120, memB[0], sizeof memB[0] / sizeof memB[0][0]) == GFX_OK, "G19-SCENE-OUTPUT-ADDED");
    ids[0] = gfx_layer_add(&A, R(10, 10, 110, 80), 0x00FF0000u, 0);     gfx_layer_add(&B, R(10, 10, 110, 80), 0x00FF0000u, 0);
    ids[1] = gfx_layer_add(&A, R(50, 30, 150, 110), 0x0000FF00u, 0);    gfx_layer_add(&B, R(50, 30, 150, 110), 0x0000FF00u, 0);
    ids[2] = gfx_layer_add(&A, R(0, 0, 20, 20), 0, sprite);             gfx_layer_add(&B, R(0, 0, 20, 20), 0, sprite);
    ids[3] = gfx_layer_add(&A, R(-30, 90, 40, 140), 0x000000FFu, 0);    gfx_layer_add(&B, R(-30, 90, 40, 140), 0x000000FFu, 0);
    ids[4] = gfx_layer_add(&A, R(100, 5, 130, 25), 0x00FFFF00u, 0);     gfx_layer_add(&B, R(100, 5, 130, 25), 0x00FFFF00u, 0);
    present(&A, 0, 0); present(&B, 0, 1);
    CHECK(same_frame(&A, &B, 0), "G19-SCENE-FIRST-FRAME-MATCHES-REFERENCE");
    A.out[0].pixels_written = 0; A.out[0].pixels_full_equivalent = 0;
    for (int step = 0; step < 300; step++) {
        unsigned op = rnd() % 8, l = rnd() % 5;
        int x = (int)(rnd() % 200) - 40, y = (int)(rnd() % 160) - 30;
        switch (op) {
        case 0: case 1: case 2: gfx_layer_move(&A, ids[l], x, y); gfx_layer_move(&B, ids[l], x, y); break;
        case 3: gfx_layer_set_color(&A, ids[l], rnd()); { uint32_t c = A.layer[ids[l]].color; gfx_layer_set_color(&B, ids[l], c); } break;
        case 4: { int v = (int)(rnd() & 1); gfx_layer_set_visible(&A, ids[l], v); gfx_layer_set_visible(&B, ids[l], v); } break;
        case 5: gfx_layer_raise(&A, ids[l]); gfx_layer_raise(&B, ids[l]); break;
        default: break;
        }
        if (step % 7 == 3) { /* present twice before one vblank: exercises dropped frames */
            uint64_t fence;
            gfx_system_render(&A, 0, &fence); gfx_system_damage_all(&B, 0); gfx_system_render(&B, 0, &fence);
            gfx_layer_move(&A, ids[0], x / 2, y / 2); gfx_layer_move(&B, ids[0], x / 2, y / 2);
        }
        present(&A, 0, 0); present(&B, 0, 1);
        if (!same_frame(&A, &B, 0)) { all_equal = 0; printf("  mismatch at step %d op %u\n", step, op); break; }
        frames++;
    }
    CHECK(all_equal && frames == 300, "G19-SCENE-300-RANDOM-FRAMES-MATCH-FULL-REPAINT-REFERENCE-WITH-BUFFER-AGE");
    CHECK(A.out[0].dropped > 0 && A.out[0].partial_repaints > 0, "G19-SCENE-EXERCISED-DROPPED-FRAMES-AND-PARTIAL-REPAINTS");
    pix_damage = A.out[0].pixels_written; pix_full = B.out[0].pixels_written;
    printf("MEASURE random-scene pixels written: damage-tracked=%llu full-repaint=%llu ratio=%.3f\n", (unsigned long long)pix_damage, (unsigned long long)pix_full, (double)pix_damage / (double)pix_full);
    CHECK(pix_damage < pix_full, "G19-MEASURE-DAMAGE-TRACKING-WRITES-FEWER-PIXELS-THAN-FULL-REPAINT");
}

static void test_cursor_workload(void)
{
    static uint32_t ma[3 * 640 * 480], mb[3 * 640 * 480];
    gfx_system A, B;
    int ca, cb;
    struct timespec t0, t1, t2;
    double da, db;
    gfx_system_init(&A, 0x00202020u); gfx_system_init(&B, 0x00202020u);
    gfx_system_add_output(&A, 0, 0, 0, 640, 480, ma, sizeof ma / sizeof ma[0]);
    gfx_system_add_output(&B, 0, 0, 0, 640, 480, mb, sizeof mb / sizeof mb[0]);
    gfx_layer_add(&A, R(20, 20, 600, 440), 0x00334455u, 0); gfx_layer_add(&B, R(20, 20, 600, 440), 0x00334455u, 0);
    ca = gfx_layer_add(&A, R(0, 0, 12, 12), 0x00FFFFFFu, 0); cb = gfx_layer_add(&B, R(0, 0, 12, 12), 0x00FFFFFFu, 0);
    present(&A, 0, 0); present(&B, 0, 1);
    A.out[0].pixels_written = 0; B.out[0].pixels_written = 0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < 300; i++) { gfx_layer_move(&A, ca, (i * 7) % 620, (i * 5) % 460); present(&A, 0, 0); }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    for (int i = 0; i < 300; i++) { gfx_layer_move(&B, cb, (i * 7) % 620, (i * 5) % 460); present(&B, 0, 1); }
    clock_gettime(CLOCK_MONOTONIC, &t2);
    da = (double)(t1.tv_sec - t0.tv_sec) * 1e3 + (double)(t1.tv_nsec - t0.tv_nsec) / 1e6;
    db = (double)(t2.tv_sec - t1.tv_sec) * 1e3 + (double)(t2.tv_nsec - t1.tv_nsec) / 1e6;
    printf("MEASURE cursor workload 640x480 300 frames: damage pixels=%llu full pixels=%llu reduction=%.1fx; host time damage=%.2fms full=%.2fms (host, not guest)\n",
           (unsigned long long)A.out[0].pixels_written, (unsigned long long)B.out[0].pixels_written,
           (double)B.out[0].pixels_written / (double)A.out[0].pixels_written, da, db);
    CHECK(same_frame(&A, &B, 0), "G19-MEASURE-CURSOR-WORKLOAD-OUTPUT-IDENTICAL");
    CHECK(A.out[0].pixels_written * 10 < B.out[0].pixels_written, "G19-MEASURE-CURSOR-WORKLOAD-AT-LEAST-10X-FEWER-PIXELS-WRITTEN");
    CHECK(A.out[0].full_repaints <= GFX_POOL_BUFFERS, "G19-MEASURE-DAMAGE-PATH-NEEDS-ONLY-INITIAL-FULL-REPAINTS");
}

static uint32_t mem2a[3 * 80 * 60], mem2b[3 * 80 * 60], mem2c[3 * 80 * 60];

static void test_multi_display(void)
{
    gfx_system s;
    uint64_t fence;
    int span, left;
    const uint32_t *pa, *pb;
    gfx_system_init(&s, 0x11u);
    CHECK(gfx_system_add_output(&s, 0, 0, 0, 80, 60, mem2a, sizeof mem2a / sizeof mem2a[0]) == GFX_OK &&
          gfx_system_add_output(&s, 1, 80, 0, 80, 60, mem2b, sizeof mem2b / sizeof mem2b[0]) == GFX_OK, "G19-MULTI-TWO-OUTPUTS-SIDE-BY-SIDE");
    CHECK(gfx_system_add_output(&s, 2, 70, 0, 80, 60, mem2c, sizeof mem2c / sizeof mem2c[0]) == GFX_ERR_LAYOUT &&
          gfx_system_add_output(&s, 2, 0, 59, 80, 60, mem2c, sizeof mem2c / sizeof mem2c[0]) == GFX_ERR_LAYOUT &&
          gfx_system_add_output(&s, 0, 200, 0, 80, 60, mem2c, sizeof mem2c / sizeof mem2c[0]) == GFX_ERR_ARGUMENT &&
          gfx_system_add_output(&s, 9, 200, 0, 80, 60, mem2c, sizeof mem2c / sizeof mem2c[0]) == GFX_ERR_ARGUMENT &&
          gfx_system_add_output(&s, 2, -1, 0, 80, 60, mem2c, sizeof mem2c / sizeof mem2c[0]) == GFX_ERR_ARGUMENT &&
          gfx_system_add_output(&s, 2, 16380, 0, 80, 60, mem2c, sizeof mem2c / sizeof mem2c[0]) == GFX_ERR_LAYOUT, "G19-MULTI-OVERLAP-DUPLICATE-NEGATIVE-AND-OVERFLOW-LAYOUTS-REJECTED");
    CHECK(gfx_system_add_output(&s, 2, 0, 60, 80, 60, mem2c, 10) == GFX_ERR_RANGE, "G19-MULTI-OUTPUT-NEEDS-ENOUGH-BUFFER-MEMORY");
    span = gfx_layer_add(&s, R(60, 10, 100, 30), 0x00AA0000u, 0);
    present(&s, 0, 0); present(&s, 1, 0);
    pa = gfx_output_scanout(&s.out[0]); pb = gfx_output_scanout(&s.out[1]);
    CHECK(pa[10 * 80 + 60] == 0x00AA0000u && pa[10 * 80 + 59] == 0x11u && pb[10 * 80 + 19] == 0x00AA0000u && pb[10 * 80 + 20] == 0x11u,
          "G19-MULTI-LAYER-SPANNING-OUTPUTS-SPLITS-CORRECTLY");
    CHECK(gfx_system_render(&s, 0, &fence) == GFX_IDLE && gfx_system_render(&s, 1, &fence) == GFX_IDLE, "G19-MULTI-UNDAMAGED-OUTPUTS-DO-NO-WORK");
    left = gfx_layer_add(&s, R(5, 5, 15, 15), 0x00BB0000u, 0);
    CHECK(gfx_system_render(&s, 1, &fence) == GFX_IDLE && gfx_system_render(&s, 0, &fence) == GFX_OK, "G19-MULTI-DAMAGE-ONLY-REACHES-INTERSECTING-OUTPUT");
    gfx_output_vblank(&s.out[0]);
    gfx_layer_move(&s, span, 150, 10);
    CHECK(gfx_system_render(&s, 0, &fence) == GFX_OK && gfx_system_render(&s, 1, &fence) == GFX_OK, "G19-MULTI-MOVING-LAYER-DAMAGES-BOTH-OLD-AND-NEW-OUTPUTS");
    gfx_output_vblank(&s.out[0]); gfx_output_vblank(&s.out[1]);
    pa = gfx_output_scanout(&s.out[0]); pb = gfx_output_scanout(&s.out[1]);
    CHECK(pa[10 * 80 + 70] == 0x11u && pb[10 * 80 + 72] == 0x00AA0000u && pb[10 * 80 + 69] == 0x11u, "G19-MULTI-MOVED-LAYER-RENDERED-ON-NEW-OUTPUT-AND-ERASED-FROM-OLD");
    CHECK(gfx_layer_remove(&s, left) == GFX_OK && gfx_layer_remove(&s, left) == GFX_ERR_ARGUMENT && gfx_system_render(&s, 0, &fence) == GFX_OK, "G19-MULTI-LAYER-REMOVAL-DAMAGES-AND-IS-VALIDATED");
    CHECK(gfx_system_remove_output(&s, 1) == GFX_OK && gfx_system_remove_output(&s, 1) == GFX_ERR_ARGUMENT && gfx_system_render(&s, 1, &fence) == GFX_ERR_ARGUMENT, "G19-MULTI-OUTPUT-HOT-REMOVAL");
    CHECK(gfx_system_add_output(&s, 1, 80, 0, 80, 60, mem2b, sizeof mem2b / sizeof mem2b[0]) == GFX_OK && gfx_system_render(&s, 1, &fence) == GFX_OK, "G19-MULTI-OUTPUT-HOT-ADD-GETS-FULL-FIRST-FRAME");
}

static void test_layers(void)
{
    gfx_system s;
    static uint32_t m[3 * 32 * 32];
    int ids[GFX_MAX_LAYERS], extra;
    gfx_system_init(&s, 0);
    gfx_system_add_output(&s, 0, 0, 0, 32, 32, m, sizeof m / sizeof m[0]);
    for (unsigned i = 0; i < GFX_MAX_LAYERS; i++) ids[i] = gfx_layer_add(&s, R(0, 0, 4, 4), 1, 0);
    extra = gfx_layer_add(&s, R(0, 0, 4, 4), 1, 0);
    CHECK(ids[GFX_MAX_LAYERS - 1] >= 0 && extra == GFX_ERR_FULL, "G19-LAYER-COUNT-BOUNDED");
    CHECK(gfx_layer_add(&s, R(0, 0, 0, 4), 1, 0) == GFX_ERR_ARGUMENT && gfx_layer_add(&s, R(0, 0, 99999, 4), 1, 0) == GFX_ERR_ARGUMENT &&
          gfx_layer_move(&s, ids[0], 99999, 0) == GFX_ERR_RANGE && gfx_layer_move(&s, 99, 0, 0) == GFX_ERR_ARGUMENT && gfx_layer_set_color(&s, -1, 0) == GFX_ERR_ARGUMENT,
          "G19-LAYER-ARGUMENTS-VALIDATED");
    {
        uint64_t f;
        gfx_layer_remove(&s, ids[1]);
        for (unsigned i = 2; i < GFX_MAX_LAYERS; i++) gfx_layer_remove(&s, ids[i]);
        gfx_layer_set_color(&s, ids[0], 0x77u);
        gfx_layer_add(&s, R(0, 0, 4, 4), 0x88u, 0);
        gfx_system_render(&s, 0, &f); gfx_output_vblank(&s.out[0]);
        CHECK(gfx_output_scanout(&s.out[0])[0] == 0x88u, "G19-LAYER-LATER-LAYER-DRAWS-ABOVE");
        gfx_layer_raise(&s, ids[0]);
        gfx_system_render(&s, 0, &f); gfx_output_vblank(&s.out[0]);
        CHECK(gfx_output_scanout(&s.out[0])[0] == 0x77u, "G19-LAYER-RAISE-CHANGES-STACKING");
        gfx_layer_set_visible(&s, ids[0], 0);
        gfx_system_render(&s, 0, &f); gfx_output_vblank(&s.out[0]);
        CHECK(gfx_output_scanout(&s.out[0])[0] == 0x88u, "G19-LAYER-HIDDEN-LAYER-NOT-DRAWN");
    }
}

static uint32_t tpix[64 * 64];
static gfx_cmd C(uint32_t op, int a0, int a1, int a2, int a3, int a4, int a5, uint32_t color)
{
    gfx_cmd c = { op, { a0, a1, a2, a3, a4, a5 }, color };
    return c;
}

static unsigned count_pixels(uint32_t v)
{
    unsigned n = 0;
    for (unsigned i = 0; i < 64 * 64; i++) if (tpix[i] == v) n++;
    return n;
}

static void test_gpu(void)
{
    gfx_surface t = { tpix, 64, 64, 64 };
    gfx_exec_stats st;
    int fb;
    gfx_cmd cmds[GFX_MAX_CMDS + 1];
    gfx_cmd ok[2];
    CHECK(gfx_api_support("gles2-subset-software") == GFX_API_SOFTWARE_AVAILABLE && gfx_api_support("raspberry-pi-v3d") == GFX_API_UNAVAILABLE &&
          gfx_api_support("vulkan") == GFX_API_UNAVAILABLE && gfx_api_support("opengl-conformant") == GFX_API_UNAVAILABLE && gfx_api_support("x") == GFX_API_UNKNOWN &&
          gfx_api_support(0) == GFX_API_UNKNOWN, "G19-GPU-API-SUPPORT-TABLE-HONEST-ABOUT-V3D-VULKAN-AND-CONFORMANCE");
    memset(tpix, 0, sizeof tpix);
    ok[0] = C(GFX_CMD_CLEAR, 0, 0, 0, 0, 0, 0, 5);
    ok[1] = C(GFX_CMD_RECT, 10, 10, 20, 30, 0, 0, 9);
    CHECK(gfx_gpu_submit(&gfx_backend_software, 0, &t, ok, 2, &st, &fb) == GFX_OK && !fb && st.pixels == 64u * 64u + 200u && count_pixels(9) == 200 && count_pixels(5) == 64 * 64 - 200,
          "G19-GPU-SOFTWARE-CLEAR-AND-RECT-EXACT");
    memset(tpix, 0, sizeof tpix);
    {
        gfx_cmd s2[2] = { C(GFX_CMD_SCISSOR, 8, 8, 16, 16, 0, 0, 0), C(GFX_CMD_RECT, 0, 0, 64, 64, 0, 0, 7) };
        gfx_gpu_submit(&gfx_backend_software, 0, &t, s2, 2, &st, &fb);
        CHECK(count_pixels(7) == 64 && tpix[8 * 64 + 8] == 7 && tpix[7 * 64 + 8] == 0 && tpix[16 * 64 + 16] == 0, "G19-GPU-SCISSOR-CLIPS-DRAWING");
        s2[0] = C(GFX_CMD_SCISSOR, -100, -100, 1000, 1000, 0, 0, 0);
        memset(tpix, 0, sizeof tpix);
        gfx_gpu_submit(&gfx_backend_software, 0, &t, s2, 2, &st, &fb);
        CHECK(count_pixels(7) == 64 * 64, "G19-GPU-SCISSOR-CLAMPED-TO-TARGET");
    }
    memset(tpix, 0, sizeof tpix);
    {
        gfx_cmd tri = C(GFX_CMD_TRIANGLE, 0, 0, 40, 0, 0, 40, 3);
        unsigned n;
        gfx_gpu_submit(&gfx_backend_software, 0, &t, &tri, 1, &st, &fb);
        n = count_pixels(3);
        CHECK(n >= 780 && n <= 860 && tpix[1 * 64 + 1] == 3 && tpix[39 * 64 + 39] == 0 && tpix[5 * 64 + 50] == 0 && st.pixels == n, "G19-GPU-TRIANGLE-COVERAGE-WITHIN-RASTER-TOLERANCE");
        memset(tpix, 0, sizeof tpix);
        tri = C(GFX_CMD_TRIANGLE, 0, 40, 40, 0, 0, 0, 3);
        gfx_gpu_submit(&gfx_backend_software, 0, &t, &tri, 1, &st, &fb);
        CHECK(count_pixels(3) == n || (count_pixels(3) >= 780 && count_pixels(3) <= 860), "G19-GPU-TRIANGLE-WINDING-INDEPENDENT");
        memset(tpix, 0, sizeof tpix);
        tri = C(GFX_CMD_TRIANGLE, 5, 5, 20, 20, 35, 35, 3);
        gfx_gpu_submit(&gfx_backend_software, 0, &t, &tri, 1, &st, &fb);
        CHECK(st.pixels == 0 || st.pixels < 40, "G19-GPU-DEGENERATE-TRIANGLE-DRAWS-NOTHING-OR-SLIVER");
        tri = C(GFX_CMD_TRIANGLE, -16000, -16000, 16000, -16000, 0, 16000, 4);
        gfx_gpu_submit(&gfx_backend_software, 0, &t, &tri, 1, &st, &fb);
        CHECK(st.pixels <= 64u * 64u, "G19-GPU-HUGE-OFFSCREEN-TRIANGLE-CLIPPED-SAFELY");
    }
    memset(tpix, 0, sizeof tpix);
    CHECK(gfx_gpu_submit(&gfx_backend_v3d_unavailable, 0, &t, ok, 2, &st, &fb) == GFX_ERR_NO_HARDWARE && count_pixels(0) == 64 * 64 && st.pixels == 0, "G19-GPU-V3D-BACKEND-FAILS-CLOSED-AND-WRITES-NOTHING");
    CHECK(gfx_gpu_submit(&gfx_backend_v3d_unavailable, &gfx_backend_software, &t, ok, 2, &st, &fb) == GFX_OK && fb == 1 && count_pixels(9) == 200, "G19-GPU-FALLBACK-TO-SOFTWARE-WHEN-HARDWARE-UNAVAILABLE");
    CHECK(gfx_gpu_submit(&gfx_backend_v3d_unavailable, &gfx_backend_v3d_unavailable, &t, ok, 2, &st, &fb) == GFX_ERR_NO_HARDWARE, "G19-GPU-HARDWARE-IS-NEVER-USED-AS-FALLBACK");
    {
        gfx_cmd bad[2];
        memset(tpix, 0, sizeof tpix);
        bad[0] = C(GFX_CMD_RECT, 0, 0, 10, 10, 0, 0, 1);
        bad[1] = C(99, 0, 0, 0, 0, 0, 0, 1);
        CHECK(gfx_gpu_submit(&gfx_backend_software, 0, &t, bad, 2, &st, &fb) == GFX_ERR_COMMAND && count_pixels(0) == 64 * 64, "G19-GPU-INVALID-STREAM-REJECTED-BEFORE-ANY-DRAWING");
        bad[1] = C(GFX_CMD_RECT, 0, 0, 99999, 10, 0, 0, 1);
        CHECK(gfx_cmd_validate(bad, 2) == GFX_ERR_COMMAND, "G19-GPU-OUT-OF-RANGE-COORDINATES-REJECTED");
        bad[1] = C(GFX_CMD_RECT, 10, 10, 5, 5, 0, 0, 1);
        CHECK(gfx_cmd_validate(bad, 2) == GFX_ERR_COMMAND && gfx_cmd_validate(0, 1) == GFX_ERR_COMMAND && gfx_cmd_validate(bad, 0) == GFX_ERR_COMMAND, "G19-GPU-INVERTED-RECT-AND-EMPTY-STREAM-REJECTED");
    }
    for (unsigned i = 0; i < GFX_MAX_CMDS + 1; i++) cmds[i] = C(GFX_CMD_RECT, 0, 0, 1, 1, 0, 0, 1);
    CHECK(gfx_cmd_validate(cmds, GFX_MAX_CMDS) == GFX_OK && gfx_cmd_validate(cmds, GFX_MAX_CMDS + 1) == GFX_ERR_COMMAND, "G19-GPU-COMMAND-COUNT-BOUNDED");
    {
        gfx_surface bad = { tpix, 64, 64, 10 };
        gfx_surface nul = { 0, 64, 64, 64 };
        CHECK(gfx_gpu_submit(&gfx_backend_software, 0, &bad, ok, 2, &st, &fb) == GFX_ERR_ARGUMENT && gfx_gpu_submit(&gfx_backend_software, 0, &nul, ok, 2, &st, &fb) == GFX_ERR_ARGUMENT &&
              gfx_gpu_submit(&gfx_backend_software, 0, &t, ok, 2, 0, &fb) == GFX_ERR_ARGUMENT, "G19-GPU-TARGET-SURFACE-VALIDATED");
    }
}

int main(void)
{
    test_damage();
    test_pool();
    test_scene_equivalence();
    test_cursor_workload();
    test_multi_display();
    test_layers();
    test_gpu();
    printf(failures ? "G19 GFX RESULT: FAIL (%d)\n" : "G19 GFX RESULT: PASS\n", failures);
    return failures ? 1 : 0;
}
