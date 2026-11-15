#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chroma_adapter.h"
#include "compat_registry.h"
#include "hmac_sha256.h"
#include "sha256.h"

static int failures;
#define CHECK(c, name) do { if (c) printf("PASS %s\n", name); else { printf("FAIL %s\n", name); failures++; } } while (0)

static uint8_t key[32];
static uint8_t data[OS_CN_CNRB_MAX + 16];
static uint8_t blob[OS_CN_CNRB_MAX_BLOB + 16];
static uint8_t quarantine[OS_CN_CNRB_MAX_BLOB + 16];

static void hexof(const uint8_t *d, size_t n, char *o)
{
    static const char x[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) { o[2 * i] = x[d[i] >> 4]; o[2 * i + 1] = x[d[i] & 15]; }
    o[2 * n] = 0;
}
static void fill(size_t n) { for (size_t i = 0; i < n; i++) data[i] = (uint8_t)((i * 31u + 7u) & 255u); }
static int unhex(const char *s, uint8_t *out, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned v;
        if (sscanf(s + 2 * i, "%2x", &v) != 1) return 0;
        out[i] = (uint8_t)v;
    }
    return 1;
}

static int hmac_case(const uint8_t *k, size_t kl, const uint8_t *m, size_t ml, const char *expect)
{
    uint8_t t[32];
    char h[65];
    os_hmac_sha256(k, kl, m, ml, t);
    hexof(t, 32, h);
    return strcmp(h, expect) == 0;
}

static void resign(uint8_t *b, size_t len)
{
    uint8_t t[32];
    os_hmac_sha256(key, 32, b, len - 32u, t);
    memcpy(b + len - 32u, t, 32);
}

