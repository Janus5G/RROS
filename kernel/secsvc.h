#ifndef EXPERIMENTALOS_KERNEL_SECSVC_H
#define EXPERIMENTALOS_KERNEL_SECSVC_H

#include <stddef.h>
#include <stdint.h>

/*
 * G20 security service: accounts, roles, service identities, sessions, quotas,
 * credential protection and a hash-chained audit log. No allocation, no
 * hardware access. Randomness is never generated here: salts, session secrets
 * and challenge nonces come from caller-supplied entropy, because no verified
 * hardware entropy source exists yet (HARDWARE_NOT_VERIFIED). Passwords are
 * stored only as salted PBKDF2-HMAC-SHA256 hashes.
 */

#define SEC_MAX_PRINCIPALS 12u
#define SEC_MAX_SESSIONS 8u
#define SEC_NAME_MAX 24u
#define SEC_PASSWORD_MIN 8u
#define SEC_PASSWORD_MAX 64u
#define SEC_PBKDF_ITERATIONS 1000u
#define SEC_MAX_FAILURES 5u
#define SEC_LOCKOUT_TICKS 300u
#define SEC_SESSION_TTL 600u
#define SEC_NONCE_TTL 30u
#define SEC_WINDOW_TICKS 100u
#define SEC_AUDIT_CAP 64u

enum sec_role { SEC_ROLE_NONE = 0, SEC_ROLE_GUEST = 1, SEC_ROLE_USER = 2, SEC_ROLE_OPERATOR = 3,
                SEC_ROLE_ADMIN = 4, SEC_ROLE_OWNER = 5, SEC_ROLE_SERVICE = 10 };

#define SEC_PERM_READ 0x001u
#define SEC_PERM_WRITE 0x002u
#define SEC_PERM_REQUEST_INSTALL 0x004u
#define SEC_PERM_APPROVE_INSTALL 0x008u
#define SEC_PERM_MANAGE_ACCOUNTS 0x010u
#define SEC_PERM_MANAGE_KEYS 0x020u
#define SEC_PERM_MANAGE_SERVICES 0x040u
#define SEC_PERM_VIEW_AUDIT 0x080u
#define SEC_PERM_ROLLBACK 0x100u
#define SEC_PERM_RECOVER 0x200u
#define SEC_PERM_SEND_PACKAGE 0x400u
#define SEC_PERM_ALL 0x7FFu
/* The most a service identity can ever hold, whatever its registration asks for. */
#define SEC_SERVICE_PERM_CEILING (SEC_PERM_READ | SEC_PERM_WRITE | SEC_PERM_REQUEST_INSTALL | SEC_PERM_SEND_PACKAGE)

enum sec_status {
    SEC_OK = 0, SEC_ERR_ARGUMENT = -1, SEC_ERR_DENIED = -2, SEC_ERR_AUTH = -3, SEC_ERR_LOCKED = -4,
    SEC_ERR_EXISTS = -5, SEC_ERR_NOT_FOUND = -6, SEC_ERR_FULL = -7, SEC_ERR_QUOTA = -8,
    SEC_ERR_EXPIRED = -9, SEC_ERR_REPLAY = -10, SEC_ERR_STATE = -11, SEC_ERR_WEAK = -12
};

enum sec_action {
    SEC_A_BOOTSTRAP = 1, SEC_A_LOGIN_OK, SEC_A_LOGIN_FAIL, SEC_A_LOGIN_LOCKED, SEC_A_LOGOUT, SEC_A_DENIED,
    SEC_A_ACCOUNT_CREATE, SEC_A_ROLE_CHANGE, SEC_A_DISABLE, SEC_A_PASSWORD_CHANGE, SEC_A_SERVICE_REGISTER,
    SEC_A_SERVICE_AUTH_OK, SEC_A_SERVICE_AUTH_FAIL, SEC_A_QUOTA_DENIED, SEC_A_KEY_ADD, SEC_A_KEY_REVOKE,
    SEC_A_PEER_ENROLL, SEC_A_PEER_REVOKE, SEC_A_FRAME_REJECT, SEC_A_PKG_STAGE, SEC_A_PKG_REJECT,
    SEC_A_APPROVE, SEC_A_INSTALL_OK, SEC_A_INSTALL_FAIL, SEC_A_ROLLBACK, SEC_A_RECOVER, SEC_A_SEND
};

typedef struct {
    uint32_t slot;
    uint8_t secret[16];
} sec_token;

