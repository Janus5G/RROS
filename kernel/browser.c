#include "browser.h"

static int starts(const char *s, const char *p)
{
    while (*p) { if (*s++ != *p++) return 0; }
    return 1;
}

static size_t slen(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

void os_browser_init(os_browser *b, os_net_transport transport, void *ctx)
{
    if (!b) return;
    os_net_policy_init(&b->policy, OS_BROWSER_FETCH_MAX);
    b->transport = transport; b->ctx = ctx;
    b->history_count = b->history_pos = 0;
    b->has_page = 0; b->text_len = 0; b->text[0] = 0; b->title[0] = 0;
    b->last_status = 0; b->scripts_run = b->scripts_failed = 0; b->scroll = 0;
    b->doc.count = 0; b->next.count = 0;
}

int os_browser_allow_host(os_browser *b, const char *host)
{
    return b ? os_net_policy_allow(&b->policy, host) : OS_BR_ERR_ARGUMENT;
}

static void append(char *out, size_t cap, size_t *len, const char *s, size_t n)
{
    for (size_t i = 0; i < n && *len + 1u < cap; i++) out[(*len)++] = s[i];
    out[*len] = 0;
}

/* Fetches *url, builds the new page into scratch state and commits it only on success. */
static int load(os_browser *b, os_url *url)
{
    os_http_response resp;
    char text[OS_BROWSER_TEXT_MAX];
    char title[OS_BROWSER_TITLE_MAX];
    size_t tl = 0, rl = 0;
    unsigned run = 0, bad = 0;
    int rc = os_net_fetch(&b->policy, b->transport, b->ctx, url, b->buffer, sizeof b->buffer, &resp);
    if (rc != OS_NET_OK) return rc;
    if (resp.status != 200) { b->last_status = resp.status; return OS_BR_ERR_STATUS; }
    if (!starts(resp.content_type, "text/html")) return OS_BR_ERR_TYPE;
    rc = os_html_parse(&b->next, (const char *)b->buffer + resp.body_off, resp.body_len);
    if (rc != OS_HTML_OK) return rc;
    rc = os_html_render(&b->next, OS_BROWSER_WIDTH, text, sizeof text, &tl);
    if (rc != OS_HTML_OK) return rc;
    for (unsigned i = 0; i < b->next.script_count; i++) {
        const os_html_node *nd = &b->next.nodes[b->next.script_node[i]];
        os_js_result jr;
        os_js_run(b->next.pool + nd->text_off, nd->text_len, OS_JS_FUEL_DEFAULT, &jr);
        run++;
        if (tl == 0 || text[tl - 1u] != '\n') { if (tl + 1u < sizeof text) text[tl++] = '\n'; }
        if (jr.status == OS_JS_OK) {
            if (jr.output_len) {
                append(text, sizeof text, &tl, "[script output]\n", 16);
                append(text, sizeof text, &tl, jr.output, jr.output_len);
            }
        } else {
            bad++;
            append(text, sizeof text, &tl, "[script error: ", 15);
            append(text, sizeof text, &tl, jr.error, slen(jr.error));
            append(text, sizeof text, &tl, "]\n", 2);
        }
    }
    for (size_t i = 0; i < b->next.title_len && i + 1u < sizeof title; i++) title[rl++] = b->next.pool[b->next.title_off + i];
    title[rl] = 0;
    b->doc = b->next;
    for (size_t i = 0; i <= tl; i++) b->text[i] = text[i];
    b->text_len = tl;
    for (size_t i = 0; i <= rl; i++) b->title[i] = title[i];
    b->has_page = 1; b->last_status = 200; b->scripts_run = run; b->scripts_failed = bad; b->scroll = 0;
    return OS_BR_OK;
}

static int navigate_url(os_browser *b, os_url *url)
{
    int rc = load(b, url);
    if (rc != OS_BR_OK) return rc;
    if (b->history_count) b->history_count = b->history_pos + 1u;
    if (b->history_count == OS_BROWSER_HISTORY) {
        for (unsigned i = 1; i < OS_BROWSER_HISTORY; i++) b->history[i - 1u] = b->history[i];
        b->history_count--;
    }
    b->history[b->history_count] = *url;
    b->history_pos = b->history_count++;
    return OS_BR_OK;
}

int os_browser_navigate(os_browser *b, const char *text)
{
    os_url url;
    int rc;
    if (!b || !text || !b->transport) return OS_BR_ERR_ARGUMENT;
    rc = os_url_parse(text, &url);
    if (rc != OS_NET_OK) return rc;
    return navigate_url(b, &url);
}

int os_browser_follow_link(os_browser *b, unsigned n)
{
    char href[OS_HTML_ATTR_MAX + 1u];
    os_url target;
    int rc;
    if (!b || !b->has_page) return OS_BR_ERR_NOPAGE;
    if (os_html_link_href(&b->doc, n, href, sizeof href) < 0) return OS_BR_ERR_LINK;
    rc = os_url_resolve(&b->history[b->history_pos], href, &target);
    if (rc != OS_NET_OK) return rc;
    return navigate_url(b, &target);
}

static int move(os_browser *b, int delta)
{
    os_url url;
    int rc;
    unsigned pos;
    if (!b || !b->has_page) return OS_BR_ERR_NOPAGE;
    if ((delta < 0 && b->history_pos == 0) || (delta > 0 && b->history_pos + 1u >= b->history_count)) return OS_BR_ERR_NOHISTORY;
    pos = (unsigned)((int)b->history_pos + delta);
    url = b->history[pos];
    rc = load(b, &url);
    if (rc != OS_BR_OK) return rc;
    b->history[pos] = url;
    b->history_pos = pos;
    return OS_BR_OK;
}

int os_browser_back(os_browser *b) { return move(b, -1); }
int os_browser_forward(os_browser *b) { return move(b, 1); }

int os_browser_reload(os_browser *b)
{
    os_url url;
    int rc;
    if (!b || !b->has_page) return OS_BR_ERR_NOPAGE;
    url = b->history[b->history_pos];
    rc = load(b, &url);
    if (rc == OS_BR_OK) b->history[b->history_pos] = url;
    return rc;
}

const char *os_browser_text(const os_browser *b) { return b ? b->text : ""; }

unsigned os_browser_line_count(const os_browser *b)
{
    unsigned n = 0;
    if (!b || !b->text_len) return 0;
    for (size_t i = 0; i < b->text_len; i++) if (b->text[i] == '\n') n++;
    return n + 1u;
}

unsigned os_browser_view(const os_browser *b, unsigned rows, char *out, size_t cap)
{
    unsigned line = 0, copied = 0;
    size_t o = 0;
    if (!b || !out || cap == 0) return 0;
    out[0] = 0;
    for (size_t i = 0; i <= b->text_len && copied < rows; i++) {
        int end = i == b->text_len || b->text[i] == '\n';
        if (line >= b->scroll && i < b->text_len && !end && o + 1u < cap) out[o++] = b->text[i];
        if (end) {
            if (line >= b->scroll) {
                if (o + 1u < cap) out[o++] = '\n';
                copied++;
            }
            line++;
        }
    }
    out[o] = 0;
    return copied;
}

void os_browser_scroll(os_browser *b, int delta)
{
    unsigned lines, max;
    long pos;
    if (!b) return;
    lines = os_browser_line_count(b);
    max = lines ? lines - 1u : 0u;
    pos = (long)b->scroll + delta;
    if (pos < 0) pos = 0;
    if ((unsigned long)pos > max) pos = (long)max;
    b->scroll = (unsigned)pos;
}

int os_browser_fetch_image(os_browser *b, unsigned n, os_image *img)
{
    char src[OS_HTML_ATTR_MAX + 1u];
    os_url target;
    os_http_response resp;
    int rc;
    if (!b || !img) return OS_BR_ERR_ARGUMENT;
    if (!b->has_page) return OS_BR_ERR_NOPAGE;
    if (os_html_image_src(&b->doc, n, src, sizeof src) < 0) return OS_BR_ERR_LINK;
    rc = os_url_resolve(&b->history[b->history_pos], src, &target);
    if (rc != OS_NET_OK) return rc;
    rc = os_net_fetch(&b->policy, b->transport, b->ctx, &target, b->buffer, sizeof b->buffer, &resp);
    if (rc != OS_NET_OK) return rc;
    if (resp.status != 200) return OS_BR_ERR_STATUS;
    if (!starts(resp.content_type, "image/bmp") && !starts(resp.content_type, "image/x-portable-pixmap")) return OS_BR_ERR_TYPE;
    return os_image_decode(b->buffer + resp.body_off, resp.body_len, img);
}
