#include "roboforge_adapter.h"
#include "jsonrpc.h"
#include "sha256.h"

/* sha256 of the tools/list "result" object emitted by the pinned RoboForge revision. */
#define OS_RF_TOOLS_LIST_SHA256 "46ac77d6a772efa4dc69c5b56b929d87ebe3918caf6aa7b297c97bdecfa1bffe"

static const os_rf_contract contracts[OS_RF_TOOL_COUNT] = {
    { "roboforge_catalog_v1", 0 },
    { "roboforge_module_status_v1", 0 },
    { "roboforge_refract_compile_cpl_v1", 1 },
};

static size_t cstr_len(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static int starts_ci(const char *s, const char *prefix)
{
    while (*prefix) { if (lower(*s) != *prefix) return 0; s++; prefix++; }
    return 1;
}
static int contains(const char *s, const char *needle)
{
    size_t n = cstr_len(needle);
    for (; *s; s++) {
        size_t i = 0;
        while (i < n && s[i] == needle[i]) i++;
        if (i == n) return 1;
    }
    return 0;
}

const os_rf_contract *os_rf_contract_at(int index)
{
    return (index >= 0 && index < OS_RF_TOOL_COUNT) ? &contracts[index] : 0;
}

int os_rf_tool_from_name(const char *name)
{
    if (!name) return -1;
    for (int i = 0; i < OS_RF_TOOL_COUNT; i++)
        if (str_eq(contracts[i].name, name)) return i;
    return -1;
}

void os_rf_policy_init(os_rf_policy *p)
{
    p->enabled = 0;
    p->refract_opt_in = 0;
    p->tools_verified = 0;
}

static int valid_host(const char *h)
{
    const char *rest;
    if (starts_ci(h, "127.0.0.1")) rest = h + 9;
    else if (starts_ci(h, "localhost")) rest = h + 9;
    else if (starts_ci(h, "[::1]")) rest = h + 5;
    else return 0;
    if (*rest == 0) return 1;
    if (*rest != ':') return 0;
    rest++;
    size_t digits = 0;
    while (rest[digits] >= '0' && rest[digits] <= '9') digits++;
    return digits >= 1u && digits <= 5u && rest[digits] == 0;
}

int os_rf_transport_allowed(const os_rf_policy *p, int remote_is_loopback, const char *host,
                            const char *origin, const char *sec_fetch_site)
{
    if (!p || !p->enabled || !remote_is_loopback || !host || !valid_host(host)) return 0;
    if (origin) {
        const char *scheme = "http://";
        size_t i = 0;
        while (scheme[i] && origin[i] == scheme[i]) i++;
        if (scheme[i] || !str_eq(origin + i, host)) return 0;
    }
    if (sec_fetch_site && str_eq(sec_fetch_site, "cross-site")) return 0;
    return 1;
}

static int supported_version(const char *v)
{
    return str_eq(v, "2025-11-25") || str_eq(v, "2025-06-18") || str_eq(v, "2025-03-26");
}

int os_rf_http_headers_ok(const char *content_type, const char *accept,
                          const char *session_id, const char *protocol_version)
{
    if (!content_type || !starts_ci(content_type, "application/json")) return 0;
    const char *rest = content_type + 16;
    while (*rest == ' ' || *rest == '\t') rest++;
    if (*rest != 0 && *rest != ';') return 0;
    if (!accept || !(contains(accept, "application/json") || contains(accept, "*/*"))) return 0;
    if (session_id) return 0;
    if (protocol_version && !supported_version(protocol_version)) return 0;
    return 1;
}

static const char hexd[] = "0123456789abcdef";

int os_rf_tools_fingerprint_hex(const char *text, size_t len, char hex[65])
{
    size_t off, n;
    uint8_t d[32];
    if (!text || !hex || len > OS_RF_MAX_REPLY || !os_jrpc_validate_document(text, len)) return OS_RF_ERR_REPLY;
    if (!os_jrpc_member_object(text, 0, len, "result", &off, &n)) return OS_RF_ERR_REPLY;
    os_sha256_hash(text + off, n, d);
    for (int i = 0; i < 32; i++) { hex[2 * i] = hexd[d[i] >> 4]; hex[2 * i + 1] = hexd[d[i] & 15]; }
    hex[64] = 0;
    return OS_RF_OK;
}

int os_rf_verify_tools_list(os_rf_policy *p, const char *text, size_t len)
{
    char hex[65];
    if (!p) return OS_RF_ERR_ARGUMENT;
    p->tools_verified = 0;
    int rc = os_rf_tools_fingerprint_hex(text, len, hex);
    if (rc != OS_RF_OK) return rc;
    if (!str_eq(hex, OS_RF_TOOLS_LIST_SHA256)) return OS_RF_ERR_FINGERPRINT;
    p->tools_verified = 1;
    return OS_RF_OK;
}

static int valid_module_id(const char *s)
{
    size_t n = cstr_len(s);
    if (n < 1u || n > OS_RF_MODULE_ID_MAX) return 0;
    if (!(s[0] >= 'a' && s[0] <= 'z')) return 0;
    for (size_t i = 1; i < n; i++)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '-')) return 0;
    return 1;
}

