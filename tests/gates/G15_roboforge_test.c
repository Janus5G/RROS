#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mcp_client.h"
#include "roboforge_adapter.h"

static int failures;
#define CHECK(c, name) do { if (c) printf("PASS %s\n", name); else { printf("FAIL %s\n", name); failures++; } } while (0)

static const char *dir;

/* Fixtures are captured replies of the real RoboForge gateway at the pinned revision. */
static size_t load(const char *name, char *buf, size_t cap)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) { printf("FAIL fixture %s missing\n", path); exit(2); }
    size_t n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = 0;
    return n;
}

static char rep[OS_RF_MAX_REPLY + 64];
static char req[OS_RF_MAX_REQUEST + 64];

static int feed(os_mcp_client *c, const char *name, os_mcp_client_result *r)
{
    size_t n = load(name, rep, sizeof rep);
    return os_mcp_client_on_message(c, rep, n, r);
}

int main(int argc, char **argv)
{
    os_mcp_client c;
    os_rf_policy p;
    os_mcp_client_result r;
    char hex[65];
    size_t n;

    if (argc < 2) { printf("usage: %s <fixture-dir>\n", argv[0]); return 2; }
    dir = argv[1];

    CHECK(sizeof OS_RF_PINNED_REVISION == 41, "G15-ADAPTER-PINNED-REVISION-FORMAT");
    CHECK(OS_RF_TOOL_COUNT == 3 && os_rf_tool_from_name("roboforge_catalog_v1") == OS_RF_TOOL_CATALOG &&
          os_rf_tool_from_name("roboforge_module_status_v1") == OS_RF_TOOL_STATUS &&
          os_rf_tool_from_name("roboforge_refract_compile_cpl_v1") == OS_RF_TOOL_REFRACT,
          "G15-CONTRACT-THREE-FROZEN-TOOLS");
    CHECK(os_rf_tool_from_name("roboforge_shell_v1") < 0 && os_rf_tool_from_name("dispatch") < 0 &&
          os_rf_tool_from_name(NULL) < 0, "G15-CONTRACT-NO-ARBITRARY-DISPATCH");
    CHECK(!os_rf_contract_at(0)->controlled && !os_rf_contract_at(1)->controlled && os_rf_contract_at(2)->controlled,
          "G15-CONTRACT-REFRACT-IS-CONTROLLED");

    /* Disabled by default. */
    os_rf_policy_init(&p);
    CHECK(!p.enabled && !p.refract_opt_in && !p.tools_verified, "G15-POLICY-DISABLED-BY-DEFAULT");
    CHECK(!os_rf_transport_allowed(&p, 1, "127.0.0.1:3001", NULL, NULL), "G15-TRANSPORT-DISABLED-DENIES-LOOPBACK");
    os_mcp_client_init(&c, 100);
    CHECK(os_rf_build_call(&c, &p, 1, OS_RF_TOOL_CATALOG, NULL, req, sizeof req) == OS_RF_ERR_DISABLED,
          "G15-CALL-DISABLED-REFUSED");

    /* Loopback-only transport. */
    p.enabled = 1;
    CHECK(os_rf_transport_allowed(&p, 1, "127.0.0.1:3001", NULL, NULL) &&
          os_rf_transport_allowed(&p, 1, "localhost:3001", "http://localhost:3001", "same-origin") &&
          os_rf_transport_allowed(&p, 1, "[::1]:3001", NULL, NULL) &&
          os_rf_transport_allowed(&p, 1, "127.0.0.1", NULL, NULL), "G15-TRANSPORT-LOOPBACK-ALLOWED");
    CHECK(!os_rf_transport_allowed(&p, 0, "127.0.0.1:3001", NULL, NULL), "G15-TRANSPORT-REMOTE-ADDRESS-DENIED");
    CHECK(!os_rf_transport_allowed(&p, 1, "attacker.invalid", NULL, NULL) &&
          !os_rf_transport_allowed(&p, 1, "127.0.0.1.evil.test", NULL, NULL) &&
          !os_rf_transport_allowed(&p, 1, "0.0.0.0:3001", NULL, NULL) &&
          !os_rf_transport_allowed(&p, 1, "127.0.0.1:123456", NULL, NULL) &&
          !os_rf_transport_allowed(&p, 1, "127.0.0.1:", NULL, NULL) &&
          !os_rf_transport_allowed(&p, 1, NULL, NULL, NULL), "G15-TRANSPORT-FORGED-HOST-DENIED");
    CHECK(!os_rf_transport_allowed(&p, 1, "127.0.0.1:3001", "http://evil.test", NULL) &&
          !os_rf_transport_allowed(&p, 1, "127.0.0.1:3001", "http://127.0.0.1:3002", NULL),
          "G15-TRANSPORT-CROSS-ORIGIN-DENIED");
    CHECK(!os_rf_transport_allowed(&p, 1, "127.0.0.1:3001", NULL, "cross-site"), "G15-TRANSPORT-CROSS-SITE-DENIED");

    CHECK(os_rf_http_headers_ok("application/json", "application/json", NULL, NULL) &&
          os_rf_http_headers_ok("Application/JSON; charset=utf-8", "*/*", NULL, "2025-11-25") &&
          os_rf_http_headers_ok("application/json", "application/json", NULL, "2025-03-26"),
          "G15-HTTP-HEADERS-ACCEPTED");
    CHECK(!os_rf_http_headers_ok("text/plain", "application/json", NULL, NULL) &&
          !os_rf_http_headers_ok("application/jsonx", "application/json", NULL, NULL) &&
          !os_rf_http_headers_ok("application/json", "text/html", NULL, NULL) &&
          !os_rf_http_headers_ok("application/json", "application/json", "abc", NULL) &&
          !os_rf_http_headers_ok("application/json", "application/json", NULL, "2024-01-01"),
          "G15-HTTP-HEADERS-REJECTED-STATELESS-ONLY");

    /* Handshake against the real gateway's initialize reply. */
    os_mcp_client_init(&c, 100);
    CHECK(os_mcp_client_build_initialize(&c, 1, req, sizeof req) > 0 &&
          feed(&c, "initialize.json", &r) == OS_MCP_CLIENT_OK && !r.is_error &&
          strcmp(c.protocol, "2025-06-18") == 0, "G15-LIFE-REAL-INITIALIZE-REPLY-ACCEPTED");
    CHECK(os_mcp_client_build_initialized(&c, req, sizeof req) > 0, "G15-LIFE-INITIALIZED-SENT");

    /* No call before the tool definitions were verified. */
    CHECK(os_rf_build_call(&c, &p, 2, OS_RF_TOOL_CATALOG, NULL, req, sizeof req) == OS_RF_ERR_FINGERPRINT,
          "G15-CALL-REFUSED-BEFORE-FINGERPRINT-VERIFIED");

    /* tools/list fingerprint. */
    CHECK(os_mcp_client_build_list(&c, 2, req, sizeof req) > 0, "G15-LIST-REQUEST-BUILT");
    n = load("tools_list.json", rep, sizeof rep);
    CHECK(os_rf_tools_fingerprint_hex(rep, n, hex) == OS_RF_OK &&
          strcmp(hex, "46ac77d6a772efa4dc69c5b56b929d87ebe3918caf6aa7b297c97bdecfa1bffe") == 0,
          "G15-SCHEMA-FROZEN-TOOLS-FINGERPRINT");
    CHECK(os_rf_verify_tools_list(&p, rep, n) == OS_RF_OK && p.tools_verified, "G15-SCHEMA-REAL-TOOLS-LIST-VERIFIED");
    {
        char mutated[OS_RF_MAX_REPLY + 64];
        char *q;
        memcpy(mutated, rep, n + 1);
        q = strstr(mutated, "\"maxLength\":4096");
        if (q) memcpy(q + 12, "8192", 4);
        CHECK(q != NULL && os_rf_verify_tools_list(&p, mutated, n) == OS_RF_ERR_FINGERPRINT && !p.tools_verified,
              "G15-SCHEMA-CHANGE-REVOKES-VERIFICATION");
        const char *empty = "{\"jsonrpc\":\"2.0\",\"id\":2,\"result\":{\"tools\":[]}}";
        CHECK(os_rf_verify_tools_list(&p, empty, strlen(empty)) ==
              OS_RF_ERR_FINGERPRINT, "G15-SCHEMA-EXTRA-OR-MISSING-TOOLS-DENIED");
        CHECK(os_rf_verify_tools_list(&p, "not json", 8) == OS_RF_ERR_REPLY, "G15-SCHEMA-MALFORMED-LIST-DENIED");
    }
    CHECK(os_rf_verify_tools_list(&p, rep, n) == OS_RF_OK, "G15-SCHEMA-REVERIFIED");
    CHECK(feed(&c, "tools_list.json", &r) == OS_MCP_CLIENT_OK, "G15-LIST-REPLY-CONSUMED-BY-CLIENT");

    /* Catalog and status against real replies. */
    CHECK(os_rf_build_call(&c, &p, 3, OS_RF_TOOL_CATALOG, NULL, req, sizeof req) > 0 &&
          strstr(req, "\"name\":\"roboforge_catalog_v1\"") && strstr(req, "\"arguments\":{}"), "G15-CALL-CATALOG-BUILT");
    CHECK(feed(&c, "catalog.json", &r) == OS_MCP_CLIENT_OK && !r.is_tool_error && r.untrusted,
          "G15-UNTRUSTED-REAL-CATALOG-FLAGGED-UNTRUSTED");
    CHECK(os_rf_build_call(&c, &p, 4, OS_RF_TOOL_STATUS, "janus", req, sizeof req) > 0 &&
          strstr(req, "\"module_id\":\"janus\""), "G15-CALL-STATUS-BUILT");
    CHECK(feed(&c, "status_janus.json", &r) == OS_MCP_CLIENT_OK && !r.is_tool_error, "G15-CALL-REAL-STATUS-REPLY-ACCEPTED");

    /* Schema validation of parameters. */
    {
        const char *bad[] = { "", "Janus", "1abc", "a b", "a\"b", "-x", "a_b", "../etc", NULL };
        int ok = 1;
        char longid[80];
        for (int i = 0; bad[i]; i++) {
            os_mcp_client t = c;
            ok = ok && os_rf_build_call(&t, &p, 5, OS_RF_TOOL_STATUS, bad[i], req, sizeof req) == OS_RF_ERR_SCHEMA;
        }
        memset(longid, 'a', 65); longid[65] = 0;
        ok = ok && os_rf_build_call(&c, &p, 5, OS_RF_TOOL_STATUS, longid, req, sizeof req) == OS_RF_ERR_SCHEMA;
        ok = ok && os_rf_build_call(&c, &p, 5, OS_RF_TOOL_STATUS, NULL, req, sizeof req) == OS_RF_ERR_SCHEMA;
        CHECK(ok && !c.pending, "G15-SCHEMA-MODULE-ID-PATTERN-ENFORCED");
        memset(longid, 'a', 64); longid[64] = 0;
        CHECK(os_rf_build_call(&c, &p, 5, OS_RF_TOOL_STATUS, longid, req, sizeof req) > 0, "G15-SCHEMA-MODULE-ID-MAX-LENGTH-ACCEPTED");
        os_mcp_client_build_cancel(&c, req, sizeof req);
    }
    CHECK(os_rf_build_call(&c, &p, 5, 7, NULL, req, sizeof req) == OS_RF_ERR_TOOL, "G15-CALL-UNKNOWN-TOOL-INDEX-DENIED");

    /* Refract compile preview: separately opted in, bounded, escaped. */
    CHECK(os_rf_build_call(&c, &p, 5, OS_RF_TOOL_REFRACT, "var x = 1", req, sizeof req) == OS_RF_ERR_DISABLED,
          "G15-REFRACT-DISABLED-WITHOUT-OPT-IN");
    p.refract_opt_in = 1;
    c.next_id = 5;
    {
        char big[OS_RF_SOURCE_MAX + 8];
        memset(big, 'x', sizeof big - 1); big[sizeof big - 1] = 0;
        CHECK(os_rf_build_call(&c, &p, 5, OS_RF_TOOL_REFRACT, "", req, sizeof req) == OS_RF_ERR_SCHEMA &&
              os_rf_build_call(&c, &p, 5, OS_RF_TOOL_REFRACT, NULL, req, sizeof req) == OS_RF_ERR_SCHEMA &&
              os_rf_build_call(&c, &p, 5, OS_RF_TOOL_REFRACT, big, req, sizeof req) == OS_RF_ERR_SCHEMA,
              "G15-REFRACT-SOURCE-BOUNDS-ENFORCED");
    }
    CHECK(os_rf_build_call(&c, &p, 5, OS_RF_TOOL_REFRACT, "var x = \"a\"\n\t\\", req, sizeof req) > 0 &&
          strstr(req, "var x = \\\"a\\\"\\n\\t\\\\"), "G15-REFRACT-SOURCE-JSON-ESCAPED");
    CHECK(feed(&c, "refract_disabled.json", &r) == OS_MCP_CLIENT_OK && r.is_tool_error,
          "G15-REFRACT-REAL-DISABLED-REPLY-IS-TOOL-ERROR");
    CHECK(c.state == OS_MCP_CLIENT_READY && !c.pending, "G15-RECOVERY-CLIENT-READY-AFTER-TOOL-ERROR");

    /* Reply size limits and recovery. */
    CHECK(os_rf_check_reply_size(0) == OS_RF_ERR_REPLY && os_rf_check_reply_size(OS_RF_MAX_REPLY) == OS_RF_OK &&
          os_rf_check_reply_size(OS_RF_MAX_REPLY + 1) == OS_RF_ERR_REPLY, "G15-LIMITS-REPLY-32KIB-ENFORCED");
    CHECK(OS_RF_MAX_REQUEST == 12288u && OS_RF_SOURCE_MAX == 4096u, "G15-LIMITS-REQUEST-12KIB-SOURCE-4096");
    CHECK(os_rf_build_call(&c, &p, 6, OS_RF_TOOL_CATALOG, NULL, req, 8) == OS_RF_ERR_SCHEMA && !c.pending,
          "G15-RECOVERY-OVERFLOW-LEAVES-CLIENT-IDLE");
    CHECK(os_rf_build_call(&c, &p, 6, OS_RF_TOOL_CATALOG, NULL, req, sizeof req) > 0 &&
          os_mcp_client_check_timeout(&c, 6 + 101) == OS_MCP_CLIENT_ERR_TIMEOUT, "G15-RECOVERY-OFFLINE-GATEWAY-TIMES-OUT");

    printf(failures ? "G15 RESULT: FAIL (%d)\n" : "G15 RESULT: PASS\n", failures);
    return failures ? 1 : 0;
}
