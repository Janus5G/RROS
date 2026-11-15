#include "gfx.h"

static int rect_empty(gfx_rect r) { return r.x1 <= r.x0 || r.y1 <= r.y0; }

static gfx_rect rect_isect(gfx_rect a, gfx_rect b)
{
    gfx_rect r;
    r.x0 = a.x0 > b.x0 ? a.x0 : b.x0;
    r.y0 = a.y0 > b.y0 ? a.y0 : b.y0;
    r.x1 = a.x1 < b.x1 ? a.x1 : b.x1;
    r.y1 = a.y1 < b.y1 ? a.y1 : b.y1;
    return r;
}

static gfx_rect rect_bbox(gfx_rect a, gfx_rect b)
{
    gfx_rect r;
    r.x0 = a.x0 < b.x0 ? a.x0 : b.x0;
    r.y0 = a.y0 < b.y0 ? a.y0 : b.y0;
    r.x1 = a.x1 > b.x1 ? a.x1 : b.x1;
    r.y1 = a.y1 > b.y1 ? a.y1 : b.y1;
    return r;
}

static int rect_touch(gfx_rect a, gfx_rect b)
{
    return a.x0 <= b.x1 && b.x0 <= a.x1 && a.y0 <= b.y1 && b.y0 <= a.y1;
}

static int rect_contains(gfx_rect a, gfx_rect b)
{
    return a.x0 <= b.x0 && a.y0 <= b.y0 && a.x1 >= b.x1 && a.y1 >= b.y1;
}

static uint64_t rect_area(gfx_rect r)
{
    return rect_empty(r) ? 0u : (uint64_t)(uint32_t)(r.x1 - r.x0) * (uint64_t)(uint32_t)(r.y1 - r.y0);
}

void gfx_damage_clear(gfx_damage *d)
{
    if (d) d->count = 0;
}

int gfx_damage_add(gfx_damage *d, gfx_rect r, int32_t width, int32_t height)
{
    gfx_rect bounds;
    if (!d || width <= 0 || height <= 0) return GFX_ERR_ARGUMENT;
    bounds.x0 = 0; bounds.y0 = 0; bounds.x1 = width; bounds.y1 = height;
    r = rect_isect(r, bounds);
    if (rect_empty(r)) return GFX_OK;
    for (unsigned i = 0; i < d->count; i++) if (rect_contains(d->r[i], r)) return GFX_OK;
    for (;;) {
        int merged = 0;
        for (unsigned i = 0; i < d->count; i++) {
            if (rect_touch(d->r[i], r)) {
                r = rect_bbox(d->r[i], r);
                d->r[i] = d->r[--d->count];
                merged = 1;
                break;
            }
        }
        if (!merged) break;
    }
    if (d->count >= GFX_MAX_RECTS) {
        for (unsigned i = 0; i < d->count; i++) r = rect_bbox(d->r[i], r);
        d->count = 0;
    }
    d->r[d->count++] = r;
    return GFX_OK;
}

uint64_t gfx_damage_area(const gfx_damage *d)
{
    uint64_t a = 0;
    if (!d) return 0;
    for (unsigned i = 0; i < d->count; i++) a += rect_area(d->r[i]);
    return a;
}

static int dim_ok(uint32_t v) { return v >= GFX_MIN_DIM && v <= GFX_MAX_DIM; }

int gfx_output_init(gfx_output *o, uint32_t width, uint32_t height, uint32_t *memory, size_t memory_pixels)
{
    uint64_t per;
    gfx_output zero = { 0 };
    if (!o || !memory || !dim_ok(width) || !dim_ok(height)) return GFX_ERR_ARGUMENT;
    per = (uint64_t)width * height;
    if ((uint64_t)memory_pixels < per * GFX_POOL_BUFFERS) return GFX_ERR_RANGE;
    *o = zero;
    o->width = width; o->height = height;
    o->queued = -1; o->scanout = -1; o->drawing = -1;
    for (unsigned i = 0; i < GFX_POOL_BUFFERS; i++) {
        o->buf[i].pixels = memory + (size_t)per * i;
        for (size_t k = 0; k < per; k++) o->buf[i].pixels[k] = 0;
        o->buf[i].state = GFX_BUF_FREE;
    }
    {
        gfx_rect full = { 0, 0, (int32_t)width, (int32_t)height };
        gfx_damage_add(&o->pending, full, (int32_t)width, (int32_t)height);
    }
    return GFX_OK;
}

