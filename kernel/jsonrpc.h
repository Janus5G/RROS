#ifndef EXPERIMENTALOS_KERNEL_JSONRPC_H
#define EXPERIMENTALOS_KERNEL_JSONRPC_H

#include <stddef.h>
#include <stdint.h>

#define OS_JRPC_MAX_MESSAGE 8192u
#define OS_JRPC_MAX_DEPTH 8u
#define OS_JRPC_MAX_OBJECT_KEYS 32u
#define OS_JRPC_MAX_STRING 1024u
#define OS_JRPC_METHOD_MAX 64u
#define OS_JRPC_ID_MAX 64u

#define OS_JRPC_OK 0
#define OS_JRPC_PARSE_ERROR (-32700)
#define OS_JRPC_INVALID_REQUEST (-32600)

enum os_jrpc_id_kind { OS_JRPC_ID_NONE = 0, OS_JRPC_ID_NUMBER, OS_JRPC_ID_STRING };

typedef struct {
    int kind;
    int64_t number;
    char string[OS_JRPC_ID_MAX];
} os_jrpc_id;

typedef struct {
    char method[OS_JRPC_METHOD_MAX];
    os_jrpc_id id;
    int is_notification;
    int has_params;
    size_t params_offset;
    size_t params_length;
} os_jrpc_request;

/*
 * Strict JSON-RPC 2.0 request validation for MCP: one object, no batches, no
 * duplicate keys, bounded depth/size, valid UTF-8, id never null, params an
 * object when present, unknown top-level members rejected.
 */
int os_jrpc_parse_request(const char *text, size_t length, os_jrpc_request *out);
/* Top-level member readers for a validated params object. Strings with escapes are refused. */
int os_jrpc_member_string(const char *text, size_t off, size_t len, const char *key,
                          char *out, size_t cap);
int os_jrpc_member_int(const char *text, size_t off, size_t len, const char *key, int64_t *out);
int os_jrpc_has_member(const char *text, size_t off, size_t len, const char *key);
int os_jrpc_member_bool(const char *text, size_t off, size_t len, const char *key, int *out);
/* Span of an object-valued top-level member (offset and length within text). */
int os_jrpc_member_object(const char *text, size_t off, size_t len, const char *key,
                          size_t *obj_off, size_t *obj_len);
/* True when text is exactly one valid JSON value within the parser's bounds. */
int os_jrpc_validate_document(const char *text, size_t length);

#endif
