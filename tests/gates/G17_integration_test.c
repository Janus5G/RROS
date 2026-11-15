#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "devpkg.h"
#include "eosvm.h"
#include "mcp_client.h"
#include "mcp_deploy.h"
#include "mcp_server.h"
#include "sha256.h"

static int failures;
#define CHECK(c, name) do { if (c) printf("PASS %s\n", name); else { printf("FAIL %s\n", name); failures++; } } while (0)

#define BLOCKS 512u
static uint8_t disk[BLOCKS][OS_STORAGE_BLOCK_SIZE];
static int dev_read(void *c, u32 b, u8 d[OS_STORAGE_BLOCK_SIZE]) { (void)c; if (b >= BLOCKS) return 0; memcpy(d, disk[b], OS_STORAGE_BLOCK_SIZE); return 1; }
static int dev_write(void *c, u32 b, const u8 d[OS_STORAGE_BLOCK_SIZE]) { (void)c; if (b >= BLOCKS) return 0; memcpy(disk[b], d, OS_STORAGE_BLOCK_SIZE); return 1; }
static os_storage_device device = { 0, BLOCKS, dev_read, dev_write };

static os_mcp_registry reg;
static os_dm_manager dm;
static os_mcp_server srv;
static os_mcp_client cli;
static os_storage_fs fs;
static os_pkg_manager mgr;
static uint64_t now = 10;
static char resp[16384], req[8192];

static const uint8_t KEY[16] = { 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 1, 2, 3, 4, 5, 6 };

static int send_raw(const char *msg) { resp[0] = 0; return os_mcp_server_handle(&srv, now, msg, strlen(msg), resp, sizeof resp); }
static int has(const char *needle) { return strstr(resp, needle) != 0; }

static int frozen_tool_count(void)
{
    int c = 0;
    for (unsigned i = 0; i < OS_MCP_MAX_TOOLS; i++) if (reg.tools[i].state != OS_MCP_TOOL_UNUSED) c++;
    return c;
}

static void setup(void)
{
    memset(disk, 0, sizeof disk);
    os_storage_format(&device);
    os_storage_mount(&fs, &device);
    os_mcp_registry_init(&reg);
    os_dm_init(&dm);
    os_mcp_server_init(&srv, &reg, 0x1FF, 200, 10);
    os_mcp_client_init(&cli, 5);
    os_dm_register_tools(&dm, &reg, &srv);
    for (unsigned i = 0; i < OS_DM_TOOL_COUNT; i++) {
        uint8_t fp[32];
        os_mcp_tool_fingerprint(os_dm_contracts[i].name, 1, os_dm_contracts[i].schema, strlen(os_dm_contracts[i].schema), fp);
        os_mcp_tool_approve(&reg, (int)i, fp, 100000);
    }
    os_mcp_server_set_owner_confirmation(&srv, 1);
    {
        os_mcp_client_result r;
        os_mcp_client_build_initialize(&cli, now, req, sizeof req);
        send_raw(req);
        os_mcp_client_on_message(&cli, resp, strlen(resp), &r);
        os_mcp_client_build_initialized(&cli, req, sizeof req);
        send_raw(req);
    }
}

static int call(const char *tool, const char *args)
{
    os_mcp_client_result r;
    int n = os_mcp_client_build_call(&cli, now, tool, args, req, sizeof req);
    if (n <= 0) return n;
    n = send_raw(req);
    if (n == 0) { resp[0] = 0; n = os_mcp_server_poll(&srv, now + 10, resp, sizeof resp); }
    os_mcp_client_on_message(&cli, resp, strlen(resp), &r);
    return n;
}

static size_t build_signed(uint8_t *out, size_t cap, const char *id, uint32_t version, const uint8_t *code, size_t len)
{
    os_pkg_manifest m;
    size_t n = 0;
    memset(&m, 0, sizeof m);
    strcpy(m.id, id); m.version = version; strcpy(m.arch, "aarch64"); m.kind = OS_PKG_KIND_EOSVM; m.min_abi = 1;
    if (os_pkg_build(&m, code, len, out, cap, &n) || os_pkg_sign(out, n, 1, KEY, 16)) return 0;
    return n;
}

