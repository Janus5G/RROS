#ifndef EXPERIMENTALOS_KERNEL_BROWSER_IMAGE_H
#define EXPERIMENTALOS_KERNEL_BROWSER_IMAGE_H

#include <stddef.h>
#include <stdint.h>

/*
 * Browser images v1: strict decoders for binary PPM (P6, maxval 255) and uncompressed 24-bit BMP, bounded
 * to 128x128 pixels. PNG, JPEG, GIF and anything else are refused as unsupported (compatibility limit);
 * no decompressor exists here. Plus a text-mode (ASCII luminance) presentation for the console browser.
 */
#define OS_IMG_MAX_DIM 128u
#define OS_IMG_MAX_PIXELS 16384u

enum os_image_status {
    OS_IMG_OK = 0, OS_IMG_ERR_ARGUMENT = -1, OS_IMG_ERR_FORMAT = -2, OS_IMG_ERR_UNSUPPORTED = -3,
    OS_IMG_ERR_SIZE = -4, OS_IMG_ERR_TRUNCATED = -5, OS_IMG_ERR_SIZE_OUT = -6
};

typedef struct {
    uint16_t width, height;
    uint8_t rgb[OS_IMG_MAX_PIXELS * 3u];
} os_image;

int os_image_decode(const uint8_t *data, size_t length, os_image *image);
int os_image_ascii(const os_image *image, unsigned columns, char *out, size_t capacity, size_t *length);

#endif
