#include "chroma_adapter.h"
#include "hmac_sha256.h"
#include "jsonrpc.h"
#include "sha256.h"

static const char *const formats[] = { "raw", "cpl", "cpa", "prisme-asm", "optb", 0 };
static const char hexd[] = "0123456789abcdef";

static size_t cstr_len(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void to_hex(const uint8_t *d, size_t n, char *out)
{
    for (size_t i = 0; i < n; i++) { out[2 * i] = hexd[d[i] >> 4]; out[2 * i + 1] = hexd[d[i] & 15u]; }
    out[2 * n] = 0;
}

static int b64_value(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/* Canonical base64 of exactly 32 bytes: 43 characters plus one '=' pad. */
static int b64_decode_32(const char *s, uint8_t out[32])
{
    if (cstr_len(s) != 44u || s[43] != '=') return 0;
    size_t o = 0;
    for (size_t i = 0; i < 40; i += 4) {
        int a = b64_value(s[i]), b = b64_value(s[i + 1]), c = b64_value(s[i + 2]), d = b64_value(s[i + 3]);
        if (a < 0 || b < 0 || c < 0 || d < 0) return 0;
        out[o++] = (uint8_t)((a << 2) | (b >> 4));
        out[o++] = (uint8_t)(((b & 15) << 4) | (c >> 2));
        out[o++] = (uint8_t)(((c & 3) << 6) | d);
    }
    int a = b64_value(s[40]), b = b64_value(s[41]), c = b64_value(s[42]);
    if (a < 0 || b < 0 || c < 0 || (c & 3) != 0) return 0;
    out[30] = (uint8_t)((a << 2) | (b >> 4));
    out[31] = (uint8_t)(((b & 15) << 4) | (c >> 2));
    return 1;
}

int os_cn_identity_verify(const char *node_id_hex, const char *public_key_base64)
{
    uint8_t raw[32], digest[32];
    char hex[65];
    if (!node_id_hex || !public_key_base64) return OS_CN_ERR_ARGUMENT;
    if (!b64_decode_32(public_key_base64, raw)) return OS_CN_ERR_IDENTITY;
    os_sha256_hash(raw, sizeof raw, digest);
    to_hex(digest, sizeof digest, hex);
    return str_eq(hex, node_id_hex) ? OS_CN_OK : OS_CN_ERR_IDENTITY;
}

int os_cn_identity_from_document(const char *text, size_t length)
{
    char id[80], key[80];
    if (!text || length > 4096u || !os_jrpc_validate_document(text, length)) return OS_CN_ERR_IDENTITY;
    if (!os_jrpc_member_string(text, 0, length, "nodeId", id, sizeof id) ||
        !os_jrpc_member_string(text, 0, length, "publicKeyBase64", key, sizeof key))
        return OS_CN_ERR_IDENTITY;
    return os_cn_identity_verify(id, key);
}

int os_cn_format_valid(const char *format)
{
    if (!format) return 0;
    for (int i = 0; formats[i]; i++)
        if (str_eq(formats[i], format)) return 1;
    return 0;
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static uint32_t get_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/* Writes {"format":"..","length":N,"sha256":".."}; returns length or 0 on overflow. */
static size_t canonical_meta(char *out, size_t cap, const char *format, size_t length, const uint8_t *data)
{
    char num[12], hex[65];
    uint8_t digest[32];
    size_t n = 0, d = 0;
    const char *parts[] = { "{\"format\":\"", format, "\",\"length\":", 0, ",\"sha256\":\"", hex, "\"}" };
    os_sha256_hash(data, length, digest);
    to_hex(digest, sizeof digest, hex);
    {
        char tmp[12];
        size_t t = 0;
        size_t v = length;
        do { tmp[t++] = (char)('0' + v % 10u); v /= 10u; } while (v && t < sizeof tmp);
        while (t) num[d++] = tmp[--t];
        num[d] = 0;
    }
    parts[3] = num;
    for (size_t i = 0; i < sizeof parts / sizeof parts[0]; i++)
        for (const char *s = parts[i]; *s; s++) {
            if (n + 1u >= cap) return 0;
            out[n++] = *s;
        }
    out[n] = 0;
    return n;
}

int os_cn_cnrb_pack(const uint8_t *data, size_t length, const char *format, const uint8_t key[32],
                    uint8_t *out, size_t capacity)
{
    char meta[OS_CN_CNRB_META_MAX + 1];
    uint8_t tag[32];
    if (!data || !key || !out) return OS_CN_ERR_ARGUMENT;
    if (length > OS_CN_CNRB_MAX) return OS_CN_ERR_SIZE;
    if (!os_cn_format_valid(format)) return OS_CN_ERR_FORMAT;
    size_t mlen = canonical_meta(meta, sizeof meta, format, length, data);
    if (!mlen) return OS_CN_ERR_METADATA;
    size_t total = OS_CN_CNRB_HEADER + mlen + length + OS_CN_CNRB_TAG;
    if (capacity < total) return OS_CN_ERR_SIZE;
    out[0] = 'C'; out[1] = 'N'; out[2] = 'R'; out[3] = 'B'; out[4] = 1;
    put_be32(out + 5, (uint32_t)mlen);
    put_be32(out + 9, (uint32_t)length);
    for (size_t i = 0; i < mlen; i++) out[OS_CN_CNRB_HEADER + i] = (uint8_t)meta[i];
    for (size_t i = 0; i < length; i++) out[OS_CN_CNRB_HEADER + mlen + i] = data[i];
    os_hmac_sha256(key, 32, out, total - OS_CN_CNRB_TAG, tag);
    for (size_t i = 0; i < OS_CN_CNRB_TAG; i++) out[total - OS_CN_CNRB_TAG + i] = tag[i];
    return (int)total;
}

int os_cn_cnrb_unpack(const uint8_t *blob, size_t length, const uint8_t key[32],
                      const uint8_t **data, size_t *data_length, char format[OS_CN_FORMAT_MAX])
{
    uint8_t tag[32];
    char expected[OS_CN_CNRB_META_MAX + 1];
    if (!blob || !key || !data || !data_length || !format) return OS_CN_ERR_ARGUMENT;
    if (length > OS_CN_CNRB_MAX_BLOB || length < OS_CN_CNRB_HEADER + OS_CN_CNRB_TAG) return OS_CN_ERR_SIZE;
    uint32_t mlen = get_be32(blob + 5), dlen = get_be32(blob + 9);
    if (blob[0] != 'C' || blob[1] != 'N' || blob[2] != 'R' || blob[3] != 'B' || blob[4] != 1u ||
        mlen < 1u || mlen > OS_CN_CNRB_META_MAX || dlen > OS_CN_CNRB_MAX)
        return OS_CN_ERR_HEADER;
    if (length != (size_t)OS_CN_CNRB_HEADER + mlen + dlen + OS_CN_CNRB_TAG) return OS_CN_ERR_HEADER;
    os_hmac_sha256(key, 32, blob, length - OS_CN_CNRB_TAG, tag);
    if (!os_hmac_tag_equal(tag, blob + length - OS_CN_CNRB_TAG)) return OS_CN_ERR_AUTH;
    const uint8_t *meta = blob + OS_CN_CNRB_HEADER;
    const uint8_t *payload = meta + mlen;
    for (int i = 0; formats[i]; i++) {
        size_t n = canonical_meta(expected, sizeof expected, formats[i], dlen, payload);
        if (n == mlen) {
            size_t j = 0;
            while (j < n && (uint8_t)expected[j] == meta[j]) j++;
            if (j == n) {
                for (size_t k = 0; k <= cstr_len(formats[i]); k++) format[k] = formats[i][k];
                *data = payload;
                *data_length = dlen;
                return OS_CN_OK;
            }
        }
    }
    return OS_CN_ERR_METADATA;
}

static int hex64(const char *s)
{
    if (cstr_len(s) != 64u) return 0;
    for (size_t i = 0; i < 64u; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return 0;
    return 1;
}

int os_cn_receiver_init(os_cn_receiver *r, uint8_t *buffer, size_t capacity, size_t size, const char *digest_hex)
{
    if (!r || !buffer || !digest_hex) return OS_CN_ERR_ARGUMENT;
    if (size == 0u || size > OS_CN_CNRB_MAX_BLOB || size > capacity) return OS_CN_ERR_SIZE;
    if (!hex64(digest_hex)) return OS_CN_ERR_INTEGRITY;
    r->buffer = buffer;
    r->capacity = capacity;
    r->size = size;
    r->cursor = 0;
    for (size_t i = 0; i <= 64u; i++) r->digest[i] = digest_hex[i];
    return OS_CN_OK;
}

int os_cn_receiver_append(os_cn_receiver *r, size_t offset, const uint8_t *data, size_t length)
{
    if (!r || !data || length == 0u || length > OS_CN_CNRB_CHUNK) return OS_CN_ERR_LIMIT;
    if (offset < r->cursor) {
        /* An exact retransmission of bytes already held is idempotent. */
        if (offset + length > r->cursor) return OS_CN_ERR_CURSOR;
        for (size_t i = 0; i < length; i++)
            if (r->buffer[offset + i] != data[i]) return OS_CN_ERR_CURSOR;
        return (int)r->cursor;
    }
    if (offset != r->cursor || length > r->size - r->cursor) return OS_CN_ERR_CURSOR;
    for (size_t i = 0; i < length; i++) r->buffer[offset + i] = data[i];
    r->cursor += length;
    return (int)r->cursor;
}

int os_cn_receiver_finish(os_cn_receiver *r, const uint8_t key[32], const uint8_t **data,
                          size_t *data_length, char format[OS_CN_FORMAT_MAX])
{
    uint8_t digest[32];
    char hex[65];
    if (!r) return OS_CN_ERR_ARGUMENT;
    if (r->cursor != r->size) return OS_CN_ERR_INTEGRITY;
    os_sha256_hash(r->buffer, r->size, digest);
    to_hex(digest, sizeof digest, hex);
    if (!str_eq(hex, r->digest)) return OS_CN_ERR_INTEGRITY;
    return os_cn_cnrb_unpack(r->buffer, r->size, key, data, data_length, format);
}

int os_cn_mcp_task_limits_ok(unsigned servers, unsigned approved_tools)
{
    return servers <= OS_CN_MCP_MAX_SERVERS_PER_TASK && approved_tools <= OS_CN_MCP_MAX_TOOLS_PER_TASK;
}

int os_cn_mcp_args_ok(size_t canonical_args_bytes)
{
    return canonical_args_bytes <= OS_CN_MCP_MAX_ARGS_BYTES;
}

int os_cn_mcp_result_ok(const char *text, size_t length)
{
    if (!text || length > OS_CN_MCP_MAX_RESULT_BYTES) return 0;
    for (size_t i = 0; i < length; i++)
        if (text[i] == 0) return 0;
    return 1;
}
