#include "secsvc.h"
#include "hmac_sha256.h"
#include "sha256.h"

static void wipe(void *p, size_t n)
{
    volatile uint8_t *b = (volatile uint8_t *)p;
    while (n--) {
        *b++ = 0;
    }
}

static void copy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
}

static int ct_equal(const uint8_t *a, const uint8_t *b, size_t n)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < n; i++) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return diff == 0;
}

static int all_zero(const uint8_t *p, size_t n)
{
    uint8_t acc = 0;
    for (size_t i = 0; i < n; i++) {
        acc |= p[i];
    }
    return acc == 0;
}

static size_t bounded_len(const char *s, size_t max)
{
    size_t n = 0;
    while (n < max && s[n]) {
        n++;
    }
    return n;
}

static int name_valid(const char *name)
{
    if (!name) {
        return 0;
    }
    size_t n = bounded_len(name, SEC_NAME_MAX);
    if (n == 0 || n >= SEC_NAME_MAX) {
        return 0;
    }
    for (size_t i = 0; i < n; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return 0;
        }
    }
    return 1;
}

static int name_equal(const char *a, const char *b)
{
    size_t i = 0;
    while (i < SEC_NAME_MAX && a[i] && a[i] == b[i]) {
        i++;
    }
    return i < SEC_NAME_MAX && a[i] == b[i];
}

static void name_copy(char *dst, const char *src)
{
    size_t n = bounded_len(src, SEC_NAME_MAX - 1u);
    for (size_t i = 0; i < n; i++) {
        dst[i] = src[i];
    }
    for (size_t i = n; i < SEC_NAME_MAX; i++) {
        dst[i] = 0;
    }
}

static int password_acceptable(const char *name, const char *password)
{
    if (!password) {
        return 0;
    }
    size_t n = bounded_len(password, SEC_PASSWORD_MAX + 1u);
    if (n < SEC_PASSWORD_MIN || n > SEC_PASSWORD_MAX) {
        return 0;
    }
    int varied = 0;
    for (size_t i = 1; i < n; i++) {
        if (password[i] != password[0]) {
            varied = 1;
        }
    }
    if (!varied) {
        return 0;
    }
    return !(name && name_equal(name, password));
}

void sec_pbkdf2_sha256(const uint8_t *password, size_t password_len, const uint8_t *salt, size_t salt_len,
                       uint32_t iterations, uint8_t out[32])
{
    uint8_t msg[64];
    uint8_t u[32];
    uint8_t t[32];
    if (salt_len > sizeof msg - 4u || iterations == 0) {
        wipe(out, 32);
        return;
    }
    copy(msg, salt, salt_len);
    msg[salt_len] = 0; msg[salt_len + 1] = 0; msg[salt_len + 2] = 0; msg[salt_len + 3] = 1;
    os_hmac_sha256(password, password_len, msg, salt_len + 4u, u);
    copy(t, u, 32);
    for (uint32_t i = 1; i < iterations; i++) {
        os_hmac_sha256(password, password_len, u, 32, u);
        for (unsigned k = 0; k < 32; k++) {
            t[k] ^= u[k];
        }
    }
    copy(out, t, 32);
    wipe(u, sizeof u);
    wipe(t, sizeof t);
    wipe(msg, sizeof msg);
}

static void hash_password(const char *password, const uint8_t salt[16], uint8_t out[32])
{
    sec_pbkdf2_sha256((const uint8_t *)password, bounded_len(password, SEC_PASSWORD_MAX), salt, 16,
                      SEC_PBKDF_ITERATIONS, out);
}

void sec_init(sec_system *s, const uint8_t server_key[32])
{
    wipe(s, sizeof *s);
    copy(s->server_key, server_key, 32);
}

/* ---------------- audit ---------------- */

static void audit_chain(const uint8_t prev[32], const sec_audit_entry *e, uint8_t out[32])
{
    uint8_t buf[32 + 8 + 8 + 4 + 4 + 4];
    copy(buf, prev, 32);
    for (unsigned i = 0; i < 8; i++) {
        buf[32 + i] = (uint8_t)(e->seq >> (8 * i));
        buf[40 + i] = (uint8_t)(e->tick >> (8 * i));
    }
    for (unsigned i = 0; i < 4; i++) {
        buf[48 + i] = (uint8_t)((uint32_t)e->actor >> (8 * i));
        buf[52 + i] = (uint8_t)(e->action >> (8 * i));
        buf[56 + i] = (uint8_t)((uint32_t)e->result >> (8 * i));
    }
    os_sha256_hash(buf, sizeof buf, out);
}

