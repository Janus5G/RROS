#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "chacha20.h"
#include "devpkg.h"
#include "secdeploy.h"
#include "secsvc.h"
#include "sha256.h"

static int failures;
#define CHECK(c, name) do { if (c) printf("PASS %s\n", name); else { printf("FAIL %s\n", name); failures++; } } while (0)

static uint64_t now = 1000;

static const uint8_t *fill(uint8_t seed, size_t n)
{
    static uint8_t buf[8][64];
    static unsigned idx;
    uint8_t *p = buf[idx++ % 8u];
    for (size_t i = 0; i < n; i++) p[i] = (uint8_t)(seed * 31u + i * 7u + 1u);
    return p;
}
static const uint8_t *salt(uint8_t n) { return fill(n, 16); }
static const uint8_t *ent(void)
{
    static uint8_t counter;
    return fill((uint8_t)(++counter + 100u), 16);
}

static int contains(const void *hay, size_t hn, const void *needle, size_t nn)
{
    const uint8_t *h = hay;
    for (size_t i = 0; i + nn <= hn; i++) if (memcmp(h + i, needle, nn) == 0) return 1;
    return 0;
}

static void hex(const char *s, uint8_t *out, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned v;
        sscanf(s + 2 * i, "%2x", &v);
        out[i] = (uint8_t)v;
    }
}

/* ------------------------------------------------------------------ primitives */

static void test_primitives(void)
{
    uint8_t key[32], nonce[12] = { 0, 0, 0, 0, 0, 0, 0, 0x4a, 0, 0, 0, 0 };
    const char *pt = "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it.";
    uint8_t ct[128], back[128], want[16], got[32], exp[32];
    for (unsigned i = 0; i < 32; i++) key[i] = (uint8_t)i;
    os_chacha20_xor(key, nonce, 1, (const uint8_t *)pt, ct, strlen(pt));
    hex("6e2e359a2568f98041ba0728dd0d6981", want, 16);
    CHECK(memcmp(ct, want, 16) == 0, "G20-CHACHA20-RFC8439-KNOWN-ANSWER");
    os_chacha20_xor(key, nonce, 1, ct, back, strlen(pt));
    CHECK(memcmp(back, pt, strlen(pt)) == 0, "G20-CHACHA20-ROUNDTRIP");
    sec_pbkdf2_sha256((const uint8_t *)"password", 8, (const uint8_t *)"salt", 4, 1, got);
    hex("120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b", exp, 32);
    CHECK(memcmp(got, exp, 32) == 0, "G20-PBKDF2-SHA256-KNOWN-ANSWER-1");
    sec_pbkdf2_sha256((const uint8_t *)"password", 8, (const uint8_t *)"salt", 4, 2, got);
    hex("ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43", exp, 32);
    CHECK(memcmp(got, exp, 32) == 0, "G20-PBKDF2-SHA256-KNOWN-ANSWER-2");
}

/* ------------------------------------------------------------------ accounts, roles, services, quotas, audit */

static sec_system S;
static const char OWNER_PW[] = "Own3r-Pass-1";

