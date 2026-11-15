#include <stdio.h>
#include <string.h>
#include "jsonrpc.h"
#include "mcp_registry.h"
#include "sha256.h"

static int failures;
#define CHECK(c, name) do { if (c) printf("PASS %s\n", name); else { printf("FAIL %s\n", name); failures++; } } while (0)

static void hex(const uint8_t d[32], char out[65])
{
    static const char x[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) { out[2 * i] = x[d[i] >> 4]; out[2 * i + 1] = x[d[i] & 15]; }
    out[64] = 0;
}

static int sha_is(const void *data, size_t n, const char *expect)
{
    uint8_t d[32]; char h[65];
    os_sha256_hash(data, n, d); hex(d, h);
    return strcmp(h, expect) == 0;
}

static int rpc(const char *s) { os_jrpc_request r; return os_jrpc_parse_request(s, strlen(s), &r); }

static void test_sha(void)
{
    CHECK(sha_is("abc", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), "G14-SHA256-ABC");
    CHECK(sha_is("", 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"), "G14-SHA256-EMPTY");
    const char *m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    CHECK(sha_is(m, strlen(m), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"), "G14-SHA256-448BIT");
    os_sha256 c; uint8_t d[32]; char h[65]; char buf[1000];
    memset(buf, 'a', sizeof buf);
    os_sha256_init(&c);
    for (int i = 0; i < 1000; i++) os_sha256_update(&c, buf, sizeof buf);
    os_sha256_final(&c, d); hex(d, h);
    CHECK(strcmp(h, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0") == 0, "G14-SHA256-MILLION-A");
}

static void test_jsonrpc(void)
{
    os_jrpc_request r;
    const char *ok = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\",\"params\":{}}";
    CHECK(os_jrpc_parse_request(ok, strlen(ok), &r) == OS_JRPC_OK && !r.is_notification &&
          r.id.kind == OS_JRPC_ID_NUMBER && r.id.number == 1 && strcmp(r.method, "tools/list") == 0,
          "G14-JRPC-VALID-REQUEST");
    const char *n = "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}";
    CHECK(os_jrpc_parse_request(n, strlen(n), &r) == OS_JRPC_OK && r.is_notification, "G14-JRPC-NOTIFICATION");
    CHECK(rpc("{\"jsonrpc\":\"2.0\",\"id\":\"a\",\"method\":\"ping\"}") == OS_JRPC_OK, "G14-JRPC-STRING-ID");
    CHECK(rpc("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"x\",") == OS_JRPC_PARSE_ERROR, "G14-JRPC-TRUNCATED");
    CHECK(rpc("") == OS_JRPC_PARSE_ERROR || rpc("") == OS_JRPC_INVALID_REQUEST, "G14-JRPC-EMPTY");
    CHECK(rpc("[{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"x\"}]") == OS_JRPC_INVALID_REQUEST, "G14-JRPC-BATCH-REJECTED");
    CHECK(rpc("{\"jsonrpc\":\"2.0\",\"id\":null,\"method\":\"x\"}") == OS_JRPC_INVALID_REQUEST, "G14-JRPC-NULL-ID");
    CHECK(rpc("{\"jsonrpc\":\"1.0\",\"id\":1,\"method\":\"x\"}") == OS_JRPC_INVALID_REQUEST, "G14-JRPC-VERSION");
    CHECK(rpc("{\"jsonrpc\":\"2.0\",\"id\":1}") == OS_JRPC_INVALID_REQUEST, "G14-JRPC-NO-METHOD");
    CHECK(rpc("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"rpc.x\"}") == OS_JRPC_INVALID_REQUEST, "G14-JRPC-RESERVED-METHOD");
    CHECK(rpc("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"x\",\"params\":[]}") == OS_JRPC_INVALID_REQUEST, "G14-JRPC-ARRAY-PARAMS");
    CHECK(rpc("{\"jsonrpc\":\"2.0\",\"id\":1,\"id\":2,\"method\":\"x\"}") != OS_JRPC_OK, "G14-JRPC-DUPLICATE-KEY");
    CHECK(rpc("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"x\",\"extra\":1}") == OS_JRPC_INVALID_REQUEST, "G14-JRPC-UNKNOWN-MEMBER");
    CHECK(rpc("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"x\",\"params\":{\"a\":[[[[[[[[[1]]]]]]]]]}}") != OS_JRPC_OK, "G14-JRPC-DEPTH-LIMIT");
    CHECK(rpc("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"x\",\"params\":{\"a\":\"\xff\"}}") != OS_JRPC_OK, "G14-JRPC-BAD-UTF8");
    char big[OS_JRPC_MAX_MESSAGE + 64];
    memset(big, ' ', sizeof big);
    memcpy(big, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"x\"}", 37);
    CHECK(os_jrpc_parse_request(big, sizeof big, &r) != OS_JRPC_OK, "G14-JRPC-SIZE-LIMIT");
}

#define SCHEMA "{\"type\":\"object\",\"properties\":{}}"

static void test_registry(void)
{
    static os_mcp_registry reg;
    uint8_t fp[32];
    os_mcp_registry_init(&reg);
    int t = os_mcp_tool_register(&reg, "experimentalos_development_capabilities_v1", 1, SCHEMA, strlen(SCHEMA), 0x1, 0);
    int s = os_mcp_tool_register(&reg, "experimentalos_install_authorize_v1", 1, SCHEMA, strlen(SCHEMA), 0x3, 1);
    CHECK(t == 0 && s == 1, "G14-REG-REGISTER");
    CHECK(os_mcp_tool_register(&reg, "experimentalos_install_authorize_v1", 1, SCHEMA, strlen(SCHEMA), 0, 0) == OS_MCP_ERR_DUPLICATE, "G14-REG-DUPLICATE");
    CHECK(os_mcp_tool_register(&reg, "Bad Name", 1, SCHEMA, strlen(SCHEMA), 0, 0) == OS_MCP_ERR_NAME, "G14-REG-BAD-NAME");
    CHECK(os_mcp_authorize_call(&reg, "experimentalos_development_capabilities_v1", 1, 10, 1, 0) == OS_MCP_ERR_NOT_APPROVED, "G14-REG-UNAPPROVED-DENIED");
    CHECK(os_mcp_authorize_call(&reg, "missing", 1, 10, 1, 0) == OS_MCP_ERR_NOT_FOUND, "G14-REG-UNKNOWN-DENIED");
    uint8_t wrong[32] = {0};
    CHECK(os_mcp_tool_approve(&reg, t, wrong, 100) == OS_MCP_ERR_FINGERPRINT, "G14-REG-WRONG-FINGERPRINT");
    os_mcp_tool_fingerprint("experimentalos_development_capabilities_v1", 1, SCHEMA, strlen(SCHEMA), fp);
    CHECK(os_mcp_tool_approve(&reg, t, fp, 100) == OS_MCP_OK, "G14-REG-APPROVE");
    CHECK(os_mcp_authorize_call(&reg, "experimentalos_development_capabilities_v1", 1, 10, 1, 0) == t, "G14-REG-CALL-ALLOWED");
    CHECK(os_mcp_authorize_call(&reg, "experimentalos_development_capabilities_v1", 1, 10, 1, 0) == OS_MCP_ERR_REPLAY, "G14-REG-REPLAY-DENIED");
    CHECK(os_mcp_authorize_call(&reg, "experimentalos_development_capabilities_v1", 0, 10, 2, 0) == OS_MCP_ERR_CAPABILITY, "G14-REG-CAPABILITY-DENIED");
    CHECK(os_mcp_authorize_call(&reg, "experimentalos_development_capabilities_v1", 1, 100, 3, 0) == OS_MCP_ERR_EXPIRED, "G14-REG-EXPIRY");
    os_mcp_tool_fingerprint("experimentalos_install_authorize_v1", 1, SCHEMA, strlen(SCHEMA), fp);
    os_mcp_tool_approve(&reg, s, fp, 100);
    CHECK(os_mcp_authorize_call(&reg, "experimentalos_install_authorize_v1", 3, 10, 4, 0) == OS_MCP_ERR_SIDE_EFFECT, "G14-REG-SIDE-EFFECT-NEEDS-OWNER");
    CHECK(os_mcp_authorize_call(&reg, "experimentalos_install_authorize_v1", 3, 10, 5, 1) == s, "G14-REG-SIDE-EFFECT-OWNER-OK");
    const char *changed = "{\"type\":\"object\",\"properties\":{\"x\":{}}}";
    os_mcp_tool_update_schema(&reg, s, changed, strlen(changed));
    CHECK(os_mcp_authorize_call(&reg, "experimentalos_install_authorize_v1", 3, 10, 6, 1) == OS_MCP_ERR_NOT_APPROVED, "G14-REG-RUG-PULL-REJECTED");
    os_mcp_tool_revoke(&reg, t);
    CHECK(os_mcp_authorize_call(&reg, "experimentalos_development_capabilities_v1", 1, 10, 7, 0) == OS_MCP_ERR_NOT_APPROVED, "G14-REG-REVOKED");
    CHECK(os_mcp_audit_verify(&reg) == OS_MCP_OK, "G14-REG-AUDIT-CHAIN");
    reg.audit[2].result ^= 1;
    CHECK(os_mcp_audit_verify(&reg) != OS_MCP_OK, "G14-REG-AUDIT-TAMPER-DETECTED");
    reg.audit[2].result ^= 1;
    reg.audit_count = OS_MCP_AUDIT_MAX - 1u;
    CHECK(os_mcp_authorize_call(&reg, "experimentalos_install_authorize_v1", 3, 10, 99, 1) == OS_MCP_ERR_FULL, "G14-REG-AUDIT-FULL-FAILS-CLOSED");
}

int main(void)
{
    test_sha();
    test_jsonrpc();
    test_registry();
    printf(failures ? "G14 MCP HOST TESTS FAIL (%d)\n" : "G14 MCP HOST TESTS PASS\n", failures);
    return failures ? 1 : 0;
}