void sec_audit_log(sec_system *s, int actor, uint32_t action, int result, uint64_t now)
{
    unsigned idx = (unsigned)(s->audit_total % SEC_AUDIT_CAP);
    uint8_t prev[32];
    if (s->audit_total == 0) {
        copy(prev, s->audit_anchor, 32);
    } else {
        copy(prev, s->audit[(s->audit_total - 1u) % SEC_AUDIT_CAP].chain, 32);
    }
    if (s->audit_total >= SEC_AUDIT_CAP) {
        copy(s->audit_anchor, s->audit[idx].chain, 32);
    }
    sec_audit_entry *e = &s->audit[idx];
    e->seq = s->audit_total;
    e->tick = now;
    e->actor = actor;
    e->action = action;
    e->result = result;
    audit_chain(prev, e, e->chain);
    s->audit_total++;
}

uint64_t sec_audit_total(const sec_system *s)
{
    return s->audit_total;
}

unsigned sec_audit_retained(const sec_system *s)
{
    return s->audit_total < SEC_AUDIT_CAP ? (unsigned)s->audit_total : SEC_AUDIT_CAP;
}

const sec_audit_entry *sec_audit_get(const sec_system *s, unsigned index)
{
    unsigned n = sec_audit_retained(s);
    if (index >= n) {
        return (const sec_audit_entry *)0;
    }
    unsigned start = s->audit_total >= SEC_AUDIT_CAP ? (unsigned)(s->audit_total % SEC_AUDIT_CAP) : 0u;
    return &s->audit[(start + index) % SEC_AUDIT_CAP];
}

int sec_audit_verify(const sec_system *s)
{
    unsigned n = sec_audit_retained(s);
    uint8_t prev[32];
    copy(prev, s->audit_anchor, 32);
    for (unsigned i = 0; i < n; i++) {
        const sec_audit_entry *e = sec_audit_get(s, i);
        uint8_t expect[32];
        if (e->seq != s->audit_total - n + i) {
            return 0;
        }
        audit_chain(prev, e, expect);
        if (!ct_equal(expect, e->chain, 32)) {
            return 0;
        }
        copy(prev, e->chain, 32);
    }
    return 1;
}

/* ---------------- sessions ---------------- */

int sec_find_principal(const sec_system *s, const char *name)
{
    if (!name_valid(name)) {
        return -1;
    }
    for (unsigned i = 0; i < SEC_MAX_PRINCIPALS; i++) {
        if (s->principals[i].used && name_equal(s->principals[i].name, name)) {
            return (int)i;
        }
    }
    return -1;
}

static void session_mac(const sec_system *s, uint32_t slot, const uint8_t secret[16], uint8_t out[32])
{
    uint8_t msg[20];
    copy(msg, secret, 16);
    msg[16] = (uint8_t)slot; msg[17] = (uint8_t)(slot >> 8); msg[18] = (uint8_t)(slot >> 16); msg[19] = (uint8_t)(slot >> 24);
    os_hmac_sha256(s->server_key, 32, msg, sizeof msg, out);
}

static int session_create(sec_system *s, int principal, const uint8_t entropy[16], uint64_t now, sec_token *out)
{
    if (all_zero(entropy, 16)) {
        return SEC_ERR_ARGUMENT;
    }
    for (unsigned i = 0; i < SEC_MAX_SESSIONS; i++) {
        sec_session *se = &s->sessions[i];
        if (se->used && now >= se->expiry) {
            se->used = 0;
        }
        if (!se->used) {
            se->used = 1;
            se->principal = principal;
            se->expiry = now + SEC_SESSION_TTL;
            out->slot = i;
            copy(out->secret, entropy, 16);
            session_mac(s, i, entropy, se->secret_mac);
            return SEC_OK;
        }
    }
    return SEC_ERR_FULL;
}