static void test_security_service(void)
{
    sec_token owner, admin, oper, user, guest, t, t2, bad;
    uint8_t sk[32];
    memset(sk, 0x5a, sizeof sk);
    sec_init(&S, sk);

    CHECK(sec_bootstrap_owner(&S, "owner", "short", salt(1), now) == SEC_ERR_WEAK, "G20-PASSWORD-POLICY-REJECTS-SHORT");
    CHECK(sec_bootstrap_owner(&S, "owner", "aaaaaaaaaa", salt(1), now) == SEC_ERR_WEAK, "G20-PASSWORD-POLICY-REJECTS-REPEATED-CHARACTER");
    CHECK(sec_bootstrap_owner(&S, "owner", "owner", salt(1), now) == SEC_ERR_WEAK, "G20-PASSWORD-POLICY-REJECTS-NAME-AS-PASSWORD");
    CHECK(sec_bootstrap_owner(&S, "Bad Name", OWNER_PW, salt(1), now) == SEC_ERR_WEAK, "G20-ACCOUNT-NAME-VALIDATED");
    CHECK(sec_bootstrap_owner(&S, "owner", OWNER_PW, (const uint8_t[16]){ 0 }, now) == SEC_ERR_WEAK, "G20-ZERO-SALT-REFUSED");
    CHECK(sec_bootstrap_owner(&S, "owner", OWNER_PW, salt(1), now) == SEC_OK, "G20-BOOTSTRAP-OWNER");
    CHECK(sec_bootstrap_owner(&S, "owner2", OWNER_PW, salt(2), now) == SEC_ERR_STATE, "G20-BOOTSTRAP-ONLY-ONCE");

    CHECK(sec_login(&S, "owner", "wrong-password", ent(), now, &t) == SEC_ERR_AUTH, "G20-LOGIN-WRONG-PASSWORD-REFUSED");
    CHECK(sec_login(&S, "nobody", OWNER_PW, ent(), now, &t) == SEC_ERR_AUTH, "G20-LOGIN-UNKNOWN-USER-REFUSED-SAME-ERROR");
    CHECK(sec_login(&S, "owner", OWNER_PW, (const uint8_t[16]){ 0 }, now, &t) == SEC_ERR_ARGUMENT, "G20-LOGIN-ZERO-ENTROPY-REFUSED");
    CHECK(sec_login(&S, "owner", OWNER_PW, ent(), now, &owner) == SEC_OK, "G20-LOGIN-OWNER");

    CHECK(sec_account_create(&S, &owner, "admin", "Adm1n-Pass-22", SEC_ROLE_ADMIN, salt(3), now) == SEC_OK, "G20-OWNER-CREATES-ADMIN");
    CHECK(sec_account_create(&S, &owner, "oper", "Oper-Pass-333", SEC_ROLE_OPERATOR, salt(4), now) == SEC_OK, "G20-OWNER-CREATES-OPERATOR");
    CHECK(sec_account_create(&S, &owner, "user", "Us3r-Pass-4444", SEC_ROLE_USER, salt(5), now) == SEC_OK, "G20-OWNER-CREATES-USER");
    CHECK(sec_account_create(&S, &owner, "guest", "Gue5t-Pass-5555", SEC_ROLE_GUEST, salt(6), now) == SEC_OK, "G20-OWNER-CREATES-GUEST");
    CHECK(sec_account_create(&S, &owner, "user", "Us3r-Pass-4444", SEC_ROLE_USER, salt(7), now) == SEC_ERR_EXISTS, "G20-DUPLICATE-ACCOUNT-REFUSED");
    CHECK(sec_login(&S, "admin", "Adm1n-Pass-22", ent(), now, &admin) == SEC_OK &&
          sec_login(&S, "oper", "Oper-Pass-333", ent(), now, &oper) == SEC_OK &&
          sec_login(&S, "user", "Us3r-Pass-4444", ent(), now, &user) == SEC_OK &&
          sec_login(&S, "guest", "Gue5t-Pass-5555", ent(), now, &guest) == SEC_OK, "G20-LOGIN-ALL-ROLES");

    CHECK(sec_authorize(&S, &guest, SEC_PERM_READ, now) == SEC_OK && sec_authorize(&S, &guest, SEC_PERM_WRITE, now) == SEC_ERR_DENIED, "G20-ROLE-GUEST-READ-ONLY");
    CHECK(sec_authorize(&S, &user, SEC_PERM_WRITE, now) == SEC_OK && sec_authorize(&S, &user, SEC_PERM_REQUEST_INSTALL, now) == SEC_ERR_DENIED, "G20-ROLE-USER-CANNOT-INSTALL");
    CHECK(sec_authorize(&S, &oper, SEC_PERM_REQUEST_INSTALL, now) == SEC_OK && sec_authorize(&S, &oper, SEC_PERM_MANAGE_ACCOUNTS, now) == SEC_ERR_DENIED, "G20-ROLE-OPERATOR-CANNOT-MANAGE-ACCOUNTS");
    CHECK(sec_authorize(&S, &admin, SEC_PERM_MANAGE_ACCOUNTS, now) == SEC_OK, "G20-ROLE-ADMIN-MANAGES-ACCOUNTS");
    CHECK(sec_authorize(&S, &admin, SEC_PERM_APPROVE_INSTALL, now) == SEC_ERR_DENIED &&
          sec_authorize(&S, &admin, SEC_PERM_MANAGE_KEYS, now) == SEC_ERR_DENIED &&
          sec_authorize(&S, &admin, SEC_PERM_ROLLBACK, now) == SEC_ERR_DENIED, "G20-ROLE-ADMIN-CANNOT-APPROVE-OR-HOLD-KEYS-OR-ROLLBACK");
    CHECK(sec_authorize(&S, &owner, SEC_PERM_ALL, now) == SEC_OK, "G20-ROLE-OWNER-HOLDS-ALL");
    CHECK(sec_authorize(&S, &owner, 0, now) == SEC_ERR_DENIED, "G20-EMPTY-PERMISSION-REQUEST-DENIED");

    CHECK(sec_account_create(&S, &admin, "admin2", "Adm1n-Pass-22b", SEC_ROLE_ADMIN, salt(8), now) == SEC_ERR_DENIED, "G20-ADMIN-CANNOT-CREATE-ADMIN");
    CHECK(sec_account_create(&S, &admin, "owner2", "Own3r-Pass-22b", SEC_ROLE_OWNER, salt(8), now) == SEC_ERR_DENIED, "G20-ADMIN-CANNOT-CREATE-OWNER");
    CHECK(sec_account_create(&S, &owner, "owner2", "Own3r-Pass-22b", SEC_ROLE_OWNER, salt(8), now) == SEC_ERR_DENIED, "G20-NO-SECOND-OWNER");
    CHECK(sec_account_create(&S, &owner, "weird", "Wei4d-Pass-1234", 99, salt(8), now) == SEC_ERR_DENIED, "G20-INVALID-ROLE-REFUSED");
    CHECK(sec_account_create(&S, &oper, "user9", "Us3r-Pass-9999", SEC_ROLE_USER, salt(8), now) == SEC_ERR_DENIED, "G20-OPERATOR-CANNOT-CREATE-ACCOUNTS");
    CHECK(sec_account_set_role(&S, &admin, "oper", SEC_ROLE_ADMIN, now) == SEC_ERR_DENIED, "G20-ADMIN-CANNOT-PROMOTE-TO-ADMIN");
    CHECK(sec_account_set_role(&S, &admin, "owner", SEC_ROLE_USER, now) == SEC_ERR_DENIED, "G20-ADMIN-CANNOT-DEMOTE-OWNER");
    CHECK(sec_account_set_role(&S, &admin, "admin", SEC_ROLE_USER, now) == SEC_ERR_DENIED, "G20-ADMIN-CANNOT-CHANGE-OWN-ROLE");
    CHECK(sec_account_set_role(&S, &admin, "oper", SEC_ROLE_USER, now) == SEC_OK, "G20-ADMIN-DEMOTES-OPERATOR");
    CHECK(sec_authorize(&S, &oper, SEC_PERM_READ, now) == SEC_ERR_AUTH, "G20-ROLE-CHANGE-REVOKES-SESSIONS");
    CHECK(sec_account_set_role(&S, &owner, "oper", SEC_ROLE_OPERATOR, now) == SEC_OK &&
          sec_login(&S, "oper", "Oper-Pass-333", ent(), now, &oper) == SEC_OK, "G20-OWNER-RESTORES-OPERATOR");

    CHECK(sec_account_set_disabled(&S, &admin, "user", 1, now) == SEC_OK && sec_authorize(&S, &user, SEC_PERM_READ, now) == SEC_ERR_AUTH, "G20-DISABLE-KILLS-SESSIONS");
    CHECK(sec_login(&S, "user", "Us3r-Pass-4444", ent(), now, &t) == SEC_ERR_AUTH, "G20-DISABLED-ACCOUNT-CANNOT-LOGIN");
    CHECK(sec_account_set_disabled(&S, &admin, "user", 0, now) == SEC_OK && sec_login(&S, "user", "Us3r-Pass-4444", ent(), now, &user) == SEC_OK, "G20-REENABLE-ACCOUNT");

    /* credential protection */
    {
        int leaked = contains(&S, sizeof S, OWNER_PW, strlen(OWNER_PW)) || contains(&S, sizeof S, "Adm1n-Pass-22", 13) ||
                     contains(&S, sizeof S, "Us3r-Pass-4444", 14);
        CHECK(!leaked, "G20-PLAINTEXT-PASSWORDS-NEVER-STORED");
        int a = sec_find_principal(&S, "admin"), o = sec_find_principal(&S, "owner");
        CHECK(a >= 0 && o >= 0 && memcmp(S.principals[a].secret_hash, S.principals[o].secret_hash, 32) != 0, "G20-PASSWORD-HASHES-DIFFER");
    }
    CHECK(sec_account_create(&S, &owner, "twin1", "Same-Pass-1234", SEC_ROLE_USER, salt(10), now) == SEC_OK &&
          sec_account_create(&S, &owner, "twin2", "Same-Pass-1234", SEC_ROLE_USER, salt(11), now) == SEC_OK &&
          memcmp(S.principals[sec_find_principal(&S, "twin1")].secret_hash, S.principals[sec_find_principal(&S, "twin2")].secret_hash, 32) != 0,
          "G20-SAME-PASSWORD-DIFFERENT-SALT-DIFFERENT-HASH");

    /* password change */
    CHECK(sec_login(&S, "user", "Us3r-Pass-4444", ent(), now, &t) == SEC_OK && sec_login(&S, "user", "Us3r-Pass-4444", ent(), now, &t2) == SEC_OK, "G20-TWO-SESSIONS");
    CHECK(sec_change_password(&S, &t, "wrong-old-pass", "N3w-Password-77", salt(12), now) == SEC_ERR_AUTH, "G20-PASSWORD-CHANGE-NEEDS-OLD-PASSWORD");
    CHECK(sec_change_password(&S, &t, "Us3r-Pass-4444", "Us3r-Pass-4444", salt(12), now) == SEC_ERR_WEAK, "G20-PASSWORD-CHANGE-REJECTS-REUSE");
    CHECK(sec_change_password(&S, &t, "Us3r-Pass-4444", "user", salt(12), now) == SEC_ERR_WEAK, "G20-PASSWORD-CHANGE-ENFORCES-POLICY");
    CHECK(sec_change_password(&S, &t, "Us3r-Pass-4444", "N3w-Password-77", salt(12), now) == SEC_OK, "G20-PASSWORD-CHANGED");
    CHECK(sec_authorize(&S, &t2, SEC_PERM_READ, now) == SEC_ERR_AUTH && sec_authorize(&S, &t, SEC_PERM_READ, now) == SEC_OK, "G20-PASSWORD-CHANGE-REVOKES-OTHER-SESSIONS");
    CHECK(sec_login(&S, "user", "Us3r-Pass-4444", ent(), now, &t2) == SEC_ERR_AUTH && sec_login(&S, "user", "N3w-Password-77", ent(), now, &t2) == SEC_OK, "G20-OLD-PASSWORD-NO-LONGER-WORKS");

    /* lockout */
    for (unsigned i = 0; i < SEC_MAX_FAILURES; i++) sec_login(&S, "guest", "bad-guess-1234", ent(), now, &t);
    CHECK(sec_login(&S, "guest", "Gue5t-Pass-5555", ent(), now, &t) == SEC_ERR_LOCKED, "G20-LOCKOUT-AFTER-REPEATED-FAILURES");
    CHECK(sec_login(&S, "guest", "Gue5t-Pass-5555", ent(), now + SEC_LOCKOUT_TICKS - 1, &t) == SEC_ERR_LOCKED, "G20-LOCKOUT-HOLDS-FOR-FULL-PERIOD");
    CHECK(sec_login(&S, "guest", "Gue5t-Pass-5555", ent(), now + SEC_LOCKOUT_TICKS, &t) == SEC_OK, "G20-LOCKOUT-EXPIRES");

    /* tokens */
    bad = admin; bad.secret[0] ^= 1;
    CHECK(sec_authorize(&S, &bad, SEC_PERM_READ, now) == SEC_ERR_AUTH, "G20-FORGED-TOKEN-SECRET-REFUSED");
    bad = admin; bad.slot = (admin.slot + 1u) % SEC_MAX_SESSIONS;
    CHECK(sec_authorize(&S, &bad, SEC_PERM_READ, now) == SEC_ERR_AUTH, "G20-TOKEN-BOUND-TO-ITS-SLOT");
    bad = admin; bad.slot = 99;
    CHECK(sec_authorize(&S, &bad, SEC_PERM_READ, now) == SEC_ERR_AUTH && sec_authorize(&S, 0, SEC_PERM_READ, now) == SEC_ERR_AUTH, "G20-OUT-OF-RANGE-AND-NULL-TOKEN-REFUSED");
    CHECK(sec_logout(&S, &oper, now) == SEC_OK && sec_authorize(&S, &oper, SEC_PERM_READ, now) == SEC_ERR_AUTH, "G20-LOGOUT-INVALIDATES-TOKEN");
    CHECK(sec_authorize(&S, &admin, SEC_PERM_READ, now + SEC_SESSION_TTL) == SEC_ERR_EXPIRED, "G20-SESSION-EXPIRES");
    CHECK(sec_authorize(&S, &admin, SEC_PERM_READ, now) == SEC_ERR_AUTH, "G20-EXPIRED-SESSION-STAYS-DEAD");
    CHECK(sec_login(&S, "admin", "Adm1n-Pass-22", ent(), now, &admin) == SEC_OK, "G20-RELOGIN-ADMIN");

    /* service identities */
    {
        uint8_t key[32], nonce[16], resp[32], wrong[32];
        sec_token svc, svc2;
        memcpy(key, fill(40, 32), 32);
        CHECK(sec_service_register(&S, &oper, "builder", key, SEC_PERM_SEND_PACKAGE, now) == SEC_ERR_AUTH ||
              sec_service_register(&S, &user, "builder", key, SEC_PERM_SEND_PACKAGE, now) == SEC_ERR_DENIED, "G20-SERVICE-REGISTRATION-NEEDS-PRIVILEGE");
        CHECK(sec_service_register(&S, &admin, "builder", key, SEC_PERM_MANAGE_KEYS, now) == SEC_ERR_DENIED, "G20-SERVICE-CANNOT-REQUEST-MANAGEMENT-PERMISSION");
        CHECK(sec_service_register(&S, &admin, "builder", key, SEC_PERM_APPROVE_INSTALL, now) == SEC_ERR_DENIED, "G20-SERVICE-CANNOT-REQUEST-APPROVAL-PERMISSION");
        CHECK(sec_service_register(&S, &admin, "builder", (const uint8_t[32]){ 0 }, SEC_PERM_SEND_PACKAGE, now) == SEC_ERR_WEAK, "G20-SERVICE-ZERO-KEY-REFUSED");
        CHECK(sec_service_register(&S, &admin, "builder", key, SEC_PERM_SEND_PACKAGE | SEC_PERM_READ, now) == SEC_OK, "G20-SERVICE-REGISTERED");
        CHECK(sec_login(&S, "builder", "anything-goes-1", ent(), now, &t) == SEC_ERR_AUTH, "G20-SERVICE-CANNOT-PASSWORD-LOGIN");
        CHECK(sec_service_challenge(&S, "user", ent(), now, nonce) == SEC_ERR_AUTH, "G20-ACCOUNT-CANNOT-USE-SERVICE-AUTH");
        CHECK(sec_service_authenticate(&S, "builder", wrong, ent(), now, &svc) == SEC_ERR_REPLAY, "G20-SERVICE-AUTH-WITHOUT-CHALLENGE-REFUSED");
        CHECK(sec_service_challenge(&S, "builder", ent(), now, nonce) == SEC_OK, "G20-SERVICE-CHALLENGE-ISSUED");
        sec_service_response(key, nonce, "builder", resp);
        memcpy(wrong, resp, 32); wrong[5] ^= 0x40;
        CHECK(sec_service_authenticate(&S, "builder", wrong, ent(), now, &svc) == SEC_ERR_AUTH, "G20-SERVICE-WRONG-RESPONSE-REFUSED");
        CHECK(sec_service_authenticate(&S, "builder", resp, ent(), now, &svc) == SEC_ERR_REPLAY, "G20-CHALLENGE-BURNED-BY-FAILED-ATTEMPT");
        CHECK(sec_service_challenge(&S, "builder", ent(), now, nonce) == SEC_OK, "G20-SERVICE-SECOND-CHALLENGE");
        sec_service_response(key, nonce, "builder", resp);
        CHECK(sec_service_authenticate(&S, "builder", resp, ent(), now, &svc) == SEC_OK, "G20-SERVICE-AUTHENTICATES");
        CHECK(sec_service_authenticate(&S, "builder", resp, ent(), now, &svc2) == SEC_ERR_REPLAY, "G20-SERVICE-RESPONSE-REPLAY-REFUSED");
        CHECK(sec_authorize(&S, &svc, SEC_PERM_SEND_PACKAGE, now) == SEC_OK, "G20-SERVICE-HOLDS-GRANTED-PERMISSION");
        CHECK(sec_authorize(&S, &svc, SEC_PERM_APPROVE_INSTALL, now) == SEC_ERR_DENIED &&
              sec_authorize(&S, &svc, SEC_PERM_MANAGE_ACCOUNTS, now) == SEC_ERR_DENIED &&
              sec_authorize(&S, &svc, SEC_PERM_REQUEST_INSTALL, now) == SEC_ERR_DENIED, "G20-SERVICE-DENIED-UNGRANTED-AND-PRIVILEGED-PERMISSIONS");
        CHECK(sec_service_challenge(&S, "builder", ent(), now, nonce) == SEC_OK, "G20-SERVICE-CHALLENGE-FOR-EXPIRY");
        sec_service_response(key, nonce, "builder", resp);
        CHECK(sec_service_authenticate(&S, "builder", resp, ent(), now + SEC_NONCE_TTL, &svc2) == SEC_ERR_EXPIRED, "G20-SERVICE-CHALLENGE-EXPIRES");
        CHECK(sec_account_set_disabled(&S, &admin, "builder", 1, now) == SEC_ERR_DENIED, "G20-SERVICE-NOT-MANAGEABLE-AS-ACCOUNT");
        CHECK(S.principals[sec_find_principal(&S, "builder")].is_service == 1, "G20-SERVICE-PRINCIPAL-IS-FLAGGED-AS-SERVICE");
    }

    /* quotas */
    {
        sec_token q;
        CHECK(sec_quota_set(&S, &t, "user", 1000, 1000, now) == SEC_ERR_DENIED, "G20-QUOTA-CHANGE-NEEDS-PRIVILEGE");
        CHECK(sec_quota_set(&S, &admin, "user", 10, 3, now) == SEC_OK, "G20-QUOTA-SET");
        CHECK(sec_login(&S, "user", "N3w-Password-77", ent(), now, &q) == SEC_OK, "G20-LOGIN-QUOTA-USER");
        CHECK(sec_charge_storage(&S, &q, 6, now) == SEC_OK, "G20-STORAGE-CHARGE-WITHIN-QUOTA");
        CHECK(sec_charge_storage(&S, &q, 5, now) == SEC_ERR_QUOTA, "G20-STORAGE-CHARGE-OVER-QUOTA-REFUSED");
        CHECK(sec_charge_storage(&S, &q, 0xFFFFFFFFu, now) == SEC_ERR_QUOTA, "G20-STORAGE-CHARGE-OVERFLOW-REFUSED");
        CHECK(sec_charge_storage(&S, &q, 4, now) == SEC_OK, "G20-STORAGE-CHARGE-EXACTLY-AT-QUOTA");
        CHECK(sec_release_storage(&S, &q, 20, now) == SEC_ERR_ARGUMENT, "G20-STORAGE-RELEASE-UNDERFLOW-REFUSED");
        CHECK(sec_release_storage(&S, &q, 10, now) == SEC_OK && sec_charge_storage(&S, &q, 10, now) == SEC_OK, "G20-STORAGE-RELEASE-FREES-QUOTA");
        CHECK(sec_charge_request(&S, &q, now) == SEC_OK && sec_charge_request(&S, &q, now) == SEC_OK && sec_charge_request(&S, &q, now) == SEC_OK, "G20-REQUEST-QUOTA-WITHIN-WINDOW");
        CHECK(sec_charge_request(&S, &q, now) == SEC_ERR_QUOTA, "G20-REQUEST-QUOTA-EXHAUSTED");
        CHECK(sec_charge_request(&S, &q, now + SEC_WINDOW_TICKS) == SEC_OK, "G20-REQUEST-QUOTA-RESETS-NEXT-WINDOW");
    }

    /* audit */
    {
        unsigned denied = 0, n = sec_audit_retained(&S);
        for (unsigned i = 0; i < n; i++) if (sec_audit_get(&S, i)->action == SEC_A_DENIED) denied++;
        CHECK(sec_audit_verify(&S) && n > 20, "G20-AUDIT-CHAIN-VERIFIES");
        CHECK(denied >= 10, "G20-DENIALS-ARE-AUDITED");
        CHECK(sec_audit_get(&S, n) == 0, "G20-AUDIT-OUT-OF-RANGE-REFUSED");
        sec_audit_entry saved = *sec_audit_get(&S, 3);
        ((sec_audit_entry *)sec_audit_get(&S, 3))->result ^= 1;
        CHECK(!sec_audit_verify(&S), "G20-AUDIT-TAMPER-DETECTED");
        *((sec_audit_entry *)sec_audit_get(&S, 3)) = saved;
        CHECK(sec_audit_verify(&S), "G20-AUDIT-RESTORED-VERIFIES");
        ((sec_audit_entry *)sec_audit_get(&S, 0))->actor ^= 1;
        CHECK(!sec_audit_verify(&S), "G20-AUDIT-FIRST-ENTRY-TAMPER-DETECTED");
        ((sec_audit_entry *)sec_audit_get(&S, 0))->actor ^= 1;
        for (unsigned i = 0; i < 100; i++) sec_audit_log(&S, 0, SEC_A_LOGIN_OK, 0, now);
        CHECK(sec_audit_retained(&S) == SEC_AUDIT_CAP && sec_audit_verify(&S), "G20-AUDIT-RING-WRAP-STILL-VERIFIES");
        S.audit_anchor[0] ^= 1;
        CHECK(!sec_audit_verify(&S), "G20-AUDIT-ANCHOR-TAMPER-DETECTED");
        S.audit_anchor[0] ^= 1;
        ((sec_audit_entry *)sec_audit_get(&S, SEC_AUDIT_CAP - 1))->tick ^= 1;
        CHECK(!sec_audit_verify(&S), "G20-AUDIT-LAST-ENTRY-TAMPER-DETECTED");
    }
}

