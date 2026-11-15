#include <stdio.h>
#include <string.h>
#include "mcp_client.h"
#include "roboforge_adapter.h"

/*
 * Interactive adapter session. Every request is printed as "REQ|NOTIFY <json>";
 * the transport driver answers "RSP <json>" on stdin ("NONE" for notifications).
 */
static int failures;
#define CHECK(c, name) do { if (c) printf("PASS %s\n", name); else { printf("FAIL %s\n", name); failures++; } } while (0)

static char req[OS_RF_MAX_REQUEST + 64];
static char line[OS_RF_MAX_REPLY + 64];

static size_t exchange(const char *kind, const char *json)
{
    printf("%s %s\n", kind, json);
    fflush(stdout);
    if (!fgets(line, sizeof line, stdin)) return 0;
    size_t n = strlen(line);
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
    return n;
}

static int round_trip(os_mcp_client *c, const char *json, os_mcp_client_result *r)
{
    size_t n = exchange("REQ", json);
    if (strncmp(line, "RSP ", 4) != 0 || os_rf_check_reply_size(n - 4) != OS_RF_OK) return -100;
    return os_mcp_client_on_message(c, line + 4, n - 4, r);
}

int main(int argc, char **argv)
{
    os_mcp_client c;
    os_rf_policy p;
    os_mcp_client_result r;
    int refract = argc > 1 && strcmp(argv[1], "refract") == 0;

    os_mcp_client_init(&c, 100);
    os_rf_policy_init(&p);
    p.enabled = 1;
    p.refract_opt_in = refract;

    CHECK(os_mcp_client_build_initialize(&c, 1, req, sizeof req) > 0 && round_trip(&c, req, &r) == OS_MCP_CLIENT_OK && !r.is_error,
          "G15-LIVE-INITIALIZE");
    CHECK(os_mcp_client_build_initialized(&c, req, sizeof req) > 0, "G15-LIVE-INITIALIZED-BUILT");
    exchange("NOTIFY", req);

    CHECK(os_mcp_client_build_list(&c, 2, req, sizeof req) > 0, "G15-LIVE-LIST-BUILT");
    {
        size_t n = exchange("REQ", req);
        int ok = strncmp(line, "RSP ", 4) == 0 && os_rf_verify_tools_list(&p, line + 4, n - 4) == OS_RF_OK;
        CHECK(ok, "G15-LIVE-TOOLS-FINGERPRINT-MATCHES-PINNED");
        CHECK(os_mcp_client_on_message(&c, line + 4, n - 4, &r) == OS_MCP_CLIENT_OK, "G15-LIVE-LIST-CONSUMED");
    }

    CHECK(os_rf_build_call(&c, &p, 3, OS_RF_TOOL_CATALOG, NULL, req, sizeof req) > 0 &&
          round_trip(&c, req, &r) == OS_MCP_CLIENT_OK && !r.is_tool_error && r.untrusted, "G15-LIVE-CATALOG");
    CHECK(os_rf_build_call(&c, &p, 4, OS_RF_TOOL_STATUS, "janus", req, sizeof req) > 0 &&
          round_trip(&c, req, &r) == OS_MCP_CLIENT_OK && !r.is_tool_error, "G15-LIVE-STATUS-KNOWN-MODULE");
    {
        int rc;
        CHECK(os_rf_build_call(&c, &p, 5, OS_RF_TOOL_STATUS, "no-such-module", req, sizeof req) > 0, "G15-LIVE-UNKNOWN-MODULE-BUILT");
        rc = round_trip(&c, req, &r);
        CHECK(rc == OS_MCP_CLIENT_ERR_REMOTE && r.is_error && r.error_code == -32602, "G15-LIVE-UNKNOWN-MODULE-REJECTED-BY-GATEWAY");
        CHECK(c.state == OS_MCP_CLIENT_READY && !c.pending, "G15-LIVE-RECOVERS-AFTER-REMOTE-ERROR");
    }
    if (!refract) {
        CHECK(os_rf_build_call(&c, &p, 6, OS_RF_TOOL_REFRACT, "var x = 1", req, sizeof req) == OS_RF_ERR_DISABLED,
              "G15-LIVE-REFRACT-REFUSED-WITHOUT-OPT-IN");
    } else {
        CHECK(os_rf_build_call(&c, &p, 6, OS_RF_TOOL_REFRACT, "var x = 1", req, sizeof req) > 0 &&
              round_trip(&c, req, &r) == OS_MCP_CLIENT_OK, "G15-LIVE-REFRACT-REPLY");
        printf("INFO refract is_tool_error=%d\n", r.is_tool_error);
    }
    printf(failures ? "G15-LIVE RESULT: FAIL (%d)\n" : "G15-LIVE RESULT: PASS\n", failures);
    fflush(stdout);
    return failures ? 1 : 0;
}
