#ifndef EXPERIMENTALOS_KERNEL_DEVPKG_H
#define EXPERIMENTALOS_KERNEL_DEVPKG_H

#include <stddef.h>
#include <stdint.h>
#include "eosvm.h"
#include "storage.h"

/*
 * EOSPKG v1 package container and local package manager.
 *
 *   "EPK1" | u16 format | u16 manifest_len | u32 payload_len | u8 signer | 3x0 |
 *   manifest JSON | payload | SHA-256 over all preceding bytes | HMAC-SHA256(key, digest) or 32x0
 *
 * The manifest (configuration JSON) is separate from the payload (executable or
 * resource artifact) and binds it through payload_sha256. Signatures use a
 * provisioned symmetric key (HMAC-SHA256); they are not public-key signatures.
 * Installation needs a recipient-side approval bound to the exact package digest.
 */
#define OS_PKG_MAGIC "EPK1"
#define OS_PKG_FORMAT 1u
#define OS_PKG_HEADER 16u
#define OS_PKG_MANIFEST_MAX 512u
#define OS_PKG_PAYLOAD_MAX 4096u
#define OS_PKG_MAX_BYTES (OS_PKG_HEADER + OS_PKG_MANIFEST_MAX + OS_PKG_PAYLOAD_MAX + 64u)
#define OS_PKG_ID_MAX 32u
#define OS_PKG_KEY_SLOTS 2u
#define OS_PKG_MAX_INSTALLED 8u
#define OS_PKG_MAX_APPROVALS 4u

enum os_pkg_kind { OS_PKG_KIND_EOSVM = 1, OS_PKG_KIND_APP_IMAGE = 2 };

enum os_pkg_status {
    OS_PKG_OK = 0, OS_PKG_ERR_ARGUMENT = -1, OS_PKG_ERR_FORMAT = -2, OS_PKG_ERR_MANIFEST = -3,
    OS_PKG_ERR_INTEGRITY = -4, OS_PKG_ERR_SIGNATURE = -5, OS_PKG_ERR_COMPAT = -6,
    OS_PKG_ERR_DEPENDENCY = -7, OS_PKG_ERR_APPROVAL = -8, OS_PKG_ERR_VERSION = -9,
    OS_PKG_ERR_STORAGE = -10, OS_PKG_ERR_NOT_FOUND = -11, OS_PKG_ERR_KIND = -12,
    OS_PKG_ERR_FULL = -13, OS_PKG_ERR_EXPIRED = -14
};

typedef struct {
    char id[OS_PKG_ID_MAX];
    uint32_t version;
    char arch[12];             /* "x86_64", "aarch64" or "any" */
    int kind;
    char dependency[OS_PKG_ID_MAX]; /* empty when none */
    uint32_t min_abi;
    uint32_t size_kb;
    uint8_t payload_sha256[32];
} os_pkg_manifest;

typedef struct {
    os_pkg_manifest manifest;
    int signer;                /* 0 = unsigned, otherwise key slot 1..OS_PKG_KEY_SLOTS */
    uint8_t digest[32];
    uint8_t signature[32];
    const uint8_t *payload;
    size_t payload_len;
} os_pkg_view;

/* Builds an UNSIGNED package; the manifest payload hash and size are computed here. */
int os_pkg_build(const os_pkg_manifest *manifest, const uint8_t *payload, size_t payload_len,
                 uint8_t *out, size_t out_cap, size_t *out_len);
int os_pkg_sign(uint8_t *package, size_t length, int slot, const uint8_t *key, size_t key_len);
/* Strict structural, manifest, payload-hash and digest verification; does not check the signature. */
int os_pkg_inspect(const uint8_t *package, size_t length, os_pkg_view *view);
int os_pkg_verify_signature(const os_pkg_view *view, const uint8_t *key, size_t key_len);

typedef struct {
    int used;
    char id[OS_PKG_ID_MAX];
    uint32_t version;
    uint32_t previous;         /* version replaced by the last upgrade, 0 when none */
    uint8_t digest[32];
    uint8_t payload_sha256[32];
    int kind;
} os_pkg_installed;

typedef struct {
    int used;
    char id[OS_PKG_ID_MAX];
    uint32_t version;
    uint8_t digest[32];
    uint64_t expiry;
} os_pkg_approval;

typedef struct {
    os_storage_fs *fs;
    char arch[12];
    uint32_t abi;
    uint8_t keys[OS_PKG_KEY_SLOTS][16];
    int key_set[OS_PKG_KEY_SLOTS];
    int rollback_authorized;
    os_pkg_installed installed[OS_PKG_MAX_INSTALLED];
    os_pkg_approval approvals[OS_PKG_MAX_APPROVALS];
} os_pkg_manager;

int os_pkg_manager_init(os_pkg_manager *mgr, os_storage_fs *fs, const char *arch, uint32_t abi);
int os_pkg_provision_key(os_pkg_manager *mgr, int slot, const uint8_t key[16]);
/* Loads the persisted installed-version index; a missing index is empty, a corrupt one fails. */
int os_pkg_load_state(os_pkg_manager *mgr);
/* Offline import: verifies signature and compatibility, then stages "pkg/<id>-<version>". */
int os_pkg_import(os_pkg_manager *mgr, const uint8_t *package, size_t length);
/* Offline export: copies a staged package to "out/<id>-<version>" for removable media. */
int os_pkg_export(os_pkg_manager *mgr, const char *id, uint32_t version);
/* Recipient-side approval, bound to the staged package digest. Not exposed over MCP. */
int os_pkg_approve(os_pkg_manager *mgr, const char *id, uint32_t version, const uint8_t digest[32],
                   uint64_t expiry, uint64_t now);
int os_pkg_set_rollback_policy(os_pkg_manager *mgr, int authorized);
int os_pkg_install(os_pkg_manager *mgr, const char *id, uint32_t version, uint64_t now);
int os_pkg_find_installed(const os_pkg_manager *mgr, const char *id);
/* Runs an installed EOSVM package after re-verifying the stored payload against its recorded hash. */
int os_pkg_run(os_pkg_manager *mgr, const char *id, uint32_t fuel, os_eosvm_result *result);

#endif
