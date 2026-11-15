#include "jsonrpc.h"

typedef struct {
    const char *text;
    size_t length;
    size_t pos;
} cursor;

static int at_end(const cursor *c) { return c->pos >= c->length; }
static int peek(const cursor *c) { return at_end(c) ? -1 : (unsigned char)c->text[c->pos]; }

static void skip_ws(cursor *c)
{
    while (!at_end(c)) {
        char ch = c->text[c->pos];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') c->pos++;
        else break;
    }
}

static int is_hex(int ch)
{
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

/* Parses a string; reports the raw span between the quotes and whether it has escapes. */
static int parse_string(cursor *c, size_t *start, size_t *len, int *escaped)
{
    if (peek(c) != '"') return 0;
    c->pos++;
    *start = c->pos;
    *escaped = 0;
    for (;;) {
        if (at_end(c)) return 0;
        unsigned char ch = (unsigned char)c->text[c->pos];
        if (ch == '"') break;
        if (ch < 0x20u) return 0;
        if (ch == '\\') {
            *escaped = 1;
            c->pos++;
            int e = peek(c);
            if (e == 'u') {
                for (int i = 1; i <= 4; i++) {
                    if (c->pos + (size_t)i >= c->length || !is_hex((unsigned char)c->text[c->pos + (size_t)i])) return 0;
                }
                c->pos += 5;
            } else if (e == '"' || e == '\\' || e == '/' || e == 'b' || e == 'f' ||
                       e == 'n' || e == 'r' || e == 't') {
                c->pos++;
            } else {
                return 0;
            }
        } else if (ch < 0x80u) {
            c->pos++;
        } else {
            size_t extra = ch >= 0xf0u ? 3u : ch >= 0xe0u ? 2u : ch >= 0xc2u ? 1u : 0u;
            if (extra == 0 || ch > 0xf4u || c->pos + extra >= c->length) return 0;
            for (size_t i = 1; i <= extra; i++)
                if (((unsigned char)c->text[c->pos + i] & 0xc0u) != 0x80u) return 0;
            if (ch == 0xe0u && (unsigned char)c->text[c->pos + 1] < 0xa0u) return 0;
            if (ch == 0xedu && (unsigned char)c->text[c->pos + 1] >= 0xa0u) return 0;
            if (ch == 0xf0u && (unsigned char)c->text[c->pos + 1] < 0x90u) return 0;
            if (ch == 0xf4u && (unsigned char)c->text[c->pos + 1] >= 0x90u) return 0;
            c->pos += extra + 1u;
        }
        if (c->pos - *start > OS_JRPC_MAX_STRING) return 0;
    }
    *len = c->pos - *start;
    c->pos++;
    return 1;
}

/* Parses a number token; sets integer value when it is a plain integer. */
static int parse_number(cursor *c, int *is_int, int64_t *value)
{
    size_t start = c->pos;
    int negative = 0;
    *is_int = 1;
    if (peek(c) == '-') { negative = 1; c->pos++; }
    int ch = peek(c);
    if (ch == '0') {
        c->pos++;
    } else if (ch >= '1' && ch <= '9') {
        while (peek(c) >= '0' && peek(c) <= '9') c->pos++;
    } else {
        return 0;
    }
    if (peek(c) == '.') {
        *is_int = 0;
        c->pos++;
        if (!(peek(c) >= '0' && peek(c) <= '9')) return 0;
        while (peek(c) >= '0' && peek(c) <= '9') c->pos++;
    }
    if (peek(c) == 'e' || peek(c) == 'E') {
        *is_int = 0;
        c->pos++;
        if (peek(c) == '+' || peek(c) == '-') c->pos++;
        if (!(peek(c) >= '0' && peek(c) <= '9')) return 0;
        while (peek(c) >= '0' && peek(c) <= '9') c->pos++;
    }
    if (*is_int) {
        size_t digits = c->pos - start - (size_t)negative;
        if (digits > 16u) return 0;
        int64_t v = 0;
        for (size_t i = start + (size_t)negative; i < c->pos; i++) v = v * 10 + (c->text[i] - '0');
        if (v > 9007199254740991LL) return 0;
        *value = negative ? -v : v;
    }
    return 1;
}

static int literal(cursor *c, const char *word)
{
    size_t i = 0;
    while (word[i]) {
        if (c->pos + i >= c->length || c->text[c->pos + i] != word[i]) return 0;
        i++;
    }
    c->pos += i;
    return 1;
}

static int span_equal(const cursor *c, size_t a, size_t alen, size_t b, size_t blen)
{
    if (alen != blen) return 0;
    for (size_t i = 0; i < alen; i++)
        if (c->text[a + i] != c->text[b + i]) return 0;
    return 1;
}

static int parse_value(cursor *c, unsigned depth);

static int parse_object(cursor *c, unsigned depth)
{
    size_t keys[OS_JRPC_MAX_OBJECT_KEYS][2];
    unsigned count = 0;
    if (depth > OS_JRPC_MAX_DEPTH) return 0;
    c->pos++;
    skip_ws(c);
    if (peek(c) == '}') { c->pos++; return 1; }
    for (;;) {
        size_t start, len;
        int escaped;
        skip_ws(c);
        if (!parse_string(c, &start, &len, &escaped)) return 0;
        if (count == OS_JRPC_MAX_OBJECT_KEYS) return 0;
        /* Escaped keys are compared raw, so an escaped alias cannot be a duplicate. Reject them. */
        if (escaped) return 0;
        for (unsigned i = 0; i < count; i++)
            if (span_equal(c, keys[i][0], keys[i][1], start, len)) return 0;
        keys[count][0] = start;
        keys[count][1] = len;
        count++;
        skip_ws(c);
        if (peek(c) != ':') return 0;
        c->pos++;
        if (!parse_value(c, depth + 1u)) return 0;
        skip_ws(c);
        if (peek(c) == ',') { c->pos++; continue; }
        if (peek(c) == '}') { c->pos++; return 1; }
        return 0;
    }
}

static int parse_array(cursor *c, unsigned depth)
{
    if (depth > OS_JRPC_MAX_DEPTH) return 0;
    c->pos++;
    skip_ws(c);
    if (peek(c) == ']') { c->pos++; return 1; }
    for (;;) {
        if (!parse_value(c, depth + 1u)) return 0;
        skip_ws(c);
        if (peek(c) == ',') { c->pos++; continue; }
        if (peek(c) == ']') { c->pos++; return 1; }
        return 0;
    }
}

static int parse_value(cursor *c, unsigned depth)
{
    skip_ws(c);
    int ch = peek(c);
    size_t s, l;
    int e, is_int;
    int64_t v;
    if (ch == '{') return parse_object(c, depth);
    if (ch == '[') return parse_array(c, depth);
    if (ch == '"') return parse_string(c, &s, &l, &e);
    if (ch == 't') return literal(c, "true");
    if (ch == 'f') return literal(c, "false");
    if (ch == 'n') return literal(c, "null");
    return parse_number(c, &is_int, &v);
}

static int key_is(const cursor *c, size_t start, size_t len, const char *name)
{
    size_t i = 0;
    while (name[i]) {
        if (i >= len || c->text[start + i] != name[i]) return 0;
        i++;
    }
    return i == len;
}

static int method_char_ok(char ch)
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
           ch == '_' || ch == '.' || ch == '/' || ch == '-' || ch == ':';
}

