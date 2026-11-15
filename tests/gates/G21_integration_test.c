#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "devpkg.h"
#include "hmac_sha256.h"
#include "secdeploy.h"
#include "secsvc.h"
#include "sha256.h"
#include "storage.h"

/* Cross-module integration: secsvc + secdeploy + devpkg + hmac/sha256 + storage across a restart. */

static int failures;
#define CHECK(c, name) do { if (c) printf("PASS %s\n", name); else { printf("FAIL %s\n", name); failures++; } } while (0)

static uint64_t now = 1000;
static const char OWNER_PW[] = "Own3r-Pass-1";
static const char OP_PW[] = "Op3rator-Pass-9";
static const uint8_t SIGKEY[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };

static const uint8_t *fill(uint8_t seed, size_t n)
{
    static uint8_t buf[8][64];
    static unsigned idx;
    uint8_t *p = buf[idx++ % 8u];
    for (size_t i = 0; i < n; i++) p[i] = (uint8_t)(seed * 31u + i * 7u + 1u);
    return p;
}
static const uint8_t *ent(void)
{
    static uint8_t counter;
    return fill((uint8_t)(++counter + 100u), 16);
}

typedef struct {
    sec_system sec;
    secd_node node;
    sec_token owner, op;
    uint8_t disk[96 * OS_STORAGE_BLOCK_SIZE];
    os_storage_fs fs;
    os_storage_device dev;
    uint8_t state_key[32];
} N;
static N A, B, C;
static uint8_t PSK_AB[32], PSK_AC[32];
static uint8_t frame[SECD_FRAME_MAX], env[SECD_ENVELOPE_MAX], inner[OS_PKG_MAX_BYTES];
static size_t frame_len, env_len, inner_len;

static int disk_read(void *ctx, u32 block, u8 data[OS_STORAGE_BLOCK_SIZE])
{
    N *n = ctx;
    if (block >= 96) return 0;
    memcpy(data, n->disk + (size_t)block * OS_STORAGE_BLOCK_SIZE, OS_STORAGE_BLOCK_SIZE);
    return 1;
}
static int disk_write(void *ctx, u32 block, const u8 data[OS_STORAGE_BLOCK_SIZE])
{
    N *n = ctx;
    if (block >= 96) return 0;
    memcpy(n->disk + (size_t)block * OS_STORAGE_BLOCK_SIZE, data, OS_STORAGE_BLOCK_SIZE);
    return 1;
}

static void login(N *n)
{
    sec_logout(&n->sec, &n->owner, now);
    sec_logout(&n->sec, &n->op, now);
    sec_login(&n->sec, "owner", OWNER_PW, ent(), now, &n->owner);
    sec_login(&n->sec, "op", OP_PW, ent(), now, &n->op);
}

static void boot_node(N *n, const char *id, const char *arch, int first)
{
    uint8_t sk[32];
    memset(sk, (int)id[5], sizeof sk);
    memset(&n->sec, 0, sizeof n->sec);
    memset(&n->node, 0, sizeof n->node);
    sec_init(&n->sec, sk);
    sec_bootstrap_owner(&n->sec, "owner", OWNER_PW, fill(21, 16), now);
    sec_login(&n->sec, "owner", OWNER_PW, ent(), now, &n->owner);
    sec_account_create(&n->sec, &n->owner, "op", OP_PW, SEC_ROLE_OPERATOR, fill(22, 16), now);
    sec_login(&n->sec, "op", OP_PW, ent(), now, &n->op);
    secd_node_init(&n->node, &n->sec, id, arch, 64);
    secd_key_add(&n->node, &n->owner, SIGKEY, 0, 1000000, now);
    if (first) {
        memcpy(n->state_key, fill((uint8_t)id[5], 32), 32);
        n->dev.context = n;
        n->dev.block_count = 96;
        n->dev.read_block = disk_read;
        n->dev.write_block = disk_write;
        memset(n->disk, 0, sizeof n->disk);
        os_storage_format(&n->dev);
    }
}

#define STATE_HDR 12u
typedef struct {
    uint32_t gen;
    uint8_t body[sizeof(((secd_node *)0)->installed) + sizeof(((secd_node *)0)->floors)];
} state_blob;

static int persist(N *n, uint32_t gen)
{
    state_blob b;
    uint8_t tag[32], out[sizeof b + 32];
    memset(&b, 0, sizeof b);
    b.gen = gen;
    memcpy(b.body, n->node.installed, sizeof n->node.installed);
    memcpy(b.body + sizeof n->node.installed, n->node.floors, sizeof n->node.floors);
    os_hmac_sha256(n->state_key, 32, (const uint8_t *)&b, sizeof b, tag);
    memcpy(out, &b, sizeof b);
    memcpy(out + sizeof b, tag, 32);
    if (!os_storage_mount(&n->fs, &n->dev)) return -1;
    return os_storage_write(&n->fs, "/node.state", out, sizeof out) ? 0 : -2;
}

