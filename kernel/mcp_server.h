#ifndef EXPERIMENTALOS_KERNEL_MCP_SERVER_H
#define EXPERIMENTALOS_KERNEL_MCP_SERVER_H

#include <stddef.h>
#include <stdint.h>
#include "jsonrpc.h"
#include "mcp_registry.h"

#define OS_MCP_PROTOCOL_LATEST "2025-06-18"
#define OS_MCP_PROTOCOL_PREVIOUS "2025-03-26"
#define OS_MCP_RESULT_TEXT_MAX 1024u
#define OS_MCP_ARGS_MAX 2048u
#define OS_MCP_MAX_CONSECUTIVE_INVALID 3u

#define OS_MCP_ERR_CODE_BUSY (-32001)
#define OS_MCP_ERR_CODE_LIFECYCLE (-32002)
#define OS_MCP_ERR_CODE_DENIED (-32003)
#define OS_MCP_ERR_CODE_TIMEOUT (-32004)
#define OS_MCP_ERR_CODE_METHOD (-32601)
#define OS_MCP_ERR_CODE_PARAMS (-32602)

enum os_mcp_conn_state { OS_MCP_CONN_NEW = 0, OS_MCP_CONN_INITIALIZED, OS_MCP_CONN_READY, OS_MCP_CONN_CLOSED };

/*
 * Tool handler: reads arguments from text[off..off+len), writes plain text.
 * Returns 0 success, 1 tool-level error (isError), -1 invalid arguments.
 */
typedef int (*os_mcp_tool_fn)(void *ctx, const char *text, size_t off, size_t len,
                              char *out, size_t cap);

typedef struct {
    os_mcp_tool_fn fn;
    void *ctx;
    uint32_t cost_ticks;
} os_mcp_binding;

typedef struct {
    os_mcp_registry *registry;
    os_mcp_binding bindings[OS_MCP_MAX_TOOLS];
    int state;
    uint32_t caller_caps;
    int owner_confirmed;
    uint32_t max_calls;
    uint32_t calls_used;
    uint32_t timeout_ticks;
    uint32_t invalid_streak;
    uint64_t next_nonce;
    int inflight;
    int inflight_tool;
    os_jrpc_id inflight_id;
    uint64_t inflight_start;
    char inflight_args[OS_MCP_ARGS_MAX];
    size_t inflight_args_len;
    uint32_t cancelled_count;
    uint32_t timed_out_count;
} os_mcp_server;

void os_mcp_server_init(os_mcp_server *s, os_mcp_registry *registry, uint32_t caller_caps,
                        uint32_t max_calls, uint32_t timeout_ticks);
int os_mcp_server_bind(os_mcp_server *s, int tool, os_mcp_tool_fn fn, void *ctx,
                       uint32_t cost_ticks);
/* Host-side, never reachable from the wire. */
void os_mcp_server_set_owner_confirmation(os_mcp_server *s, int confirmed);
/* New transport connection: session state and counters reset; registry approvals persist. */
void os_mcp_server_reset_connection(os_mcp_server *s);
/* Returns response length, 0 when no response is due, or <0 on closed/overflow. */
int os_mcp_server_handle(os_mcp_server *s, uint64_t now, const char *in, size_t in_len,
                         char *out, size_t cap);
/* Completes or times out the in-flight call; returns response length or 0. */
int os_mcp_server_poll(os_mcp_server *s, uint64_t now, char *out, size_t cap);

/* Escapes untrusted text for embedding in a JSON string (non-ASCII becomes '?'). */
size_t os_mcp_escape_text(const char *in, size_t n, char *out, size_t cap);

#endif
