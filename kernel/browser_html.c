#include "browser_html.h"

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static int is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

typedef struct {
    os_html_doc *d;
    const char *h;
    size_t n;
    int stack[OS_HTML_MAX_DEPTH];
    unsigned depth;
    int pending_ws;
} parser_t;

static const struct { const char *name; uint8_t tag; uint8_t is_void; } TAGS[] = {
    { "html", OS_TAG_HTML, 0 }, { "head", OS_TAG_HEAD, 0 }, { "body", OS_TAG_BODY, 0 }, { "title", OS_TAG_TITLE, 0 },
    { "h1", OS_TAG_H1, 0 }, { "h2", OS_TAG_H2, 0 }, { "h3", OS_TAG_H3, 0 }, { "p", OS_TAG_P, 0 },
    { "div", OS_TAG_DIV, 0 }, { "br", OS_TAG_BR, 1 }, { "a", OS_TAG_A, 0 }, { "ul", OS_TAG_UL, 0 },
    { "li", OS_TAG_LI, 0 }, { "b", OS_TAG_B, 0 }, { "i", OS_TAG_I, 0 }, { "pre", OS_TAG_PRE, 0 },
    { "img", OS_TAG_IMG, 1 }, { "script", OS_TAG_SCRIPT, 0 }, { "style", OS_TAG_STYLE, 0 },
    { "form", OS_TAG_FORM, 0 }, { "input", OS_TAG_INPUT, 1 }, { "hr", OS_TAG_HR, 1 },
};

static int name_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static int pool_store(os_html_doc *d, const char *s, size_t len, uint16_t *off)
{
    if (d->pool_len + len + 1u > OS_HTML_POOL) return 0;
    *off = (uint16_t)d->pool_len;
    for (size_t i = 0; i < len; i++) d->pool[d->pool_len++] = s[i];
    d->pool[d->pool_len++] = 0;
    return 1;
}

static int add_node(parser_t *p, uint8_t tag, os_html_node **out)
{
    os_html_doc *d = p->d;
    os_html_node *nd;
    if (d->count >= OS_HTML_MAX_NODES) return OS_HTML_ERR_LIMIT;
    nd = &d->nodes[d->count];
    nd->tag = tag;
    nd->space_before = (uint8_t)(p->pending_ws ? 1 : 0);
    p->pending_ws = 0;
    nd->parent = p->depth ? (int16_t)p->stack[p->depth - 1u] : (int16_t)-1;
    nd->end = (uint16_t)(d->count + 1u);
    nd->text_off = nd->text_len = nd->attr_off = nd->attr_len = 0;
    d->count++;
    *out = nd;
    return OS_HTML_OK;
}

static int in_pre(const parser_t *p)
{
    for (unsigned i = 0; i < p->depth; i++) if (p->d->nodes[p->stack[i]].tag == OS_TAG_PRE) return 1;
    return 0;
}

static void pop_to(parser_t *p, unsigned new_depth)
{
    while (p->depth > new_depth) {
        p->depth--;
        p->d->nodes[p->stack[p->depth]].end = (uint16_t)p->d->count;
    }
}

static int decode_entity(const char *h, size_t n, size_t i, char *out, size_t *used)
{
    static const struct { const char *name; char ch; } E[] = {
        { "amp;", '&' }, { "lt;", '<' }, { "gt;", '>' }, { "quot;", '"' }, { "apos;", '\'' }, { "nbsp;", ' ' }
    };
    for (unsigned k = 0; k < sizeof E / sizeof E[0]; k++) {
        size_t l = 0;
        while (E[k].name[l] && i + 1u + l < n && h[i + 1u + l] == E[k].name[l]) l++;
        if (!E[k].name[l]) { *out = E[k].ch; *used = 1u + l; return 1; }
    }
    if (i + 2u < n && h[i + 1u] == '#') {
        size_t j = i + 2u;
        unsigned v = 0;
        int hex = 0, digits = 0;
        if (j < n && (h[j] == 'x' || h[j] == 'X')) { hex = 1; j++; }
        while (j < n && digits < 6) {
            char c = h[j];
            unsigned dv;
            if (c >= '0' && c <= '9') dv = (unsigned)(c - '0');
            else if (hex && lower(c) >= 'a' && lower(c) <= 'f') dv = (unsigned)(lower(c) - 'a' + 10);
            else break;
            v = v * (hex ? 16u : 10u) + dv;
            digits++; j++;
        }
        if (digits && j < n && h[j] == ';') {
            *out = (v >= 32 && v < 127) ? (char)v : '?';
            *used = j + 1u - i;
            return 1;
        }
    }
    return 0;
}

