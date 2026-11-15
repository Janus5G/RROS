#include "mcp_client.h"
#include "jsonrpc.h"
#include "mcp_server.h"

typedef struct { char *buf; size_t cap, len; int overflow; } app;

static void put_c(app *a, char c)
{
    if (a->len + 1u >= a->cap) { a->overflow = 1; return; }
    a->buf[a->len++] = c;
    a->buf[a->len] = 0;
}
static void put_s(app *a, const char *s) { while (*s) put_c(a, *s++); }
static void put_i64(app *a, int64_t v)
{
    char t[24];
    int n = 0;
    uint64_t u = v < 0 ? (uint64_t)0 - (uint64_t)v : (uint64_t)v;
    if (v < 0) put_c(a, '-');
    do { t[n++] = (char)('0' + u % 10u); u /= 10u; } while (u);
    while (n) put_c(a, t[--n]);
}
static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static size_t cstr_len(const char *s) { size_t n = 0; while (s[n]) n++; return n; }

void os_mcp_client_init(os_mcp_client *c, uint32_t timeout_ticks)
{
    volatile uint8_t *p = (volatile uint8_t *)c;
    for (size_t i = 0; i < sizeof *c; i++) p[i] = 0;
    c->timeout_ticks = timeout_ticks;
    c->next_id = 1;
}

static void begin_request(os_mcp_client *c, app *a, uint64_t now, const char *method)
{
    c->pending_id = c->next_id++;
    c->pending = 1;
    c->sent_at = now;
    put_s(a, "{\"jsonrpc\":\"2.0\",\"id\":");
    put_i64(a, c->pending_id);
    put_s(a, ",\"method\":\"");
    put_s(a, method);
    put_c(a, '"');
}

static int done(os_mcp_client *c, const app *a)
{
    if (a->overflow) { c->pending = 0; return OS_MCP_CLIENT_ERR_ARGUMENT; }
    return (int)a->len;
}

int os_mcp_client_build_initialize(os_mcp_client *c, uint64_t now, char *out, size_t cap)
{
    app a = { out, cap, 0, 0 };
    if (!c || !out || cap < 2u) return OS_MCP_CLIENT_ERR_ARGUMENT;
    if (c->state != OS_MCP_CLIENT_NEW || c->pending) return OS_MCP_CLIENT_ERR_STATE;
    out[0] = 0;
    begin_request(c, &a, now, "initialize");
    put_s(&a, ",\"params\":{\"protocolVersion\":\"" OS_MCP_PROTOCOL_LATEST
              "\",\"capabilities\":{},\"clientInfo\":{\"name\":\"experimentalos-client\",\"version\":\"1\"}}}");
    c->pending_is_initialize = 1;
    c->state = OS_MCP_CLIENT_INITIALIZING;
    return done(c, &a);
}

int os_mcp_client_build_initialized(os_mcp_client *c, char *out, size_t cap)
{
    app a = { out, cap, 0, 0 };
    if (!c || !out || cap < 2u) return OS_MCP_CLIENT_ERR_ARGUMENT;
    if (c->state != OS_MCP_CLIENT_CONNECTED) return OS_MCP_CLIENT_ERR_STATE;
    out[0] = 0;
    put_s(&a, "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}");
    c->state = OS_MCP_CLIENT_READY;
    return done(c, &a);
}

int os_mcp_client_build_list(os_mcp_client *c, uint64_t now, char *out, size_t cap)
{
    app a = { out, cap, 0, 0 };
    if (!c || !out || cap < 2u) return OS_MCP_CLIENT_ERR_ARGUMENT;
    if (c->state != OS_MCP_CLIENT_READY || c->pending) return OS_MCP_CLIENT_ERR_STATE;
    out[0] = 0;
    begin_request(c, &a, now, "tools/list");
    put_c(&a, '}');
    return done(c, &a);
}

