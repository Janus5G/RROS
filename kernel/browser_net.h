#ifndef EXPERIMENTALOS_KERNEL_BROWSER_NET_H
#define EXPERIMENTALOS_KERNEL_BROWSER_NET_H

#include <stddef.h>
#include <stdint.h>

/*
 * Browser networking v1 (pure logic): strict http(s) URL handling, an HTTP/1.1 GET request builder and a
 * bounded response parser, and a policy-checked fetch over a caller-supplied transport. There is no socket
 * or TLS code here: the transport is an injected callback, and with an empty allow-list every fetch is denied.
 * Compatibility limits: GET only, Content-Length bodies only (no chunked/compressed), no cookies, no TLS.
 */
#define OS_URL_MAX 255u
#define OS_URL_HOST_MAX 64u
#define OS_URL_PATH_MAX 192u
#define OS_HTTP_HEADER_MAX 2048u
#define OS_NET_ALLOW_MAX 4u
#define OS_NET_MAX_REDIRECTS 3u

enum os_net_status {
    OS_NET_OK = 0, OS_NET_ERR_ARGUMENT = -1, OS_NET_ERR_URL = -2, OS_NET_ERR_SCHEME = -3, OS_NET_ERR_RESPONSE = -4,
    OS_NET_ERR_UNSUPPORTED = -5, OS_NET_ERR_DENIED = -6, OS_NET_ERR_TRANSPORT = -7, OS_NET_ERR_TLS = -8,
    OS_NET_ERR_REDIRECTS = -9, OS_NET_ERR_SIZE = -10
};

typedef struct {
    uint8_t https;
    uint16_t port;
    char host[OS_URL_HOST_MAX];
    char path[OS_URL_PATH_MAX];
} os_url;

typedef struct {
    int status;
    char content_type[48];
    char location[OS_URL_PATH_MAX];
    size_t body_off;
    size_t body_len;
} os_http_response;

typedef int (*os_net_transport)(void *ctx, const os_url *url, const char *request, size_t request_len,
                                uint8_t *response, size_t capacity, size_t *response_len);

typedef struct {
    char allowed_host[OS_NET_ALLOW_MAX][OS_URL_HOST_MAX];
    unsigned allowed_count;
    uint32_t max_response;
} os_net_policy;

int os_url_parse(const char *text, os_url *url);
int os_url_resolve(const os_url *base, const char *ref, os_url *out);
int os_url_format(const os_url *url, char *out, size_t capacity);
int os_http_build_request(const os_url *url, char *out, size_t capacity);
int os_http_parse_response(const uint8_t *buffer, size_t length, os_http_response *response);

void os_net_policy_init(os_net_policy *policy, uint32_t max_response);
int os_net_policy_allow(os_net_policy *policy, const char *host);
/* Fetches url (following at most 3 policy-checked redirects). *url is updated to the final location. */
int os_net_fetch(const os_net_policy *policy, os_net_transport transport, void *ctx, os_url *url,
                 uint8_t *buffer, size_t capacity, os_http_response *response);

#endif