int os_jrpc_parse_request(const char *text, size_t length, os_jrpc_request *out)
{
    if (!text || !out) return OS_JRPC_INVALID_REQUEST;
    char *raw = (char *)out;
    for (size_t i = 0; i < sizeof *out; i++) raw[i] = 0;
    if (length == 0 || length > OS_JRPC_MAX_MESSAGE) return OS_JRPC_PARSE_ERROR;

    /* Pass 1: the whole message must be well-formed JSON with nothing trailing. */
    cursor c = { text, length, 0 };
    if (!parse_value(&c, 1u)) return OS_JRPC_PARSE_ERROR;
    skip_ws(&c);
    if (!at_end(&c)) return OS_JRPC_PARSE_ERROR;

    /* Pass 2: structural request checks on the top-level object. */
    c.pos = 0;
    skip_ws(&c);
    if (peek(&c) != '{') return OS_JRPC_INVALID_REQUEST;
    c.pos++;
    int have_version = 0, have_method = 0;
    skip_ws(&c);
    if (peek(&c) == '}') return OS_JRPC_INVALID_REQUEST;
    for (;;) {
        size_t ks, kl, vs, vl;
        int esc;
        skip_ws(&c);
        if (!parse_string(&c, &ks, &kl, &esc)) return OS_JRPC_PARSE_ERROR;
        skip_ws(&c);
        c.pos++;
        skip_ws(&c);
        size_t value_start = c.pos;
        if (key_is(&c, ks, kl, "jsonrpc")) {
            if (!parse_string(&c, &vs, &vl, &esc) || esc || vl != 3u ||
                text[vs] != '2' || text[vs + 1] != '.' || text[vs + 2] != '0')
                return OS_JRPC_INVALID_REQUEST;
            have_version = 1;
        } else if (key_is(&c, ks, kl, "method")) {
            if (!parse_string(&c, &vs, &vl, &esc) || esc || vl == 0 || vl >= OS_JRPC_METHOD_MAX)
                return OS_JRPC_INVALID_REQUEST;
            for (size_t i = 0; i < vl; i++)
                if (!method_char_ok(text[vs + i])) return OS_JRPC_INVALID_REQUEST;
            if (vl >= 4u && text[vs] == 'r' && text[vs + 1] == 'p' && text[vs + 2] == 'c' && text[vs + 3] == '.')
                return OS_JRPC_INVALID_REQUEST;
            for (size_t i = 0; i < vl; i++) out->method[i] = text[vs + i];
            have_method = 1;
        } else if (key_is(&c, ks, kl, "id")) {
            int is_int;
            int64_t n;
            if (peek(&c) == '"') {
                if (!parse_string(&c, &vs, &vl, &esc) || esc || vl == 0 || vl >= OS_JRPC_ID_MAX)
                    return OS_JRPC_INVALID_REQUEST;
                for (size_t i = 0; i < vl; i++) out->id.string[i] = text[vs + i];
                out->id.kind = OS_JRPC_ID_STRING;
            } else if (parse_number(&c, &is_int, &n) && is_int) {
                out->id.kind = OS_JRPC_ID_NUMBER;
                out->id.number = n;
            } else {
                return OS_JRPC_INVALID_REQUEST;
            }
        } else if (key_is(&c, ks, kl, "params")) {
            if (peek(&c) != '{' || !parse_value(&c, 2u)) return OS_JRPC_INVALID_REQUEST;
            out->has_params = 1;
            out->params_offset = value_start;
            out->params_length = c.pos - value_start;
        } else {
            return OS_JRPC_INVALID_REQUEST;
        }
        skip_ws(&c);
        if (peek(&c) == ',') { c.pos++; continue; }
        break;
    }
    if (!have_version || !have_method) return OS_JRPC_INVALID_REQUEST;
    out->is_notification = out->id.kind == OS_JRPC_ID_NONE;
    return OS_JRPC_OK;
}