static int add_text(parser_t *p, size_t s, size_t e)
{
    os_html_doc *d = p->d;
    os_html_node *nd;
    unsigned start = d->pool_len;
    int pre = in_pre(p), lead_ws = 0, last_ws = 0, any = 0;
    size_t i = s;
    if (pre) {
        for (; i < e; i++) {
            char c = p->h[i];
            size_t used;
            if (c == '&' && decode_entity(p->h, e, i, &c, &used)) i += used - 1u;
            if (c == '\r') continue;
            if (d->pool_len + 2u > OS_HTML_POOL) return OS_HTML_ERR_LIMIT;
            d->pool[d->pool_len++] = (c == '\n' || (c >= 32 && c < 127)) ? c : '?';
            any = 1;
        }
    } else {
        for (; i < e; i++) {
            char c = p->h[i];
            size_t used;
            if (c == '&' && decode_entity(p->h, e, i, &c, &used)) i += used - 1u;
            if (is_ws(c)) { if (!any) lead_ws = 1; last_ws = 1; continue; }
            if (last_ws && any) {
                if (d->pool_len + 2u > OS_HTML_POOL) return OS_HTML_ERR_LIMIT;
                d->pool[d->pool_len++] = ' ';
            }
            last_ws = 0;
            if (d->pool_len + 2u > OS_HTML_POOL) return OS_HTML_ERR_LIMIT;
            d->pool[d->pool_len++] = (c >= 32 && c < 127) ? c : '?';
            any = 1;
        }
    }
    if (!any) {
        d->pool_len = start;
        if (!pre) p->pending_ws = p->pending_ws || lead_ws;
        return OS_HTML_OK;
    }
    d->pool[d->pool_len++] = 0;
    {
        int rc = add_node(p, OS_TAG_TEXT, &nd);
        if (rc) return rc;
    }
    if (lead_ws) nd->space_before = 1;
    nd->text_off = (uint16_t)start;
    nd->text_len = (uint16_t)(d->pool_len - start - 1u);
    p->pending_ws = (!pre && last_ws) ? 1 : 0;
    if (nd->parent >= 0 && d->nodes[nd->parent].tag == OS_TAG_TITLE && d->title_len == 0) {
        d->title_off = nd->text_off;
        d->title_len = nd->text_len;
    }
    return OS_HTML_OK;
}

static int starts_ci(const parser_t *p, size_t i, const char *lit)
{
    for (size_t k = 0; lit[k]; k++) if (i + k >= p->n || lower(p->h[i + k]) != lit[k]) return 0;
    return 1;
}