int sec_session_principal(sec_system *s, const sec_token *token, uint64_t now)
{
    if (!token || token->slot >= SEC_MAX_SESSIONS) {
        return SEC_ERR_AUTH;
    }
    sec_session *se = &s->sessions[token->slot];
    uint8_t mac[32];
    if (!se->used) {
        return SEC_ERR_AUTH;
    }
    session_mac(s, token->slot, token->secret, mac);
    int ok = ct_equal(mac, se->secret_mac, 32);
    wipe(mac, sizeof mac);
    if (!ok) {
        return SEC_ERR_AUTH;
    }
    if (now >= se->expiry) {
        se->used = 0;
        return SEC_ERR_EXPIRED;
    }
    sec_principal *p = &s->principals[se->principal];
    if (!p->used || p->disabled) {
        se->used = 0;
        return SEC_ERR_AUTH;
    }
    return se->principal;
}

int sec_session_role(sec_system *s, const sec_token *token, uint64_t now)
{
    int p = sec_session_principal(s, token, now);
    return p < 0 ? p : s->principals[p].role;
}

uint32_t sec_role_permissions(int role)
{
    uint32_t user = SEC_PERM_READ | SEC_PERM_WRITE;
    uint32_t operator_ = user | SEC_PERM_REQUEST_INSTALL | SEC_PERM_SEND_PACKAGE | SEC_PERM_VIEW_AUDIT;
    switch (role) {
    case SEC_ROLE_GUEST: return SEC_PERM_READ;
    case SEC_ROLE_USER: return user;
    case SEC_ROLE_OPERATOR: return operator_;
    case SEC_ROLE_ADMIN: return operator_ | SEC_PERM_MANAGE_ACCOUNTS | SEC_PERM_MANAGE_SERVICES | SEC_PERM_RECOVER;
    case SEC_ROLE_OWNER: return SEC_PERM_ALL;
    default: return 0;
    }
}

static uint32_t principal_perms(const sec_principal *p)
{
    return p->is_service ? (p->service_perms & SEC_SERVICE_PERM_CEILING) : sec_role_permissions(p->role);
}

int sec_authorize(sec_system *s, const sec_token *token, uint32_t perm, uint64_t now)
{
    int p = sec_session_principal(s, token, now);
    if (p < 0) {
        sec_audit_log(s, -1, SEC_A_DENIED, p, now);
        return p;
    }
    if (perm == 0 || (principal_perms(&s->principals[p]) & perm) != perm) {
        sec_audit_log(s, p, SEC_A_DENIED, SEC_ERR_DENIED, now);
        return SEC_ERR_DENIED;
    }
    return SEC_OK;
}

static void revoke_sessions(sec_system *s, int principal, int keep_slot)
{
    for (unsigned i = 0; i < SEC_MAX_SESSIONS; i++) {
        if (s->sessions[i].used && s->sessions[i].principal == principal && (int)i != keep_slot) {
            s->sessions[i].used = 0;
        }
    }
}

/* ---------------- accounts ---------------- */

static int new_principal(sec_system *s, const char *name)
{
    if (!name_valid(name)) {
        return SEC_ERR_ARGUMENT;
    }
    if (sec_find_principal(s, name) >= 0) {
        return SEC_ERR_EXISTS;
    }
    for (unsigned i = 0; i < SEC_MAX_PRINCIPALS; i++) {
        if (!s->principals[i].used) {
            wipe(&s->principals[i], sizeof s->principals[i]);
            s->principals[i].used = 1;
            name_copy(s->principals[i].name, name);
            s->principals[i].quota_storage_kb = 64;
            s->principals[i].quota_requests = 100;
            return (int)i;
        }
    }
    return SEC_ERR_FULL;
}

int sec_bootstrap_owner(sec_system *s, const char *name, const char *password, const uint8_t salt[16], uint64_t now)
{
    if (s->bootstrapped) {
        return SEC_ERR_STATE;
    }
    for (unsigned i = 0; i < SEC_MAX_PRINCIPALS; i++) {
        if (s->principals[i].used) {
            return SEC_ERR_STATE;
        }
    }
    if (!name_valid(name) || !password_acceptable(name, password) || all_zero(salt, 16)) {
        return SEC_ERR_WEAK;
    }
    int p = new_principal(s, name);
    if (p < 0) {
        return p;
    }
    sec_principal *pr = &s->principals[p];
    pr->role = SEC_ROLE_OWNER;
    pr->quota_storage_kb = 1024;
    pr->quota_requests = 1000;
    copy(pr->salt, salt, 16);
    hash_password(password, salt, pr->secret_hash);
    s->bootstrapped = 1;
    sec_audit_log(s, p, SEC_A_BOOTSTRAP, SEC_OK, now);
    return SEC_OK;
}