int os_mcp_client_build_call(os_mcp_client *c, uint64_t now, const char *tool,
                             const char *arguments_json, char *out, size_t cap)
{
    app a = { out, cap, 0, 0 };
    if (!c || !tool || !arguments_json || !out || cap < 2u) return OS_MCP_CLIENT_ERR_ARGUMENT;
    if (c->state != OS_MCP_CLIENT_READY || c->pending) return OS_MCP_CLIENT_ERR_STATE;
    if (arguments_json[0] != '{' || !os_jrpc_validate_document(arguments_json, cstr_len(arguments_json)))
        return OS_MCP_CLIENT_ERR_ARGUMENT;
    for (const char *p = tool; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_')) return OS_MCP_CLIENT_ERR_ARGUMENT;
    out[0] = 0;
    begin_request(c, &a, now, "tools/call");
    put_s(&a, ",\"params\":{\"name\":\"");
    put_s(&a, tool);
    put_s(&a, "\",\"arguments\":");
    put_s(&a, arguments_json);
    put_s(&a, "}}");
    return done(c, &a);
}

int os_mcp_client_build_cancel(os_mcp_client *c, char *out, size_t cap)
{
    app a = { out, cap, 0, 0 };
    if (!c || !out || cap < 2u) return OS_MCP_CLIENT_ERR_ARGUMENT;
    if (!c->pending) return OS_MCP_CLIENT_ERR_STATE;
    out[0] = 0;
    put_s(&a, "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/cancelled\",\"params\":{\"requestId\":");
    put_i64(&a, c->pending_id);
    put_s(&a, ",\"reason\":\"client cancelled\"}}");
    c->pending = 0;
    return done(c, &a);
}

int os_mcp_client_on_message(os_mcp_client *c, const char *text, size_t len,
                             os_mcp_client_result *res)
{
    char ver[24];
    int64_t id, code;
    size_t off, n;
    if (!c || !text || !res) return OS_MCP_CLIENT_ERR_ARGUMENT;
    res->is_error = 0; res->error_code = 0; res->is_tool_error = 0;
    res->result_offset = 0; res->result_length = 0; res->untrusted = 1;
    if (c->state == OS_MCP_CLIENT_FAILED) return OS_MCP_CLIENT_ERR_STATE;
    if (!os_jrpc_validate_document(text, len) || text[0] != '{') return OS_MCP_CLIENT_ERR_MALFORMED;
    if (!os_jrpc_member_string(text, 0, len, "jsonrpc", ver, sizeof ver) || !str_eq(ver, "2.0"))
        return OS_MCP_CLIENT_ERR_MALFORMED;
    if (!os_jrpc_member_int(text, 0, len, "id", &id)) return OS_MCP_CLIENT_ERR_MALFORMED;
    if (!c->pending || id != c->pending_id) return OS_MCP_CLIENT_ERR_UNSOLICITED;
    if (os_jrpc_member_object(text, 0, len, "error", &off, &n)) {
        if (!os_jrpc_member_int(text, off, n, "code", &code)) return OS_MCP_CLIENT_ERR_MALFORMED;
        c->pending = 0;
        res->is_error = 1;
        res->error_code = (int)code;
        if (c->pending_is_initialize) { c->state = OS_MCP_CLIENT_FAILED; c->pending_is_initialize = 0; }
        return OS_MCP_CLIENT_ERR_REMOTE;
    }
    if (!os_jrpc_member_object(text, 0, len, "result", &off, &n)) return OS_MCP_CLIENT_ERR_MALFORMED;
    res->result_offset = off;
    res->result_length = n;
    if (c->pending_is_initialize) {
        c->pending_is_initialize = 0;
        c->pending = 0;
        if (!os_jrpc_member_string(text, off, n, "protocolVersion", c->protocol, sizeof c->protocol) ||
            !(str_eq(c->protocol, OS_MCP_PROTOCOL_LATEST) || str_eq(c->protocol, OS_MCP_PROTOCOL_PREVIOUS))) {
            c->state = OS_MCP_CLIENT_FAILED;
            return OS_MCP_CLIENT_ERR_VERSION;
        }
        c->state = OS_MCP_CLIENT_CONNECTED;
        return OS_MCP_CLIENT_OK;
    }
    c->pending = 0;
    (void)os_jrpc_member_bool(text, off, n, "isError", &res->is_tool_error);
    return OS_MCP_CLIENT_OK;
}

int os_mcp_client_check_timeout(os_mcp_client *c, uint64_t now)
{
    if (!c || !c->pending) return OS_MCP_CLIENT_OK;
    if (now >= c->sent_at && now - c->sent_at >= c->timeout_ticks) {
        c->pending = 0;
        if (c->pending_is_initialize) { c->state = OS_MCP_CLIENT_FAILED; c->pending_is_initialize = 0; }
        return OS_MCP_CLIENT_ERR_TIMEOUT;
    }
    return OS_MCP_CLIENT_OK;
}