/* ------------------------------------------------------------------ three-node deployment */

typedef struct {
    sec_system sec;
    secd_node node;
    sec_token owner, op;
} N;
static N A, B, C, M;

static const char OP_PW[] = "Op3rator-Pass-9";
static const uint8_t SIGKEY[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
static const uint8_t KFUT[16] = { 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36 };
static const uint8_t KOLD[16] = { 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56 };
static const uint8_t KREV[16] = { 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76 };
static const uint8_t KUNK[16] = { 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96 };
static uint8_t PSK_AB[32], PSK_AC[32], PSK_X[32];
static uint8_t frame[SECD_FRAME_MAX], env[SECD_ENVELOPE_MAX], inner[OS_PKG_MAX_BYTES];
static size_t frame_len, env_len, inner_len;

static void relogin(N *n)
{
    sec_logout(&n->sec, &n->owner, now);
    sec_logout(&n->sec, &n->op, now);
    sec_login(&n->sec, "owner", OWNER_PW, ent(), now, &n->owner);
    sec_login(&n->sec, "op", OP_PW, ent(), now, &n->op);
}

static void relogin_all(void)
{
    now += 5;
    relogin(&A); relogin(&B); relogin(&C); relogin(&M);
}

static void setup_node(N *n, const char *id, const char *arch, uint32_t quota)
{
    uint8_t sk[32];
    memset(sk, (int)id[5], sizeof sk);
    sec_init(&n->sec, sk);
    sec_bootstrap_owner(&n->sec, "owner", OWNER_PW, salt(21), now);
    sec_login(&n->sec, "owner", OWNER_PW, ent(), now, &n->owner);
    sec_account_create(&n->sec, &n->owner, "op", OP_PW, SEC_ROLE_OPERATOR, salt(22), now);
    sec_login(&n->sec, "op", OP_PW, ent(), now, &n->op);
    secd_node_init(&n->node, &n->sec, id, arch, quota);
}

static void make_inner(const char *id, uint32_t version, const char *arch, const char *dep, size_t payload_len)
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
    for (size_t i = 0; i < payload_len; i++) payload[i] = (uint8_t)(i * 13u + version);
    if (payload_len >= 12) {
        payload[0] = 0x45; payload[1] = 0x58; payload[2] = 0x50; payload[3] = 0x41;
        payload[8] = (uint8_t)(strcmp(arch, "aarch64") == 0 ? 2 : 1); payload[9] = payload[10] = payload[11] = 0;
    }
    if (os_pkg_build(&m, payload, payload_len, inner, sizeof inner, &inner_len) != OS_PKG_OK) inner_len = 0;
}

