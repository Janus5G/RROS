#ifndef EXPERIMENTALOS_KERNEL_SECDEPLOY_H
#define EXPERIMENTALOS_KERNEL_SECDEPLOY_H

#include <stddef.h>
#include <stdint.h>
#include "devpkg.h"
#include "secsvc.h"

/*
 * G20 secure package deployment between nodes (simulation only).
 *
 * Authenticated peer enrollment (pre-shared key), encrypted and replay-protected
 * transfer (ChaCha20 + HMAC-SHA256, encrypt-then-MAC), signed package
 * envelopes with key identity, validity window and revocation, anti-downgrade
 * version floors, recipient-side approval bound to the package digest, journalled
 * installation with automatic rollback, explicit rollback and crash recovery.
 *
 * Package signatures use shared-secret HMAC-SHA256 keys, not public-key
 * signatures, so a node that can verify can also forge. Installs update node
 * metadata only; no package payload is executed or written to storage here.
 * Nothing in this module touches a network or hardware.
 */

#define SECD_ID_MAX 16u
#define SECD_MAX_KEYS 4u
#define SECD_MAX_PEERS 4u
#define SECD_MAX_INSTALLED 6u
#define SECD_MAX_STAGED 2u
#define SECD_MAX_APPROVALS 2u
#define SECD_ENVELOPE_HEADER 20u
#define SECD_ENVELOPE_MAX (SECD_ENVELOPE_HEADER + OS_PKG_MAX_BYTES + 32u)
#define SECD_FRAME_HEADER 48u
#define SECD_FRAME_MAX (SECD_FRAME_HEADER + SECD_ENVELOPE_MAX + 32u)

enum secd_status {
    SECD_OK = 0, SECD_ERR_ARGUMENT = -1, SECD_ERR_DENIED = -2, SECD_ERR_PEER = -3, SECD_ERR_REPLAY = -4,
    SECD_ERR_AUTH = -5, SECD_ERR_FORMAT = -6, SECD_ERR_KEY = -7, SECD_ERR_SIGNATURE = -8,
    SECD_ERR_MANIFEST = -9, SECD_ERR_INTEGRITY = -10, SECD_ERR_COMPAT = -11, SECD_ERR_DEPENDENCY = -12,
    SECD_ERR_VERSION = -13, SECD_ERR_QUOTA = -14, SECD_ERR_APPROVAL = -15, SECD_ERR_STATE = -16,
    SECD_ERR_FAULT = -17, SECD_ERR_NOT_FOUND = -18, SECD_ERR_FULL = -19, SECD_ERR_POLICY = -20
};

enum secd_fault { SECD_FAULT_NONE = 0, SECD_FAULT_INTERRUPT_BEFORE, SECD_FAULT_CRASH_MID, SECD_FAULT_VERIFY_FAIL };

typedef struct {
    int used;
    uint8_t key_id[8];
    uint8_t key[16];
    uint64_t not_before, not_after;
    int revoked;
} secd_key;

typedef struct {
    int used;
    char id[SECD_ID_MAX];
    uint8_t psk[32];
    uint64_t last_seq;
    int revoked;
} secd_peer;

typedef struct {
    int used;
    char id[OS_PKG_ID_MAX];
    uint32_t version, previous, size_kb, previous_size_kb;
    uint8_t digest[32], previous_digest[32];
    uint8_t key_id[8];
    int pending;
} secd_installed;

typedef struct {
    int used;
    os_pkg_manifest manifest;
    uint8_t digest[32];
    uint8_t key_id[8];
} secd_staged;

typedef struct {
    int used;
    char id[OS_PKG_ID_MAX];
    uint32_t version;
    uint8_t digest[32];
    uint64_t expiry;
} secd_approval;

typedef struct {
    int used;
    char id[OS_PKG_ID_MAX];
    uint32_t version;
} secd_floor;

typedef struct {
    sec_system *sec;
    char id[SECD_ID_MAX];
    char arch[12];
    uint32_t quota_kb, used_kb;
    secd_key keys[SECD_MAX_KEYS];
    secd_peer peers[SECD_MAX_PEERS];
    secd_installed installed[SECD_MAX_INSTALLED];
    secd_staged staged[SECD_MAX_STAGED];
    secd_approval approvals[SECD_MAX_APPROVALS];
    secd_floor floors[SECD_MAX_INSTALLED];
    uint64_t send_seq;
    int rollback_authorized;
    secd_installed journal[SECD_MAX_INSTALLED];
    uint32_t journal_used_kb;
    int journal_valid;
    uint8_t scratch[SECD_ENVELOPE_MAX];
} secd_node;

int secd_node_init(secd_node *node, sec_system *sec, const char *id, const char *arch, uint32_t quota_kb);
void secd_key_id(const uint8_t key[16], uint8_t id[8]);
int secd_key_add(secd_node *node, const sec_token *token, const uint8_t key[16], uint64_t not_before,
                 uint64_t not_after, uint64_t now);
int secd_key_revoke(secd_node *node, const sec_token *token, const uint8_t key_id[8], uint64_t now);
int secd_peer_enroll(secd_node *node, const sec_token *token, const char *peer_id, const uint8_t psk[32],
                     uint64_t now);
int secd_peer_revoke(secd_node *node, const sec_token *token, const char *peer_id, uint64_t now);
int secd_set_rollback_policy(secd_node *node, const sec_token *token, int authorized, uint64_t now);

/* Signs an inner EOSPKG (unsigned or not) into a deployable envelope. */
int secd_envelope_build(const uint8_t *inner, size_t inner_len, const uint8_t key[16], uint8_t *out, size_t cap,
                        size_t *out_len);
int secd_send(secd_node *node, const sec_token *token, const char *recipient, const uint8_t *plain, size_t length,
              uint8_t *frame, size_t cap, size_t *frame_len, uint64_t now);
/* Authenticates, decrypts, verifies and stages a frame. Nothing is staged on any failure. */
int secd_receive(secd_node *node, const sec_token *token, const uint8_t *frame, size_t length, uint64_t now);
int secd_approve(secd_node *node, const sec_token *token, const char *id, uint32_t version, const uint8_t digest[32],
                 uint64_t expiry, uint64_t now);
int secd_install(secd_node *node, const sec_token *token, const char *id, uint32_t version, uint64_t now, int fault);
int secd_discard(secd_node *node, const sec_token *token, const char *id, uint32_t version, uint64_t now);
int secd_rollback(secd_node *node, const sec_token *token, const char *id, uint64_t now);
/* Restores the pre-install journal after an interrupted installation. */
int secd_recover(secd_node *node, const sec_token *token, uint64_t now);
int secd_find_installed(const secd_node *node, const char *id);
int secd_find_staged(const secd_node *node, const char *id, uint32_t version);
/* Installed packages signed by a key that is now revoked; reported, never silently removed. */
unsigned secd_count_revoked_installed(const secd_node *node);

#endif
