#include "devpkg.h"
#include "hmac_sha256.h"
#include "jsonrpc.h"
#include "sha256.h"

static size_t str_len(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
static int str_eq(const char *a, const char *b) { size_t i = 0; while (a[i] && a[i] == b[i]) i++; return a[i] == b[i]; }
static void mem_copy(void *d, const void *s, size_t n) { for (size_t i = 0; i < n; i++) ((uint8_t *)d)[i] = ((const uint8_t *)s)[i]; }
static void mem_zero(void *d, size_t n) { for (size_t i = 0; i < n; i++) ((uint8_t *)d)[i] = 0; }
static int mem_eq(const uint8_t *a, const uint8_t *b, size_t n) { uint8_t x = 0; for (size_t i = 0; i < n; i++) x |= (uint8_t)(a[i] ^ b[i]); return x == 0; }
static int is_zero(const uint8_t *a, size_t n) { uint8_t x = 0; for (size_t i = 0; i < n; i++) x |= a[i]; return x == 0; }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t get16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }

static int id_ok(const char *s)
{
    size_t n = str_len(s);
    if (n == 0 || n >= OS_PKG_ID_MAX) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        int alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (!(alnum || (i > 0 && (c == '.' || c == '_' || c == '-')))) return 0;
    }
    return 1;
}

static int arch_ok(const char *a) { return str_eq(a, "x86_64") || str_eq(a, "aarch64") || str_eq(a, "any"); }
static const char *kind_name(int k) { return k == OS_PKG_KIND_EOSVM ? "eosvm" : k == OS_PKG_KIND_APP_IMAGE ? "app-image" : ""; }

typedef struct { char *b; size_t cap, len; int fail; } wr;
static void w_c(wr *w, char c) { if (w->len + 1u < w->cap) { w->b[w->len++] = c; w->b[w->len] = 0; } else w->fail = 1; }
static void w_s(wr *w, const char *s) { while (*s) w_c(w, *s++); }
static void w_u(wr *w, uint32_t v)
{
    char t[10]; int n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (n) w_c(w, t[--n]);
}
static void w_key(wr *w, const char *k) { w_c(w, '"'); w_s(w, k); w_s(w, "\":"); }
static void w_str(wr *w, const char *v) { w_c(w, '"'); w_s(w, v); w_c(w, '"'); }

static void hex_encode(const uint8_t *d, char out[65])
{
    static const char x[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) { out[2 * i] = x[d[i] >> 4]; out[2 * i + 1] = x[d[i] & 15]; }
    out[64] = 0;
}

static int hex_decode(const char *s, uint8_t out[32])
{
    for (int i = 0; i < 32; i++) {
        int v[2];
        for (int j = 0; j < 2; j++) {
            char c = s[2 * i + j];
            v[j] = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
            if (v[j] < 0) return 0;
        }
        out[i] = (uint8_t)(v[0] * 16 + v[1]);
    }
    return 1;
}

static int manifest_text(const os_pkg_manifest *m, char *out, size_t cap)
{
    wr w = { out, cap, 0, 0 };
    char hex[65];
    if (cap) out[0] = 0;
    hex_encode(m->payload_sha256, hex);
    w_c(&w, '{');
    w_key(&w, "id"); w_str(&w, m->id); w_c(&w, ',');
    w_key(&w, "version"); w_u(&w, m->version); w_c(&w, ',');
    w_key(&w, "arch"); w_str(&w, m->arch); w_c(&w, ',');
    w_key(&w, "kind"); w_str(&w, kind_name(m->kind)); w_c(&w, ',');
    w_key(&w, "dependency"); w_str(&w, m->dependency); w_c(&w, ',');
    w_key(&w, "min_abi"); w_u(&w, m->min_abi); w_c(&w, ',');
    w_key(&w, "size_kb"); w_u(&w, m->size_kb); w_c(&w, ',');
    w_key(&w, "payload_sha256"); w_str(&w, hex);
    w_c(&w, '}');
    return w.fail ? -1 : (int)w.len;
}