static void make_env(const char *id, uint32_t version, const char *arch, const char *dep, size_t payload_len, const uint8_t key[16])
{
    make_inner(id, version, arch, dep, payload_len);
    if (secd_envelope_build(inner, inner_len, key, env, sizeof env, &env_len) != SECD_OK) env_len = 0;
}

static int seal(N *from, const char *to_id)
{
    return secd_send(&from->node, &from->op, to_id, env, env_len, frame, sizeof frame, &frame_len, now);
}

static int deliver(N *from, N *to, const char *to_id)
{
    now++;
    if (seal(from, to_id) != SECD_OK) return 12345;
    return secd_receive(&to->node, &to->op, frame, frame_len, now);
}

static int approve(N *n, const char *id, uint32_t version)
{
    int s = secd_find_staged(&n->node, id, version);
    if (s < 0) return SECD_ERR_NOT_FOUND;
    return secd_approve(&n->node, &n->owner, id, version, n->node.staged[s].digest, now + 1000, now);
}

static int pull(N *from, N *to, const char *to_id, const char *id, uint32_t version, const char *arch, const char *dep, size_t len, const uint8_t key[16])
{
    make_env(id, version, arch, dep, len, key);
    return deliver(from, to, to_id);
}

static int install_ok(N *n, const char *id, uint32_t version)
{
    now++;
    return approve(n, id, version) == SECD_OK && secd_install(&n->node, &n->op, id, version, now, SECD_FAULT_NONE) == SECD_OK;
}

