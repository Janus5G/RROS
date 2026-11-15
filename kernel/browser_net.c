#include "browser_net.h"

static size_t slen(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static int seq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static int prefix_ci(const char *s, const char *p)
{
    while (*p) {
        if (lower(*s) != *p) return 0;
        s++; p++;
    }
    return 1;
}

static int valid_chars(const char *s)
{
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x21 || c > 0x7e || c == '\\' || c == '<' || c == '>' || c == '"' || c == '`') return 0;
        if (c == '%' && s[1] == '0' && s[2] == '0') return 0;
    }
    return 1;
}

typedef struct { char *p; size_t cap, len; int fail; } buf_t;

static void put(buf_t *b, const char *s)
{
    while (*s) {
        if (b->len + 1u >= b->cap) { b->fail = 1; return; }
        b->p[b->len++] = *s++;
    }
}

static void put_u(buf_t *b, unsigned v)
{
    char t[8];
    int k = 0;
    do { t[k++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (k) {
        if (b->len + 1u >= b->cap) { b->fail = 1; return; }
        b->p[b->len++] = t[--k];
    }
}

static int normalize_path(char *path)
{
    char tmp[OS_URL_PATH_MAX];
    size_t o = 0, i = 1, q = 0, n = slen(path);
    int trailing = 0;
    if (path[0] != '/') return -1;
    while (path[q] && path[q] != '?') q++;
    tmp[o++] = '/';
    while (i <= q) {
        size_t s = i, e = i, sl;
        while (e < q && path[e] != '/') e++;
        sl = e - s;
        if (sl == 0) {
            if (e >= q) { trailing = 1; break; }
            i = e + 1u;
            continue;
        }
        trailing = 0;
        if (sl == 1 && path[s] == '.') {
            trailing = 1;
        } else if (sl == 2 && path[s] == '.' && path[s + 1u] == '.') {
            while (o > 1 && tmp[o - 1u] != '/') o--;
            if (o > 1) o--;
            trailing = 1;
        } else {
            if (o + sl + 2u >= sizeof tmp) return -1;
            if (o > 1) tmp[o++] = '/';
            for (size_t k = 0; k < sl; k++) tmp[o++] = path[s + k];
        }
        if (e >= q) break;
        i = e + 1u;
    }
    if (trailing && o > 1) tmp[o++] = '/';
    if (o + (n - q) + 1u > sizeof tmp) return -1;
    for (size_t k = q; k < n; k++) tmp[o++] = path[k];
    tmp[o] = 0;
    for (size_t k = 0; k <= o; k++) path[k] = tmp[k];
    return 0;
}

static int host_ok(const char *h, char *out)
{
    size_t n = 0;
    if (!*h) return 0;
    for (; h[n]; n++) {
        char c = lower(h[n]);
        if (n + 1u >= OS_URL_HOST_MAX) return 0;
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-')) return 0;
        if (c == '.' && h[n + 1u] == '.') return 0;
        out[n] = c;
    }
    out[n] = 0;
    if (out[0] == '.' || out[0] == '-' || out[n - 1u] == '.' || out[n - 1u] == '-') return 0;
    return 1;
}

int os_url_parse(const char *t, os_url *u)
{
    size_t n, p;
    char host[OS_URL_HOST_MAX + 2u];
    size_t hl = 0, pl = 0;
    unsigned port = 0;
    int have_port = 0;
    if (!t || !u) return OS_NET_ERR_ARGUMENT;
    n = slen(t);
    if (n == 0 || n > OS_URL_MAX || !valid_chars(t)) return OS_NET_ERR_URL;
    if (prefix_ci(t, "http://")) { u->https = 0; p = 7; }
    else if (prefix_ci(t, "https://")) { u->https = 1; p = 8; }
    else return OS_NET_ERR_SCHEME;
    while (t[p] && t[p] != '/' && t[p] != '?' && t[p] != '#' && t[p] != ':') {
        if (hl + 1u >= sizeof host) return OS_NET_ERR_URL;
        host[hl++] = t[p++];
    }
    host[hl] = 0;
    if (!host_ok(host, u->host)) return OS_NET_ERR_URL;
    if (t[p] == ':') {
        p++;
        while (t[p] >= '0' && t[p] <= '9') {
            port = port * 10u + (unsigned)(t[p++] - '0');
            have_port = 1;
            if (port > 65535u) return OS_NET_ERR_URL;
        }
        if (!have_port || port == 0) return OS_NET_ERR_URL;
    }
    u->port = (uint16_t)(have_port ? port : (u->https ? 443u : 80u));
    if (t[p] && t[p] != '/' && t[p] != '?' && t[p] != '#') return OS_NET_ERR_URL;
    if (t[p] != '/') u->path[pl++] = '/';
    while (t[p] && t[p] != '#') {
        if (pl + 1u >= OS_URL_PATH_MAX) return OS_NET_ERR_URL;
        u->path[pl++] = t[p++];
    }
    u->path[pl] = 0;
    return normalize_path(u->path) == 0 ? OS_NET_OK : OS_NET_ERR_URL;
}

int os_url_format(const os_url *u, char *out, size_t cap)
{
    buf_t b = { out, cap, 0, 0 };
    if (!u || !out || cap == 0) return OS_NET_ERR_ARGUMENT;
    put(&b, u->https ? "https://" : "http://");
    put(&b, u->host);
    if (u->port != (u->https ? 443u : 80u)) { put(&b, ":"); put_u(&b, u->port); }
    put(&b, u->path);
    if (b.fail) return OS_NET_ERR_SIZE;
    out[b.len] = 0;
    return OS_NET_OK;
}

int os_url_resolve(const os_url *base, const char *ref, os_url *out)
{
    char tmp[OS_URL_PATH_MAX];
    size_t o = 0, i = 0;
    if (!base || !ref || !out) return OS_NET_ERR_ARGUMENT;
    if (slen(ref) > OS_URL_MAX || !valid_chars(ref)) return OS_NET_ERR_URL;
    if (!*ref || *ref == '#') { *out = *base; return OS_NET_OK; }
    for (size_t k = 0; ref[k] && ref[k] != '/' && ref[k] != '?' && ref[k] != '#'; k++) {
        if (ref[k] == ':') {
            if (prefix_ci(ref, "http://") || prefix_ci(ref, "https://")) return os_url_parse(ref, out);
            return OS_NET_ERR_SCHEME;
        }
    }
    if (ref[0] == '/' && ref[1] == '/') return OS_NET_ERR_UNSUPPORTED;
    if (ref[0] != '/') {
        size_t bq = 0, last = 0;
        while (base->path[bq] && base->path[bq] != '?') { if (base->path[bq] == '/') last = bq; bq++; }
        if (ref[0] == '?') {
            for (size_t k = 0; k < bq; k++) { if (o + 1u >= sizeof tmp) return OS_NET_ERR_URL; tmp[o++] = base->path[k]; }
        } else {
            for (size_t k = 0; k <= last; k++) { if (o + 1u >= sizeof tmp) return OS_NET_ERR_URL; tmp[o++] = base->path[k]; }
        }
    }
    while (ref[i] && ref[i] != '#') {
        if (o + 1u >= sizeof tmp) return OS_NET_ERR_URL;
        tmp[o++] = ref[i++];
    }
    tmp[o] = 0;
    if (normalize_path(tmp) != 0) return OS_NET_ERR_URL;
    *out = *base;
    for (size_t k = 0; k <= slen(tmp); k++) out->path[k] = tmp[k];
    return OS_NET_OK;
}

int os_http_build_request(const os_url *u, char *out, size_t cap)
{
    buf_t b = { out, cap, 0, 0 };
    if (!u || !out || cap == 0) return OS_NET_ERR_ARGUMENT;
    put(&b, "GET "); put(&b, u->path); put(&b, " HTTP/1.1\r\nHost: "); put(&b, u->host);
    if (u->port != (u->https ? 443u : 80u)) { put(&b, ":"); put_u(&b, u->port); }
    put(&b, "\r\nUser-Agent: ExperimentalOS-Browser/1\r\nAccept: text/html, image/bmp, image/x-portable-pixmap\r\nConnection: close\r\n\r\n");
    if (b.fail) return OS_NET_ERR_SIZE;
    out[b.len] = 0;
    return (int)b.len;
}

int os_http_parse_response(const uint8_t *buf, size_t len, os_http_response *r)
{
    size_t i = 0, hend = 0;
    int have_cl = 0;
    size_t cl = 0;
    if (!buf || !r) return OS_NET_ERR_ARGUMENT;
    r->status = 0; r->content_type[0] = 0; r->location[0] = 0; r->body_off = 0; r->body_len = 0;
    if (len < 16) return OS_NET_ERR_RESPONSE;
    for (size_t k = 3; k < len && k < OS_HTTP_HEADER_MAX + 4u; k++) {
        if (buf[k - 3u] == '\r' && buf[k - 2u] == '\n' && buf[k - 1u] == '\r' && buf[k] == '\n') { hend = k + 1u; break; }
    }
    if (!hend) return OS_NET_ERR_RESPONSE;
    for (size_t k = 0; k < hend; k++) {
        uint8_t c = buf[k];
        if (c == '\r') { if (k + 1u >= hend || buf[k + 1u] != '\n') return OS_NET_ERR_RESPONSE; }
        else if (c == '\n') { if (k == 0 || buf[k - 1u] != '\r') return OS_NET_ERR_RESPONSE; }
        else if (c < 0x20 || c > 0x7e) return OS_NET_ERR_RESPONSE;
    }
    if (!((buf[5] == '1' && buf[7] == '1') || (buf[5] == '1' && buf[7] == '0')) ||
        buf[0] != 'H' || buf[1] != 'T' || buf[2] != 'T' || buf[3] != 'P' || buf[4] != '/' || buf[6] != '.' || buf[8] != ' ')
        return OS_NET_ERR_RESPONSE;
    for (i = 9; i < 12; i++) if (buf[i] < '0' || buf[i] > '9') return OS_NET_ERR_RESPONSE;
    r->status = (buf[9] - '0') * 100 + (buf[10] - '0') * 10 + (buf[11] - '0');
    if (r->status < 100 || r->status > 599 || (buf[12] != ' ' && buf[12] != '\r')) return OS_NET_ERR_RESPONSE;
    while (i < hend && !(buf[i] == '\r' && buf[i + 1u] == '\n')) i++;
    i += 2u;
    while (i + 2u < hend + 0u && !(buf[i] == '\r' && buf[i + 1u] == '\n')) {
        char name[24];
        size_t nl = 0, vs, ve;
        while (buf[i] != ':' ) {
            uint8_t c = buf[i];
            int tok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
            if (!tok || nl + 1u >= sizeof name) return OS_NET_ERR_RESPONSE;
            name[nl++] = lower((char)c);
            i++;
        }
        name[nl] = 0;
        if (nl == 0) return OS_NET_ERR_RESPONSE;
        i++;
        while (buf[i] == ' ' || buf[i] == '\t') i++;
        vs = i;
        while (!(buf[i] == '\r' && buf[i + 1u] == '\n')) i++;
        ve = i;
        while (ve > vs && (buf[ve - 1u] == ' ' || buf[ve - 1u] == '\t')) ve--;
        i += 2u;
        if (seq(name, "transfer-encoding") || seq(name, "content-encoding")) return OS_NET_ERR_UNSUPPORTED;
        if (seq(name, "content-length")) {
            size_t v = 0;
            if (ve == vs || ve - vs > 9u) return OS_NET_ERR_RESPONSE;
            for (size_t k = vs; k < ve; k++) {
                if (buf[k] < '0' || buf[k] > '9') return OS_NET_ERR_RESPONSE;
                v = v * 10u + (size_t)(buf[k] - '0');
            }
            if (have_cl && v != cl) return OS_NET_ERR_RESPONSE;
            have_cl = 1; cl = v;
        } else if (seq(name, "content-type")) {
            size_t n = 0;
            for (size_t k = vs; k < ve && buf[k] != ';'; k++) {
                if (n + 1u >= sizeof r->content_type) return OS_NET_ERR_RESPONSE;
                r->content_type[n++] = lower((char)buf[k]);
            }
            while (n && r->content_type[n - 1u] == ' ') n--;
            r->content_type[n] = 0;
        } else if (seq(name, "location")) {
            if (ve - vs + 1u >= sizeof r->location) return OS_NET_ERR_RESPONSE;
            for (size_t k = vs; k < ve; k++) r->location[k - vs] = (char)buf[k];
            r->location[ve - vs] = 0;
        }
    }
    r->body_off = hend;
    if (have_cl) {
        if (cl != len - hend) return OS_NET_ERR_RESPONSE;
        r->body_len = cl;
    } else {
        r->body_len = len - hend;
    }
    return OS_NET_OK;
}

void os_net_policy_init(os_net_policy *p, uint32_t max_response)
{
    if (!p) return;
    p->allowed_count = 0;
    p->max_response = max_response;
    for (unsigned i = 0; i < OS_NET_ALLOW_MAX; i++) p->allowed_host[i][0] = 0;
}

int os_net_policy_allow(os_net_policy *p, const char *host)
{
    char h[OS_URL_HOST_MAX];
    if (!p || !host || !host_ok(host, h) || p->allowed_count >= OS_NET_ALLOW_MAX) return OS_NET_ERR_ARGUMENT;
    for (size_t k = 0; k < OS_URL_HOST_MAX; k++) { p->allowed_host[p->allowed_count][k] = h[k]; if (!h[k]) break; }
    p->allowed_count++;
    return OS_NET_OK;
}

static int allowed(const os_net_policy *p, const char *host)
{
    for (unsigned i = 0; i < p->allowed_count; i++) if (seq(p->allowed_host[i], host)) return 1;
    return 0;
}

int os_net_fetch(const os_net_policy *p, os_net_transport tr, void *ctx, os_url *url,
                 uint8_t *buffer, size_t cap, os_http_response *resp)
{
    char req[512];
    if (!p || !tr || !url || !buffer || !resp) return OS_NET_ERR_ARGUMENT;
    for (unsigned redir = 0;; redir++) {
        size_t got = 0;
        int rl, rc;
        os_url next;
        if (!allowed(p, url->host)) return OS_NET_ERR_DENIED;
        if (url->https) return OS_NET_ERR_TLS;
        rl = os_http_build_request(url, req, sizeof req);
        if (rl < 0) return rl;
        if (tr(ctx, url, req, (size_t)rl, buffer, cap, &got) != 0) return OS_NET_ERR_TRANSPORT;
        if (got > cap || got > p->max_response) return OS_NET_ERR_SIZE;
        rc = os_http_parse_response(buffer, got, resp);
        if (rc != OS_NET_OK) return rc;
        if ((resp->status == 301 || resp->status == 302 || resp->status == 303 || resp->status == 307 ||
             resp->status == 308) && resp->location[0]) {
            if (redir >= OS_NET_MAX_REDIRECTS) return OS_NET_ERR_REDIRECTS;
            rc = os_url_resolve(url, resp->location, &next);
            if (rc != OS_NET_OK) return rc;
            *url = next;
            continue;
        }
        return OS_NET_OK;
    }
}