/* Restores only authenticated state whose generation is not below the externally held minimum. */
static int restore(N *n, uint32_t min_gen)
{
    state_blob b;
    uint8_t in[sizeof b + 32], tag[32];
    usize got = 0;
    if (!os_storage_mount(&n->fs, &n->dev)) return -1;
    if (!os_storage_read(&n->fs, "/node.state", 0, in, sizeof in, &got) || got != sizeof in) return -2;
    memcpy(&b, in, sizeof b);
    os_hmac_sha256(n->state_key, 32, (const uint8_t *)&b, sizeof b, tag);
    if (!os_hmac_tag_equal(tag, in + sizeof b)) return -3;
    if (b.gen < min_gen) return -4;
    memcpy(n->node.installed, b.body, sizeof n->node.installed);
    memcpy(n->node.floors, b.body + sizeof n->node.installed, sizeof n->node.floors);
    n->node.used_kb = 0;
    for (unsigned i = 0; i < SECD_MAX_INSTALLED; i++)
        if (n->node.installed[i].used) n->node.used_kb += n->node.installed[i].size_kb;
    return 0;
}

static void make_env(const char *id, uint32_t version, const char *arch, const char *dep, size_t len)
{
    static uint8_t payload[OS_PKG_PAYLOAD_MAX];
    os_pkg_manifest m;
    memset(&m, 0, sizeof m);
    snprintf(m.id, sizeof m.id, "%s", id);
    m.version = version;
    snprintf(m.arch, sizeof m.arch, "%s", arch);
    snprintf(m.dependency, sizeof m.dependency, "%s", dep);
    m.kind = OS_PKG_KIND_APP_IMAGE;
    m.min_abi = 1;
    for (size_t i = 0; i < len; i++) payload[i] = (uint8_t)(i * 13u + version);
    payload[0] = 0x45; payload[1] = 0x58; payload[2] = 0x50; payload[3] = 0x41;
    payload[8] = (uint8_t)(strcmp(arch, "aarch64") == 0 ? 2 : 1);
    payload[9] = payload[10] = payload[11] = 0;
    if (os_pkg_build(&m, payload, len, inner, sizeof inner, &inner_len) != OS_PKG_OK) inner_len = 0;
    if (secd_envelope_build(inner, inner_len, SIGKEY, env, sizeof env, &env_len) != SECD_OK) env_len = 0;
}

static int ship(N *from, N *to, const char *to_id, const char *id, uint32_t ver, const char *arch, const char *dep, size_t len)
{
    make_env(id, ver, arch, dep, len);
    now++;
    if (secd_send(&from->node, &from->op, to_id, env, env_len, frame, sizeof frame, &frame_len, now) != SECD_OK) return 12345;
    return secd_receive(&to->node, &to->op, frame, frame_len, now);
}

static int install(N *n, const char *id, uint32_t ver)
{
    int s = secd_find_staged(&n->node, id, ver);
    now++;
    if (s < 0) return SECD_ERR_NOT_FOUND;
    int r = secd_approve(&n->node, &n->owner, id, ver, n->node.staged[s].digest, now + 500, now);
    if (r != SECD_OK) return r;
    return secd_install(&n->node, &n->op, id, ver, now, SECD_FAULT_NONE);
}

static void pair(N *x, const char *xid, N *y, const char *yid, const uint8_t *psk)
{
    secd_peer_enroll(&x->node, &x->owner, yid, psk, now);
    secd_peer_enroll(&y->node, &y->owner, xid, psk, now);
}