int gfx_output_damage(gfx_output *o, gfx_rect r)
{
    if (!o || !o->buf[0].pixels) return GFX_ERR_ARGUMENT;
    return gfx_damage_add(&o->pending, r, (int32_t)o->width, (int32_t)o->height);
}

int gfx_output_begin(gfx_output *o, gfx_frame *f)
{
    int pick = -1;
    gfx_buffer *b;
    uint64_t age;
    int need_full;
    if (!o || !f || !o->buf[0].pixels) return GFX_ERR_ARGUMENT;
    if (o->drawing >= 0) return GFX_ERR_STATE;
    for (int i = 0; i < (int)GFX_POOL_BUFFERS; i++) {
        if (o->buf[i].state != GFX_BUF_FREE) continue;
        if (pick < 0 || (o->buf[i].valid && (!o->buf[pick].valid || o->buf[i].seq > o->buf[pick].seq))) pick = i;
    }
    if (pick < 0) return GFX_ERR_BUSY;
    b = &o->buf[pick];
    age = o->frame_seq - b->seq;
    need_full = !b->valid || age > GFX_HISTORY;
    f->buffer = pick;
    f->seq = o->frame_seq + 1u;
    f->own = o->pending;
    f->repaint.count = 0;
    if (need_full) {
        gfx_rect full = { 0, 0, (int32_t)o->width, (int32_t)o->height };
        gfx_damage_add(&f->repaint, full, (int32_t)o->width, (int32_t)o->height);
        f->full = 1;
        o->full_repaints++;
    } else {
        f->repaint = o->pending;
        for (uint64_t s = b->seq + 1u; s <= o->frame_seq; s++) {
            const gfx_damage *h = &o->history[s % GFX_HISTORY];
            for (unsigned i = 0; i < h->count; i++)
                gfx_damage_add(&f->repaint, h->r[i], (int32_t)o->width, (int32_t)o->height);
        }
        f->full = 0;
        o->partial_repaints++;
    }
    o->pending.count = 0;
    b->state = GFX_BUF_DRAWING;
    o->drawing = pick;
    o->acquires++;
    return GFX_OK;
}

uint32_t *gfx_output_frame_pixels(gfx_output *o, const gfx_frame *f)
{
    if (!o || !f || f->buffer < 0 || f->buffer >= (int)GFX_POOL_BUFFERS || o->drawing != f->buffer) return 0;
    return o->buf[f->buffer].pixels;
}

int gfx_output_submit(gfx_output *o, const gfx_frame *f, uint64_t *fence)
{
    gfx_buffer *b;
    if (!o || !f || !fence || f->buffer < 0 || f->buffer >= (int)GFX_POOL_BUFFERS) return GFX_ERR_ARGUMENT;
    if (o->drawing != f->buffer || f->seq != o->frame_seq + 1u) return GFX_ERR_STATE;
    b = &o->buf[f->buffer];
    o->history[f->seq % GFX_HISTORY] = f->own;
    b->seq = f->seq;
    b->valid = 1;
    o->frame_seq = f->seq;
    if (o->queued >= 0) {
        o->buf[o->queued].state = GFX_BUF_FREE;
        o->dropped++;
    }
    b->state = GFX_BUF_QUEUED;
    o->queued = f->buffer;
    o->drawing = -1;
    o->pixels_full_equivalent += (uint64_t)o->width * o->height;
    *fence = f->seq;
    return GFX_OK;
}

int gfx_output_abort(gfx_output *o, const gfx_frame *f)
{
    gfx_buffer *b;
    if (!o || !f || f->buffer < 0 || f->buffer >= (int)GFX_POOL_BUFFERS || o->drawing != f->buffer) return GFX_ERR_STATE;
    b = &o->buf[f->buffer];
    b->state = GFX_BUF_FREE;
    b->valid = 0;
    o->drawing = -1;
    for (unsigned i = 0; i < f->own.count; i++)
        gfx_damage_add(&o->pending, f->own.r[i], (int32_t)o->width, (int32_t)o->height);
    return GFX_OK;
}