static int parse_start_tag(parser_t *p, size_t *pos)
{
    size_t i = *pos + 1u;
    char name[12];
    size_t nl = 0;
    uint8_t tag = OS_TAG_OTHER, is_void = 0;
    int self_close = 0;
    os_html_node *nd;
    uint16_t aoff = 0, alen = 0;
    int have_attr = 0, rc;
    while (i < p->n && !is_ws(p->h[i]) && p->h[i] != '>' && p->h[i] != '/') {
        if (nl + 1u < sizeof name) name[nl++] = lower(p->h[i]);
        else nl = sizeof name;
        i++;
    }
    name[nl < sizeof name ? nl : sizeof name - 1u] = 0;
    if (nl < sizeof name) {
        for (unsigned k = 0; k < sizeof TAGS / sizeof TAGS[0]; k++)
            if (name_eq(TAGS[k].name, name)) { tag = TAGS[k].tag; is_void = TAGS[k].is_void; }
    }
    rc = add_node(p, tag, &nd);
    if (rc) return rc;
    for (;;) {
        char an[14];
        size_t al = 0, vs = 0, ve = 0;
        int quoted = 0;
        while (i < p->n && (is_ws(p->h[i]) || p->h[i] == '/')) { if (p->h[i] == '/') self_close = 1; i++; }
        if (i >= p->n) return OS_HTML_ERR_SYNTAX;
        if (p->h[i] == '>') { i++; break; }
        self_close = 0;
        while (i < p->n && !is_ws(p->h[i]) && p->h[i] != '=' && p->h[i] != '>' && p->h[i] != '/') {
            if (al + 1u < sizeof an) an[al++] = lower(p->h[i]);
            i++;
        }
        an[al] = 0;
        while (i < p->n && is_ws(p->h[i])) i++;
        if (i < p->n && p->h[i] == '=') {
            i++;
            while (i < p->n && is_ws(p->h[i])) i++;
            if (i < p->n && (p->h[i] == '"' || p->h[i] == '\'')) {
                char q = p->h[i++];
                quoted = 1;
                vs = i;
                while (i < p->n && p->h[i] != q) i++;
                if (i >= p->n) return OS_HTML_ERR_SYNTAX;
                ve = i++;
            } else {
                vs = i;
                while (i < p->n && !is_ws(p->h[i]) && p->h[i] != '>') i++;
                ve = i;
            }
        }
        (void)quoted;
        if (al >= 2 && an[0] == 'o' && an[1] == 'n') p->d->ignored_handlers++;
        if (!have_attr && ve > vs && ve - vs <= OS_HTML_ATTR_MAX &&
            ((tag == OS_TAG_A && name_eq(an, "href")) || (tag == OS_TAG_IMG && name_eq(an, "src")) ||
             (tag == OS_TAG_INPUT && name_eq(an, "name")))) {
            int ok = 1;
            for (size_t k = vs; k < ve; k++) if ((unsigned char)p->h[k] < 0x20 || (unsigned char)p->h[k] > 0x7e) ok = 0;
            if (ok && pool_store(p->d, p->h + vs, ve - vs, &aoff)) { alen = (uint16_t)(ve - vs); have_attr = 1; }
        }
    }
    nd->attr_off = aoff; nd->attr_len = alen;
    if (tag == OS_TAG_A && alen && p->d->link_count < OS_HTML_MAX_LINKS)
        p->d->link_node[p->d->link_count++] = (uint16_t)(nd - p->d->nodes);
    if (tag == OS_TAG_IMG) p->d->image_count++;
    *pos = i;
    if (tag == OS_TAG_SCRIPT || tag == OS_TAG_STYLE) {
        const char *close = tag == OS_TAG_SCRIPT ? "</script" : "</style";
        size_t cs = i;
        while (cs < p->n && !starts_ci(p, cs, close)) cs++;
        if (cs >= p->n) return OS_HTML_ERR_SYNTAX;
        if (tag == OS_TAG_SCRIPT) {
            if (cs - i > OS_HTML_SCRIPT_MAX || p->d->script_count >= OS_HTML_MAX_SCRIPTS) return OS_HTML_ERR_LIMIT;
            if (!pool_store(p->d, p->h + i, cs - i, &nd->text_off)) return OS_HTML_ERR_LIMIT;
            nd->text_len = (uint16_t)(cs - i);
            p->d->script_node[p->d->script_count++] = (uint16_t)(nd - p->d->nodes);
        }
        while (cs < p->n && p->h[cs] != '>') cs++;
        if (cs >= p->n) return OS_HTML_ERR_SYNTAX;
        *pos = cs + 1u;
        return OS_HTML_OK;
    }
    if (is_void || self_close) return OS_HTML_OK;
    if (p->depth >= OS_HTML_MAX_DEPTH) return OS_HTML_ERR_DEPTH;
    p->stack[p->depth++] = (int)(nd - p->d->nodes);
    return OS_HTML_OK;
}