int sec_login(sec_system *s, const char *name, const char *password, const uint8_t entropy[16], uint64_t now,
              sec_token *out)
{
    static const uint8_t dummy_salt[16] = {1};
    uint8_t hash[32];
    if (!name || !password || !out || bounded_len(password, SEC_PASSWORD_MAX + 1u) > SEC_PASSWORD_MAX) {
        return SEC_ERR_ARGUMENT;
    }
    int p = sec_find_principal(s, name);
    if (p < 0 || s->principals[p].is_service || s->principals[p].disabled) {
        hash_password(password, dummy_salt, hash);   /* equalizes timing for unknown names */
        wipe(hash, sizeof hash);
        sec_audit_log(s, -1, SEC_A_LOGIN_FAIL, SEC_ERR_AUTH, now);
        return SEC_ERR_AUTH;
    }
    sec_principal *pr = &s->principals[p];
    if (pr->locked_until != 0) {
        if (now < pr->locked_until) {
            sec_audit_log(s, p, SEC_A_LOGIN_LOCKED, SEC_ERR_LOCKED, now);
            return SEC_ERR_LOCKED;
        }
        pr->locked_until = 0;
        pr->failures = 0;
    }
    hash_password(password, pr->salt, hash);
    int ok = ct_equal(hash, pr->secret_hash, 32);
    wipe(hash, sizeof hash);
    if (!ok) {
        if (++pr->failures >= SEC_MAX_FAILURES) {
            pr->locked_until = now + SEC_LOCKOUT_TICKS;
            pr->failures = 0;
        }
        sec_audit_log(s, p, SEC_A_LOGIN_FAIL, SEC_ERR_AUTH, now);
        return SEC_ERR_AUTH;
    }
    pr->failures = 0;
    int rc = session_create(s, p, entropy, now, out);
    sec_audit_log(s, p, SEC_A_LOGIN_OK, rc, now);
    return rc;
}

int sec_logout(sec_system *s, const sec_token *token, uint64_t now)
{
    int p = sec_session_principal(s, token, now);
    if (p < 0) {
        return p;
    }
    s->sessions[token->slot].used = 0;
    sec_audit_log(s, p, SEC_A_LOGOUT, SEC_OK, now);
    return SEC_OK;
}

static int valid_account_role(int role)
{
    return role >= SEC_ROLE_GUEST && role <= SEC_ROLE_OWNER;
}

int sec_account_create(sec_system *s, const sec_token *actor, const char *name, const char *password, int role,
                       const uint8_t salt[16], uint64_t now)
{
    int rc = sec_authorize(s, actor, SEC_PERM_MANAGE_ACCOUNTS, now);
    if (rc != SEC_OK) {
        return rc;
    }
    int a = sec_session_principal(s, actor, now);
    if (!valid_account_role(role) || role >= s->principals[a].role) {
        sec_audit_log(s, a, SEC_A_DENIED, SEC_ERR_DENIED, now);
        return SEC_ERR_DENIED;
    }
    if (!name_valid(name) || !password_acceptable(name, password) || all_zero(salt, 16)) {
        return SEC_ERR_WEAK;
    }
    int p = new_principal(s, name);
    if (p < 0) {
        return p;
    }
    sec_principal *pr = &s->principals[p];
    pr->role = role;
    copy(pr->salt, salt, 16);
    hash_password(password, salt, pr->secret_hash);
    sec_audit_log(s, a, SEC_A_ACCOUNT_CREATE, SEC_OK, now);
    return SEC_OK;
}

static int manageable_target(sec_system *s, const sec_token *actor, const char *name, uint64_t now, int *actor_out)
{
    int rc = sec_authorize(s, actor, SEC_PERM_MANAGE_ACCOUNTS, now);
    if (rc != SEC_OK) {
        return rc;
    }
    int a = sec_session_principal(s, actor, now);
    int t = sec_find_principal(s, name);
    if (t < 0) {
        return SEC_ERR_NOT_FOUND;
    }
    if (t == a || s->principals[t].is_service || s->principals[t].role >= s->principals[a].role) {
        sec_audit_log(s, a, SEC_A_DENIED, SEC_ERR_DENIED, now);
        return SEC_ERR_DENIED;
    }
    *actor_out = a;
    return t;
}