enum { K_ID, K_VERSION, K_ARCH, K_KIND, K_DEP, K_MINABI, K_SIZE, K_SHA, K_COUNT };
static const char *const key_names[K_COUNT] = { "id", "version", "arch", "kind", "dependency", "min_abi", "size_kb", "payload_sha256" };

static void skip_ws(const char *t, size_t n, size_t *i) { while (*i < n && (t[*i] == ' ' || t[*i] == '\t' || t[*i] == '\n' || t[*i] == '\r')) (*i)++; }

static int read_string(const char *t, size_t n, size_t *i, char *out, size_t cap)
{
    size_t k = 0;
    if (*i >= n || t[*i] != '"') return 0;
    (*i)++;
    while (*i < n && t[*i] != '"') {
        unsigned char c = (unsigned char)t[*i];
        if (c < 0x20 || c >= 0x7f || c == '\\' || k + 1u >= cap) return 0;
        out[k++] = (char)c;
        (*i)++;
    }
    if (*i >= n) return 0;
    (*i)++;
    out[k] = 0;
    return 1;
}

static int read_uint(const char *t, size_t n, size_t *i, uint32_t *out)
{
    uint64_t v = 0;
    size_t start = *i;
    while (*i < n && t[*i] >= '0' && t[*i] <= '9') {
        v = v * 10u + (uint64_t)(t[*i] - '0');
        if (v > 0xffffffffu) return 0;
        (*i)++;
    }
    if (*i == start || (*i - start > 1u && t[start] == '0')) return 0;
    *out = (uint32_t)v;
    return 1;
}

static int manifest_parse(const char *t, size_t n, os_pkg_manifest *m)
{
    size_t i = 0;
    unsigned seen = 0;
    char kind[12], sha[80], key[20];
    mem_zero(m, sizeof *m);
    if (!os_jrpc_validate_document(t, n)) return 0;
    skip_ws(t, n, &i);
    if (i >= n || t[i++] != '{') return 0;
    for (;;) {
        int idx = -1;
        skip_ws(t, n, &i);
        if (!read_string(t, n, &i, key, sizeof key)) return 0;
        for (int k = 0; k < K_COUNT; k++) if (str_eq(key, key_names[k])) idx = k;
        if (idx < 0 || (seen & (1u << idx))) return 0;
        seen |= 1u << idx;
        skip_ws(t, n, &i);
        if (i >= n || t[i++] != ':') return 0;
        skip_ws(t, n, &i);
        switch (idx) {
        case K_ID: if (!read_string(t, n, &i, m->id, sizeof m->id)) return 0; break;
        case K_ARCH: if (!read_string(t, n, &i, m->arch, sizeof m->arch)) return 0; break;
        case K_KIND: if (!read_string(t, n, &i, kind, sizeof kind)) return 0; break;
        case K_DEP: if (!read_string(t, n, &i, m->dependency, sizeof m->dependency)) return 0; break;
        case K_SHA: if (!read_string(t, n, &i, sha, sizeof sha)) return 0; break;
        case K_VERSION: if (!read_uint(t, n, &i, &m->version)) return 0; break;
        case K_MINABI: if (!read_uint(t, n, &i, &m->min_abi)) return 0; break;
        case K_SIZE: if (!read_uint(t, n, &i, &m->size_kb)) return 0; break;
        }
        skip_ws(t, n, &i);
        if (i >= n) return 0;
        if (t[i] == ',') { i++; continue; }
        if (t[i] == '}') { i++; break; }
        return 0;
    }
    skip_ws(t, n, &i);
    if (i != n || seen != (1u << K_COUNT) - 1u) return 0;
    if (str_eq(kind, "eosvm")) m->kind = OS_PKG_KIND_EOSVM;
    else if (str_eq(kind, "app-image")) m->kind = OS_PKG_KIND_APP_IMAGE;
    else return 0;
    if (!id_ok(m->id) || !arch_ok(m->arch) || m->version == 0 || (m->dependency[0] && !id_ok(m->dependency))) return 0;
    if (str_len(sha) != 64 || !hex_decode(sha, m->payload_sha256)) return 0;
    return 1;
}