static void parse_end_tag(parser_t *p, size_t *pos)
{
    size_t i = *pos + 2u;
    char name[12];
    size_t nl = 0;
    uint8_t tag = OS_TAG_OTHER;
    while (i < p->n && p->h[i] != '>' && !is_ws(p->h[i])) {
        if (nl + 1u < sizeof name) name[nl++] = lower(p->h[i]);
        else nl = sizeof name;
        i++;
    }
    name[nl < sizeof name ? nl : sizeof name - 1u] = 0;
    while (i < p->n && p->h[i] != '>') i++;
    *pos = i < p->n ? i + 1u : i;
    if (nl < sizeof name)
        for (unsigned k = 0; k < sizeof TAGS / sizeof TAGS[0]; k++) if (name_eq(TAGS[k].name, name)) tag = TAGS[k].tag;
    for (unsigned k = p->depth; k > 0; k--) {
        if (p->d->nodes[p->stack[k - 1u]].tag == tag) { pop_to(p, k - 1u); return; }
    }
}

int os_html_parse(os_html_doc *d, const char *h, size_t n)
{
    parser_t p;
    size_t i = 0;
    int rc = OS_HTML_OK;
    if (!d || !h) return OS_HTML_ERR_ARGUMENT;
    d->count = 0; d->pool_len = 0; d->title_off = d->title_len = 0; d->script_count = 0; d->link_count = 0;
    d->image_count = 0; d->ignored_handlers = 0;
    if (n > OS_HTML_MAX_BYTES) return OS_HTML_ERR_SIZE;
    p.d = d; p.h = h; p.n = n; p.depth = 0; p.pending_ws = 0;
    while (i < n && rc == OS_HTML_OK) {
        if (h[i] == '<' && i + 1u < n) {
            if (starts_ci(&p, i, "<!--")) {
                size_t j = i + 4u;
                while (j + 2u < n + 0u && !(h[j] == '-' && h[j + 1u] == '-' && h[j + 2u] == '>')) j++;
                if (j + 2u >= n) { rc = OS_HTML_ERR_SYNTAX; break; }
                i = j + 3u;
                continue;
            }
            if (h[i + 1u] == '!') {
                while (i < n && h[i] != '>') i++;
                if (i >= n) { rc = OS_HTML_ERR_SYNTAX; break; }
                i++;
                continue;
            }
            if (h[i + 1u] == '/') { parse_end_tag(&p, &i); continue; }
            if (is_alpha(h[i + 1u])) { rc = parse_start_tag(&p, &i); continue; }
        }
        {
            size_t j = i + 1u;
            while (j < n && h[j] != '<') j++;
            rc = add_text(&p, i, j);
            i = j;
        }
    }
    if (rc != OS_HTML_OK) { d->count = 0; d->pool_len = 0; d->script_count = 0; d->link_count = 0; d->image_count = 0; return rc; }
    pop_to(&p, 0);
    return OS_HTML_OK;
}

typedef struct {
    char *out;
    size_t cap, len;
    unsigned width, col;
    int pending, last_blank, fail;
    const os_html_doc *d;
} render_t;

static void rput(render_t *r, char c)
{
    if (r->len + 1u >= r->cap) { r->fail = 1; return; }
    r->out[r->len++] = c;
}

static void block(render_t *r, int blank)
{
    if (r->col > 0) { rput(r, '\n'); r->col = 0; r->last_blank = 0; }
    if (blank && r->len > 0 && !r->last_blank) { rput(r, '\n'); r->last_blank = 1; }
    r->pending = 0;
}

