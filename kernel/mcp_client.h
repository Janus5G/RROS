#ifndef EXPERIMENTALOS_KERNEL_MCP_CLIENT_H
#define EXPERIMENTALOS_KERNEL_MCP_CLIENT_H

#include <stddef.h>
#include <stdint.h>

enum os_mcp_client_state {
    OS_MCP_CLIENT_NEW = 0, OS_MCP_CLIENT_INITIALIZING, OS_MCP_CLIENT_CONNECTED,
    OS_MCP_CLIENT_READY, OS_MCP_CLIENT_FAILED
};

enum os_mcp_client_status {
    OS_MCP_CLIENT_OK = 0,
    OS_MCP_CLIENT_ERR_STATE = -1,
    OS_MCP_CLIENT_ERR_ARGUMENT = -2,
    OS_MCP_CLIENT_ERR_MALFORMED = -3,
    OS_MCP_CLIENT_ERR_UNSOLICITED = -4,
    OS_MCP_CLIENT_ERR_VERSION = -5,
    OS_MCP_CLIENT_ERR_TIMEOUT = -6,
    OS_MCP_CLIENT_ERR_REMOTE = -7
};

typedef struct {
    int state;
    int64_t next_id;
    int64_t pending_id;
    int pending;
    int pending_is_initialize;
    uint64_t sent_at;
    uint32_t timeout_ticks;
    char protocol[24];
} os_mcp_client;

typedef struct {
    int is_error;
    int error_code;
    int is_tool_error;
    size_t result_offset;
    size_t result_length;
    int untrusted;
} os_mcp_client_result;

void os_mcp_client_init(os_mcp_client *c, uint32_t timeout_ticks);
int os_mcp_client_build_initialize(os_mcp_client *c, uint64_t now, char *out, size_t cap);
int os_mcp_client_build_initialized(os_mcp_client *c, char *out, size_t cap);
int os_mcp_client_build_list(os_mcp_client *c, uint64_t now, char *out, size_t cap);
/* arguments_json must be a JSON object text; it is validated before being sent. */
int os_mcp_client_build_call(os_mcp_client *c, uint64_t now, const char *tool,
                             const char *arguments_json, char *out, size_t cap);
int os_mcp_client_build_cancel(os_mcp_client *c, char *out, size_t cap);
/*
 * Validates a server message against the outstanding request. Results are
 * untrusted data: the client never acts on their content.
 */
int os_mcp_client_on_message(os_mcp_client *c, const char *text, size_t len,
                             os_mcp_client_result *res);
int os_mcp_client_check_timeout(os_mcp_client *c, uint64_t now);

#endif