int sec_account_set_role(sec_system *s, const sec_token *actor, const char *name, int role, uint64_t now)
{
    int a = 0;
    int t = manageable_target(s, actor, name, now, &a);
    if (t < 0) {
        return t;
    }
    if (!valid_account_role(role) || role >= s->principals[a].role) {
        sec_audit_log(s, a, SEC_A_DENIED, SEC_ERR_DENIED, now);
        return SEC_ERR_DENIED;
    }
    s->principals[t].role = role;
    revoke_sessions(s, t, -1);
    sec_audit_log(s, a, SEC_A_ROLE_CHANGE, SEC_OK, now);
    return SEC_OK;
}

int sec_account_set_disabled(sec_system *s, const sec_token *actor, const char *name, int disabled, uint64_t now)
{
    int a = 0;
    int t = manageable_target(s, actor, name, now, &a);
    if (t < 0) {
        return t;
    }
    s->principals[t].disabled = disabled ? 1 : 0;
    if (disabled) {
        revoke_sessions(s, t, -1);
    }
    sec_audit_log(s, a, SEC_A_DISABLE, SEC_OK, now);
    return SEC_OK;
}

int sec_change_password(sec_system *s, const sec_token *token, const char *old_password, const char *new_password,
                        const uint8_t salt[16], uint64_t now)
{
    uint8_t hash[32];
    int p = sec_session_principal(s, token, now);
    if (p < 0) {
        return p;
    }
    sec_principal *pr = &s->principals[p];
    if (pr->is_service || !old_password || bounded_len(old_password, SEC_PASSWORD_MAX + 1u) > SEC_PASSWORD_MAX) {
        return SEC_ERR_ARGUMENT;
    }
    hash_password(old_password, pr->salt, hash);
    int ok = ct_equal(hash, pr->secret_hash, 32);
    wipe(hash, sizeof hash);
    if (!ok) {
        sec_audit_log(s, p, SEC_A_PASSWORD_CHANGE, SEC_ERR_AUTH, now);
        return SEC_ERR_AUTH;
    }
    if (!password_acceptable(pr->name, new_password) || all_zero(salt, 16)) {
        return SEC_ERR_WEAK;
    }
    hash_password(new_password, pr->salt, hash);
    int reuse = ct_equal(hash, pr->secret_hash, 32);
    wipe(hash, sizeof hash);
    if (reuse) {
        return SEC_ERR_WEAK;
    }
    hash_password(new_password, salt, hash);
    copy(pr->salt, salt, 16);
    copy(pr->secret_hash, hash, 32);
    wipe(hash, sizeof hash);
    revoke_sessions(s, p, (int)token->slot);
    sec_audit_log(s, p, SEC_A_PASSWORD_CHANGE, SEC_OK, now);
    return SEC_OK;
}

/* ---------------- service identities ---------------- */

int sec_service_register(sec_system *s, const sec_token *actor, const char *name, const uint8_t key[32],
                         uint32_t perms, uint64_t now)
{
    int rc = sec_authorize(s, actor, SEC_PERM_MANAGE_SERVICES, now);
    if (rc != SEC_OK) {
        return rc;
    }
    int a = sec_session_principal(s, actor, now);
    if ((perms & ~SEC_SERVICE_PERM_CEILING) != 0) {
        sec_audit_log(s, a, SEC_A_DENIED, SEC_ERR_DENIED, now);
        return SEC_ERR_DENIED;
    }
    if (all_zero(key, 32)) {
        return SEC_ERR_WEAK;
    }
    int p = new_principal(s, name);
    if (p < 0) {
        return p;
    }
    sec_principal *pr = &s->principals[p];
    pr->is_service = 1;
    pr->role = SEC_ROLE_SERVICE;
    pr->service_perms = perms;
    pr->quota_storage_kb = 32;
    copy(pr->secret_hash, key, 32);
    sec_audit_log(s, a, SEC_A_SERVICE_REGISTER, SEC_OK, now);
    return SEC_OK;
}

void sec_service_response(const uint8_t key[32], const uint8_t nonce[16], const char *name, uint8_t out[32])
{
    uint8_t msg[4 + 16 + SEC_NAME_MAX];
    size_t n = bounded_len(name, SEC_NAME_MAX - 1u);
    copy(msg, "SVC1", 4);
    copy(msg + 4, nonce, 16);
    copy(msg + 20, name, n);
    os_hmac_sha256(key, 32, msg, 20u + n, out);
}