static void emit_word(render_t *r, const char *w, size_t len)
{
    size_t k = 0;
    if (r->col > 0 && r->col + (r->pending ? 1u : 0u) + len > r->width) { rput(r, '\n'); r->col = 0; r->pending = 0; }
    if (r->col > 0 && r->pending) { rput(r, ' '); r->col++; }
    r->pending = 0;
    while (k < len) {
        if (r->col >= r->width) { rput(r, '\n'); r->col = 0; }
        rput(r, w[k++]);
        r->col++;
    }
    r->last_blank = 0;
}

static void emit_lit(render_t *r, const char *s)
{
    size_t l = 0;
    while (s[l]) l++;
    emit_word(r, s, l);
}

static void emit_text(render_t *r, const char *s, size_t len, int pre)
{
    size_t i = 0;
    if (pre) {
        for (; i < len; i++) {
            if (s[i] == '\n') { rput(r, '\n'); r->col = 0; continue; }
            if (r->col >= r->width) { rput(r, '\n'); r->col = 0; }
            rput(r, s[i]); r->col++;
            r->last_blank = 0;
        }
        return;
    }
    while (i < len) {
        size_t j;
        if (s[i] == ' ') { r->pending = 1; i++; continue; }
        j = i;
        while (j < len && s[j] != ' ') j++;
        emit_word(r, s + i, j - i);
        i = j;
    }
}

static unsigned render_node(render_t *r, unsigned idx, int pre, int upper);

