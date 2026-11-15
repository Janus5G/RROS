#ifndef EXPERIMENTALOS_KERNEL_ROBOFORGE_ADAPTER_H
#define EXPERIMENTALOS_KERNEL_ROBOFORGE_ADAPTER_H

/*
 * Optional, opt-in client-side adapter for the RoboForge MCP gateway.
 * RoboForge is an external local service: it is never exposed, proxied or
 * used as the remote Pi transfer service, and nothing here grants authority.
 */

#include <stddef.h>
#include <stdint.h>
#include "mcp_client.h"

#define OS_RF_PINNED_REVISION "b0dc46f860faedb6f00a36b2c636a2b31c2b290e"
#define OS_RF_MAX_REQUEST 12288u
#define OS_RF_MAX_REPLY 32768u
#define OS_RF_MODULE_ID_MAX 64u
#define OS_RF_SOURCE_MAX 4096u

enum os_rf_tool { OS_RF_TOOL_CATALOG = 0, OS_RF_TOOL_STATUS, OS_RF_TOOL_REFRACT, OS_RF_TOOL_COUNT };

enum os_rf_status {
    OS_RF_OK = 0,
    OS_RF_ERR_ARGUMENT = -1,
    OS_RF_ERR_DISABLED = -2,
    OS_RF_ERR_TOOL = -3,
    OS_RF_ERR_SCHEMA = -4,
    OS_RF_ERR_TRANSPORT = -5,
    OS_RF_ERR_FINGERPRINT = -6,
    OS_RF_ERR_REPLY = -7
};

typedef struct {
    const char *name;
    int controlled; /* the gateway treats it as a controlled (non metadata-only) tool */
} os_rf_contract;

typedef struct {
    int enabled;        /* owner opt-in; off by default */
    int refract_opt_in; /* separate owner opt-in for the compile preview */
    int tools_verified; /* set only after the tools/list fingerprint matched */
} os_rf_policy;

const os_rf_contract *os_rf_contract_at(int index);
int os_rf_tool_from_name(const char *name);

void os_rf_policy_init(os_rf_policy *p);

/* Loopback-only transport rules mirroring the gateway: any NULL header is "absent". */
int os_rf_transport_allowed(const os_rf_policy *p, int remote_is_loopback, const char *host,
                            const char *origin, const char *sec_fetch_site);
int os_rf_http_headers_ok(const char *content_type, const char *accept,
                          const char *session_id, const char *protocol_version);

/* Verifies a tools/list reply against the frozen, pinned definition set. */
int os_rf_verify_tools_list(os_rf_policy *p, const char *text, size_t len);
int os_rf_tools_fingerprint_hex(const char *text, size_t len, char hex[65]);

/*
 * Builds a tools/call request after validating the parameter: module id for
 * OS_RF_TOOL_STATUS, source text for OS_RF_TOOL_REFRACT, ignored for catalog.
 */
int os_rf_build_call(os_mcp_client *c, const os_rf_policy *p, uint64_t now, int tool,
                     const char *param, char *out, size_t cap);

/* Bounded, fail-closed check of a reply before the client consumes it. */
int os_rf_check_reply_size(size_t len);

#endif