int gfx_output_vblank(gfx_output *o)
{
    if (!o || !o->buf[0].pixels) return GFX_ERR_ARGUMENT;
    o->vblanks++;
    if (o->queued >= 0) {
        if (o->scanout >= 0) o->buf[o->scanout].state = GFX_BUF_FREE;
        o->scanout = o->queued;
        o->buf[o->scanout].state = GFX_BUF_SCANOUT;
        o->shown_seq = o->buf[o->scanout].seq;
        o->queued = -1;
        o->flips++;
        return 1;
    }
    o->repeats++;
    return 0;
}

int gfx_output_fence_signaled(const gfx_output *o, uint64_t fence)
{
    if (!o || fence > o->frame_seq) return 0;
    return fence <= o->shown_seq;
}

const uint32_t *gfx_output_scanout(const gfx_output *o)
{
    if (!o || o->scanout < 0) return 0;
    return o->buf[o->scanout].pixels;
}

/* ---------- rasterization primitives ---------- */

static int surface_ok(const gfx_surface *s)
{
    return s && s->pixels && s->width >= 1 && s->height >= 1 && s->width <= (int32_t)GFX_MAX_DIM &&
           s->height <= (int32_t)GFX_MAX_DIM && s->stride >= s->width;
}

static uint64_t fill_rect(gfx_surface *s, gfx_rect clip, gfx_rect r, uint32_t color)
{
    gfx_rect bounds = { 0, 0, s->width, s->height };
    uint64_t n;
    r = rect_isect(rect_isect(r, clip), bounds);
    if (rect_empty(r)) return 0;
    for (int32_t y = r.y0; y < r.y1; y++) {
        uint32_t *row = s->pixels + (size_t)y * (size_t)s->stride;
        for (int32_t x = r.x0; x < r.x1; x++) row[x] = color;
    }
    n = rect_area(r);
    return n;
}

static uint64_t fill_triangle(gfx_surface *s, gfx_rect clip, const int32_t *v, uint32_t color)
{
    gfx_rect bounds = { 0, 0, s->width, s->height }, bb;
    int64_t x0 = v[0], y0 = v[1], x1 = v[2], y1 = v[3], x2 = v[4], y2 = v[5], area;
    uint64_t n = 0;
    bb.x0 = (int32_t)(x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2));
    bb.y0 = (int32_t)(y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2));
    bb.x1 = (int32_t)(x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2)) + 1;
    bb.y1 = (int32_t)(y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2)) + 1;
    bb = rect_isect(rect_isect(bb, clip), bounds);
    if (rect_empty(bb)) return 0;
    area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
    if (area == 0) return 0;
    for (int32_t y = bb.y0; y < bb.y1; y++) {
        int64_t py = 2 * (int64_t)y + 1;
        uint32_t *row = s->pixels + (size_t)y * (size_t)s->stride;
        for (int32_t x = bb.x0; x < bb.x1; x++) {
            int64_t px = 2 * (int64_t)x + 1;
            int64_t w0 = (2 * (x1 - x0)) * (py - 2 * y0) - (2 * (y1 - y0)) * (px - 2 * x0);
            int64_t w1 = (2 * (x2 - x1)) * (py - 2 * y1) - (2 * (y2 - y1)) * (px - 2 * x1);
            int64_t w2 = (2 * (x0 - x2)) * (py - 2 * y2) - (2 * (y0 - y2)) * (px - 2 * x2);
            int inside = area > 0 ? (w0 >= 0 && w1 >= 0 && w2 >= 0) : (w0 <= 0 && w1 <= 0 && w2 <= 0);
            if (inside) { row[x] = color; n++; }
        }
    }
    return n;
}

/* ---------- multi-display system ---------- */

int gfx_system_init(gfx_system *s, uint32_t background)
{
    gfx_system zero = { 0 };
    if (!s) return GFX_ERR_ARGUMENT;
    *s = zero;
    s->background = background;
    return GFX_OK;
}