static int app_image_arch_ok(const os_pkg_manifest *m, const uint8_t *p, size_t n)
{
    uint32_t arch;
    if (n < 12u || get32(p) != 0x41505845u) return 0;
    arch = get32(p + 8);
    return (arch == 1u && str_eq(m->arch, "x86_64")) || (arch == 2u && str_eq(m->arch, "aarch64"));
}

int os_pkg_build(const os_pkg_manifest *manifest, const uint8_t *payload, size_t payload_len,
                 uint8_t *out, size_t out_cap, size_t *out_len)
{
    os_pkg_manifest m;
    char text[OS_PKG_MANIFEST_MAX + 1u];
    int mlen;
    size_t total;
    uint8_t digest[32];
    if (!manifest || !payload || !out || !out_len) return OS_PKG_ERR_ARGUMENT;
    *out_len = 0;
    if (payload_len == 0 || payload_len > OS_PKG_PAYLOAD_MAX) return OS_PKG_ERR_ARGUMENT;
    m = *manifest;
    if (!id_ok(m.id) || !arch_ok(m.arch) || m.version == 0 || (m.dependency[0] && !id_ok(m.dependency)) ||
        (m.kind != OS_PKG_KIND_EOSVM && m.kind != OS_PKG_KIND_APP_IMAGE)) return OS_PKG_ERR_MANIFEST;
    m.size_kb = (uint32_t)((payload_len + 1023u) / 1024u);
    os_sha256_hash(payload, payload_len, m.payload_sha256);
    mlen = manifest_text(&m, text, sizeof text);
    if (mlen <= 0) return OS_PKG_ERR_MANIFEST;
    total = OS_PKG_HEADER + (size_t)mlen + payload_len + 64u;
    if (total > out_cap) return OS_PKG_ERR_ARGUMENT;
    mem_zero(out, OS_PKG_HEADER);
    mem_copy(out, OS_PKG_MAGIC, 4);
    out[4] = (uint8_t)OS_PKG_FORMAT;
    out[6] = (uint8_t)mlen; out[7] = (uint8_t)((unsigned)mlen >> 8);
    put32(out + 8, (uint32_t)payload_len);
    mem_copy(out + OS_PKG_HEADER, text, (size_t)mlen);
    mem_copy(out + OS_PKG_HEADER + (size_t)mlen, payload, payload_len);
    os_sha256_hash(out, total - 64u, digest);
    mem_copy(out + total - 64u, digest, 32);
    mem_zero(out + total - 32u, 32);
    *out_len = total;
    return OS_PKG_OK;
}

int os_pkg_inspect(const uint8_t *p, size_t length, os_pkg_view *view)
{
    size_t mlen, plen;
    uint8_t digest[32];
    uint8_t psha[32];
    os_pkg_view v;
    if (!p || !view) return OS_PKG_ERR_ARGUMENT;
    mem_zero(view, sizeof *view);
    mem_zero(&v, sizeof v);
    if (length < OS_PKG_HEADER + 64u || length > OS_PKG_MAX_BYTES) return OS_PKG_ERR_FORMAT;
    if (!mem_eq(p, (const uint8_t *)OS_PKG_MAGIC, 4) || get16(p + 4) != OS_PKG_FORMAT) return OS_PKG_ERR_FORMAT;
    mlen = get16(p + 6);
    plen = get32(p + 8);
    v.signer = p[12];
    if (mlen == 0 || mlen > OS_PKG_MANIFEST_MAX || plen == 0 || plen > OS_PKG_PAYLOAD_MAX ||
        v.signer > (int)OS_PKG_KEY_SLOTS || !is_zero(p + 13, 3) ||
        length != OS_PKG_HEADER + mlen + plen + 64u) return OS_PKG_ERR_FORMAT;
    os_sha256_hash(p, length - 64u, digest);
    if (!mem_eq(digest, p + length - 64u, 32)) return OS_PKG_ERR_INTEGRITY;
    mem_copy(v.digest, digest, 32);
    mem_copy(v.signature, p + length - 32u, 32);
    if ((v.signer == 0) != is_zero(v.signature, 32)) return OS_PKG_ERR_SIGNATURE;
    if (!manifest_parse((const char *)p + OS_PKG_HEADER, mlen, &v.manifest)) return OS_PKG_ERR_MANIFEST;
    v.payload = p + OS_PKG_HEADER + mlen;
    v.payload_len = plen;
    os_sha256_hash(v.payload, plen, psha);
    if (!mem_eq(psha, v.manifest.payload_sha256, 32)) return OS_PKG_ERR_INTEGRITY;
    if (v.manifest.size_kb != (plen + 1023u) / 1024u) return OS_PKG_ERR_MANIFEST;
    if (v.manifest.kind == OS_PKG_KIND_EOSVM) {
        if (os_eosvm_validate(v.payload, plen)) return OS_PKG_ERR_FORMAT;
    } else if (!app_image_arch_ok(&v.manifest, v.payload, plen)) {
        return OS_PKG_ERR_COMPAT;
    }
    *view = v;
    return OS_PKG_OK;
}