/* Locates a top-level member of an already validated object; returns value start or -1. */
static long find_member(const char *text, size_t off, size_t len, const char *key, cursor *c)
{
    c->text = text;
    c->length = off + len;
    c->pos = off;
    skip_ws(c);
    if (peek(c) != '{') return -1;
    c->pos++;
    for (;;) {
        size_t start, klen;
        int escaped;
        skip_ws(c);
        if (!parse_string(c, &start, &klen, &escaped)) return -1;
        skip_ws(c);
        if (peek(c) != ':') return -1;
        c->pos++;
        skip_ws(c);
        if (key_is(c, start, klen, key)) return (long)c->pos;
        if (!parse_value(c, 1u)) return -1;
        skip_ws(c);
        if (peek(c) == ',') { c->pos++; continue; }
        return -1;
    }
}

int os_jrpc_member_string(const char *text, size_t off, size_t len, const char *key,
                          char *out, size_t cap)
{
    cursor c;
    size_t start, slen;
    int escaped;
    if (!text || !key || !out || cap == 0) return 0;
    if (find_member(text, off, len, key, &c) < 0) return 0;
    if (!parse_string(&c, &start, &slen, &escaped) || escaped || slen >= cap) return 0;
    for (size_t i = 0; i < slen; i++) out[i] = text[start + i];
    out[slen] = 0;
    return 1;
}

int os_jrpc_member_int(const char *text, size_t off, size_t len, const char *key, int64_t *out)
{
    cursor c;
    int is_int;
    if (!text || !key || !out) return 0;
    if (find_member(text, off, len, key, &c) < 0) return 0;
    return parse_number(&c, &is_int, out) && is_int;
}

int os_jrpc_has_member(const char *text, size_t off, size_t len, const char *key)
{
    cursor c;
    if (!text || !key || len == 0) return 0;
    return find_member(text, off, len, key, &c) >= 0;
}

int os_jrpc_member_bool(const char *text, size_t off, size_t len, const char *key, int *out)
{
    cursor c;
    if (!text || !key || !out) return 0;
    if (find_member(text, off, len, key, &c) < 0) return 0;
    if (peek(&c) == 't' && literal(&c, "true")) { *out = 1; return 1; }
    if (peek(&c) == 'f' && literal(&c, "false")) { *out = 0; return 1; }
    return 0;
}

int os_jrpc_member_object(const char *text, size_t off, size_t len, const char *key,
                          size_t *obj_off, size_t *obj_len)
{
    cursor c;
    if (!text || !key || !obj_off || !obj_len) return 0;
    if (find_member(text, off, len, key, &c) < 0 || peek(&c) != '{') return 0;
    size_t start = c.pos;
    if (!parse_object(&c, 1u)) return 0;
    *obj_off = start;
    *obj_len = c.pos - start;
    return 1;
}

int os_jrpc_validate_document(const char *text, size_t length)
{
    cursor c = { text, length, 0 };
    if (!text || length == 0 || length > OS_JRPC_MAX_MESSAGE) return 0;
    if (!parse_value(&c, 0u)) return 0;
    skip_ws(&c);
    return at_end(&c);
}