static void test_deployment(void)
{
    static uint8_t copy[SECD_FRAME_MAX];
    memcpy(PSK_AB, fill(70, 32), 32); memcpy(PSK_AC, fill(71, 32), 32); memcpy(PSK_X, fill(72, 32), 32);
    setup_node(&A, "node-a", "x86_64", 64);
    setup_node(&B, "node-b", "x86_64", 64);
    setup_node(&C, "node-c", "aarch64", 4);
    setup_node(&M, "node-a", "x86_64", 64);   /* attacker claiming node-a's identity */

    CHECK(secd_peer_enroll(&A.node, &A.op, "node-b", PSK_AB, now) == SECD_ERR_DENIED, "G20-PEER-ENROLLMENT-NEEDS-OWNER");
    CHECK(secd_peer_enroll(&A.node, &A.owner, "node-b", PSK_AB, now) == SECD_OK &&
          secd_peer_enroll(&A.node, &A.owner, "node-c", PSK_AC, now) == SECD_OK &&
          secd_peer_enroll(&B.node, &B.owner, "node-a", PSK_AB, now) == SECD_OK &&
          secd_peer_enroll(&C.node, &C.owner, "node-a", PSK_AC, now) == SECD_OK, "G20-THREE-NODES-ENROLLED");
    CHECK(secd_peer_enroll(&A.node, &A.owner, "node-b", PSK_AB, now) == SECD_ERR_ARGUMENT, "G20-DUPLICATE-PEER-REFUSED");
    CHECK(secd_peer_enroll(&A.node, &A.owner, "node-z", (const uint8_t[32]){ 0 }, now) == SECD_ERR_ARGUMENT, "G20-ZERO-PEER-KEY-REFUSED");
    CHECK(secd_peer_enroll(&A.node, &A.owner, "Bad Id", PSK_X, now) == SECD_ERR_ARGUMENT, "G20-PEER-ID-VALIDATED");
    CHECK(secd_key_add(&B.node, &B.op, SIGKEY, 0, 1000000, now) == SECD_ERR_DENIED, "G20-KEY-ADD-NEEDS-OWNER");
    CHECK(secd_key_add(&B.node, &B.owner, SIGKEY, 0, 1000000, now) == SECD_OK &&
          secd_key_add(&C.node, &C.owner, SIGKEY, 0, 1000000, now) == SECD_OK, "G20-SIGNING-KEY-REGISTERED");
    CHECK(secd_key_add(&B.node, &B.owner, SIGKEY, 0, 1000000, now) == SECD_ERR_ARGUMENT, "G20-DUPLICATE-KEY-REFUSED");
    CHECK(secd_key_add(&B.node, &B.owner, KUNK, 50, 50, now) == SECD_ERR_ARGUMENT, "G20-EMPTY-KEY-WINDOW-REFUSED");
    CHECK(secd_key_add(&B.node, &B.owner, KFUT, now + 5000, 1000000, now) == SECD_OK &&
          secd_key_add(&B.node, &B.owner, KOLD, 0, now - 1, now) == SECD_OK &&
          secd_key_add(&B.node, &B.owner, KREV, 0, 1000000, now) == SECD_OK, "G20-KEY-WINDOWS-REGISTERED");
    CHECK(secd_key_add(&B.node, &B.owner, KUNK, 0, 1000000, now) == SECD_ERR_FULL, "G20-KEY-REGISTRY-BOUNDED");

    /* frame confidentiality and authorized activation */
    relogin_all();
    make_env("secret-package-name", 1, "x86_64", "", 600, SIGKEY);
    CHECK(env_len > 0 && contains(env, env_len, "secret-package-name", 19), "G20-ENVELOPE-CONTAINS-PLAINTEXT-ID");
    now++;
    CHECK(seal(&A, "node-b") == SECD_OK, "G20-SEAL-FRAME");
    CHECK(!contains(frame, frame_len, "secret-package-name", 19) && !contains(frame, frame_len, "x86_64", 6), "G20-TRANSFER-IS-ENCRYPTED");
    memcpy(copy, frame, frame_len);
    size_t first_len = frame_len;
    CHECK(seal(&A, "node-b") == SECD_OK && memcmp(copy + 48, frame + 48, 16) != 0, "G20-SAME-PLAINTEXT-DIFFERENT-CIPHERTEXT-PER-FRAME");
    CHECK(secd_receive(&B.node, &B.op, copy, first_len, now) == SECD_OK, "G20-AUTHORIZED-FRAME-RECEIVED-AND-STAGED");
    CHECK(secd_receive(&B.node, &B.op, copy, first_len, now) == SECD_ERR_REPLAY, "G20-REPLAYED-FRAME-REFUSED");
    CHECK(secd_receive(&B.node, &B.op, frame, frame_len, now) == SECD_OK, "G20-LATER-SEQUENCE-STILL-ACCEPTED");
    CHECK(secd_receive(&B.node, &B.op, copy, first_len, now) == SECD_ERR_REPLAY, "G20-OLD-SEQUENCE-REFUSED-AFTER-NEWER");
    CHECK(secd_find_installed(&B.node, "secret-package-name") < 0 && B.node.used_kb == 0, "G20-STAGING-INSTALLS-NOTHING");
    {
        int s = secd_find_staged(&B.node, "secret-package-name", 1);
        uint8_t wrong[32];
        memcpy(wrong, B.node.staged[s].digest, 32); wrong[0] ^= 1;
        CHECK(secd_install(&B.node, &B.op, "secret-package-name", 1, now, 0) == SECD_ERR_APPROVAL, "G20-INSTALL-REFUSED-WITHOUT-APPROVAL");
        CHECK(secd_approve(&B.node, &B.op, "secret-package-name", 1, B.node.staged[s].digest, now + 100, now) == SECD_ERR_DENIED, "G20-OPERATOR-CANNOT-APPROVE");
        CHECK(secd_approve(&B.node, &B.owner, "secret-package-name", 1, wrong, now + 100, now) == SECD_ERR_APPROVAL, "G20-APPROVAL-BOUND-TO-PACKAGE-DIGEST");
        CHECK(secd_approve(&B.node, &B.owner, "secret-package-name", 1, B.node.staged[s].digest, now, now) == SECD_ERR_APPROVAL, "G20-ALREADY-EXPIRED-APPROVAL-REFUSED");
        CHECK(secd_approve(&B.node, &B.owner, "secret-package-name", 2, B.node.staged[s].digest, now + 100, now) == SECD_ERR_NOT_FOUND, "G20-APPROVAL-FOR-UNSTAGED-VERSION-REFUSED");
        CHECK(secd_approve(&B.node, &B.owner, "secret-package-name", 1, B.node.staged[s].digest, now + 50, now) == SECD_OK, "G20-RECIPIENT-APPROVES-EXACT-DIGEST");
        CHECK(secd_install(&B.node, &B.op, "secret-package-name", 1, now + 50, 0) == SECD_ERR_APPROVAL, "G20-EXPIRED-APPROVAL-REFUSED-AT-INSTALL");
        CHECK(approve(&B, "secret-package-name", 1) == SECD_OK, "G20-APPROVAL-RENEWED");
        CHECK(secd_install(&B.node, &B.op, "secret-package-name", 1, now, 0) == SECD_OK, "G20-AUTHORIZED-ACTIVATION");
        CHECK(secd_find_installed(&B.node, "secret-package-name") >= 0 && B.node.used_kb == 1, "G20-INSTALLED-AND-ACCOUNTED");
        CHECK(secd_find_staged(&B.node, "secret-package-name", 1) < 0, "G20-STAGING-CLEARED-AFTER-INSTALL");
        CHECK(secd_install(&B.node, &B.op, "secret-package-name", 1, now, 0) == SECD_ERR_NOT_FOUND, "G20-APPROVAL-IS-SINGLE-USE");
    }

    /* tampering of frames */
    relogin_all();
    make_env("core", 1, "x86_64", "", 300, SIGKEY);
    now++;
    CHECK(seal(&A, "node-b") == SECD_OK, "G20-SEAL-FOR-TAMPER-TESTS");
    memcpy(copy, frame, frame_len);
    first_len = frame_len;
    {
        static uint8_t t[SECD_FRAME_MAX];
        memcpy(t, copy, first_len); t[SECD_FRAME_HEADER + 10] ^= 1;
        CHECK(secd_receive(&B.node, &B.op, t, first_len, now) == SECD_ERR_AUTH, "G20-CIPHERTEXT-BIT-FLIP-REFUSED");
        memcpy(t, copy, first_len); t[first_len - 1] ^= 1;
        CHECK(secd_receive(&B.node, &B.op, t, first_len, now) == SECD_ERR_AUTH, "G20-TAG-BIT-FLIP-REFUSED");
        memcpy(t, copy, first_len); t[36] ^= 1;
        CHECK(secd_receive(&B.node, &B.op, t, first_len, now) == SECD_ERR_AUTH, "G20-SEQUENCE-FIELD-TAMPER-REFUSED");
        memcpy(t, copy, first_len); t[25] ^= 1;
        CHECK(secd_receive(&B.node, &B.op, t, first_len, now) == SECD_ERR_PEER, "G20-RECIPIENT-FIELD-TAMPER-REFUSED");
        memcpy(t, copy, first_len); t[10] ^= 1;
        CHECK(secd_receive(&B.node, &B.op, t, first_len, now) == SECD_ERR_PEER, "G20-SENDER-FIELD-TAMPER-REFUSED");
        memcpy(t, copy, first_len); t[44] ^= 1;
        CHECK(secd_receive(&B.node, &B.op, t, first_len, now) == SECD_ERR_FORMAT, "G20-LENGTH-FIELD-TAMPER-REFUSED");
        memcpy(t, copy, first_len); t[0] = 'X';
        CHECK(secd_receive(&B.node, &B.op, t, first_len, now) == SECD_ERR_FORMAT, "G20-BAD-FRAME-MAGIC-REFUSED");
        CHECK(secd_receive(&B.node, &B.op, copy, first_len - 1, now) == SECD_ERR_FORMAT, "G20-TRUNCATED-FRAME-REFUSED");
        CHECK(secd_receive(&B.node, &B.op, copy, 10, now) == SECD_ERR_FORMAT && secd_receive(&B.node, &B.op, copy, 0, now) == SECD_ERR_FORMAT &&
              secd_receive(&B.node, &B.op, 0, 100, now) == SECD_ERR_FORMAT, "G20-DEGENERATE-FRAMES-REFUSED");
        memcpy(t, copy, first_len); t[first_len] = 0;
        CHECK(secd_receive(&B.node, &B.op, t, first_len + 1, now) == SECD_ERR_FORMAT, "G20-EXTENDED-FRAME-REFUSED");
        CHECK(secd_receive(&B.node, &B.op, t, SECD_FRAME_MAX + 1, now) == SECD_ERR_FORMAT, "G20-OVERSIZED-FRAME-REFUSED");
    }
    CHECK(secd_find_staged(&B.node, "core", 1) < 0, "G20-TAMPERED-FRAMES-STAGED-NOTHING");
    CHECK(secd_receive(&B.node, &B.op, copy, first_len, now) == SECD_OK, "G20-UNTAMPERED-FRAME-STILL-ACCEPTED-AFTER-ATTACKS");
    CHECK(secd_receive(&B.node, &B.op, copy, first_len, now) == SECD_ERR_REPLAY, "G20-REPLAY-OF-ACCEPTED-FRAME-REFUSED");

    /* unauthorized senders */
    relogin_all();
    CHECK(secd_peer_enroll(&M.node, &M.owner, "node-b", PSK_X, now) == SECD_OK, "G20-ATTACKER-KNOWS-ITS-OWN-KEY");
    make_env("evil", 1, "x86_64", "", 100, SIGKEY);
    CHECK(deliver(&M, &B, "node-b") == SECD_ERR_AUTH, "G20-IMPERSONATION-WITH-WRONG-PEER-KEY-REFUSED");
    CHECK(secd_find_staged(&B.node, "evil", 1) < 0, "G20-IMPERSONATOR-STAGED-NOTHING");
    CHECK(secd_peer_enroll(&A.node, &A.owner, "mallory", PSK_X, now) == SECD_OK, "G20-SENDER-NODE-ENROLLED-ON-A-ONLY");
    make_env("core2", 1, "x86_64", "", 100, SIGKEY);
    CHECK(deliver(&A, &B, "mallory") == SECD_ERR_PEER || seal(&A, "node-b") == SECD_OK, "G20-SEAL-TO-UNKNOWN-RECIPIENT-NOT-POSSIBLE-WITHOUT-ENROLLMENT");
    CHECK(secd_send(&A.node, &A.op, "node-q", env, env_len, frame, sizeof frame, &frame_len, now) == SECD_ERR_PEER, "G20-SEND-TO-UNENROLLED-RECIPIENT-REFUSED");
    now++;
    CHECK(seal(&A, "node-c") == SECD_OK && secd_receive(&B.node, &B.op, frame, frame_len, now) == SECD_ERR_PEER, "G20-FRAME-FOR-OTHER-RECIPIENT-REFUSED");
    now++;
    CHECK(seal(&A, "node-b") == SECD_OK && secd_receive(&A.node, &A.op, frame, frame_len, now) == SECD_ERR_PEER, "G20-REFLECTED-FRAME-REFUSED");
    CHECK(secd_peer_enroll(&B.node, &B.owner, "mallory", PSK_X, now) == SECD_OK, "G20-MALLORY-ENROLLED-ON-B");
    {
        N X;
        memset(&X, 0, sizeof X);
        setup_node(&X, "mallory", "x86_64", 64);
        secd_peer_enroll(&X.node, &X.owner, "node-b", PSK_X, now);
        make_env("mal", 1, "x86_64", "", 100, SIGKEY);
        CHECK(deliver(&X, &B, "node-b") == SECD_OK, "G20-ENROLLED-PEER-ACCEPTED");
        CHECK(secd_peer_revoke(&B.node, &B.op, "mallory", now) == SECD_ERR_DENIED, "G20-PEER-REVOCATION-NEEDS-OWNER");
        CHECK(secd_peer_revoke(&B.node, &B.owner, "mallory", now) == SECD_OK, "G20-PEER-REVOKED");
        CHECK(deliver(&X, &B, "node-b") == SECD_ERR_PEER, "G20-REVOKED-PEER-REFUSED");
        int zero = 1;
        for (unsigned i = 0; i < SECD_MAX_PEERS; i++) if (B.node.peers[i].used && B.node.peers[i].revoked)
            for (unsigned k = 0; k < 32; k++) if (B.node.peers[i].psk[k]) zero = 0;
        CHECK(zero, "G20-REVOKED-PEER-KEY-WIPED");
        CHECK(secd_peer_revoke(&B.node, &B.owner, "ghost", now) == SECD_ERR_NOT_FOUND, "G20-REVOKE-UNKNOWN-PEER-REFUSED");
    }

    /* envelope level attacks, all delivered over an authenticated channel */
    relogin_all();
    make_env("env1", 1, "x86_64", "", 200, SIGKEY);
    env[SECD_ENVELOPE_HEADER + 40] ^= 1;
    CHECK(deliver(&A, &B, "node-b") == SECD_ERR_SIGNATURE, "G20-PACKAGE-TAMPER-DETECTED-BY-SIGNATURE");
    make_inner("env2", 1, "x86_64", "", 200);
    inner[inner_len - 64 - 5] ^= 1;
    secd_envelope_build(inner, inner_len, SIGKEY, env, sizeof env, &env_len);
    CHECK(deliver(&A, &B, "node-b") == SECD_ERR_INTEGRITY, "G20-SIGNED-BUT-CORRUPT-PAYLOAD-REFUSED");
    make_inner("env3", 1, "x86_64", "", 200);
    inner[OS_PKG_HEADER] = '[';   /* manifest is no longer a JSON object */
    {
        uint8_t digest[32];
        os_sha256_hash(inner, inner_len - 64, digest);
        memcpy(inner + inner_len - 64, digest, 32);
    }
    secd_envelope_build(inner, inner_len, SIGKEY, env, sizeof env, &env_len);
    CHECK(deliver(&A, &B, "node-b") == SECD_ERR_MANIFEST, "G20-MALFORMED-MANIFEST-JSON-REFUSED-EVEN-WHEN-SIGNED");
    make_inner("env4", 1, "x86_64", "", 200);
    {
        /* well formed but with a duplicate key: rename "arch" to "id" */
        for (size_t i = OS_PKG_HEADER; i + 6 < inner_len; i++)
            if (memcmp(inner + i, "\"arch\"", 6) == 0) { memcpy(inner + i, "\"id\"  ", 6); break; }
        uint8_t digest[32];
        os_sha256_hash(inner, inner_len - 64, digest);
        memcpy(inner + inner_len - 64, digest, 32);
    }
    secd_envelope_build(inner, inner_len, SIGKEY, env, sizeof env, &env_len);
    CHECK(deliver(&A, &B, "node-b") == SECD_ERR_MANIFEST, "G20-DUPLICATE-OR-MISSING-MANIFEST-KEYS-REFUSED");
    make_env("env5", 1, "x86_64", "", 200, SIGKEY);
    env[0] = 'X';
    CHECK(deliver(&A, &B, "node-b") == SECD_ERR_FORMAT, "G20-BAD-ENVELOPE-MAGIC-REFUSED");
    make_env("env6", 1, "x86_64", "", 200, SIGKEY);
    env[16] ^= 1;
    CHECK(deliver(&A, &B, "node-b") == SECD_ERR_FORMAT, "G20-ENVELOPE-LENGTH-MISMATCH-REFUSED");
    make_env("env7", 1, "x86_64", "", 200, SIGKEY);
    env[6] = 1;
    CHECK(deliver(&A, &B, "node-b") == SECD_ERR_FORMAT, "G20-ENVELOPE-RESERVED-BITS-REFUSED");
    env_len = 10;
    CHECK(deliver(&A, &B, "node-b") == SECD_ERR_FORMAT, "G20-SHORT-ENVELOPE-REFUSED");
    CHECK(secd_envelope_build(inner, 0, SIGKEY, env, sizeof env, &env_len) == SECD_ERR_ARGUMENT &&
          secd_envelope_build(inner, inner_len, SIGKEY, env, 10, &env_len) == SECD_ERR_ARGUMENT, "G20-ENVELOPE-BUILD-BOUNDS-CHECKED");
    for (const char *id = "env"; *id; id++) {}
    CHECK(secd_find_staged(&B.node, "env1", 1) < 0 && secd_find_staged(&B.node, "env2", 1) < 0 && secd_find_staged(&B.node, "env3", 1) < 0 &&
          secd_find_staged(&B.node, "env4", 1) < 0, "G20-REJECTED-PACKAGES-STAGED-NOTHING");

    /* signing key identity and revocation */
    relogin_all();
    CHECK(pull(&A, &B, "node-b", "k1", 1, "x86_64", "", 100, KUNK) == SECD_ERR_KEY, "G20-UNKNOWN-SIGNING-KEY-REFUSED");
    CHECK(pull(&A, &B, "node-b", "k2", 1, "x86_64", "", 100, KFUT) == SECD_ERR_KEY, "G20-KEY-NOT-YET-VALID-REFUSED");
    CHECK(pull(&A, &B, "node-b", "k3", 1, "x86_64", "", 100, KOLD) == SECD_ERR_KEY, "G20-EXPIRED-KEY-REFUSED");
    make_env("k4", 1, "x86_64", "", 100, SIGKEY);
    env[env_len - 1] ^= 1;
    CHECK(deliver(&A, &B, "node-b") == SECD_ERR_SIGNATURE, "G20-INVALID-SIGNATURE-REFUSED");
    {
        uint8_t id[8];
        secd_key_id(SIGKEY, id);
        make_env("k5", 1, "x86_64", "", 100, SIGKEY);
        memcpy(env + 8, id, 8);
        env[8] ^= 0;   /* identity unchanged, signature made with another key below */
        make_inner("k6", 1, "x86_64", "", 100);
        secd_envelope_build(inner, inner_len, KUNK, env, sizeof env, &env_len);
        memcpy(env + 8, id, 8);   /* claims SIGKEY's identity, signed by KUNK */
        CHECK(deliver(&A, &B, "node-b") == SECD_ERR_SIGNATURE, "G20-FORGED-KEY-IDENTITY-REFUSED");
        CHECK(secd_key_revoke(&B.node, &B.op, id, now) == SECD_ERR_DENIED, "G20-KEY-REVOCATION-NEEDS-OWNER");
        uint8_t nope[8] = { 0 };
        CHECK(secd_key_revoke(&B.node, &B.owner, nope, now) == SECD_ERR_NOT_FOUND, "G20-REVOKE-UNKNOWN-KEY-REFUSED");
    }
    CHECK(pull(&A, &B, "node-b", "late", 1, "x86_64", "", 100, SIGKEY) == SECD_ERR_FULL, "G20-STAGING-AREA-BOUNDED");
    CHECK(secd_discard(&B.node, &(sec_token){ .slot = 7 }, "mal", 1, now) == SECD_ERR_DENIED, "G20-DISCARD-NEEDS-PRIVILEGE");
    CHECK(secd_discard(&B.node, &B.op, "mal", 9, now) == SECD_ERR_NOT_FOUND, "G20-DISCARD-UNKNOWN-VERSION-REFUSED");
    CHECK(secd_discard(&B.node, &B.op, "mal", 1, now) == SECD_OK && secd_find_staged(&B.node, "mal", 1) < 0, "G20-DISCARD-FREES-STAGING-SLOT");
    CHECK(pull(&A, &B, "node-b", "late", 1, "x86_64", "", 100, KREV) == SECD_OK && install_ok(&B, "late", 1), "G20-PACKAGE-SIGNED-BY-KEY-BEFORE-REVOCATION-INSTALLS");
    CHECK(pull(&A, &B, "node-b", "late2", 1, "x86_64", "", 100, KREV) == SECD_OK && approve(&B, "late2", 1) == SECD_OK, "G20-SECOND-PACKAGE-STAGED-AND-APPROVED");
    {
        uint8_t id[8];
        secd_key_id(KREV, id);
        CHECK(secd_key_revoke(&B.node, &B.owner, id, now) == SECD_OK, "G20-SIGNING-KEY-REVOKED");
    }
    CHECK(secd_install(&B.node, &B.op, "late2", 1, now, 0) == SECD_ERR_KEY && secd_find_installed(&B.node, "late2") < 0, "G20-REVOCATION-BLOCKS-ALREADY-STAGED-AND-APPROVED-INSTALL");
    CHECK(pull(&A, &B, "node-b", "late3", 1, "x86_64", "", 100, KREV) == SECD_ERR_KEY, "G20-REVOKED-KEY-REFUSED-ON-RECEIVE");
    CHECK(secd_count_revoked_installed(&B.node) == 1, "G20-INSTALLED-PACKAGES-FROM-REVOKED-KEY-REPORTED");
    {
        int k = -1;
        uint8_t zero[16] = { 0 };
        for (unsigned i = 0; i < SECD_MAX_KEYS; i++) if (B.node.keys[i].used && B.node.keys[i].revoked) k = (int)i;
        CHECK(k >= 0 && memcmp(B.node.keys[k].key, zero, 16) == 0, "G20-REVOKED-KEY-MATERIAL-WIPED");
    }

    /* compatibility and dependencies */
    relogin_all();
    CHECK(pull(&A, &B, "node-b", "armonly", 1, "aarch64", "", 100, SIGKEY) == SECD_ERR_COMPAT, "G20-INCOMPATIBLE-ARCHITECTURE-REFUSED");
    CHECK(pull(&A, &C, "node-c", "x86only", 1, "x86_64", "", 100, SIGKEY) == SECD_ERR_COMPAT, "G20-INCOMPATIBLE-ARCHITECTURE-REFUSED-ON-AARCH64-NODE");
    CHECK(pull(&A, &B, "node-b", "portable", 1, "any", "", 100, SIGKEY) == SECD_ERR_MANIFEST, "G20-APP-IMAGE-WITH-ANY-ARCH-REFUSED-FAIL-CLOSED");
    CHECK(pull(&A, &B, "node-b", "needs-ghost", 1, "x86_64", "ghost", 100, SIGKEY) == SECD_ERR_DEPENDENCY, "G20-MISSING-DEPENDENCY-REFUSED");
    CHECK(pull(&A, &B, "node-b", "needs-core", 1, "x86_64", "core", 100, SIGKEY) == SECD_ERR_DEPENDENCY, "G20-DEPENDENCY-MUST-BE-INSTALLED-NOT-MERELY-STAGED");
    CHECK(install_ok(&B, "core", 1), "G20-DEPENDENCY-INSTALLED");
    CHECK(pull(&A, &B, "node-b", "needs-core", 1, "x86_64", "core", 100, SIGKEY) == SECD_OK && install_ok(&B, "needs-core", 1), "G20-DEPENDENT-PACKAGE-INSTALLS-AFTER-DEPENDENCY");
    CHECK(pull(&A, &C, "node-c", "core", 1, "aarch64", "", 3000, SIGKEY) == SECD_OK && install_ok(&C, "core", 1), "G20-AARCH64-NODE-INSTALLS-ITS-OWN-PACKAGE");
    CHECK(C.node.used_kb == 3, "G20-AARCH64-NODE-ACCOUNTING");
    CHECK(pull(&A, &C, "node-c", "big", 1, "aarch64", "", 2000, SIGKEY) == SECD_ERR_QUOTA, "G20-NODE-STORAGE-QUOTA-ENFORCED");
    CHECK(pull(&A, &C, "node-c", "small", 1, "aarch64", "", 1000, SIGKEY) == SECD_OK, "G20-PACKAGE-WITHIN-REMAINING-QUOTA-ACCEPTED");

    /* faults, recovery, rollback and version policy */
    relogin_all();
    CHECK(pull(&A, &B, "node-b", "svc", 1, "x86_64", "", 100, SIGKEY) == SECD_OK && install_ok(&B, "svc", 1), "G20-SERVICE-PACKAGE-V1-INSTALLED");
    CHECK(pull(&A, &B, "node-b", "svc", 1, "x86_64", "", 100, SIGKEY) == SECD_ERR_VERSION, "G20-REINSTALL-SAME-VERSION-REFUSED");
    CHECK(pull(&A, &B, "node-b", "svc", 2, "x86_64", "", 2000, SIGKEY) == SECD_OK && approve(&B, "svc", 2) == SECD_OK, "G20-V2-STAGED-AND-APPROVED");
    {
        uint32_t used_before = B.node.used_kb;
        CHECK(secd_install(&B.node, &B.op, "svc", 2, now, SECD_FAULT_INTERRUPT_BEFORE) == SECD_ERR_FAULT &&
              B.node.installed[secd_find_installed(&B.node, "svc")].version == 1 && !B.node.journal_valid && B.node.used_kb == used_before,
              "G20-INTERRUPTION-BEFORE-COMMIT-CHANGES-NOTHING");
        CHECK(secd_install(&B.node, &B.op, "svc", 2, now, SECD_FAULT_CRASH_MID) == SECD_ERR_FAULT && B.node.journal_valid &&
              B.node.installed[secd_find_installed(&B.node, "svc")].pending, "G20-CRASH-MID-INSTALL-LEAVES-JOURNAL");
        CHECK(secd_install(&B.node, &B.op, "svc", 2, now, 0) == SECD_ERR_STATE, "G20-INSTALL-REFUSED-WHILE-RECOVERY-PENDING");
        make_env("other", 1, "x86_64", "", 100, SIGKEY);
        CHECK(deliver(&A, &B, "node-b") == SECD_ERR_STATE, "G20-RECEIVE-REFUSED-WHILE-RECOVERY-PENDING");
        CHECK(secd_recover(&B.node, &B.op, now) == SECD_ERR_DENIED, "G20-RECOVERY-NEEDS-PRIVILEGE");
        CHECK(secd_recover(&B.node, &B.owner, now) == SECD_OK, "G20-RECOVERY-RESTORES-JOURNAL");
        CHECK(B.node.installed[secd_find_installed(&B.node, "svc")].version == 1 && !B.node.installed[secd_find_installed(&B.node, "svc")].pending &&
              B.node.used_kb == used_before && !B.node.journal_valid, "G20-RECOVERED-STATE-MATCHES-PRE-INSTALL-STATE");
        CHECK(secd_recover(&B.node, &B.owner, now) == SECD_ERR_STATE, "G20-RECOVERY-WITHOUT-JOURNAL-REFUSED");
        CHECK(secd_install(&B.node, &B.op, "svc", 2, now, SECD_FAULT_VERIFY_FAIL) == SECD_ERR_FAULT &&
              B.node.installed[secd_find_installed(&B.node, "svc")].version == 1 && !B.node.journal_valid && B.node.used_kb == used_before,
              "G20-FAILED-INSTALLATION-ROLLS-BACK-AUTOMATICALLY");
        CHECK(secd_install(&B.node, &B.op, "svc", 2, now, 0) == SECD_ERR_APPROVAL, "G20-FAILED-INSTALL-BURNS-THE-APPROVAL");
        CHECK(approve(&B, "svc", 2) == SECD_OK && secd_install(&B.node, &B.op, "svc", 2, now, 0) == SECD_OK, "G20-RETRY-WITH-FRESH-APPROVAL-SUCCEEDS");
        CHECK(B.node.installed[secd_find_installed(&B.node, "svc")].version == 2 && B.node.installed[secd_find_installed(&B.node, "svc")].previous == 1, "G20-UPGRADE-RECORDS-PREVIOUS-VERSION");
        CHECK(secd_rollback(&B.node, &B.owner, "svc", now) == SECD_ERR_POLICY, "G20-ROLLBACK-REFUSED-WITHOUT-POLICY");
        CHECK(secd_set_rollback_policy(&B.node, &B.op, 1, now) == SECD_ERR_DENIED, "G20-ROLLBACK-POLICY-NEEDS-OWNER");
        CHECK(secd_set_rollback_policy(&B.node, &B.owner, 1, now) == SECD_OK, "G20-ROLLBACK-POLICY-AUTHORIZED");
        CHECK(secd_rollback(&B.node, &B.op, "svc", now) == SECD_ERR_DENIED, "G20-OPERATOR-CANNOT-ROLL-BACK");
        CHECK(secd_rollback(&B.node, &B.owner, "svc", now) == SECD_OK && B.node.installed[secd_find_installed(&B.node, "svc")].version == 1 &&
              B.node.installed[secd_find_installed(&B.node, "svc")].previous == 0 && B.node.used_kb == used_before, "G20-ROLLBACK-RESTORES-PREVIOUS-VERSION");
        CHECK(secd_rollback(&B.node, &B.owner, "svc", now) == SECD_ERR_NOT_FOUND && secd_rollback(&B.node, &B.owner, "ghost", now) == SECD_ERR_NOT_FOUND, "G20-ROLLBACK-WITHOUT-PREVIOUS-REFUSED");
        CHECK(pull(&A, &B, "node-b", "svc", 2, "x86_64", "", 2000, SIGKEY) == SECD_ERR_VERSION, "G20-ROLLED-BACK-VERSION-CANNOT-BE-REINSTALLED");
        CHECK(pull(&A, &B, "node-b", "svc", 3, "x86_64", "", 2000, SIGKEY) == SECD_OK && install_ok(&B, "svc", 3), "G20-NEWER-VERSION-INSTALLS-AFTER-ROLLBACK");
        CHECK(pull(&A, &B, "node-b", "svc", 1, "x86_64", "", 100, SIGKEY) == SECD_ERR_VERSION, "G20-DOWNGRADE-ATTACK-REFUSED");
    }

    CHECK(secd_discard(&B.node, &B.op, "late2", 1, now) == SECD_OK && approve(&B, "late2", 1) == SECD_ERR_NOT_FOUND, "G20-DISCARD-DROPS-STAGED-PACKAGE-AND-APPROVAL");
    /* capability denial across principals */
    relogin_all();
    {
        sec_token usr, svc;
        uint8_t key[32], nonce[16], resp[32];
        memcpy(key, fill(90, 32), 32);
        sec_account_create(&B.sec, &B.owner, "usr", "Us3r-Pass-4444", SEC_ROLE_USER, salt(30), now);
        sec_login(&B.sec, "usr", "Us3r-Pass-4444", ent(), now, &usr);
        make_env("cap1", 1, "x86_64", "", 100, SIGKEY);
        now++;
        seal(&A, "node-b");
        CHECK(secd_receive(&B.node, &usr, frame, frame_len, now) == SECD_ERR_DENIED, "G20-USER-CANNOT-RECEIVE-PACKAGES");
        CHECK(secd_send(&B.node, &usr, "node-a", env, env_len, copy, sizeof copy, &first_len, now) == SECD_ERR_DENIED, "G20-USER-CANNOT-SEND-PACKAGES");
        CHECK(secd_install(&B.node, &usr, "svc", 3, now, 0) == SECD_ERR_DENIED && secd_approve(&B.node, &usr, "svc", 3, key, now + 5, now) == SECD_ERR_DENIED &&
              secd_rollback(&B.node, &usr, "svc", now) == SECD_ERR_DENIED && secd_recover(&B.node, &usr, now) == SECD_ERR_DENIED &&
              secd_key_add(&B.node, &usr, KUNK, 0, 5000, now) == SECD_ERR_DENIED, "G20-USER-DENIED-INSTALL-APPROVE-ROLLBACK-RECOVER-AND-KEY-MANAGEMENT");
        CHECK(secd_receive(&B.node, &B.op, frame, frame_len, now) == SECD_OK, "G20-OPERATOR-RECEIVES-PACKAGE");
        CHECK(sec_service_register(&B.sec, &B.owner, "ci", key, SEC_PERM_REQUEST_INSTALL | SEC_PERM_SEND_PACKAGE, now) == SEC_OK, "G20-SERVICE-IDENTITY-REGISTERED-ON-NODE");
        sec_service_challenge(&B.sec, "ci", ent(), now, nonce);
        sec_service_response(key, nonce, "ci", resp);
        CHECK(sec_service_authenticate(&B.sec, "ci", resp, ent(), now, &svc) == SEC_OK, "G20-SERVICE-AUTHENTICATED-ON-NODE");
        make_env("cap2", 1, "x86_64", "", 100, SIGKEY);
        now++;
        seal(&A, "node-b");
        CHECK(secd_receive(&B.node, &svc, frame, frame_len, now) == SECD_OK, "G20-SERVICE-WITH-CAPABILITY-RECEIVES");
        CHECK(secd_approve(&B.node, &svc, "cap2", 1, B.node.staged[secd_find_staged(&B.node, "cap2", 1)].digest, now + 100, now) == SECD_ERR_DENIED, "G20-SERVICE-CANNOT-APPROVE-ITS-OWN-REQUEST");
        CHECK(secd_set_rollback_policy(&B.node, &svc, 0, now) == SECD_ERR_DENIED && secd_peer_enroll(&B.node, &svc, "node-q", PSK_X, now) == SECD_ERR_DENIED,
              "G20-SERVICE-CANNOT-CHANGE-POLICY-OR-ENROLL-PEERS");
        CHECK(secd_install(&B.node, &svc, "cap2", 1, now, 0) == SECD_ERR_APPROVAL, "G20-SERVICE-CANNOT-INSTALL-WITHOUT-HUMAN-APPROVAL");
        CHECK(secd_receive(&B.node, &(sec_token){ .slot = 7 }, frame, frame_len, now) == SECD_ERR_DENIED, "G20-FORGED-TOKEN-CANNOT-RECEIVE");
    }

    /* request quota on the receiving operator */
    relogin_all();
    CHECK(sec_quota_set(&B.sec, &B.owner, "op", 64, 2, now) == SEC_OK, "G20-OPERATOR-REQUEST-QUOTA-SET");
    relogin(&B);
    now += SEC_WINDOW_TICKS;
    {
        int quota_hit = 0;
        for (unsigned i = 0; i < 4; i++) {
            make_env("q", 10 + i, "x86_64", "", 100, SIGKEY);
            now++;
            seal(&A, "node-b");
            if (secd_receive(&B.node, &B.op, frame, frame_len, now) == SECD_ERR_QUOTA) quota_hit = 1;
        }
        CHECK(quota_hit, "G20-REQUEST-RATE-QUOTA-STOPS-FLOODING");
    }

    /* audit across the deployment */
    {
        unsigned denied = 0, rejects = 0, installs = 0, n = sec_audit_retained(&B.sec);
        for (unsigned i = 0; i < n; i++) {
            const sec_audit_entry *e = sec_audit_get(&B.sec, i);
            denied += e->action == SEC_A_DENIED;
            rejects += e->action == SEC_A_FRAME_REJECT || e->action == SEC_A_PKG_REJECT;
            installs += e->action == SEC_A_INSTALL_OK;
        }
        CHECK(sec_audit_verify(&B.sec) && sec_audit_verify(&A.sec) && sec_audit_verify(&C.sec), "G20-DEPLOYMENT-AUDIT-CHAINS-VERIFY");
        CHECK(denied > 0 && rejects > 0 && installs > 0 && sec_audit_total(&B.sec) > SEC_AUDIT_CAP / 2, "G20-DEPLOYMENT-AUDIT-RECORDS-DENIALS-REJECTIONS-AND-INSTALLS");
        CHECK(!contains(&B.sec, sizeof B.sec, PSK_AB, 32) && !contains(&A.sec, sizeof A.sec, PSK_AB, 32), "G20-PEER-KEYS-NEVER-ENTER-THE-AUDIT-OR-ACCOUNT-STORE");
    }
}

int main(void)
{
    test_primitives();
    test_security_service();
    test_deployment();
    if (failures) {
        printf("G20 SECURITY RESULT: FAIL (%d)\n", failures);
        return 1;
    }
    printf("G20 SECURITY RESULT: PASS\n");
    return 0;
}
