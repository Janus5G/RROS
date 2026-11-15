#ifndef EXPERIMENTALOS_KERNEL_GFX_H
#define EXPERIMENTALOS_KERNEL_GFX_H

#include <stddef.h>
#include <stdint.h>

/*
 * G19 graphics pipeline: damage tracking, reusable buffer pool with buffer-age
 * repaint, vblank/fence frame synchronization, multi-display layout, a bounded
 * GLES-like command stream with a software rasterizer, and a GPU transport
 * abstraction. The only operational renderer is the software one; the
 * Raspberry Pi V3D backend is present as a fail-closed stub because no verified
 * hardware interface exists. All memory is caller supplied; nothing allocates.
 */

#define GFX_MAX_RECTS 8u
#define GFX_MIN_DIM 16u
#define GFX_MAX_DIM 4096u
#define GFX_COORD_LIMIT 16384
#define GFX_POOL_BUFFERS 3u
#define GFX_HISTORY 4u
#define GFX_MAX_OUTPUTS 4u
#define GFX_MAX_LAYERS 16u
#define GFX_MAX_CMDS 256u

enum {
    GFX_OK = 0, GFX_IDLE = 1,
    GFX_ERR_ARGUMENT = -1, GFX_ERR_RANGE = -2, GFX_ERR_STATE = -3, GFX_ERR_FULL = -4,
    GFX_ERR_LAYOUT = -5, GFX_ERR_COMMAND = -6, GFX_ERR_NO_HARDWARE = -7, GFX_ERR_BUSY = -8
};

typedef struct { int32_t x0, y0, x1, y1; } gfx_rect; /* half-open */

typedef struct {
    gfx_rect r[GFX_MAX_RECTS];
    unsigned count;
} gfx_damage;

typedef struct {
    uint32_t *pixels;
    int32_t width, height, stride;
} gfx_surface;

void gfx_damage_clear(gfx_damage *d);
int gfx_damage_add(gfx_damage *d, gfx_rect r, int32_t width, int32_t height);
uint64_t gfx_damage_area(const gfx_damage *d);

typedef enum { GFX_BUF_FREE = 0, GFX_BUF_DRAWING, GFX_BUF_QUEUED, GFX_BUF_SCANOUT } gfx_buf_state;

typedef struct {
    uint32_t *pixels;
    gfx_buf_state state;
    uint64_t seq;
    int valid;
} gfx_buffer;

typedef struct {
    gfx_buffer buf[GFX_POOL_BUFFERS];
    gfx_damage history[GFX_HISTORY];
    gfx_damage pending;
    uint32_t width, height;
    int queued, scanout, drawing;
    uint64_t frame_seq, shown_seq;
    uint64_t vblanks, flips, repeats, dropped, acquires, idle_frames;
    uint64_t full_repaints, partial_repaints, pixels_written, pixels_full_equivalent;
} gfx_output;

typedef struct {
    int buffer;
    uint64_t seq;
    int full;
    gfx_damage repaint;
    gfx_damage own;
} gfx_frame;

int gfx_output_init(gfx_output *o, uint32_t width, uint32_t height, uint32_t *memory, size_t memory_pixels);
int gfx_output_damage(gfx_output *o, gfx_rect r);
int gfx_output_begin(gfx_output *o, gfx_frame *f);
int gfx_output_submit(gfx_output *o, const gfx_frame *f, uint64_t *fence);
int gfx_output_abort(gfx_output *o, const gfx_frame *f);
int gfx_output_vblank(gfx_output *o);
int gfx_output_fence_signaled(const gfx_output *o, uint64_t fence);
const uint32_t *gfx_output_scanout(const gfx_output *o);
uint32_t *gfx_output_frame_pixels(gfx_output *o, const gfx_frame *f);

typedef struct {
    int used, visible;
    uint32_t z;
    gfx_rect rect;
    uint32_t color;
    const uint32_t *src; /* optional, rect-sized, caller keeps it alive */
} gfx_layer;

typedef struct {
    gfx_output out[GFX_MAX_OUTPUTS];
    int active[GFX_MAX_OUTPUTS];
    int32_t ox[GFX_MAX_OUTPUTS], oy[GFX_MAX_OUTPUTS];
    gfx_layer layer[GFX_MAX_LAYERS];
    uint32_t z_counter, background;
} gfx_system;

int gfx_system_init(gfx_system *s, uint32_t background);
int gfx_system_add_output(gfx_system *s, unsigned id, int32_t ox, int32_t oy, uint32_t w, uint32_t h,
                          uint32_t *memory, size_t memory_pixels);
int gfx_system_remove_output(gfx_system *s, unsigned id);
int gfx_layer_add(gfx_system *s, gfx_rect rect, uint32_t color, const uint32_t *src);
int gfx_layer_remove(gfx_system *s, int id);
int gfx_layer_move(gfx_system *s, int id, int32_t x, int32_t y);
int gfx_layer_set_color(gfx_system *s, int id, uint32_t color);
int gfx_layer_set_visible(gfx_system *s, int id, int visible);
int gfx_layer_raise(gfx_system *s, int id);
/* Returns GFX_OK after presenting a frame, GFX_IDLE when nothing is damaged. */
int gfx_system_render(gfx_system *s, unsigned id, uint64_t *fence);
int gfx_system_damage_all(gfx_system *s, unsigned id);

typedef enum { GFX_CMD_CLEAR = 1, GFX_CMD_SCISSOR, GFX_CMD_RECT, GFX_CMD_TRIANGLE } gfx_cmd_op;

typedef struct {
    uint32_t op;
    int32_t a[6];
    uint32_t color;
} gfx_cmd;

typedef struct { uint64_t pixels; } gfx_exec_stats;

typedef struct {
    const char *name;
    int hardware;
    int (*submit)(gfx_surface *target, const gfx_cmd *cmds, unsigned count, gfx_exec_stats *stats);
} gfx_backend;

extern const gfx_backend gfx_backend_software;
extern const gfx_backend gfx_backend_v3d_unavailable;

int gfx_cmd_validate(const gfx_cmd *cmds, unsigned count);
int gfx_gpu_submit(const gfx_backend *preferred, const gfx_backend *fallback, gfx_surface *target,
                   const gfx_cmd *cmds, unsigned count, gfx_exec_stats *stats, int *used_fallback);

typedef enum { GFX_API_UNKNOWN = 0, GFX_API_SOFTWARE_AVAILABLE, GFX_API_UNAVAILABLE } gfx_api_status;
gfx_api_status gfx_api_support(const char *name);

#endif