static void number_suffix(render_t *r, unsigned n)
{
    char t[8];
    unsigned k = 0, v = n;
    t[k++] = ']';
    do { t[k++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    t[k++] = '[';
    {
        char w[8];
        for (unsigned a = 0; a < k; a++) w[a] = t[k - 1u - a];
        r->pending = 0;
        emit_word(r, w, k);
    }
}

static int link_number(const os_html_doc *d, unsigned idx)
{
    for (unsigned i = 0; i < d->link_count; i++) if (d->link_node[i] == idx) return (int)i + 1;
    return 0;
}

static unsigned render_node(render_t *r, unsigned idx, int pre, int upper)
{
    const os_html_doc *d = r->d;
    const os_html_node *nd = &d->nodes[idx];
    unsigned next = idx + 1u, c;
    int blank = 0, bullet = 0, child_pre = pre, child_upper = upper;
    switch ((enum os_html_tag)nd->tag) {
    case OS_TAG_HEAD: case OS_TAG_TITLE: case OS_TAG_SCRIPT: case OS_TAG_STYLE:
        return nd->end;
    case OS_TAG_TEXT: {
        if (nd->space_before && !pre) r->pending = 1;
        if (upper) {
            char tmp[OS_HTML_POOL > 512 ? 512 : OS_HTML_POOL];
            size_t l = nd->text_len < sizeof tmp ? nd->text_len : sizeof tmp - 1u;
            for (size_t i = 0; i < l; i++) { char ch = d->pool[nd->text_off + i]; tmp[i] = (ch >= 'a' && ch <= 'z') ? (char)(ch - 32) : ch; }
            emit_text(r, tmp, l, pre);
        } else {
            emit_text(r, d->pool + nd->text_off, nd->text_len, pre);
        }
        return nd->end;
    }
    case OS_TAG_BR: rput(r, '\n'); r->col = 0; r->pending = 0; return nd->end;
    case OS_TAG_HR: block(r, 0); for (unsigned i = 0; i < r->width && i < 40u; i++) { rput(r, '-'); r->col++; } block(r, 0); return nd->end;
    case OS_TAG_IMG: {
        char w[8];
        unsigned k = 0, num = 0;
        for (unsigned i = 0; i <= idx; i++) if (d->nodes[i].tag == OS_TAG_IMG) num++;
        if (nd->space_before) r->pending = 1;
        emit_lit(r, "[img");
        { unsigned v = num; char t[6]; unsigned m = 0; do { t[m++] = (char)('0' + v % 10u); v /= 10u; } while (v); while (m) w[k++] = t[--m]; }
        r->pending = 0;
        emit_word(r, w, k);
        r->pending = 0;
        emit_lit(r, "]");
        return nd->end;
    }
    case OS_TAG_INPUT:
        if (nd->space_before) r->pending = 1;
        emit_lit(r, "[input]");
        return nd->end;
    case OS_TAG_P: case OS_TAG_H1: case OS_TAG_H2: case OS_TAG_H3: blank = 1; break;
    case OS_TAG_DIV: case OS_TAG_UL: case OS_TAG_FORM: case OS_TAG_BODY: break;
    case OS_TAG_LI: bullet = 1; break;
    case OS_TAG_PRE: blank = 1; child_pre = 1; break;
    default: break;
    }
    if (nd->tag == OS_TAG_H1) child_upper = 1;
    if (nd->tag == OS_TAG_P || nd->tag == OS_TAG_H1 || nd->tag == OS_TAG_H2 || nd->tag == OS_TAG_H3 || nd->tag == OS_TAG_DIV ||
        nd->tag == OS_TAG_UL || nd->tag == OS_TAG_FORM || nd->tag == OS_TAG_BODY || nd->tag == OS_TAG_LI || nd->tag == OS_TAG_PRE) {
        block(r, blank);
        if (bullet) { emit_lit(r, "*"); }
    } else if (nd->space_before) {
        r->pending = 1;
    }
    for (c = idx + 1u; c < nd->end;) c = render_node(r, c, child_pre, child_upper);
    if (nd->tag == OS_TAG_A) {
        int ln = link_number(d, idx);
        if (ln > 0) number_suffix(r, (unsigned)ln);
    }
    if (nd->tag == OS_TAG_P || nd->tag == OS_TAG_H1 || nd->tag == OS_TAG_H2 || nd->tag == OS_TAG_H3 ||
        nd->tag == OS_TAG_PRE) block(r, 1);
    else if (nd->tag == OS_TAG_DIV || nd->tag == OS_TAG_LI || nd->tag == OS_TAG_UL) block(r, 0);
    (void)next;
    return nd->end;
}

int os_html_render(const os_html_doc *d, unsigned width, char *out, size_t cap, size_t *length)
{
    render_t r;
    unsigned i = 0;
    if (!d || !out || cap == 0 || width < 20 || width > 120) return OS_HTML_ERR_ARGUMENT;
    r.out = out; r.cap = cap; r.len = 0; r.width = width; r.col = 0; r.pending = 0; r.last_blank = 1; r.fail = 0; r.d = d;
    while (i < d->count) i = render_node(&r, i, 0, 0);
    while (r.len > 0 && (out[r.len - 1u] == '\n' || out[r.len - 1u] == ' ')) r.len--;
    out[r.len] = 0;
    if (length) *length = r.len;
    return r.fail ? OS_HTML_ERR_RENDER : OS_HTML_OK;
}

static int copy_attr(const os_html_doc *d, unsigned idx, char *out, size_t cap)
{
    const os_html_node *nd = &d->nodes[idx];
    if (!nd->attr_len || (size_t)nd->attr_len + 1u > cap) return OS_HTML_ERR_ARGUMENT;
    for (unsigned i = 0; i <= nd->attr_len; i++) out[i] = d->pool[nd->attr_off + i];
    return nd->attr_len;
}

int os_html_link_href(const os_html_doc *d, unsigned n, char *out, size_t cap)
{
    if (!d || !out || n == 0 || n > d->link_count) return OS_HTML_ERR_ARGUMENT;
    return copy_attr(d, d->link_node[n - 1u], out, cap);
}

int os_html_image_src(const os_html_doc *d, unsigned n, char *out, size_t cap)
{
    unsigned seen = 0;
    if (!d || !out || n == 0) return OS_HTML_ERR_ARGUMENT;
    for (unsigned i = 0; i < d->count; i++) {
        if (d->nodes[i].tag != OS_TAG_IMG) continue;
        if (++seen == n) return copy_attr(d, i, out, cap);
    }
    return OS_HTML_ERR_ARGUMENT;
}
