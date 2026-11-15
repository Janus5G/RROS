#ifndef EXPERIMENTALOS_KERNEL_BROWSER_HTML_H
#define EXPERIMENTALOS_KERNEL_BROWSER_HTML_H

#include <stddef.h>
#include <stdint.h>

/*
 * Browser HTML v1: a bounded HTML subset parser and a text-mode renderer.
 * Compatibility limits: at most 8 KiB of input, 192 nodes, depth 16, no CSS (style content is dropped),
 * no forms submission, no frames, no entity decoding inside attribute values. Scripts are only collected
 * (as raw text) for the sandboxed JS subset; inline on* handlers are counted and ignored, never run.
 */
#define OS_HTML_MAX_BYTES 8192u
#define OS_HTML_MAX_NODES 192u
#define OS_HTML_MAX_DEPTH 16u
#define OS_HTML_POOL 6144u
#define OS_HTML_MAX_SCRIPTS 4u
#define OS_HTML_SCRIPT_MAX 1024u
#define OS_HTML_ATTR_MAX 191u
#define OS_HTML_MAX_LINKS 16u

enum os_html_status {
    OS_HTML_OK = 0, OS_HTML_ERR_ARGUMENT = -1, OS_HTML_ERR_SIZE = -2, OS_HTML_ERR_SYNTAX = -3,
    OS_HTML_ERR_LIMIT = -4, OS_HTML_ERR_DEPTH = -5, OS_HTML_ERR_RENDER = -6
};

enum os_html_tag {
    OS_TAG_TEXT = 0, OS_TAG_HTML, OS_TAG_HEAD, OS_TAG_BODY, OS_TAG_TITLE, OS_TAG_H1, OS_TAG_H2, OS_TAG_H3,
    OS_TAG_P, OS_TAG_DIV, OS_TAG_BR, OS_TAG_A, OS_TAG_UL, OS_TAG_LI, OS_TAG_B, OS_TAG_I, OS_TAG_PRE,
    OS_TAG_IMG, OS_TAG_SCRIPT, OS_TAG_STYLE, OS_TAG_FORM, OS_TAG_INPUT, OS_TAG_HR, OS_TAG_OTHER
};

typedef struct {
    uint8_t tag;
    uint8_t space_before;
    int16_t parent;
    uint16_t end;
    uint16_t text_off, text_len;
    uint16_t attr_off, attr_len;
} os_html_node;

typedef struct {
    os_html_node nodes[OS_HTML_MAX_NODES];
    unsigned count;
    char pool[OS_HTML_POOL];
    unsigned pool_len;
    uint16_t title_off, title_len;
    uint16_t script_node[OS_HTML_MAX_SCRIPTS];
    unsigned script_count;
    uint16_t link_node[OS_HTML_MAX_LINKS];
    unsigned link_count;
    unsigned image_count;
    unsigned ignored_handlers;
} os_html_doc;

int os_html_parse(os_html_doc *doc, const char *html, size_t length);
/* Renders wrapped text (width 20..120); links are numbered [1]..[16]. Output is NUL-terminated. */
int os_html_render(const os_html_doc *doc, unsigned width, char *out, size_t capacity, size_t *length);
/* Copies link n (1-based) href; returns its length or a negative status. */
int os_html_link_href(const os_html_doc *doc, unsigned n, char *out, size_t capacity);
int os_html_image_src(const os_html_doc *doc, unsigned n, char *out, size_t capacity);

#endif