int os_pkg_verify_signature(const os_pkg_view *view, const uint8_t *key, size_t key_len)
{
    uint8_t tag[32];
    if (!view || !key || key_len == 0) return OS_PKG_ERR_ARGUMENT;
    if (view->signer == 0) return OS_PKG_ERR_SIGNATURE;
    os_hmac_sha256(key, key_len, view->digest, 32, tag);
    return os_hmac_tag_equal(tag, view->signature) ? OS_PKG_OK : OS_PKG_ERR_SIGNATURE;
}

int os_pkg_sign(uint8_t *package, size_t length, int slot, const uint8_t *key, size_t key_len)
{
    os_pkg_view v;
    uint8_t digest[32], tag[32];
    int r;
    if (!package || !key || key_len == 0 || slot < 1 || slot > (int)OS_PKG_KEY_SLOTS) return OS_PKG_ERR_ARGUMENT;
    r = os_pkg_inspect(package, length, &v);
    if (r) return r;
    if (v.signer != 0) return OS_PKG_ERR_ARGUMENT;
    package[12] = (uint8_t)slot;
    os_sha256_hash(package, length - 64u, digest);
    mem_copy(package + length - 64u, digest, 32);
    os_hmac_sha256(key, key_len, digest, 32, tag);
    mem_copy(package + length - 32u, tag, 32);
    return OS_PKG_OK;
}

static int make_path(char *out, size_t cap, const char *dir, const char *id, uint32_t version)
{
    wr w = { out, cap, 0, 0 };
    if (cap) out[0] = 0;
    w_s(&w, dir); w_s(&w, id); w_c(&w, '-'); w_u(&w, version);
    return w.fail || w.len >= OS_STORAGE_PATH_MAX ? 0 : 1;
}

int os_pkg_manager_init(os_pkg_manager *mgr, os_storage_fs *fs, const char *arch, uint32_t abi)
{
    if (!mgr || !fs || !arch || !(str_eq(arch, "x86_64") || str_eq(arch, "aarch64")) || abi == 0) return OS_PKG_ERR_ARGUMENT;
    mem_zero(mgr, sizeof *mgr);
    mgr->fs = fs;
    mem_copy(mgr->arch, arch, str_len(arch) + 1u);
    mgr->abi = abi;
    return OS_PKG_OK;
}

int os_pkg_provision_key(os_pkg_manager *mgr, int slot, const uint8_t key[16])
{
    if (!mgr || !key || slot < 1 || slot > (int)OS_PKG_KEY_SLOTS || mgr->key_set[slot - 1]) return OS_PKG_ERR_ARGUMENT;
    mem_copy(mgr->keys[slot - 1], key, 16);
    mgr->key_set[slot - 1] = 1;
    return OS_PKG_OK;
}

int os_pkg_set_rollback_policy(os_pkg_manager *mgr, int authorized)
{
    if (!mgr) return OS_PKG_ERR_ARGUMENT;
    mgr->rollback_authorized = authorized != 0;
    return OS_PKG_OK;
}

#define STATE_ENTRY 106u
#define STATE_BYTES (OS_PKG_MAX_INSTALLED * STATE_ENTRY + 32u)

