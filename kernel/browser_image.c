#include "browser_image.h"

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | (rd16(p + 2) << 16); }

static int ppm_number(const uint8_t *d, size_t n, size_t *pos, uint32_t *out)
{
    size_t i = *pos;
    uint32_t v = 0;
    int digits = 0;
    for (;;) {
        while (i < n && (d[i] == ' ' || d[i] == '\t' || d[i] == '\n' || d[i] == '\r')) i++;
        if (i < n && d[i] == '#') { while (i < n && d[i] != '\n') i++; continue; }
        break;
    }
    while (i < n && d[i] >= '0' && d[i] <= '9') {
        v = v * 10u + (uint32_t)(d[i++] - '0');
        if (++digits > 5) return 0;
    }
    if (!digits) return 0;
    *pos = i; *out = v;
    return 1;
}

static int decode_ppm(const uint8_t *d, size_t n, os_image *img)
{
    size_t pos = 2;
    uint32_t w, h, maxv;
    size_t bytes;
    if (!ppm_number(d, n, &pos, &w) || !ppm_number(d, n, &pos, &h) || !ppm_number(d, n, &pos, &maxv)) return OS_IMG_ERR_FORMAT;
    if (maxv != 255 || pos >= n || !(d[pos] == ' ' || d[pos] == '\t' || d[pos] == '\n' || d[pos] == '\r')) return OS_IMG_ERR_FORMAT;
    pos++;
    if (w == 0 || h == 0 || w > OS_IMG_MAX_DIM || h > OS_IMG_MAX_DIM) return OS_IMG_ERR_SIZE;
    bytes = (size_t)w * h * 3u;
    if (n - pos < bytes) return OS_IMG_ERR_TRUNCATED;
    if (n - pos > bytes) return OS_IMG_ERR_FORMAT;
    for (size_t i = 0; i < bytes; i++) img->rgb[i] = d[pos + i];
    img->width = (uint16_t)w; img->height = (uint16_t)h;
    return OS_IMG_OK;
}

static int decode_bmp(const uint8_t *d, size_t n, os_image *img)
{
    uint32_t off, hsz, w, bpp, comp;
    int32_t hraw;
    uint32_t h, stride;
    int top_down;
    if (n < 54) return OS_IMG_ERR_TRUNCATED;
    if (rd32(d + 2) != n || rd32(d + 6) != 0) return OS_IMG_ERR_FORMAT;
    off = rd32(d + 10); hsz = rd32(d + 14);
    if (hsz != 40) return OS_IMG_ERR_UNSUPPORTED;
    w = rd32(d + 18); hraw = (int32_t)rd32(d + 22);
    bpp = rd16(d + 28); comp = rd32(d + 30);
    if (rd16(d + 26) != 1 || bpp != 24 || comp != 0) return OS_IMG_ERR_UNSUPPORTED;
    top_down = hraw < 0;
    h = top_down ? (hraw == INT32_MIN ? 0xFFFFFFFFu : (uint32_t)(-hraw)) : (uint32_t)hraw;
    if (w == 0 || h == 0 || w > OS_IMG_MAX_DIM || h > OS_IMG_MAX_DIM) return OS_IMG_ERR_SIZE;
    stride = (w * 3u + 3u) & ~3u;
    if (off < 54 || off > n || (size_t)stride * h > n - off) return OS_IMG_ERR_TRUNCATED;
    if ((size_t)stride * h != n - off) return OS_IMG_ERR_FORMAT;
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *row = d + off + (size_t)(top_down ? y : h - 1u - y) * stride;
        for (uint32_t x = 0; x < w; x++) {
            uint8_t *o = img->rgb + ((size_t)y * w + x) * 3u;
            o[0] = row[x * 3u + 2u]; o[1] = row[x * 3u + 1u]; o[2] = row[x * 3u];
        }
    }
    img->width = (uint16_t)w; img->height = (uint16_t)h;
    return OS_IMG_OK;
}

int os_image_decode(const uint8_t *d, size_t n, os_image *img)
{
    if (!d || !img) return OS_IMG_ERR_ARGUMENT;
    img->width = img->height = 0;
    if (n < 2) return OS_IMG_ERR_FORMAT;
    if (d[0] == 'P' && d[1] == '6') return decode_ppm(d, n, img);
    if (d[0] == 'B' && d[1] == 'M') return decode_bmp(d, n, img);
    if ((n >= 8 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G') || (d[0] == 0xFF && d[1] == 0xD8) ||
        (n >= 6 && d[0] == 'G' && d[1] == 'I' && d[2] == 'F')) return OS_IMG_ERR_UNSUPPORTED;
    return OS_IMG_ERR_FORMAT;
}

int os_image_ascii(const os_image *img, unsigned cols, char *out, size_t cap, size_t *len)
{
    static const char ramp[] = " .:-=+*#%@";
    unsigned rows, w, h;
    size_t o = 0;
    if (!img || !out || !img->width || cols < 4 || cols > 80) return OS_IMG_ERR_ARGUMENT;
    w = img->width; h = img->height;
    if (cols > w) cols = w;
    rows = (h * cols) / (w * 2u);
    if (rows == 0) rows = 1;
    if ((size_t)(cols + 1u) * rows + 1u > cap) return OS_IMG_ERR_SIZE_OUT;
    for (unsigned r = 0; r < rows; r++) {
        unsigned y0 = r * h / rows, y1 = (r + 1u) * h / rows;
        if (y1 <= y0) y1 = y0 + 1u;
        for (unsigned c = 0; c < cols; c++) {
            unsigned x0 = c * w / cols, x1 = (c + 1u) * w / cols, count = 0;
            uint32_t sum = 0;
            if (x1 <= x0) x1 = x0 + 1u;
            for (unsigned y = y0; y < y1 && y < h; y++)
                for (unsigned x = x0; x < x1 && x < w; x++) {
                    const uint8_t *p = img->rgb + ((size_t)y * w + x) * 3u;
                    sum += (uint32_t)(p[0] * 299u + p[1] * 587u + p[2] * 114u) / 1000u;
                    count++;
                }
            out[o++] = ramp[(count ? sum / count : 0u) * 9u / 255u];
        }
        out[o++] = '\n';
    }
    out[o] = 0;
    if (len) *len = o;
    return OS_IMG_OK;
}