int gfx_system_add_output(gfx_system *s, unsigned id, int32_t ox, int32_t oy, uint32_t w, uint32_t h,
                          uint32_t *memory, size_t memory_pixels)
{
    gfx_rect n;
    if (!s || id >= GFX_MAX_OUTPUTS || s->active[id]) return GFX_ERR_ARGUMENT;
    if (ox < 0 || oy < 0 || !dim_ok(w) || !dim_ok(h)) return GFX_ERR_ARGUMENT;
    if ((int64_t)ox + w > GFX_COORD_LIMIT || (int64_t)oy + h > GFX_COORD_LIMIT) return GFX_ERR_LAYOUT;
    n.x0 = ox; n.y0 = oy; n.x1 = ox + (int32_t)w; n.y1 = oy + (int32_t)h;
    for (unsigned i = 0; i < GFX_MAX_OUTPUTS; i++) {
        gfx_rect e;
        if (!s->active[i]) continue;
        e.x0 = s->ox[i]; e.y0 = s->oy[i]; e.x1 = e.x0 + (int32_t)s->out[i].width; e.y1 = e.y0 + (int32_t)s->out[i].height;
        if (!rect_empty(rect_isect(n, e))) return GFX_ERR_LAYOUT;
    }
    if (gfx_output_init(&s->out[id], w, h, memory, memory_pixels) != GFX_OK) return GFX_ERR_RANGE;
    s->active[id] = 1; s->ox[id] = ox; s->oy[id] = oy;
    return GFX_OK;
}

int gfx_system_remove_output(gfx_system *s, unsigned id)
{
    gfx_output zero = { 0 };
    if (!s || id >= GFX_MAX_OUTPUTS || !s->active[id]) return GFX_ERR_ARGUMENT;
    s->out[id] = zero;
    s->active[id] = 0;
    return GFX_OK;
}

static void mark(gfx_system *s, gfx_rect r)
{
    for (unsigned i = 0; i < GFX_MAX_OUTPUTS; i++) {
        gfx_rect l;
        if (!s->active[i]) continue;
        l.x0 = r.x0 - s->ox[i]; l.x1 = r.x1 - s->ox[i];
        l.y0 = r.y0 - s->oy[i]; l.y1 = r.y1 - s->oy[i];
        gfx_output_damage(&s->out[i], l);
    }
}

static int layer_ok(const gfx_system *s, int id)
{
    return s && id >= 0 && id < (int)GFX_MAX_LAYERS && s->layer[id].used;
}

static int rect_in_limit(gfx_rect r)
{
    return r.x0 >= -GFX_COORD_LIMIT && r.y0 >= -GFX_COORD_LIMIT && r.x1 <= GFX_COORD_LIMIT && r.y1 <= GFX_COORD_LIMIT &&
           !rect_empty(r);
}

int gfx_layer_add(gfx_system *s, gfx_rect rect, uint32_t color, const uint32_t *src)
{
    if (!s || !rect_in_limit(rect)) return GFX_ERR_ARGUMENT;
    for (unsigned i = 0; i < GFX_MAX_LAYERS; i++) {
        gfx_layer *l = &s->layer[i];
        if (l->used) continue;
        l->used = 1; l->visible = 1; l->rect = rect; l->color = color; l->src = src;
        l->z = ++s->z_counter;
        mark(s, rect);
        return (int)i;
    }
    return GFX_ERR_FULL;
}

int gfx_layer_remove(gfx_system *s, int id)
{
    if (!layer_ok(s, id)) return GFX_ERR_ARGUMENT;
    if (s->layer[id].visible) mark(s, s->layer[id].rect);
    s->layer[id].used = 0;
    return GFX_OK;
}

int gfx_layer_move(gfx_system *s, int id, int32_t x, int32_t y)
{
    gfx_layer *l;
    gfx_rect old, moved;
    int64_t w, h;
    if (!layer_ok(s, id)) return GFX_ERR_ARGUMENT;
    l = &s->layer[id];
    old = l->rect;
    w = (int64_t)old.x1 - old.x0; h = (int64_t)old.y1 - old.y0;
    if (x < -GFX_COORD_LIMIT || y < -GFX_COORD_LIMIT || x + w > GFX_COORD_LIMIT || y + h > GFX_COORD_LIMIT) return GFX_ERR_RANGE;
    moved.x0 = x; moved.y0 = y; moved.x1 = (int32_t)(x + w); moved.y1 = (int32_t)(y + h);
    l->rect = moved;
    if (l->visible) { mark(s, old); mark(s, moved); }
    return GFX_OK;
}