static int save_state(os_pkg_manager *mgr)
{
    uint8_t buf[STATE_BYTES];
    mem_zero(buf, sizeof buf);
    for (size_t i = 0; i < OS_PKG_MAX_INSTALLED; i++) {
        uint8_t *e = buf + i * STATE_ENTRY;
        const os_pkg_installed *s = &mgr->installed[i];
        if (!s->used) continue;
        e[0] = 1;
        mem_copy(e + 1, s->id, OS_PKG_ID_MAX);
        put32(e + 33, s->version); put32(e + 37, s->previous);
        mem_copy(e + 41, s->digest, 32); mem_copy(e + 73, s->payload_sha256, 32);
        e[105] = (uint8_t)s->kind;
    }
    os_sha256_hash(buf, OS_PKG_MAX_INSTALLED * STATE_ENTRY, buf + OS_PKG_MAX_INSTALLED * STATE_ENTRY);
    return os_storage_write(mgr->fs, "/pkg/state", buf, sizeof buf) ? OS_PKG_OK : OS_PKG_ERR_STORAGE;
}

int os_pkg_load_state(os_pkg_manager *mgr)
{
    uint8_t buf[STATE_BYTES];
    uint8_t d[32];
    usize size = 0, got = 0;
    os_pkg_installed table[OS_PKG_MAX_INSTALLED];
    if (!mgr) return OS_PKG_ERR_ARGUMENT;
    if (!os_storage_exists(mgr->fs, "/pkg/state", &size)) return OS_PKG_OK;
    if (size != STATE_BYTES || !os_storage_read(mgr->fs, "/pkg/state", 0, buf, sizeof buf, &got) || got != sizeof buf)
        return OS_PKG_ERR_INTEGRITY;
    os_sha256_hash(buf, OS_PKG_MAX_INSTALLED * STATE_ENTRY, d);
    if (!mem_eq(d, buf + OS_PKG_MAX_INSTALLED * STATE_ENTRY, 32)) return OS_PKG_ERR_INTEGRITY;
    mem_zero(table, sizeof table);
    for (size_t i = 0; i < OS_PKG_MAX_INSTALLED; i++) {
        const uint8_t *e = buf + i * STATE_ENTRY;
        os_pkg_installed *s = &table[i];
        if (e[0] == 0) continue;
        if (e[0] != 1 || e[1 + OS_PKG_ID_MAX - 1u] != 0) return OS_PKG_ERR_INTEGRITY;
        s->used = 1;
        mem_copy(s->id, e + 1, OS_PKG_ID_MAX);
        s->version = get32(e + 33); s->previous = get32(e + 37);
        mem_copy(s->digest, e + 41, 32); mem_copy(s->payload_sha256, e + 73, 32);
        s->kind = e[105];
        if (!id_ok(s->id) || s->version == 0 || (s->kind != OS_PKG_KIND_EOSVM && s->kind != OS_PKG_KIND_APP_IMAGE))
            return OS_PKG_ERR_INTEGRITY;
    }
    mem_copy(mgr->installed, table, sizeof table);
    return OS_PKG_OK;
}

int os_pkg_find_installed(const os_pkg_manager *mgr, const char *id)
{
    if (!mgr || !id) return -1;
    for (size_t i = 0; i < OS_PKG_MAX_INSTALLED; i++)
        if (mgr->installed[i].used && str_eq(mgr->installed[i].id, id)) return (int)i;
    return -1;
}

/* Inspect, require a valid signature from a provisioned key, and check node compatibility. */
static int accept(const os_pkg_manager *mgr, const uint8_t *pkg, size_t len, os_pkg_view *v)
{
    int r = os_pkg_inspect(pkg, len, v);
    if (r) return r;
    if (v->signer == 0 || !mgr->key_set[v->signer - 1]) return OS_PKG_ERR_SIGNATURE;
    r = os_pkg_verify_signature(v, mgr->keys[v->signer - 1], 16);
    if (r) return r;
    if (!str_eq(v->manifest.arch, "any") && !str_eq(v->manifest.arch, mgr->arch)) return OS_PKG_ERR_COMPAT;
    if (v->manifest.min_abi > mgr->abi) return OS_PKG_ERR_COMPAT;
    return OS_PKG_OK;
}