int main(int argc, char **argv)
{
    char path[512], line[512];
    FILE *f;
    int cases = 0, identities = 0, cases_ok = 1, identities_ok = 1, tamper_ok;
    char first_name[48] = "", first_fmt[24] = "";
    size_t first_len = 0;
    int first_blob = 0;

    if (argc < 2) { printf("usage: %s <fixture-dir>\n", argv[0]); return 2; }
    snprintf(path, sizeof path, "%s/vectors.txt", argv[1]);
    f = fopen(path, "r");
    if (!f) { printf("FAIL fixture %s missing\n", path); return 2; }

    /* HMAC-SHA256, RFC 4231 test cases 1, 2 and 6. */
    {
        uint8_t k1[20], k6[131];
        memset(k1, 0x0b, sizeof k1);
        memset(k6, 0xaa, sizeof k6);
        CHECK(hmac_case(k1, 20, (const uint8_t *)"Hi There", 8,
                        "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7") &&
              hmac_case((const uint8_t *)"Jefe", 4, (const uint8_t *)"what do ya want for nothing?", 28,
                        "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843") &&
              hmac_case(k6, 131, (const uint8_t *)"Test Using Larger Than Block-Size Key - Hash Key First", 54,
                        "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"),
              "G16-HMAC-RFC4231-VECTORS");
        uint8_t a[32] = {0}, b[32] = {0};
        CHECK(os_hmac_tag_equal(a, b) && (b[31] = 1, !os_hmac_tag_equal(a, b)), "G16-HMAC-TAG-COMPARE");
    }

    while (fgets(line, sizeof line, f)) {
        char name[48], fmt[24], dig[80], b64[80], hexkey[80];
        unsigned long len, blen;
        if (sscanf(line, "key %79s", hexkey) == 1) { if (!unhex(hexkey, key, 32)) return 2; continue; }
        if (sscanf(line, "case %47s %23s %lu %79s %lu", name, fmt, &len, dig, &blen) == 5) {
            uint8_t d[32];
            char h[65];
            int n;
            cases++;
            fill(len);
            n = os_cn_cnrb_pack(data, len, fmt, key, blob, sizeof blob);
            os_sha256_hash(blob, n > 0 ? (size_t)n : 0u, d);
            hexof(d, 32, h);
            if (n < 0 || (unsigned long)n != blen || strcmp(h, dig) != 0) { cases_ok = 0; printf("FAIL case %s\n", name); }
            else {
                const uint8_t *out; size_t ol; char of[OS_CN_FORMAT_MAX];
                if (os_cn_cnrb_unpack(blob, (size_t)n, key, &out, &ol, of) != OS_CN_OK || ol != len ||
                    memcmp(out, data, len) != 0 || strcmp(of, fmt) != 0) { cases_ok = 0; printf("FAIL unpack %s\n", name); }
            }
            if (!first_blob && len > 0 && len < 100) {
                snprintf(first_name, sizeof first_name, "%s", name); snprintf(first_fmt, sizeof first_fmt, "%s", fmt);
                first_len = len; first_blob = 1;
            }
            continue;
        }
        if (sscanf(line, "identity %79s %79s", dig, b64) == 2) {
            identities++;
            if (os_cn_identity_verify(dig, b64) != OS_CN_OK) identities_ok = 0;
            continue;
        }
    }
    fclose(f);
    CHECK(cases == 7 && cases_ok, "G16-CNRB-MATCHES-UPSTREAM-BYTE-FOR-BYTE");
    CHECK(identities == 2 && identities_ok, "G16-IDENTITY-MATCHES-UPSTREAM-DERIVATION");

    /* Identity rejection. */
    {
        const char *id = "21fe31dfa154a261626bf854046fd2271b7bed4b6abe45aa58877ef47f9721b9";
        const char *pk = "11qYAYKxCrfVS/7TyWQHOg7hcvPapiMlrwIaaPcHURo=";
        const char *id2 = "39f713d0a644253f04529421b9f51b9b08979d08295959c4f3990ee617f5139f";
        char bad[256];
        int ok = 1;
        ok &= os_cn_identity_verify(id2, pk) == OS_CN_ERR_IDENTITY;
        snprintf(bad, sizeof bad, "%s", id); bad[0] = (bad[0] == '2') ? '3' : '2';
        ok &= os_cn_identity_verify(bad, pk) == OS_CN_ERR_IDENTITY;
        snprintf(bad, sizeof bad, "%s", id); for (int i = 0; bad[i]; i++) if (bad[i] >= 'a' && bad[i] <= 'f') bad[i] -= 32;
        ok &= os_cn_identity_verify(bad, pk) == OS_CN_ERR_IDENTITY; /* upstream compares lowercase hex */
        snprintf(bad, sizeof bad, "%s", pk); bad[42] = 'U'; /* non-zero spare bits: non-canonical */
        ok &= os_cn_identity_verify(id, bad) == OS_CN_ERR_IDENTITY;
        snprintf(bad, sizeof bad, "%s", pk); bad[5] = '*';
        ok &= os_cn_identity_verify(id, bad) == OS_CN_ERR_IDENTITY;
        snprintf(bad, sizeof bad, "%s", pk); bad[43] = 0;
        ok &= os_cn_identity_verify(id, bad) == OS_CN_ERR_IDENTITY;
        ok &= os_cn_identity_verify(id, "") == OS_CN_ERR_IDENTITY && os_cn_identity_verify(NULL, pk) == OS_CN_ERR_ARGUMENT;
        CHECK(ok, "G16-IDENTITY-MISMATCH-AND-NONCANONICAL-REJECTED");
        snprintf(bad, sizeof bad, "{\"nodeId\":\"%s\",\"publicKeyBase64\":\"%s\",\"created\":\"x\"}", id, pk);
        CHECK(os_cn_identity_from_document(bad, strlen(bad)) == OS_CN_OK, "G16-IDENTITY-DOCUMENT-ACCEPTED");
        snprintf(bad, sizeof bad, "{\"nodeId\":\"%s\",\"publicKeyBase64\":\"%s\"}", id2, pk);
        ok = os_cn_identity_from_document(bad, strlen(bad)) == OS_CN_ERR_IDENTITY;
        ok &= os_cn_identity_from_document("{\"nodeId\":\"x\"}", 14) == OS_CN_ERR_IDENTITY;
        ok &= os_cn_identity_from_document("[]", 2) == OS_CN_ERR_IDENTITY;
        CHECK(ok, "G16-IDENTITY-BAD-DOCUMENT-REJECTED");
    }

    /* CNRB tamper resistance on a small container. */
    {
        uint8_t small[512], copy[512];
        const uint8_t *out; size_t ol; char of[OS_CN_FORMAT_MAX];
        int n, ok = 1;
        fill(first_len);
        n = os_cn_cnrb_pack(data, first_len, first_fmt, key, small, sizeof small);
        CHECK(n > 0 && first_blob, "G16-CNRB-SMALL-CONTAINER-BUILT");
        tamper_ok = 1;
        for (int i = 0; i < n; i++) {
            memcpy(copy, small, (size_t)n);
            copy[i] ^= 1u;
            if (os_cn_cnrb_unpack(copy, (size_t)n, key, &out, &ol, of) == OS_CN_OK) tamper_ok = 0;
        }
        CHECK(tamper_ok, "G16-CNRB-ANY-SINGLE-BIT-FLIP-REJECTED");
        memcpy(copy, small, (size_t)n); copy[n] = 0;
        ok = os_cn_cnrb_unpack(copy, (size_t)n + 1u, key, &out, &ol, of) != OS_CN_OK;
        ok &= os_cn_cnrb_unpack(small, (size_t)n - 1u, key, &out, &ol, of) != OS_CN_OK;
        ok &= os_cn_cnrb_unpack(small, 10, key, &out, &ol, of) == OS_CN_ERR_SIZE;
        CHECK(ok, "G16-CNRB-TRAILING-AND-TRUNCATED-REJECTED");
        {
            uint8_t other[32];
            memset(other, 9, sizeof other);
            CHECK(os_cn_cnrb_unpack(small, (size_t)n, other, &out, &ol, of) == OS_CN_ERR_AUTH, "G16-CNRB-WRONG-KEY-REJECTED");
        }
        /* Correctly authenticated but non-canonical or inconsistent metadata is still refused. */
        {
            const char *mutations[][2] = { { "\"format\"", "\"Format\"" }, { ",\"length\"", ", \"length\"" } };
            int bad = 0;
            for (int m = 0; m < 2; m++) {
                memcpy(copy, small, (size_t)n);
                char *p = strstr((char *)copy + OS_CN_CNRB_HEADER, mutations[m][0]);
                if (p) { *(p + 1) ^= 0x20; resign(copy, (size_t)n); if (os_cn_cnrb_unpack(copy, (size_t)n, key, &out, &ol, of) != OS_CN_ERR_METADATA) bad = 1; }
                else bad = 1;
            }
            memcpy(copy, small, (size_t)n);
            { char *p = strstr((char *)copy + OS_CN_CNRB_HEADER, "\"sha256\":\"") + 10; *p = (*p == 'a') ? 'b' : 'a'; }
            resign(copy, (size_t)n);
            if (os_cn_cnrb_unpack(copy, (size_t)n, key, &out, &ol, of) != OS_CN_ERR_METADATA) bad = 1;
            memcpy(copy, small, (size_t)n);
            { char *p = strstr((char *)copy + OS_CN_CNRB_HEADER, "\"format\":\"") + 10; memcpy(p, "exe", 3); }
            resign(copy, (size_t)n);
            if (os_cn_cnrb_unpack(copy, (size_t)n, key, &out, &ol, of) == OS_CN_OK) bad = 1;
            CHECK(!bad, "G16-CNRB-AUTHENTICATED-BAD-METADATA-REJECTED");
        }
        ok = os_cn_cnrb_pack(data, first_len, "exe", key, small, sizeof small) == OS_CN_ERR_FORMAT &&
             os_cn_cnrb_pack(data, first_len, NULL, key, small, sizeof small) == OS_CN_ERR_FORMAT &&
             os_cn_cnrb_pack(data, OS_CN_CNRB_MAX + 1u, "raw", key, blob, sizeof blob) == OS_CN_ERR_SIZE &&
             os_cn_cnrb_pack(data, first_len, "raw", key, small, 20) == OS_CN_ERR_SIZE &&
             !os_cn_format_valid("sh") && !os_cn_format_valid("") && os_cn_format_valid("prisme-asm");
        CHECK(ok, "G16-CNRB-FORMAT-AND-SIZE-BOUNDS-ENFORCED");
        /* Header length fields cannot trigger over-reads. */
        memcpy(copy, small, (size_t)n);
        copy[5] = 0xff; copy[6] = 0xff; copy[7] = 0xff; copy[8] = 0xff;
        CHECK(os_cn_cnrb_unpack(copy, (size_t)n, key, &out, &ol, of) == OS_CN_ERR_HEADER, "G16-CNRB-OVERSIZED-HEADER-LENGTH-REJECTED");
    }

    /* Receiver quarantine. */
    {
        size_t len = 32769u;
        uint8_t d[32];
        char h[65];
        os_cn_receiver r;
        const uint8_t *out; size_t ol; char of[OS_CN_FORMAT_MAX];
        int n, ok;
        fill(len);
        n = os_cn_cnrb_pack(data, len, "optb", key, blob, sizeof blob);
        os_sha256_hash(blob, (size_t)n, d);
        hexof(d, 32, h);
        ok = os_cn_receiver_init(&r, quarantine, sizeof quarantine, (size_t)n, h) == OS_CN_OK;
        ok &= os_cn_receiver_append(&r, 0, blob, OS_CN_CNRB_CHUNK) == (int)OS_CN_CNRB_CHUNK;
        ok &= os_cn_receiver_append(&r, 0, blob, 1000) == (int)OS_CN_CNRB_CHUNK; /* idempotent retransmit */
        ok &= os_cn_receiver_append(&r, 100, blob + 100, OS_CN_CNRB_CHUNK) == OS_CN_ERR_CURSOR; /* partial overlap */
        ok &= os_cn_receiver_finish(&r, key, &out, &ol, of) == OS_CN_ERR_INTEGRITY; /* incomplete */
        ok &= os_cn_receiver_append(&r, (size_t)n + 5u, blob, 10) == OS_CN_ERR_CURSOR; /* gap */
        ok &= os_cn_receiver_append(&r, OS_CN_CNRB_CHUNK, blob + OS_CN_CNRB_CHUNK, (size_t)n - OS_CN_CNRB_CHUNK + 1u) == OS_CN_ERR_CURSOR; /* past end */
        ok &= os_cn_receiver_append(&r, OS_CN_CNRB_CHUNK, blob + OS_CN_CNRB_CHUNK, (size_t)n - OS_CN_CNRB_CHUNK) == n;
        ok &= os_cn_receiver_finish(&r, key, &out, &ol, of) == OS_CN_OK && ol == len && memcmp(out, data, len) == 0 &&
              strcmp(of, "optb") == 0;
        CHECK(ok, "G16-RECEIVER-RESUMABLE-AND-VERIFIED");
        ok = os_cn_receiver_append(&r, 0, blob, OS_CN_CNRB_CHUNK + 1u) == OS_CN_ERR_LIMIT &&
             os_cn_receiver_append(&r, 0, blob, 0) == OS_CN_ERR_LIMIT;
        CHECK(ok, "G16-RECEIVER-CHUNK-LIMIT-ENFORCED");
        /* wrong advertised digest and tampered content */
        os_cn_receiver_init(&r, quarantine, sizeof quarantine, (size_t)n, "0000000000000000000000000000000000000000000000000000000000000000");
        os_cn_receiver_append(&r, 0, blob, OS_CN_CNRB_CHUNK);
        os_cn_receiver_append(&r, OS_CN_CNRB_CHUNK, blob + OS_CN_CNRB_CHUNK, (size_t)n - OS_CN_CNRB_CHUNK);
        ok = os_cn_receiver_finish(&r, key, &out, &ol, of) == OS_CN_ERR_INTEGRITY;
        ok &= os_cn_receiver_init(&r, quarantine, sizeof quarantine, 0, h) == OS_CN_ERR_SIZE &&
              os_cn_receiver_init(&r, quarantine, 10, 11, h) == OS_CN_ERR_SIZE &&
              os_cn_receiver_init(&r, quarantine, sizeof quarantine, 10, "ABCDEF") == OS_CN_ERR_INTEGRITY;
        CHECK(ok, "G16-RECEIVER-DIGEST-MISMATCH-AND-BAD-INIT-REJECTED");
    }

    /* ChromaNeural MCP client limit profile. */
    CHECK(os_cn_mcp_task_limits_ok(4, 16) && !os_cn_mcp_task_limits_ok(5, 1) && !os_cn_mcp_task_limits_ok(1, 17) &&
          os_cn_mcp_args_ok(8192) && !os_cn_mcp_args_ok(8193) && os_cn_mcp_result_ok("ok", 2) &&
          !os_cn_mcp_result_ok("a\0b", 3) && !os_cn_mcp_result_ok("x", 32769) && !os_cn_mcp_result_ok(NULL, 0) &&
          os_cn_mcp_result_ok("x", 0),
          "G16-MCP-LIMIT-PROFILE-ENFORCED");

    /* Registry. */
    {
        int ok = os_compat_validate_all() == 0 && os_compat_count() == 8;
        for (size_t i = 0; i < os_compat_count(); i++) {
            const os_compat_adapter *a = os_compat_at(i);
            ok &= (a->capabilities & ~OS_COMPAT_CAP_ALLOWED) == 0 && a->preserves_upstream_authorization && a->version == 1u;
        }
        CHECK(ok, "G16-REGISTRY-LEAST-AUTHORITY-AND-VERSIONED");
        CHECK(os_compat_usable("chromaneural-identity") && os_compat_usable("chromaneural-cnrb") &&
              os_compat_usable("roboforge-mcp") && os_compat_usable("chromaplex-data-tags"),
              "G16-REGISTRY-VERIFIED-ADAPTERS-USABLE");
        CHECK(!os_compat_usable("chromaneural-node-admission") && !os_compat_usable("chromaneural-mcp-limits") &&
              !os_compat_usable("chromaplex-compiler") && !os_compat_usable("prisme-symbols") &&
              !os_compat_usable("unknown") && !os_compat_usable(NULL),
              "G16-REGISTRY-PENDING-ADAPTERS-INERT");
        CHECK(os_compat_find("prisme-symbols")->status == OS_COMPAT_DISCREPANCY &&
              os_compat_find("chromaneural-node-admission")->status == OS_COMPAT_PENDING &&
              os_compat_find("chromaneural-node-admission")->capabilities == 0u &&
              os_compat_find("chromaplex-compiler")->capabilities == 0u,
              "G16-REGISTRY-PENDING-STATUS-RECORDED-WITHOUT-AUTHORITY");
        CHECK(strstr(os_compat_find("prisme-symbols")->license, "All rights reserved") != NULL,
              "G16-REGISTRY-PRISME-LICENSE-RECORDED");
        CHECK(os_compat_at(os_compat_count()) == NULL && os_compat_find("roboforge-mcp")->max_calls == 4u,
              "G16-REGISTRY-BOUNDS");
    }

    /* PRISME structure (no byte codec). */
    CHECK(OS_PRISME_DATA_COLORS * OS_PRISME_LEVELS == OS_PRISME_DATA_SYMBOLS && os_prisme_symbol_valid(3, 6) &&
          os_prisme_symbol_valid(0, 0) && !os_prisme_symbol_valid(4, 0) && !os_prisme_symbol_valid(0, 7) &&
          os_prisme_channel_is_data(3) && !os_prisme_channel_is_data(4),
          "G16-PRISME-STRUCTURE-4X7-UV-CONTROL-ONLY");

    printf(failures ? "G16 RESULT: FAIL (%d)\n" : "G16 RESULT: PASS\n", failures);
    return failures ? 1 : 0;
}
