#ifndef EXPERIMENTALOS_KERNEL_BROWSER_H
#define EXPERIMENTALOS_KERNEL_BROWSER_H

#include "browser_html.h"
#include "browser_image.h"
#include "browser_js.h"
#include "browser_net.h"

/*
 * Browser session v1: navigation, numbered-link interaction, history, scrolling, sandboxed script output and
 * image fetching on top of the bounded parsers. Every network access goes through the allow-list policy and an
 * injected transport. Navigation is atomic: a failed load leaves the current page and history unchanged.
 * Compatibility limits are documented in browser_net.h, browser_html.h, browser_js.h and browser_image.h.
 */
#define OS_BROWSER_HISTORY 8u
#define OS_BROWSER_FETCH_MAX 16384u
#define OS_BROWSER_TEXT_MAX 6144u
#define OS_BROWSER_TITLE_MAX 64u
#define OS_BROWSER_WIDTH 60u

enum os_browser_status {
    OS_BR_OK = 0, OS_BR_ERR_ARGUMENT = -1, OS_BR_ERR_STATUS = -2, OS_BR_ERR_TYPE = -3, OS_BR_ERR_NOHISTORY = -4,
    OS_BR_ERR_NOPAGE = -5, OS_BR_ERR_LINK = -6
};

typedef struct {
    os_net_policy policy;
    os_net_transport transport;
    void *ctx;
    os_url history[OS_BROWSER_HISTORY];
    unsigned history_count, history_pos;
    int has_page;
    os_html_doc doc, next;
    uint8_t buffer[OS_BROWSER_FETCH_MAX];
    char text[OS_BROWSER_TEXT_MAX];
    size_t text_len;
    char title[OS_BROWSER_TITLE_MAX];
    int last_status;
    unsigned scripts_run, scripts_failed;
    unsigned scroll;
} os_browser;

void os_browser_init(os_browser *b, os_net_transport transport, void *ctx);
int os_browser_allow_host(os_browser *b, const char *host);
/* Returns OS_BR_OK or a negative os_net/os_html/os_browser status; on failure the page is unchanged. */
int os_browser_navigate(os_browser *b, const char *url);
int os_browser_follow_link(os_browser *b, unsigned link);
int os_browser_back(os_browser *b);
int os_browser_forward(os_browser *b);
int os_browser_reload(os_browser *b);
const char *os_browser_text(const os_browser *b);
unsigned os_browser_line_count(const os_browser *b);
/* Copies up to `rows` lines starting at the scroll position; returns the lines copied. */
unsigned os_browser_view(const os_browser *b, unsigned rows, char *out, size_t capacity);
void os_browser_scroll(os_browser *b, int delta_lines);
int os_browser_fetch_image(os_browser *b, unsigned image_number, os_image *image);

#endif