static int read_file(os_pkg_manager *mgr, const char *path, uint8_t *buf, size_t cap, size_t *len)
{
    usize size = 0, got = 0;
    if (!os_storage_exists(mgr->fs, path, &size)) return OS_PKG_ERR_NOT_FOUND;
    if (size == 0 || size > cap) return OS_PKG_ERR_FORMAT;
    if (!os_storage_read(mgr->fs, path, 0, buf, size, &got) || got != size) return OS_PKG_ERR_STORAGE;
    *len = size;
    return OS_PKG_OK;
}

int os_pkg_import(os_pkg_manager *mgr, const uint8_t *package, size_t length)
{
    os_pkg_view v;
    char path[OS_STORAGE_PATH_MAX + 8u];
    uint8_t existing[OS_PKG_MAX_BYTES];
    size_t elen = 0;
    int r;
    if (!mgr || !package) return OS_PKG_ERR_ARGUMENT;
    r = accept(mgr, package, length, &v);
    if (r) return r;
    if (!make_path(path, sizeof path, "/pkg/", v.manifest.id, v.manifest.version)) return OS_PKG_ERR_MANIFEST;
    r = read_file(mgr, path, existing, sizeof existing, &elen);
    if (r == OS_PKG_OK && !(elen == length && mem_eq(existing, package, length))) return OS_PKG_ERR_VERSION;
    if (r == OS_PKG_OK) return OS_PKG_OK;
    if (r != OS_PKG_ERR_NOT_FOUND) return r;
    return os_storage_write(mgr->fs, path, package, length) ? OS_PKG_OK : OS_PKG_ERR_STORAGE;
}

int os_pkg_export(os_pkg_manager *mgr, const char *id, uint32_t version)
{
    char path[OS_STORAGE_PATH_MAX + 8u], out[OS_STORAGE_PATH_MAX + 8u];
    uint8_t buf[OS_PKG_MAX_BYTES];
    size_t len = 0;
    os_pkg_view v;
    int r;
    if (!mgr || !id || !id_ok(id) || !make_path(path, sizeof path, "/pkg/", id, version) ||
        !make_path(out, sizeof out, "/out/", id, version)) return OS_PKG_ERR_ARGUMENT;
    r = read_file(mgr, path, buf, sizeof buf, &len);
    if (r) return r;
    r = accept(mgr, buf, len, &v);
    if (r) return r;
    return os_storage_write(mgr->fs, out, buf, len) ? OS_PKG_OK : OS_PKG_ERR_STORAGE;
}

int os_pkg_approve(os_pkg_manager *mgr, const char *id, uint32_t version, const uint8_t digest[32],
                   uint64_t expiry, uint64_t now)
{
    char path[OS_STORAGE_PATH_MAX + 8u];
    uint8_t buf[OS_PKG_MAX_BYTES];
    size_t len = 0;
    os_pkg_view v;
    int r, slot = -1;
    if (!mgr || !id || !digest || !id_ok(id) || !make_path(path, sizeof path, "/pkg/", id, version)) return OS_PKG_ERR_ARGUMENT;
    if (expiry <= now) return OS_PKG_ERR_EXPIRED;
    r = read_file(mgr, path, buf, sizeof buf, &len);
    if (r) return r;
    r = accept(mgr, buf, len, &v);
    if (r) return r;
    if (!mem_eq(v.digest, digest, 32)) return OS_PKG_ERR_APPROVAL;
    for (size_t i = 0; i < OS_PKG_MAX_APPROVALS; i++) {
        if (mgr->approvals[i].used && str_eq(mgr->approvals[i].id, id) && mgr->approvals[i].version == version) { slot = (int)i; break; }
        if (!mgr->approvals[i].used && slot < 0) slot = (int)i;
    }
    if (slot < 0) return OS_PKG_ERR_FULL;
    mgr->approvals[slot].used = 1;
    mem_copy(mgr->approvals[slot].id, id, str_len(id) + 1u);
    mgr->approvals[slot].version = version;
    mem_copy(mgr->approvals[slot].digest, v.digest, 32);
    mgr->approvals[slot].expiry = expiry;
    return OS_PKG_OK;
}

