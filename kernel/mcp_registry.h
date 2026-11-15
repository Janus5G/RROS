#ifndef EXPERIMENTALOS_KERNEL_MCP_REGISTRY_H
#define EXPERIMENTALOS_KERNEL_MCP_REGISTRY_H

#include <stddef.h>
#include <stdint.h>

#define OS_MCP_MAX_TOOLS 12u
#define OS_MCP_TOOL_NAME_MAX 48u
#define OS_MCP_SCHEMA_MAX 512u
#define OS_MCP_NONCE_WINDOW 16u
#define OS_MCP_AUDIT_MAX 256u

enum os_mcp_status {
    OS_MCP_OK = 0,
    OS_MCP_ERR_ARGUMENT = -1,
    OS_MCP_ERR_NAME = -2,
    OS_MCP_ERR_DUPLICATE = -3,
    OS_MCP_ERR_FULL = -4,
    OS_MCP_ERR_NOT_FOUND = -5,
    OS_MCP_ERR_NOT_APPROVED = -6,
    OS_MCP_ERR_FINGERPRINT = -7,
    OS_MCP_ERR_EXPIRED = -8,
    OS_MCP_ERR_CAPABILITY = -9,
    OS_MCP_ERR_REPLAY = -10,
    OS_MCP_ERR_SIDE_EFFECT = -11
};

enum os_mcp_tool_state { OS_MCP_TOOL_UNUSED = 0, OS_MCP_TOOL_PENDING, OS_MCP_TOOL_APPROVED, OS_MCP_TOOL_REVOKED };

enum os_mcp_audit_event {
    OS_MCP_AUDIT_REGISTER = 1, OS_MCP_AUDIT_APPROVE, OS_MCP_AUDIT_REVOKE,
    OS_MCP_AUDIT_SCHEMA_CHANGE, OS_MCP_AUDIT_CALL_ALLOWED, OS_MCP_AUDIT_CALL_DENIED
};

typedef struct {
    uint8_t state;
    uint8_t side_effecting;
    char name[OS_MCP_TOOL_NAME_MAX];
    uint32_t version;
    uint32_t required_caps;
    uint16_t schema_len;
    char schema[OS_MCP_SCHEMA_MAX];
    uint8_t approved_fingerprint[32];
    uint64_t approval_expiry;
} os_mcp_tool;

typedef struct {
    uint32_t sequence;
    uint8_t event;
    int8_t tool;
    int16_t result;
    uint8_t hash[32];
} os_mcp_audit_entry;

typedef struct {
    os_mcp_tool tools[OS_MCP_MAX_TOOLS];
    uint64_t nonces[OS_MCP_NONCE_WINDOW];
    uint32_t nonce_next;
    os_mcp_audit_entry audit[OS_MCP_AUDIT_MAX];
    uint32_t audit_count;
    uint8_t audit_head[32];
} os_mcp_registry;

void os_mcp_registry_init(os_mcp_registry *r);
/* Tool identity: SHA-256 over a domain tag, name, version and exact schema text. */
void os_mcp_tool_fingerprint(const char *name, uint32_t version, const char *schema,
                             size_t schema_len, uint8_t out[32]);
int os_mcp_tool_register(os_mcp_registry *r, const char *name, uint32_t version,
                         const char *schema, size_t schema_len,
                         uint32_t required_caps, int side_effecting);
/* Owner-only operations: there is deliberately no wire method for these. */
int os_mcp_tool_approve(os_mcp_registry *r, int tool, const uint8_t expected[32],
                        uint64_t expiry);
int os_mcp_tool_revoke(os_mcp_registry *r, int tool);
int os_mcp_tool_update_schema(os_mcp_registry *r, int tool, const char *schema,
                              size_t schema_len);
/* Call gate: approved, unexpired, fingerprint intact, capabilities held, fresh nonce. */
int os_mcp_authorize_call(os_mcp_registry *r, const char *name, uint32_t caller_caps,
                          uint64_t now, uint64_t nonce, int owner_confirmed);
int os_mcp_audit_verify(const os_mcp_registry *r);

#endif
