#ifndef EXPERIMENTALOS_KERNEL_MCP_DEPLOY_H
#define EXPERIMENTALOS_KERNEL_MCP_DEPLOY_H

#include <stddef.h>
#include <stdint.h>
#include "mcp_registry.h"
#include "mcp_server.h"

#define OS_DM_MAX_PACKAGES 8u
#define OS_DM_MAX_NODES 4u
#define OS_DM_MAX_PLANS 6u
#define OS_DM_ID_MAX 32u
#define OS_DM_PAYLOAD_MAX 64u
#define OS_DM_TOOL_COUNT 10u

#define OS_DM_CAP_DISCOVER 0x001u
#define OS_DM_CAP_INSPECT 0x002u
#define OS_DM_CAP_BUILD 0x004u
#define OS_DM_CAP_VALIDATE 0x008u
#define OS_DM_CAP_EXPORT 0x010u
#define OS_DM_CAP_NODE_STATUS 0x020u
#define OS_DM_CAP_PROPOSE 0x040u
#define OS_DM_CAP_AUTH_REQUEST 0x080u
#define OS_DM_CAP_ROLLBACK 0x100u

enum os_dm_plan_state { OS_DM_PLAN_UNUSED = 0, OS_DM_PLAN_PROPOSED, OS_DM_PLAN_REQUESTED,
                        OS_DM_PLAN_APPROVED, OS_DM_PLAN_ACTIVE, OS_DM_PLAN_FAILED };
enum os_dm_fault { OS_DM_FAULT_NONE = 0, OS_DM_FAULT_INTERRUPT, OS_DM_FAULT_INSTALL_FAIL };

enum os_dm_status {
    OS_DM_OK = 0, OS_DM_ERR_ARGUMENT = -1, OS_DM_ERR_NOT_FOUND = -2, OS_DM_ERR_INTEGRITY = -3,
    OS_DM_ERR_COMPAT = -4, OS_DM_ERR_STATE = -5, OS_DM_ERR_BINDING = -6, OS_DM_ERR_EXPIRED = -7,
    OS_DM_ERR_FAULT = -8, OS_DM_ERR_POLICY = -9, OS_DM_ERR_FULL = -10
};

typedef struct {
    int used;
    char id[OS_DM_ID_MAX];
    uint32_t version;
    char arch[12];
    char dependency[OS_DM_ID_MAX];
    uint32_t size_kb;
    uint8_t payload[OS_DM_PAYLOAD_MAX];
    uint32_t payload_len;
    uint8_t digest[32];
    int signer;
    uint8_t signature[32];
    char exported_to[64];
} os_dm_package;

typedef struct {
    int used;
    char id[16];
    int enrolled;
    int online;
    char arch[12];
    char base[OS_DM_ID_MAX];
    uint32_t free_kb;
    int installed;
    int previous;
    int rollback_authorized;
} os_dm_node;

typedef struct {
    int state;
    int package;
    int node;
    uint8_t approved_digest[32];
    uint32_t approved_version;
    uint64_t expiry;
} os_dm_plan;

typedef struct {
    os_dm_package packages[OS_DM_MAX_PACKAGES];
    os_dm_node nodes[OS_DM_MAX_NODES];
    os_dm_plan plans[OS_DM_MAX_PLANS];
    uint8_t trusted_key[2][16];
    int trusted_key_set[2];
} os_dm_manager;

typedef struct {
    const char *name;
    uint32_t version;
    const char *schema;
    uint32_t caps;
    int side_effecting;
    uint32_t cost_ticks;
} os_dm_contract;

extern const os_dm_contract os_dm_contracts[OS_DM_TOOL_COUNT];

void os_dm_init(os_dm_manager *dm);

/* Registers the ten frozen contracts as PENDING tools and binds their handlers. */
int os_dm_register_tools(os_dm_manager *dm, os_mcp_registry *registry, os_mcp_server *server);

/* Separate service boundaries: none of these are reachable through MCP. */
int os_dm_provision_key(os_dm_manager *dm, int slot, const uint8_t key[16]);
int os_dm_sign_package(os_dm_manager *dm, const char *package_id, int slot);
/* Host-side bridge: tracks a real EOSPKG by binding its SHA-256 digest as the simulated payload. Unsigned until os_dm_sign_package. */
int os_dm_import_real_package(os_dm_manager *dm, const char *id, uint32_t version, const char *arch,
                              const char *dependency, uint32_t size_kb, const uint8_t real_digest[32]);
int os_dm_enroll_node(os_dm_manager *dm, const char *id, const char *arch, const char *base,
                      uint32_t free_kb);
int os_dm_set_rollback_policy(os_dm_manager *dm, const char *node_id, int authorized);
int os_dm_recipient_approve(os_dm_manager *dm, int plan, const uint8_t digest[32],
                            const char *node_id, uint32_t version, uint64_t expiry, uint64_t now);
int os_dm_activate(os_dm_manager *dm, int plan, uint64_t now, int fault);
int os_dm_package_verify(const os_dm_manager *dm, int package);
int os_dm_find_package(const os_dm_manager *dm, const char *id);
int os_dm_find_node(const os_dm_manager *dm, const char *id);

#endif