int os_pkg_install(os_pkg_manager *mgr, const char *id, uint32_t version, uint64_t now)
{
    char path[OS_STORAGE_PATH_MAX + 8u], app[OS_STORAGE_PATH_MAX + 8u];
    uint8_t buf[OS_PKG_MAX_BYTES];
    size_t len = 0;
    os_pkg_view v;
    os_pkg_installed old, *slot;
    int r, existing, approval = -1, free_slot = -1;
    if (!mgr || !id || !id_ok(id) || !make_path(path, sizeof path, "/pkg/", id, version) ||
        !make_path(app, sizeof app, "/app/", id, version)) return OS_PKG_ERR_ARGUMENT;
    r = read_file(mgr, path, buf, sizeof buf, &len);
    if (r) return r;
    r = accept(mgr, buf, len, &v);
    if (r) return r;
    for (size_t i = 0; i < OS_PKG_MAX_APPROVALS; i++)
        if (mgr->approvals[i].used && str_eq(mgr->approvals[i].id, id) && mgr->approvals[i].version == version) approval = (int)i;
    if (approval < 0 || !mem_eq(mgr->approvals[approval].digest, v.digest, 32)) return OS_PKG_ERR_APPROVAL;
    if (mgr->approvals[approval].expiry <= now) { mgr->approvals[approval].used = 0; return OS_PKG_ERR_EXPIRED; }
    if (v.manifest.dependency[0] && os_pkg_find_installed(mgr, v.manifest.dependency) < 0) return OS_PKG_ERR_DEPENDENCY;
    existing = os_pkg_find_installed(mgr, id);
    if (existing >= 0) {
        uint32_t cur = mgr->installed[existing].version;
        if (version == cur) return OS_PKG_ERR_VERSION;
        if (version < cur && !mgr->rollback_authorized) return OS_PKG_ERR_VERSION;
    } else {
        for (size_t i = 0; i < OS_PKG_MAX_INSTALLED; i++) if (!mgr->installed[i].used) { free_slot = (int)i; break; }
        if (free_slot < 0) return OS_PKG_ERR_FULL;
    }
    if (!os_storage_write(mgr->fs, app, v.payload, v.payload_len)) return OS_PKG_ERR_STORAGE;
    slot = &mgr->installed[existing >= 0 ? existing : free_slot];
    old = *slot;
    slot->used = 1;
    mem_copy(slot->id, id, str_len(id) + 1u);
    slot->previous = existing >= 0 ? old.version : 0;
    slot->version = version;
    mem_copy(slot->digest, v.digest, 32);
    mem_copy(slot->payload_sha256, v.manifest.payload_sha256, 32);
    slot->kind = v.manifest.kind;
    r = save_state(mgr);
    if (r) { *slot = old; return r; }
    mgr->approvals[approval].used = 0;
    return OS_PKG_OK;
}

int os_pkg_run(os_pkg_manager *mgr, const char *id, uint32_t fuel, os_eosvm_result *result)
{
    char app[OS_STORAGE_PATH_MAX + 8u];
    uint8_t buf[OS_PKG_PAYLOAD_MAX];
    uint8_t d[32];
    size_t len = 0;
    int idx, r;
    if (!mgr || !id || !result) return OS_PKG_ERR_ARGUMENT;
    idx = os_pkg_find_installed(mgr, id);
    if (idx < 0) return OS_PKG_ERR_NOT_FOUND;
    if (mgr->installed[idx].kind != OS_PKG_KIND_EOSVM) return OS_PKG_ERR_KIND;
    if (!make_path(app, sizeof app, "/app/", id, mgr->installed[idx].version)) return OS_PKG_ERR_ARGUMENT;
    r = read_file(mgr, app, buf, sizeof buf, &len);
    if (r) return r;
    os_sha256_hash(buf, len, d);
    if (!mem_eq(d, mgr->installed[idx].payload_sha256, 32)) return OS_PKG_ERR_INTEGRITY;
    return os_eosvm_run(buf, len, fuel, result) == OS_EVM_OK ? OS_PKG_OK : OS_PKG_ERR_FORMAT;
}