int gfx_layer_set_color(gfx_system *s, int id, uint32_t color)
{
    if (!layer_ok(s, id)) return GFX_ERR_ARGUMENT;
    if (s->layer[id].color != color) {
        s->layer[id].color = color;
        if (s->layer[id].visible) mark(s, s->layer[id].rect);
    }
    return GFX_OK;
}

int gfx_layer_set_visible(gfx_system *s, int id, int visible)
{
    if (!layer_ok(s, id)) return GFX_ERR_ARGUMENT;
    visible = visible != 0;
    if (s->layer[id].visible != visible) {
        s->layer[id].visible = visible;
        mark(s, s->layer[id].rect);
    }
    return GFX_OK;
}

int gfx_layer_raise(gfx_system *s, int id)
{
    if (!layer_ok(s, id)) return GFX_ERR_ARGUMENT;
    s->layer[id].z = ++s->z_counter;
    if (s->layer[id].visible) mark(s, s->layer[id].rect);
    return GFX_OK;
}

int gfx_system_damage_all(gfx_system *s, unsigned id)
{
    gfx_rect full;
    if (!s || id >= GFX_MAX_OUTPUTS || !s->active[id]) return GFX_ERR_ARGUMENT;
    full.x0 = 0; full.y0 = 0; full.x1 = (int32_t)s->out[id].width; full.y1 = (int32_t)s->out[id].height;
    return gfx_output_damage(&s->out[id], full);
}

static uint64_t paint_region(gfx_system *s, unsigned id, gfx_surface *surf, gfx_rect region)
{
    uint64_t n = fill_rect(surf, region, region, s->background);
    uint32_t last = 0;
    for (;;) {
        const gfx_layer *pick = 0;
        for (unsigned i = 0; i < GFX_MAX_LAYERS; i++) {
            const gfx_layer *l = &s->layer[i];
            if (l->used && l->visible && l->z > last && (!pick || l->z < pick->z)) pick = l;
        }
        if (!pick) break;
        last = pick->z;
        {
            gfx_rect lr, vis;
            lr.x0 = pick->rect.x0 - s->ox[id]; lr.x1 = pick->rect.x1 - s->ox[id];
            lr.y0 = pick->rect.y0 - s->oy[id]; lr.y1 = pick->rect.y1 - s->oy[id];
            vis = rect_isect(rect_isect(lr, region), (gfx_rect){ 0, 0, surf->width, surf->height });
            if (rect_empty(vis)) continue;
            if (!pick->src) {
                n += fill_rect(surf, vis, vis, pick->color);
            } else {
                int32_t lw = pick->rect.x1 - pick->rect.x0;
                for (int32_t y = vis.y0; y < vis.y1; y++) {
                    uint32_t *row = surf->pixels + (size_t)y * (size_t)surf->stride;
                    const uint32_t *sr = pick->src + (size_t)(y - lr.y0) * (size_t)lw;
                    for (int32_t x = vis.x0; x < vis.x1; x++) row[x] = sr[x - lr.x0];
                }
                n += rect_area(vis);
            }
        }
    }
    return n;
}

int gfx_system_render(gfx_system *s, unsigned id, uint64_t *fence)
{
    gfx_output *o;
    gfx_frame f;
    gfx_surface surf;
    uint64_t written = 0;
    int rc;
    if (!s || !fence || id >= GFX_MAX_OUTPUTS || !s->active[id]) return GFX_ERR_ARGUMENT;
    o = &s->out[id];
    if (o->pending.count == 0 && o->frame_seq > 0) { o->idle_frames++; return GFX_IDLE; }
    rc = gfx_output_begin(o, &f);
    if (rc != GFX_OK) return rc;
    surf.pixels = gfx_output_frame_pixels(o, &f);
    surf.width = (int32_t)o->width; surf.height = (int32_t)o->height; surf.stride = (int32_t)o->width;
    for (unsigned i = 0; i < f.repaint.count; i++) written += paint_region(s, id, &surf, f.repaint.r[i]);
    o->pixels_written += written;
    return gfx_output_submit(o, &f, fence);
}

