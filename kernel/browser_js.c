#include "browser_js.h"

enum { V_INT = 0, V_STR, V_BOOL };

typedef struct { uint8_t type; int32_t i; uint16_t off, len; } val_t;

typedef struct {
    const char *s;
    size_t n, p;
    struct { char name[OS_JS_NAME_MAX]; uint8_t used; val_t v; } vars[OS_JS_VARS];
    char pool[OS_JS_POOL];
    unsigned pool_len, depth;
    uint32_t fuel, steps;
    int status;
    os_js_result *r;
} js_t;

static int is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
static int is_idc(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$' || (c >= '0' && c <= '9'); }
static int is_ids(char c) { return is_idc(c) && !(c >= '0' && c <= '9'); }

static void fail(js_t *j, int status, const char *msg)
{
    if (j->status != OS_JS_OK) return;
    j->status = status;
    for (unsigned i = 0; i + 1u < sizeof j->r->error; i++) { j->r->error[i] = msg[i]; j->r->error[i + 1u] = 0; if (!msg[i]) break; }
}

static void tick(js_t *j)
{
    if (++j->steps > j->fuel) fail(j, OS_JS_ERR_FUEL, "fuel exhausted");
}

static void ws(js_t *j)
{
    for (;;) {
        while (j->p < j->n && is_ws(j->s[j->p])) j->p++;
        if (j->p + 1u < j->n && j->s[j->p] == '/' && j->s[j->p + 1u] == '/') {
            while (j->p < j->n && j->s[j->p] != '\n') j->p++;
            continue;
        }
        if (j->p + 1u < j->n && j->s[j->p] == '/' && j->s[j->p + 1u] == '*') {
            j->p += 2u;
            while (j->p + 1u < j->n && !(j->s[j->p] == '*' && j->s[j->p + 1u] == '/')) j->p++;
            if (j->p + 1u >= j->n) { j->p = j->n; fail(j, OS_JS_ERR_SYNTAX, "unterminated comment"); return; }
            j->p += 2u;
            continue;
        }
        return;
    }
}

static int accept(js_t *j, char c)
{
    ws(j);
    if (j->p < j->n && j->s[j->p] == c) { j->p++; return 1; }
    return 0;
}

static int accept2(js_t *j, char a, char b)
{
    ws(j);
    if (j->p + 1u < j->n && j->s[j->p] == a && j->s[j->p + 1u] == b) { j->p += 2u; return 1; }
    return 0;
}

static void expect(js_t *j, char c)
{
    if (!accept(j, c)) fail(j, OS_JS_ERR_SYNTAX, "unexpected token");
}

static int keyword(js_t *j, const char *kw)
{
    size_t l = 0;
    ws(j);
    while (kw[l]) l++;
    if (j->p + l > j->n) return 0;
    for (size_t i = 0; i < l; i++) if (j->s[j->p + i] != kw[i]) return 0;
    if (j->p + l < j->n && is_idc(j->s[j->p + l])) return 0;
    j->p += l;
    return 1;
}

static int read_ident(js_t *j, char *name)
{
    size_t l = 0;
    ws(j);
    if (j->p >= j->n || !is_ids(j->s[j->p])) return 0;
    while (j->p < j->n && is_idc(j->s[j->p])) {
        if (l + 1u >= OS_JS_NAME_MAX) { fail(j, OS_JS_ERR_LIMIT, "identifier too long"); return 0; }
        name[l++] = j->s[j->p++];
    }
    name[l] = 0;
    return 1;
}

static int find_var(js_t *j, const char *name)
{
    for (unsigned i = 0; i < OS_JS_VARS; i++) {
        if (!j->vars[i].used) continue;
        const char *a = j->vars[i].name, *b = name;
        while (*a && *a == *b) { a++; b++; }
        if (*a == *b) return (int)i;
    }
    return -1;
}

static void set_var(js_t *j, const char *name, val_t v, int create)
{
    int i = find_var(j, name);
    if (i < 0) {
        if (!create) { fail(j, OS_JS_ERR_RUNTIME, "assignment to undeclared"); return; }
        for (unsigned k = 0; k < OS_JS_VARS; k++) {
            if (j->vars[k].used) continue;
            j->vars[k].used = 1;
            for (unsigned c = 0; c < OS_JS_NAME_MAX; c++) { j->vars[k].name[c] = name[c]; if (!name[c]) break; }
            j->vars[k].v = v;
            return;
        }
        fail(j, OS_JS_ERR_LIMIT, "too many variables");
        return;
    }
    j->vars[i].v = v;
}

static val_t mk_int(int32_t i) { val_t v; v.type = V_INT; v.i = i; v.off = v.len = 0; return v; }
static val_t mk_bool(int b) { val_t v; v.type = V_BOOL; v.i = b ? 1 : 0; v.off = v.len = 0; return v; }

static val_t mk_str(js_t *j, const char *a, size_t al, const char *b, size_t bl)
{
    val_t v;
    v.type = V_STR; v.i = 0; v.off = 0; v.len = 0;
    if (j->pool_len + al + bl + 1u > OS_JS_POOL) { fail(j, OS_JS_ERR_LIMIT, "string pool exhausted"); return v; }
    v.off = (uint16_t)j->pool_len;
    for (size_t i = 0; i < al; i++) j->pool[j->pool_len++] = a[i];
    for (size_t i = 0; i < bl; i++) j->pool[j->pool_len++] = b[i];
    j->pool[j->pool_len++] = 0;
    v.len = (uint16_t)(al + bl);
    return v;
}

static size_t int_text(int32_t x, char *t)
{
    char r[12];
    size_t k = 0, o = 0;
    uint32_t u = x < 0 ? 0u - (uint32_t)x : (uint32_t)x;
    do { r[k++] = (char)('0' + u % 10u); u /= 10u; } while (u);
    if (x < 0) t[o++] = '-';
    while (k) t[o++] = r[--k];
    t[o] = 0;
    return o;
}

/* Renders any value to text in t (cap >= 12); strings are returned by pointer. */
static const char *text_of(js_t *j, val_t v, char *t, size_t *len)
{
    if (v.type == V_STR) { *len = v.len; return j->pool + v.off; }
    if (v.type == V_BOOL) {
        const char *s = v.i ? "true" : "false";
        size_t l = 0;
        while (s[l]) { t[l] = s[l]; l++; }
        t[l] = 0; *len = l;
        return t;
    }
    *len = int_text(v.i, t);
    return t;
}

static int truthy(val_t v)
{
    if (v.type == V_STR) return v.len != 0;
    return v.i != 0;
}

static val_t expr(js_t *j, int exec);

static val_t primary(js_t *j, int exec)
{
    val_t v = mk_int(0);
    char name[OS_JS_NAME_MAX];
    ws(j);
    tick(j);
    if (j->status || j->p >= j->n) { fail(j, OS_JS_ERR_SYNTAX, "unexpected end"); return v; }
    if (j->s[j->p] >= '0' && j->s[j->p] <= '9') {
        uint32_t n = 0;
        while (j->p < j->n && j->s[j->p] >= '0' && j->s[j->p] <= '9') {
            uint32_t d = (uint32_t)(j->s[j->p++] - '0');
            if (n > (2147483647u - d) / 10u) { fail(j, OS_JS_ERR_LIMIT, "number too large"); return v; }
            n = n * 10u + d;
        }
        if (j->p < j->n && (j->s[j->p] == '.' || is_ids(j->s[j->p]))) fail(j, OS_JS_ERR_SYNTAX, "floats unsupported");
        return mk_int((int32_t)n);
    }
    if (j->s[j->p] == '"' || j->s[j->p] == '\'') {
        char q = j->s[j->p++], buf[OS_JS_POOL];
        size_t l = 0;
        while (j->p < j->n && j->s[j->p] != q) {
            char c = j->s[j->p++];
            if (c == '\\' && j->p < j->n) {
                char e = j->s[j->p++];
                c = e == 'n' ? '\n' : e;
            }
            if (c == '\n' && j->s[j->p - 1u] != 'n') { fail(j, OS_JS_ERR_SYNTAX, "newline in string"); return v; }
            if ((unsigned char)c > 0x7e || ((unsigned char)c < 0x20 && c != '\n')) { fail(j, OS_JS_ERR_SYNTAX, "bad character in string"); return v; }
            if (l + 1u >= sizeof buf) { fail(j, OS_JS_ERR_LIMIT, "string too long"); return v; }
            buf[l++] = c;
        }
        if (j->p >= j->n) { fail(j, OS_JS_ERR_SYNTAX, "unterminated string"); return v; }
        j->p++;
        return exec ? mk_str(j, buf, l, 0, 0) : mk_int(0);
    }
    if (accept(j, '(')) {
        v = expr(j, exec);
        expect(j, ')');
        return v;
    }
    if (accept(j, '-')) {
        v = primary(j, exec);
        if (exec && v.type == V_STR) { fail(j, OS_JS_ERR_RUNTIME, "unary minus on string"); return v; }
        return mk_int((int32_t)(0u - (uint32_t)v.i));
    }
    if (accept(j, '!')) {
        v = primary(j, exec);
        return mk_bool(!truthy(v));
    }
    if (keyword(j, "true")) return mk_bool(1);
    if (keyword(j, "false")) return mk_bool(0);
    if (read_ident(j, name)) {
        int i;
        if (!exec) return v;
        i = find_var(j, name);
        if (i < 0) { fail(j, OS_JS_ERR_RUNTIME, "undefined variable"); return v; }
        return j->vars[i].v;
    }
    fail(j, OS_JS_ERR_SYNTAX, "unsupported expression");
    return v;
}

static int need_int(js_t *j, val_t v, int exec)
{
    if (exec && v.type == V_STR) { fail(j, OS_JS_ERR_RUNTIME, "string in arithmetic"); return 0; }
    return v.i;
}

static val_t mul_expr(js_t *j, int exec)
{
    val_t a = primary(j, exec);
    for (;;) {
        char op = 0;
        ws(j);
        if (j->p < j->n && (j->s[j->p] == '*' || j->s[j->p] == '/' || j->s[j->p] == '%')) op = j->s[j->p++];
        else return a;
        {
            val_t b = primary(j, exec);
            int32_t x = need_int(j, a, exec), y = need_int(j, b, exec);
            tick(j);
            if (!exec || j->status) { a = mk_int(0); continue; }
            if (op == '*') a = mk_int((int32_t)((uint32_t)x * (uint32_t)y));
            else if (y == 0 || (x == INT32_MIN && y == -1)) { fail(j, OS_JS_ERR_RUNTIME, "division trap"); return a; }
            else a = mk_int(op == '/' ? x / y : x % y);
        }
    }
}

static val_t add_expr(js_t *j, int exec)
{
    val_t a = mul_expr(j, exec);
    for (;;) {
        char op = 0;
        ws(j);
        if (j->p < j->n && (j->s[j->p] == '+' || j->s[j->p] == '-')) op = j->s[j->p++];
        else return a;
        {
            val_t b = mul_expr(j, exec);
            tick(j);
            if (!exec || j->status) { a = mk_int(0); continue; }
            if (op == '+' && (a.type == V_STR || b.type == V_STR)) {
                char ta[12], tb[12];
                size_t la, lb;
                const char *sa = text_of(j, a, ta, &la), *sb = text_of(j, b, tb, &lb);
                a = mk_str(j, sa, la, sb, lb);
            } else {
                int32_t x = need_int(j, a, exec), y = need_int(j, b, exec);
                a = mk_int(op == '+' ? (int32_t)((uint32_t)x + (uint32_t)y) : (int32_t)((uint32_t)x - (uint32_t)y));
            }
        }
    }
}

static val_t cmp_expr(js_t *j, int exec)
{
    val_t a = add_expr(j, exec);
    for (;;) {
        int op = 0;
        ws(j);
        if (accept2(j, '<', '=')) op = 1;
        else if (accept2(j, '>', '=')) op = 2;
        else if (j->p < j->n && j->s[j->p] == '<' ) { j->p++; op = 3; }
        else if (j->p < j->n && j->s[j->p] == '>') { j->p++; op = 4; }
        else return a;
        {
            val_t b = add_expr(j, exec);
            int32_t x = need_int(j, a, exec), y = need_int(j, b, exec);
            tick(j);
            a = mk_bool(op == 1 ? x <= y : op == 2 ? x >= y : op == 3 ? x < y : x > y);
        }
    }
}

static int values_equal(js_t *j, val_t a, val_t b)
{
    if (a.type != b.type) return 0;
    if (a.type == V_STR) {
        if (a.len != b.len) return 0;
        for (unsigned i = 0; i < a.len; i++) if (j->pool[a.off + i] != j->pool[b.off + i]) return 0;
        return 1;
    }
    return a.i == b.i;
}

static val_t eq_expr(js_t *j, int exec)
{
    val_t a = cmp_expr(j, exec);
    for (;;) {
        int neg;
        if (accept2(j, '=', '=')) neg = 0;
        else if (accept2(j, '!', '=')) neg = 1;
        else return a;
        accept(j, '=');
        {
            val_t b = cmp_expr(j, exec);
            tick(j);
            a = mk_bool(exec ? values_equal(j, a, b) != neg : 0);
        }
    }
}

static val_t and_expr(js_t *j, int exec)
{
    val_t a = eq_expr(j, exec);
    while (accept2(j, '&', '&')) {
        val_t b = eq_expr(j, exec && truthy(a));
        a = mk_bool(truthy(a) && truthy(b));
    }
    return a;
}

static val_t expr(js_t *j, int exec)
{
    val_t a;
    if (++j->depth > OS_JS_DEPTH) { fail(j, OS_JS_ERR_LIMIT, "nesting too deep"); j->depth--; return mk_int(0); }
    a = and_expr(j, exec);
    while (accept2(j, '|', '|')) {
        val_t b = and_expr(j, exec && !truthy(a));
        a = mk_bool(truthy(a) || truthy(b));
    }
    j->depth--;
    return a;
}

static void emit(js_t *j, val_t v, int newline)
{
    char t[12];
    size_t l;
    const char *s = text_of(j, v, t, &l);
    if (j->r->output_len + l + (newline ? 1u : 0u) > OS_JS_OUTPUT_MAX) { fail(j, OS_JS_ERR_OUTPUT, "output limit"); return; }
    for (size_t i = 0; i < l; i++) j->r->output[j->r->output_len++] = s[i];
    if (newline) j->r->output[j->r->output_len++] = '\n';
    j->r->output[j->r->output_len] = 0;
}

static void stmt(js_t *j, int exec)
{
    char name[OS_JS_NAME_MAX];
    tick(j);
    if (j->status) return;
    if (++j->depth > OS_JS_DEPTH) { fail(j, OS_JS_ERR_LIMIT, "nesting too deep"); j->depth--; return; }
    ws(j);
    if (j->p >= j->n) { fail(j, OS_JS_ERR_SYNTAX, "unexpected end"); j->depth--; return; }
    if (accept(j, '{')) {
        for (;;) {
            ws(j);
            if (j->status) break;
            if (j->p >= j->n) { fail(j, OS_JS_ERR_SYNTAX, "missing }"); break; }
            if (accept(j, '}')) break;
            stmt(j, exec);
        }
    } else if (keyword(j, "var") || keyword(j, "let")) {
        val_t v;
        if (!read_ident(j, name)) fail(j, OS_JS_ERR_SYNTAX, "bad declaration");
        expect(j, '=');
        v = expr(j, exec);
        expect(j, ';');
        if (exec && !j->status) set_var(j, name, v, 1);
    } else if (keyword(j, "if")) {
        val_t c;
        expect(j, '(');
        c = expr(j, exec);
        expect(j, ')');
        stmt(j, exec && truthy(c));
        if (keyword(j, "else")) stmt(j, exec && !truthy(c));
    } else if (keyword(j, "while")) {
        size_t cond = j->p;
        for (;;) {
            val_t c;
            int run;
            j->p = cond;
            expect(j, '(');
            c = expr(j, exec);
            expect(j, ')');
            run = exec && !j->status && truthy(c);
            stmt(j, run);
            if (!run || j->status) break;
            tick(j);
        }
    } else if (keyword(j, "print") || keyword(j, "document.write")) {
        val_t v;
        expect(j, '(');
        v = expr(j, exec);
        expect(j, ')');
        expect(j, ';');
        if (exec && !j->status) emit(j, v, 1);
    } else if (read_ident(j, name)) {
        val_t v;
        expect(j, '=');
        v = expr(j, exec);
        expect(j, ';');
        if (exec && !j->status) set_var(j, name, v, 0);
    } else {
        fail(j, OS_JS_ERR_SYNTAX, "unsupported statement");
    }
    j->depth--;
}

int os_js_run(const char *src, size_t len, uint32_t fuel, os_js_result *r)
{
    js_t j;
    if (!r) return OS_JS_ERR_ARGUMENT;
    r->status = OS_JS_OK; r->steps = 0; r->output_len = 0; r->output[0] = 0; r->error[0] = 0;
    if (!src || fuel == 0) { r->status = OS_JS_ERR_ARGUMENT; return r->status; }
    j.s = src; j.n = len; j.p = 0; j.pool_len = 0; j.depth = 0; j.fuel = fuel; j.steps = 0; j.status = OS_JS_OK; j.r = r;
    for (unsigned i = 0; i < OS_JS_VARS; i++) j.vars[i].used = 0;
    for (;;) {
        ws(&j);
        if (j.status || j.p >= j.n) break;
        stmt(&j, 1);
    }
    r->status = j.status;
    r->steps = j.steps;
    return r->status;
}