typedef struct { char *buf; size_t cap, len; int overflow; } app;
static void put_c(app *a, char c)
{
    if (a->len + 1u >= a->cap) { a->overflow = 1; return; }
    a->buf[a->len++] = c;
    a->buf[a->len] = 0;
}
static void put_s(app *a, const char *s) { while (*s) put_c(a, *s++); }

static void put_escaped(app *a, const char *s)
{
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { put_c(a, '\\'); put_c(a, (char)c); }
        else if (c == '\n') put_s(a, "\\n");
        else if (c == '\r') put_s(a, "\\r");
        else if (c == '\t') put_s(a, "\\t");
        else if (c < 0x20u) { put_s(a, "\\u00"); put_c(a, hexd[c >> 4]); put_c(a, hexd[c & 15u]); }
        else put_c(a, (char)c);
    }
}

int os_rf_build_call(os_mcp_client *c, const os_rf_policy *p, uint64_t now, int tool,
                     const char *param, char *out, size_t cap)
{
    char args[OS_JRPC_MAX_MESSAGE];
    app a = { args, sizeof args, 0, 0 };
    if (!c || !p || !out) return OS_RF_ERR_ARGUMENT;
    if (!p->enabled) return OS_RF_ERR_DISABLED;
    if (tool < 0 || tool >= OS_RF_TOOL_COUNT) return OS_RF_ERR_TOOL;
    if (!p->tools_verified) return OS_RF_ERR_FINGERPRINT;
    if (tool == OS_RF_TOOL_REFRACT && !p->refract_opt_in) return OS_RF_ERR_DISABLED;
    args[0] = 0;
    put_c(&a, '{');
    if (tool == OS_RF_TOOL_STATUS) {
        if (!param || !valid_module_id(param)) return OS_RF_ERR_SCHEMA;
        put_s(&a, "\"module_id\":\"");
        put_escaped(&a, param);
        put_c(&a, '"');
    } else if (tool == OS_RF_TOOL_REFRACT) {
        size_t n = param ? cstr_len(param) : 0u;
        if (n < 1u || n > OS_RF_SOURCE_MAX) return OS_RF_ERR_SCHEMA;
        put_s(&a, "\"source\":\"");
        put_escaped(&a, param);
        put_c(&a, '"');
    }
    put_c(&a, '}');
    if (a.overflow) return OS_RF_ERR_SCHEMA;
    int rc = os_mcp_client_build_call(c, now, contracts[tool].name, args, out, cap);
    if (rc < 0) return OS_RF_ERR_SCHEMA;
    if ((size_t)rc > OS_RF_MAX_REQUEST) { c->pending = 0; return OS_RF_ERR_SCHEMA; }
    return rc;
}

int os_rf_check_reply_size(size_t len)
{
    return (len > 0u && len <= OS_RF_MAX_REPLY) ? OS_RF_OK : OS_RF_ERR_REPLY;
}