int sec_service_challenge(sec_system *s, const char *name, const uint8_t entropy[16], uint64_t now,
                          uint8_t nonce_out[16])
{
    int p = sec_find_principal(s, name);
    if (p < 0 || !s->principals[p].is_service || s->principals[p].disabled || all_zero(entropy, 16)) {
        return SEC_ERR_AUTH;
    }
    sec_principal *pr = &s->principals[p];
    copy(pr->nonce, entropy, 16);
    pr->nonce_expiry = now + SEC_NONCE_TTL;
    pr->nonce_pending = 1;
    copy(nonce_out, entropy, 16);
    return SEC_OK;
}

int sec_service_authenticate(sec_system *s, const char *name, const uint8_t response[32], const uint8_t entropy[16],
                             uint64_t now, sec_token *out)
{
    int p = sec_find_principal(s, name);
    if (p < 0 || !s->principals[p].is_service || s->principals[p].disabled) {
        sec_audit_log(s, -1, SEC_A_SERVICE_AUTH_FAIL, SEC_ERR_AUTH, now);
        return SEC_ERR_AUTH;
    }
    sec_principal *pr = &s->principals[p];
    if (!pr->nonce_pending) {
        sec_audit_log(s, p, SEC_A_SERVICE_AUTH_FAIL, SEC_ERR_REPLAY, now);
        return SEC_ERR_REPLAY;
    }
    pr->nonce_pending = 0;   /* single use, consumed before comparison */
    if (now >= pr->nonce_expiry) {
        sec_audit_log(s, p, SEC_A_SERVICE_AUTH_FAIL, SEC_ERR_EXPIRED, now);
        return SEC_ERR_EXPIRED;
    }
    uint8_t expect[32];
    sec_service_response(pr->secret_hash, pr->nonce, pr->name, expect);
    int ok = ct_equal(expect, response, 32);
    wipe(expect, sizeof expect);
    if (!ok) {
        sec_audit_log(s, p, SEC_A_SERVICE_AUTH_FAIL, SEC_ERR_AUTH, now);
        return SEC_ERR_AUTH;
    }
    int rc = session_create(s, p, entropy, now, out);
    sec_audit_log(s, p, SEC_A_SERVICE_AUTH_OK, rc, now);
    return rc;
}

/* ---------------- quotas ---------------- */

int sec_quota_set(sec_system *s, const sec_token *actor, const char *name, uint32_t storage_kb, uint32_t requests,
                  uint64_t now)
{
    int rc = sec_authorize(s, actor, SEC_PERM_MANAGE_ACCOUNTS, now);
    if (rc != SEC_OK) {
        return rc;
    }
    int t = sec_find_principal(s, name);
    if (t < 0) {
        return SEC_ERR_NOT_FOUND;
    }
    s->principals[t].quota_storage_kb = storage_kb;
    s->principals[t].quota_requests = requests;
    return SEC_OK;
}

int sec_charge_storage(sec_system *s, const sec_token *token, uint32_t kb, uint64_t now)
{
    int p = sec_session_principal(s, token, now);
    if (p < 0) {
        return p;
    }
    sec_principal *pr = &s->principals[p];
    if (kb > pr->quota_storage_kb || pr->used_storage_kb > pr->quota_storage_kb - kb) {
        sec_audit_log(s, p, SEC_A_QUOTA_DENIED, SEC_ERR_QUOTA, now);
        return SEC_ERR_QUOTA;
    }
    pr->used_storage_kb += kb;
    return SEC_OK;
}

int sec_release_storage(sec_system *s, const sec_token *token, uint32_t kb, uint64_t now)
{
    int p = sec_session_principal(s, token, now);
    if (p < 0) {
        return p;
    }
    if (kb > s->principals[p].used_storage_kb) {
        return SEC_ERR_ARGUMENT;
    }
    s->principals[p].used_storage_kb -= kb;
    return SEC_OK;
}

int sec_charge_request(sec_system *s, const sec_token *token, uint64_t now)
{
    int p = sec_session_principal(s, token, now);
    if (p < 0) {
        return p;
    }
    sec_principal *pr = &s->principals[p];
    if (now < pr->window_start || now - pr->window_start >= SEC_WINDOW_TICKS) {
        pr->window_start = now;
        pr->used_requests = 0;
    }
    if (pr->used_requests >= pr->quota_requests) {
        sec_audit_log(s, p, SEC_A_QUOTA_DENIED, SEC_ERR_QUOTA, now);
        return SEC_ERR_QUOTA;
    }
    pr->used_requests++;
    return SEC_OK;
}