int main(int argc, char **argv)
{
    static uint8_t code[OS_EOSVM_CODE_MAX], pkg[OS_PKG_MAX_BYTES];
    static char source[8192];
    size_t src_len, code_len, n;
    unsigned line;
    os_pkg_view v;
    os_eosvm_result r;
    const char *root = argc > 1 ? argv[1] : ".";
    char path[1024];
    FILE *f;

    snprintf(path, sizeof path, "%s/sdk/examples/fib.eos", root);
    f = fopen(path, "rb");
    if (!f) { printf("FAIL G17-INT-SOURCE-AVAILABLE\n"); return 1; }
    src_len = fread(source, 1, sizeof source - 1, f);
    fclose(f);
    CHECK(os_eosvm_assemble(source, src_len, code, sizeof code, &code_len, &line) == 0, "G17-INT-BUILD-TOOL-ASSEMBLES-SDK-SOURCE");
    n = build_signed(pkg, sizeof pkg, "fib", 1, code, code_len);
    CHECK(n > 0 && os_pkg_inspect(pkg, n, &v) == 0, "G17-INT-PACKAGE-BUILT-AND-SIGNED");

    setup();
    os_pkg_manager_init(&mgr, &fs, "aarch64", 1);
    os_pkg_provision_key(&mgr, 1, KEY);
    os_dm_provision_key(&dm, 0, KEY);
    os_dm_enroll_node(&dm, "pi5-a", "aarch64", "rros-base-1", 1024);
    CHECK(os_pkg_import(&mgr, pkg, n) == 0, "G17-INT-HOST-IMPORTS-REAL-PACKAGE");
    {
        int idx = os_dm_import_real_package(&dm, "fib", 1, "aarch64", "", v.manifest.size_kb, v.digest);
        CHECK(idx >= 0 && os_dm_sign_package(&dm, "fib-v1", 0) == 0 && os_dm_package_verify(&dm, idx) == 0, "G17-INT-HOST-REGISTERS-PACKAGE-FOR-MCP");
    }
    CHECK(os_dm_import_real_package(&dm, "fib", 1, "aarch64", "", 1, v.digest) == OS_DM_ERR_STATE &&
          os_dm_import_real_package(&dm, "Bad Id", 1, "aarch64", "", 1, v.digest) == OS_DM_ERR_ARGUMENT &&
          os_dm_import_real_package(&dm, "fib", 0, "aarch64", "", 1, v.digest) == OS_DM_ERR_ARGUMENT, "G17-INT-BRIDGE-VALIDATES-INPUT");

    call("experimentalos_package_integrity_inspect_v1", "{\"package\":\"fib-v1\"}");
    CHECK(has("digest=ok;signature=ok;target=ok"), "G17-INT-MCP-INSPECTS-INTEGRITY");
    call("experimentalos_pi5_compatibility_inspect_v1", "{\"package\":\"fib-v1\"}");
    CHECK(has("compatible: aarch64"), "G17-INT-MCP-COMPATIBILITY-INSPECTION");
    call("experimentalos_deployment_propose_v1", "{\"package\":\"fib-v1\",\"node\":\"pi5-a\"}");
    CHECK(has("plan=0;state=proposed;executes=no"), "G17-INT-MCP-PROPOSES-WITHOUT-EXECUTING");
    CHECK(os_pkg_find_installed(&mgr, "fib") < 0 && !os_storage_exists(&fs, "/app/fib-1", NULL), "G17-INT-PROPOSAL-INSTALLED-NOTHING");
    call("experimentalos_install_authorize_v1", "{\"plan\":0,\"approved\":true}");
    CHECK(has("-32602") && dm.plans[0].state == OS_DM_PLAN_PROPOSED, "G17-INT-MODEL-CANNOT-SELF-APPROVE");
    call("experimentalos_install_authorize_v1", "{\"plan\":0}");
    CHECK(has("awaiting-recipient-approval"), "G17-INT-MCP-REQUESTS-AUTHORIZATION-ONLY");
    CHECK(os_pkg_install(&mgr, "fib", 1, now) == OS_PKG_ERR_APPROVAL && os_pkg_find_installed(&mgr, "fib") < 0, "G17-INT-INSTALL-REFUSED-WITHOUT-RECIPIENT-APPROVAL");
    call("experimentalos_application_run_v1", "{\"package\":\"fib-v1\"}");
    CHECK(has("-32602") || has("unknown") || has("not found") || has("error"), "G17-INT-NO-EXECUTE-TOOL-EXISTS");
    CHECK(frozen_tool_count() == OS_DM_TOOL_COUNT, "G17-INT-TOOL-SET-IS-THE-FROZEN-TEN");
    CHECK(os_pkg_find_installed(&mgr, "fib") < 0, "G17-INT-NOTHING-RAN-VIA-MCP");

    /* Recipient-side (host service) approval and activation, bound to the same digest. */
    {
        uint8_t dmdigest[32];
        memcpy(dmdigest, dm.packages[0].digest, 32);
        CHECK(os_dm_recipient_approve(&dm, 0, dmdigest, "pi5-a", 1, 500, now) == 0 && os_pkg_approve(&mgr, "fib", 1, v.digest, 500, now) == 0, "G17-INT-RECIPIENT-APPROVES-BOTH-LAYERS");
        {
            /* Tamper with the staged package after approval: the real install must refuse. */
            uint8_t tampered[OS_PKG_MAX_BYTES];
            usize sz = 0, got = 0;
            os_storage_exists(&fs, "/pkg/fib-1", &sz);
            os_storage_read(&fs, "/pkg/fib-1", 0, tampered, sz, &got);
            tampered[sz - 70] ^= 1;
            os_storage_write(&fs, "/pkg/fib-1", tampered, sz);
            CHECK(os_pkg_install(&mgr, "fib", 1, now) != 0 && os_pkg_find_installed(&mgr, "fib") < 0, "G17-INT-TAMPER-AFTER-APPROVAL-REFUSED");
            os_storage_write(&fs, "/pkg/fib-1", pkg, n);
        }
        CHECK(os_dm_activate(&dm, 0, now, OS_DM_FAULT_NONE) == 0 && os_pkg_install(&mgr, "fib", 1, now) == 0, "G17-INT-AUTHORIZED-ACTIVATION-AND-INSTALL");
    }
    CHECK(os_pkg_run(&mgr, "fib", OS_EOSVM_FUEL_DEFAULT, &r) == 0 && strcmp(r.output, "0\n1\n1\n2\n3\n5\n8\n13\n21\n34\n") == 0, "G17-INT-REAL-INSTALLED-PROGRAM-EXECUTES");
    call("experimentalos_remote_node_status_v1", "{\"node\":\"pi5-a\"}");
    CHECK(has("installed=fib-v1"), "G17-INT-MCP-STATUS-REFLECTS-ACTIVATION");
    CHECK(os_mcp_audit_verify(&reg) == OS_MCP_OK, "G17-INT-AUDIT-CHAIN-VALID");
    printf(failures ? "G17 INTEGRATION RESULT: FAIL (%d)\n" : "G17 INTEGRATION RESULT: PASS\n", failures);
    return failures ? 1 : 0;
}