/* ---------- GLES-like command stream and backends ---------- */

int gfx_cmd_validate(const gfx_cmd *cmds, unsigned count)
{
    if (!cmds || count == 0 || count > GFX_MAX_CMDS) return GFX_ERR_COMMAND;
    for (unsigned i = 0; i < count; i++) {
        unsigned n;
        switch (cmds[i].op) {
        case GFX_CMD_CLEAR: n = 0; break;
        case GFX_CMD_SCISSOR: case GFX_CMD_RECT: n = 4; break;
        case GFX_CMD_TRIANGLE: n = 6; break;
        default: return GFX_ERR_COMMAND;
        }
        for (unsigned k = 0; k < n; k++)
            if (cmds[i].a[k] < -GFX_COORD_LIMIT || cmds[i].a[k] > GFX_COORD_LIMIT) return GFX_ERR_COMMAND;
        if ((cmds[i].op == GFX_CMD_SCISSOR || cmds[i].op == GFX_CMD_RECT) &&
            (cmds[i].a[2] < cmds[i].a[0] || cmds[i].a[3] < cmds[i].a[1])) return GFX_ERR_COMMAND;
    }
    return GFX_OK;
}

static int software_submit(gfx_surface *t, const gfx_cmd *cmds, unsigned count, gfx_exec_stats *st)
{
    gfx_rect scissor, full;
    uint64_t total = 0;
    if (!surface_ok(t) || !st) return GFX_ERR_ARGUMENT;
    full.x0 = 0; full.y0 = 0; full.x1 = t->width; full.y1 = t->height;
    scissor = full;
    for (unsigned i = 0; i < count; i++) {
        const gfx_cmd *c = &cmds[i];
        gfx_rect r = { c->a[0], c->a[1], c->a[2], c->a[3] };
        switch (c->op) {
        case GFX_CMD_CLEAR: total += fill_rect(t, scissor, full, c->color); break;
        case GFX_CMD_SCISSOR: scissor = rect_isect(r, full); break;
        case GFX_CMD_RECT: total += fill_rect(t, scissor, r, c->color); break;
        case GFX_CMD_TRIANGLE: total += fill_triangle(t, scissor, c->a, c->color); break;
        default: return GFX_ERR_COMMAND;
        }
    }
    st->pixels = total;
    return GFX_OK;
}

static int v3d_submit(gfx_surface *t, const gfx_cmd *cmds, unsigned count, gfx_exec_stats *st)
{
    (void)t; (void)cmds; (void)count;
    if (st) st->pixels = 0;
    return GFX_ERR_NO_HARDWARE;
}

const gfx_backend gfx_backend_software = { "software-gles-subset", 0, software_submit };
const gfx_backend gfx_backend_v3d_unavailable = { "raspberry-pi-v3d", 1, v3d_submit };

int gfx_gpu_submit(const gfx_backend *preferred, const gfx_backend *fallback, gfx_surface *target,
                   const gfx_cmd *cmds, unsigned count, gfx_exec_stats *stats, int *used_fallback)
{
    int rc = gfx_cmd_validate(cmds, count);
    if (used_fallback) *used_fallback = 0;
    if (rc != GFX_OK) return rc;
    if (!target || !stats || !surface_ok(target)) return GFX_ERR_ARGUMENT;
    if (preferred && preferred->submit) {
        rc = preferred->submit(target, cmds, count, stats);
        if (rc != GFX_ERR_NO_HARDWARE) return rc;
    }
    if (!fallback || !fallback->submit || fallback->hardware) return GFX_ERR_NO_HARDWARE;
    if (used_fallback) *used_fallback = 1;
    return fallback->submit(target, cmds, count, stats);
}

static int seq_str(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

gfx_api_status gfx_api_support(const char *name)
{
    if (!name) return GFX_API_UNKNOWN;
    if (seq_str(name, "gles2-subset-software")) return GFX_API_SOFTWARE_AVAILABLE;
    if (seq_str(name, "raspberry-pi-v3d") || seq_str(name, "vulkan") || seq_str(name, "opengl-conformant")) return GFX_API_UNAVAILABLE;
    return GFX_API_UNKNOWN;
}