typedef struct {
    int used;
    char name[SEC_NAME_MAX];
    int role;
    int is_service;
    int disabled;
    uint8_t salt[16];
    uint8_t secret_hash[32];   /* PBKDF2 hash for accounts, HMAC key for services; never readable through the API */
    uint32_t failures;
    uint64_t locked_until;
    uint32_t service_perms;
    uint32_t quota_storage_kb, used_storage_kb;
    uint32_t quota_requests, used_requests;
    uint64_t window_start;
    int nonce_pending;
    uint8_t nonce[16];
    uint64_t nonce_expiry;
} sec_principal;

typedef struct {
    int used;
    int principal;
    uint8_t secret_mac[32];
    uint64_t expiry;
} sec_session;

typedef struct {
    uint64_t seq, tick;
    int32_t actor;
    uint32_t action;
    int32_t result;
    uint8_t chain[32];
} sec_audit_entry;

typedef struct {
    sec_principal principals[SEC_MAX_PRINCIPALS];
    sec_session sessions[SEC_MAX_SESSIONS];
    uint8_t server_key[32];
    sec_audit_entry audit[SEC_AUDIT_CAP];
    uint8_t audit_anchor[32];
    uint64_t audit_total;
    int bootstrapped;
} sec_system;

void sec_init(sec_system *s, const uint8_t server_key[32]);
/* Creates the single owner. Refused once any principal exists. */
int sec_bootstrap_owner(sec_system *s, const char *name, const char *password, const uint8_t salt[16], uint64_t now);
int sec_login(sec_system *s, const char *name, const char *password, const uint8_t entropy[16], uint64_t now,
              sec_token *out);
int sec_logout(sec_system *s, const sec_token *token, uint64_t now);
/* Returns the principal index for a valid, unexpired session, otherwise a negative status. */
int sec_session_principal(sec_system *s, const sec_token *token, uint64_t now);
int sec_session_role(sec_system *s, const sec_token *token, uint64_t now);
uint32_t sec_role_permissions(int role);
/* SEC_OK when the session holds the permission; denials are audited. */
int sec_authorize(sec_system *s, const sec_token *token, uint32_t perm, uint64_t now);

int sec_account_create(sec_system *s, const sec_token *actor, const char *name, const char *password, int role,
                       const uint8_t salt[16], uint64_t now);
int sec_account_set_role(sec_system *s, const sec_token *actor, const char *name, int role, uint64_t now);
int sec_account_set_disabled(sec_system *s, const sec_token *actor, const char *name, int disabled, uint64_t now);
int sec_change_password(sec_system *s, const sec_token *token, const char *old_password, const char *new_password,
                        const uint8_t salt[16], uint64_t now);

int sec_service_register(sec_system *s, const sec_token *actor, const char *name, const uint8_t key[32],
                         uint32_t perms, uint64_t now);
int sec_service_challenge(sec_system *s, const char *name, const uint8_t entropy[16], uint64_t now, uint8_t nonce_out[16]);
int sec_service_authenticate(sec_system *s, const char *name, const uint8_t response[32], const uint8_t entropy[16],
                             uint64_t now, sec_token *out);
void sec_service_response(const uint8_t key[32], const uint8_t nonce[16], const char *name, uint8_t out[32]);

int sec_quota_set(sec_system *s, const sec_token *actor, const char *name, uint32_t storage_kb, uint32_t requests,
                  uint64_t now);
int sec_charge_storage(sec_system *s, const sec_token *token, uint32_t kb, uint64_t now);
int sec_release_storage(sec_system *s, const sec_token *token, uint32_t kb, uint64_t now);
int sec_charge_request(sec_system *s, const sec_token *token, uint64_t now);

void sec_audit_log(sec_system *s, int actor, uint32_t action, int result, uint64_t now);
uint64_t sec_audit_total(const sec_system *s);
unsigned sec_audit_retained(const sec_system *s);
/* Entry by retained index, 0 = oldest retained. */
const sec_audit_entry *sec_audit_get(const sec_system *s, unsigned index);
/* Recomputes the hash chain over the retained entries. 1 when intact. */
int sec_audit_verify(const sec_system *s);
int sec_find_principal(const sec_system *s, const char *name);

/* Exposed for known-answer tests. */
void sec_pbkdf2_sha256(const uint8_t *password, size_t password_len, const uint8_t *salt, size_t salt_len,
                       uint32_t iterations, uint8_t out[32]);

#endif
