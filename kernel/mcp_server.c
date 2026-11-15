#include "mcp_server.h"

typedef struct {
    char *buf;
    size_t cap;
    size_t len;
    int overflow;
} app;

static void put_c(app *a, char c)
{
    if (a->len + 1u >= a->cap) { a->overflow = 1; return; }
    a->buf[a->len++] = c;
    a->buf[a->len] = 0;
}

static void put_s(app *a, const char *s)
{
    while (*s) put_c(a, *s++);
}

static void put_escaped(app *a, const char *s, size_t n)
{
    char tmp[OS_MCP_RESULT_TEXT_MAX * 6u + 8u];
    size_t m = os_mcp_escape_text(s, n, tmp, sizeof tmp);
    for (size_t i = 0; i < m; i++) put_c(a, tmp[i]);
}

static void put_i64(app *a, int64_t v)
{
    char t[24];
    int n = 0;
    uint64_t u = v < 0 ? (uint64_t)0 - (uint64_t)v : (uint64_t)v;
    if (v < 0) put_c(a, '-');
    do { t[n++] = (char)('0' + u % 10u); u /= 10u; } while (u);
    while (n) put_c(a, t[--n]);
}

static size_t cstr_len(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

size_t os_mcp_escape_text(const char *in, size_t n, char *out, size_t cap)
{
    static const char hex[] = "0123456789abcdef";
    size_t w = 0;
    if (!out || cap == 0) return 0;
    for (size_t i = 0; i < n && in; i++) {
        unsigned char c = (unsigned char)in[i];
        char e[6];
        size_t k = 0;
        if (c == '"' || c == '\\') { e[k++] = '\\'; e[k++] = (char)c; }
        else if (c < 0x20u) {
            e[k++] = '\\'; e[k++] = 'u'; e[k++] = '0'; e[k++] = '0';
            e[k++] = hex[c >> 4]; e[k++] = hex[c & 15u];
        } else if (c >= 0x7fu) e[k++] = '?';
        else e[k++] = (char)c;
        if (w + k + 1u > cap) break;
        for (size_t j = 0; j < k; j++) out[w++] = e[j];
    }
    out[w] = 0;
    return w;
}

static void put_id(app *a, const os_jrpc_id *id)
{
    if (id && id->kind == OS_JRPC_ID_NUMBER) put_i64(a, id->number);
    else if (id && id->kind == OS_JRPC_ID_STRING) {
        put_c(a, '"');
        put_escaped(a, id->string, cstr_len(id->string));
        put_c(a, '"');
    } else put_s(a, "null");
}

static int finish(const app *a)
{
    return a->overflow ? -1 : (int)a->len;
}

static int error_response(app *a, const os_jrpc_id *id, int code, const char *msg)
{
    put_s(a, "{\"jsonrpc\":\"2.0\",\"id\":");
    put_id(a, id);
    put_s(a, ",\"error\":{\"code\":");
    put_i64(a, code);
    put_s(a, ",\"message\":\"");
    put_escaped(a, msg, cstr_len(msg));
    put_s(a, "\"}}");
    return finish(a);
}

static void put_result_open(app *a, const os_jrpc_id *id)
{
    put_s(a, "{\"jsonrpc\":\"2.0\",\"id\":");
    put_id(a, id);
    put_s(a, ",\"result\":");
}

static void put_hex(app *a, const uint8_t *d, size_t n)
{
    static const char x[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) { put_c(a, x[d[i] >> 4]); put_c(a, x[d[i] & 15u]); }
}

void os_mcp_server_init(os_mcp_server *s, os_mcp_registry *registry, uint32_t caller_caps,
                        uint32_t max_calls, uint32_t timeout_ticks)
{
    volatile uint8_t *p = (volatile uint8_t *)s;
    for (size_t i = 0; i < sizeof *s; i++) p[i] = 0;
    s->registry = registry;
    s->caller_caps = caller_caps;
    s->max_calls = max_calls;
    s->timeout_ticks = timeout_ticks;
}

int os_mcp_server_bind(os_mcp_server *s, int tool, os_mcp_tool_fn fn, void *ctx, uint32_t cost_ticks)
{
    if (!s || !fn || tool < 0 || tool >= (int)OS_MCP_MAX_TOOLS) return OS_MCP_ERR_ARGUMENT;
    s->bindings[tool].fn = fn;
    s->bindings[tool].ctx = ctx;
    s->bindings[tool].cost_ticks = cost_ticks;
    return OS_MCP_OK;
}

void os_mcp_server_set_owner_confirmation(os_mcp_server *s, int confirmed)
{
    if (s) s->owner_confirmed = confirmed ? 1 : 0;
}

void os_mcp_server_reset_connection(os_mcp_server *s)
{
    if (!s) return;
    s->state = OS_MCP_CONN_NEW;
    s->calls_used = 0;
    s->invalid_streak = 0;
    s->inflight = 0;
    s->owner_confirmed = 0;
}

static int protocol_supported(const char *v)
{
    return str_eq(v, OS_MCP_PROTOCOL_LATEST) || str_eq(v, OS_MCP_PROTOCOL_PREVIOUS);
}

static void put_tools_list(app *a, const os_mcp_registry *r)
{
    int first = 1;
    put_s(a, "{\"tools\":[");
    for (uint32_t i = 0; i < OS_MCP_MAX_TOOLS; i++) {
        const os_mcp_tool *t = &r->tools[i];
        uint8_t fp[32];
        if (t->state != OS_MCP_TOOL_APPROVED) continue;
        os_mcp_tool_fingerprint(t->name, t->version, t->schema, t->schema_len, fp);
        if (!first) put_c(a, ',');
        first = 0;
        put_s(a, "{\"name\":\"");
        put_s(a, t->name);
        put_s(a, "\",\"inputSchema\":");
        for (uint16_t k = 0; k < t->schema_len; k++) put_c(a, t->schema[k]);
        put_s(a, ",\"_meta\":{\"fingerprint\":\"");
        put_hex(a, fp, 32);
        put_s(a, "\",\"version\":");
        put_i64(a, t->version);
        put_s(a, "}}");
    }
    put_s(a, "]}");
}

/* Tool output is untrusted data: escaped, bounded and flagged, never interpreted. */
static int run_tool(os_mcp_server *s, int tool, const os_jrpc_id *id, const char *args,
                    size_t args_off, size_t args_len, app *a)
{
    char text[OS_MCP_RESULT_TEXT_MAX + 1];
    text[0] = 0;
    int rc = s->bindings[tool].fn(s->bindings[tool].ctx, args, args_off, args_len, text, sizeof text);
    if (rc < 0) return error_response(a, id, OS_MCP_ERR_CODE_PARAMS, "invalid tool arguments");
    text[OS_MCP_RESULT_TEXT_MAX] = 0;
    put_result_open(a, id);
    put_s(a, "{\"content\":[{\"type\":\"text\",\"text\":\"");
    put_escaped(a, text, cstr_len(text));
    put_s(a, "\"}],\"isError\":");
    put_s(a, rc == 0 ? "false" : "true");
    put_s(a, ",\"_meta\":{\"untrusted\":true}}}");
    return finish(a);
}

static int find_tool(const os_mcp_registry *r, const char *name)
{
    for (uint32_t i = 0; i < OS_MCP_MAX_TOOLS; i++)
        if (r->tools[i].state != OS_MCP_TOOL_UNUSED && str_eq(r->tools[i].name, name)) return (int)i;
    return -1;
}

static const char *denial_text(int status)
{
    switch (status) {
    case OS_MCP_ERR_NOT_APPROVED: return "tool not approved";
    case OS_MCP_ERR_FINGERPRINT: return "tool schema fingerprint mismatch";
    case OS_MCP_ERR_EXPIRED: return "tool approval expired";
    case OS_MCP_ERR_CAPABILITY: return "caller lacks required capability";
    case OS_MCP_ERR_SIDE_EFFECT: return "owner authorization required";
    case OS_MCP_ERR_REPLAY: return "replayed call";
    default: return "call denied";
    }
}

static int ids_equal(const os_jrpc_id *a, const os_jrpc_id *b)
{
    if (a->kind != b->kind) return 0;
    if (a->kind == OS_JRPC_ID_NUMBER) return a->number == b->number;
    return str_eq(a->string, b->string);
}

static void handle_cancel(os_mcp_server *s, const char *in, const os_jrpc_request *rq)
{
    os_jrpc_id id = { 0 };
    int64_t n;
    if (!s->inflight || !rq->has_params) return;
    if (os_jrpc_member_int(in, rq->params_offset, rq->params_length, "requestId", &n)) {
        id.kind = OS_JRPC_ID_NUMBER;
        id.number = n;
    } else if (os_jrpc_member_string(in, rq->params_offset, rq->params_length, "requestId",
                                     id.string, sizeof id.string)) {
        id.kind = OS_JRPC_ID_STRING;
    } else return;
    if (ids_equal(&id, &s->inflight_id)) {
        s->inflight = 0;
        s->cancelled_count++;
    }
}

static int handle_call(os_mcp_server *s, uint64_t now, const char *in, const os_jrpc_request *rq,
                       app *a)
{
    char name[OS_MCP_TOOL_NAME_MAX + 1];
    size_t aoff = 0, alen = 0;
    int tool, auth;
    if (!rq->has_params ||
        !os_jrpc_member_string(in, rq->params_offset, rq->params_length, "name", name, sizeof name))
        return error_response(a, &rq->id, OS_MCP_ERR_CODE_PARAMS, "missing tool name");
    tool = find_tool(s->registry, name);
    if (tool < 0) return error_response(a, &rq->id, OS_MCP_ERR_CODE_PARAMS, "unknown tool");
    if (s->inflight) return error_response(a, &rq->id, OS_MCP_ERR_CODE_BUSY, "call already in flight");
    if (s->calls_used >= s->max_calls)
        return error_response(a, &rq->id, OS_MCP_ERR_CODE_BUSY, "call quota exhausted");
    auth = os_mcp_authorize_call(s->registry, name, s->caller_caps, now, ++s->next_nonce,
                                 s->owner_confirmed);
    if (auth < 0) return error_response(a, &rq->id, OS_MCP_ERR_CODE_DENIED, denial_text(auth));
    if (!s->bindings[tool].fn) return error_response(a, &rq->id, OS_MCP_ERR_CODE_METHOD, "tool unavailable");
    if (!os_jrpc_member_object(in, rq->params_offset, rq->params_length, "arguments", &aoff, &alen)) {
        aoff = 0;
        alen = 0;
    }
    s->calls_used++;
    if (s->bindings[tool].cost_ticks == 0)
        return run_tool(s, tool, &rq->id, in, aoff, alen, a);
    if (alen >= OS_MCP_ARGS_MAX) return error_response(a, &rq->id, OS_MCP_ERR_CODE_PARAMS, "arguments too large");
    for (size_t i = 0; i < alen; i++) s->inflight_args[i] = in[aoff + i];
    s->inflight_args_len = alen;
    s->inflight = 1;
    s->inflight_tool = tool;
    s->inflight_id = rq->id;
    s->inflight_start = now;
    return 0;
}

int os_mcp_server_handle(os_mcp_server *s, uint64_t now, const char *in, size_t in_len,
                         char *out, size_t cap)
{
    os_jrpc_request rq;
    app a = { out, cap, 0, 0 };
    int rc;
    if (!s || !in || !out || cap < 2u) return -1;
    out[0] = 0;
    if (s->state == OS_MCP_CONN_CLOSED) return -1;
    rc = os_jrpc_parse_request(in, in_len, &rq);
    if (rc != OS_JRPC_OK) {
        os_jrpc_id none = { 0 };
        if (++s->invalid_streak > OS_MCP_MAX_CONSECUTIVE_INVALID) s->state = OS_MCP_CONN_CLOSED;
        return error_response(&a, &none, rc, rc == OS_JRPC_PARSE_ERROR ? "parse error" : "invalid request");
    }
    s->invalid_streak = 0;

    if (rq.is_notification) {
        if (str_eq(rq.method, "notifications/initialized") && s->state == OS_MCP_CONN_INITIALIZED)
            s->state = OS_MCP_CONN_READY;
        else if (str_eq(rq.method, "notifications/cancelled") && s->state == OS_MCP_CONN_READY)
            handle_cancel(s, in, &rq);
        return 0;
    }
    if (str_eq(rq.method, "ping")) {
        put_result_open(&a, &rq.id);
        put_s(&a, "{}}");
        return finish(&a);
    }
    if (str_eq(rq.method, "initialize")) {
        char version[24];
        if (s->state != OS_MCP_CONN_NEW)
            return error_response(&a, &rq.id, OS_MCP_ERR_CODE_LIFECYCLE, "already initialized");
        if (!rq.has_params || !os_jrpc_member_string(in, rq.params_offset, rq.params_length,
                                                      "protocolVersion", version, sizeof version))
            return error_response(&a, &rq.id, OS_MCP_ERR_CODE_PARAMS, "missing protocolVersion");
        s->state = OS_MCP_CONN_INITIALIZED;
        put_result_open(&a, &rq.id);
        put_s(&a, "{\"protocolVersion\":\"");
        put_s(&a, protocol_supported(version) ? version : OS_MCP_PROTOCOL_LATEST);
        put_s(&a, "\",\"capabilities\":{\"tools\":{\"listChanged\":false}},"
                  "\"serverInfo\":{\"name\":\"experimentalos-mcp\",\"version\":\"1\"}}}");
        return finish(&a);
    }
    if (s->state != OS_MCP_CONN_READY)
        return error_response(&a, &rq.id, OS_MCP_ERR_CODE_LIFECYCLE, "connection not initialized");
    if (str_eq(rq.method, "tools/list")) {
        put_result_open(&a, &rq.id);
        put_tools_list(&a, s->registry);
        put_c(&a, '}');
        return finish(&a);
    }
    if (str_eq(rq.method, "tools/call")) return handle_call(s, now, in, &rq, &a);
    return error_response(&a, &rq.id, OS_MCP_ERR_CODE_METHOD, "method not found");
}

int os_mcp_server_poll(os_mcp_server *s, uint64_t now, char *out, size_t cap)
{
    app a = { out, cap, 0, 0 };
    uint64_t elapsed;
    uint32_t cost;
    os_jrpc_id id;
    int tool;
    if (!s || !out || cap < 2u || !s->inflight) return 0;
    out[0] = 0;
    elapsed = now >= s->inflight_start ? now - s->inflight_start : 0;
    cost = s->bindings[s->inflight_tool].cost_ticks;
    id = s->inflight_id;
    tool = s->inflight_tool;
    if (cost > s->timeout_ticks) {
        if (elapsed < s->timeout_ticks) return 0;
        s->inflight = 0;
        s->timed_out_count++;
        return error_response(&a, &id, OS_MCP_ERR_CODE_TIMEOUT, "tool call timed out");
    }
    if (elapsed < cost) return 0;
    s->inflight = 0;
    return run_tool(s, tool, &id, s->inflight_args, 0, s->inflight_args_len, &a);
}