int main(void)
{
    memcpy(PSK_AB, fill(70, 32), 32);
    memcpy(PSK_AC, fill(71, 32), 32);
    boot_node(&A, "node-a", "x86_64", 1);
    boot_node(&B, "node-b", "x86_64", 1);
    boot_node(&C, "node-c", "aarch64", 1);
    pair(&A, "node-a", &B, "node-b", PSK_AB);
    pair(&A, "node-a", &C, "node-c", PSK_AC);

    CHECK(ship(&A, &B, "node-b", "core", 1, "x86_64", "", 1500) == SECD_OK && install(&B, "core", 1) == SECD_OK, "G21-NODE-B-INSTALLS-CORE-V1");
    CHECK(ship(&A, &C, "node-c", "core", 1, "aarch64", "", 1500) == SECD_OK && install(&C, "core", 1) == SECD_OK, "G21-NODE-C-INSTALLS-CORE-V1");
    CHECK(ship(&A, &C, "node-c", "core", 2, "x86_64", "", 1500) == SECD_ERR_COMPAT, "G21-CROSS-ARCH-PACKAGE-REFUSED-BY-AARCH64-NODE");
    CHECK(B.node.used_kb == 2 && C.node.used_kb == 2, "G21-NODE-ACCOUNTING-MATCHES");

    CHECK(persist(&B, 1) == 0 && persist(&C, 1) == 0, "G21-NODE-STATE-PERSISTED-TO-STORAGE");
    uint8_t before[sizeof B.node.installed];
    memcpy(before, B.node.installed, sizeof before);

    /* restart node B: fresh volatile state, durable state restored from storage */
    boot_node(&B, "node-b", "x86_64", 0);
    pair(&A, "node-a", &B, "node-b", PSK_AB);
    CHECK(secd_find_installed(&B.node, "core") < 0 && B.node.used_kb == 0, "G21-RESTART-STARTS-EMPTY");

    CHECK(restore(&B, 1) == 0, "G21-AUTHENTICATED-STATE-RESTORED");
    CHECK(memcmp(before, B.node.installed, sizeof before) == 0 && B.node.used_kb == 2, "G21-RESTORED-STATE-IDENTICAL-TO-PRE-RESTART");
    A.node.peers[0].last_seq += 0;
    login(&B);
    CHECK(ship(&A, &B, "node-b", "core", 1, "x86_64", "", 1500) == SECD_ERR_VERSION, "G21-DOWNGRADE-FLOOR-SURVIVES-RESTART");

    /* upgrade, persist, restart, rollback policy must be re-established */
    CHECK(ship(&A, &B, "node-b", "core", 2, "x86_64", "", 2500) == SECD_OK && install(&B, "core", 2) == SECD_OK, "G21-UPGRADE-AFTER-RESTART");
    CHECK(persist(&B, 2) == 0, "G21-UPGRADED-STATE-PERSISTED");
    boot_node(&B, "node-b", "x86_64", 0);
    pair(&A, "node-a", &B, "node-b", PSK_AB);
    CHECK(restore(&B, 2) == 0 && B.node.installed[secd_find_installed(&B.node, "core")].version == 2, "G21-UPGRADED-STATE-RESTORED");
    CHECK(secd_rollback(&B.node, &B.owner, "core", now) == SECD_ERR_POLICY, "G21-ROLLBACK-POLICY-NOT-PERSISTED-FAIL-CLOSED");
    CHECK(secd_set_rollback_policy(&B.node, &B.owner, 1, now) == SECD_OK && secd_rollback(&B.node, &B.owner, "core", now) == SECD_OK, "G21-ROLLBACK-AFTER-RESTART-WITH-OWNER-POLICY");
    CHECK(B.node.installed[secd_find_installed(&B.node, "core")].version == 1, "G21-ROLLBACK-RESTORED-PREVIOUS-VERSION");
    CHECK(persist(&B, 3) == 0, "G21-ROLLED-BACK-STATE-PERSISTED");

    /* storage attacks */
    CHECK(restore(&B, 4) == -4, "G21-STALE-STATE-REFUSED-BY-GENERATION-FLOOR");
    {
        N *n = &C;
        uint8_t saved[96 * OS_STORAGE_BLOCK_SIZE];
        int rejected = 1, touched = 0;
        memcpy(saved, n->disk, sizeof saved);
        for (size_t i = 8 * OS_STORAGE_BLOCK_SIZE; i < 8 * OS_STORAGE_BLOCK_SIZE + sizeof(state_blob) + 32; i++) {
            touched++;
            n->disk[i] ^= 0x01;
            boot_node(n, "node-c", "aarch64", 0);
            if (restore(n, 1) == 0) { rejected = 0; }
            memcpy(n->disk, saved, sizeof saved);
        }
        CHECK(touched > 100 && rejected, "G21-EVERY-STORED-BIT-FLIP-IS-REFUSED-OR-DETECTED");
        boot_node(n, "node-c", "aarch64", 0);
        CHECK(restore(n, 1) == 0 && n->node.used_kb == 2, "G21-INTACT-STORAGE-STILL-RESTORES");
        n->state_key[0] ^= 1;
        boot_node(n, "node-c", "aarch64", 0);
        CHECK(restore(n, 1) == -3, "G21-WRONG-STATE-KEY-REFUSED");
        n->state_key[0] ^= 1;
    }
    {
        N X;
        memset(&X, 0, sizeof X);
        boot_node(&X, "node-b", "x86_64", 1);
        CHECK(restore(&X, 0) == -2, "G21-MISSING-STATE-REFUSED");
    }

    /* privilege boundaries still hold after restarts */
    login(&B);
    {
        sec_token usr;
        sec_account_create(&B.sec, &B.owner, "usr", "Us3r-Pass-4444", SEC_ROLE_USER, fill(30, 16), now);
        sec_login(&B.sec, "usr", "Us3r-Pass-4444", ent(), now, &usr);
        CHECK(secd_install(&B.node, &usr, "core", 2, now, 0) == SECD_ERR_DENIED && secd_rollback(&B.node, &usr, "core", now) == SECD_ERR_DENIED, "G21-USER-PRIVILEGE-BOUNDARY-AFTER-RESTART");
    }
    CHECK(sec_audit_verify(&A.sec) && sec_audit_verify(&B.sec) && sec_audit_verify(&C.sec), "G21-AUDIT-CHAINS-VERIFY-ON-ALL-NODES");

    if (failures) {
        printf("G21 INTEGRATION RESULT: FAIL (%d)\n", failures);
        return 1;
    }
    printf("G21 INTEGRATION RESULT: PASS\n");
    return 0;
}
