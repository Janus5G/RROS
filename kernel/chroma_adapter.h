#ifndef EXPERIMENTALOS_KERNEL_CHROMA_ADAPTER_H
#define EXPERIMENTALOS_KERNEL_CHROMA_ADAPTER_H

/*
 * Versioned, least-authority adapter (v1) for interfaces read from the public
 * ChromaNeural source. It only verifies and frames untrusted data: it never
 * admits a node, assigns work, executes anything, opens a socket or changes
 * ChromaNeural's authentication, accounting, WorkQueue or authorization.
 */

#include <stddef.h>
#include <stdint.h>

#define OS_CN_ADAPTER_VERSION 1
#define OS_CN_PINNED_REVISION "3ce209ccd2aeb52094fbaade4429632eb17d3340"

/* CNRB v1 container (client/chroma/transport.py). */
#define OS_CN_CNRB_MAX 1048576u
#define OS_CN_CNRB_CHUNK 32768u
#define OS_CN_CNRB_HEADER 13u
#define OS_CN_CNRB_TAG 32u
#define OS_CN_CNRB_META_MAX 1024u
#define OS_CN_CNRB_MAX_BLOB (OS_CN_CNRB_MAX + 4096u)
#define OS_CN_FORMAT_MAX 12u

/* Observed ChromaNeural MCP client limits (client/chroma/mcp_client.py). */
#define OS_CN_MCP_MAX_SERVERS_PER_TASK 4u
#define OS_CN_MCP_MAX_TOOLS_PER_TASK 16u
#define OS_CN_MCP_MAX_DISCOVERED_TOOLS 32u
#define OS_CN_MCP_MAX_ARGS_BYTES 8192u
#define OS_CN_MCP_MAX_RESULT_BYTES 32768u
#define OS_CN_MCP_CALL_TIMEOUT_SECONDS 15u

enum os_cn_status {
    OS_CN_OK = 0,
    OS_CN_ERR_ARGUMENT = -1,
    OS_CN_ERR_IDENTITY = -2,
    OS_CN_ERR_FORMAT = -3,
    OS_CN_ERR_SIZE = -4,
    OS_CN_ERR_HEADER = -5,
    OS_CN_ERR_AUTH = -6,
    OS_CN_ERR_METADATA = -7,
    OS_CN_ERR_INTEGRITY = -8,
    OS_CN_ERR_CURSOR = -9,
    OS_CN_ERR_LIMIT = -10
};

/* ChromaNode identity: nodeId = hex(sha256(raw 32-byte Ed25519 public key)). */
int os_cn_identity_verify(const char *node_id_hex, const char *public_key_base64);
int os_cn_identity_from_document(const char *text, size_t length);

int os_cn_format_valid(const char *format);

int os_cn_cnrb_pack(const uint8_t *data, size_t length, const char *format, const uint8_t key[32],
                    uint8_t *out, size_t capacity);
int os_cn_cnrb_unpack(const uint8_t *blob, size_t length, const uint8_t key[32],
                      const uint8_t **data, size_t *data_length, char format[OS_CN_FORMAT_MAX]);

/* In-memory resumable quarantine, mirroring Receiver in transport.py. */
typedef struct {
    uint8_t *buffer;
    size_t capacity;
    size_t size;
    size_t cursor;
    char digest[65];
} os_cn_receiver;

int os_cn_receiver_init(os_cn_receiver *r, uint8_t *buffer, size_t capacity, size_t size, const char *digest_hex);
int os_cn_receiver_append(os_cn_receiver *r, size_t offset, const uint8_t *data, size_t length);
int os_cn_receiver_finish(os_cn_receiver *r, const uint8_t key[32], const uint8_t **data,
                          size_t *data_length, char format[OS_CN_FORMAT_MAX]);

/* MCP client limit profile; results are untrusted text. */
int os_cn_mcp_task_limits_ok(unsigned servers, unsigned approved_tools);
int os_cn_mcp_args_ok(size_t canonical_args_bytes);
int os_cn_mcp_result_ok(const char *text, size_t length);

#endif
