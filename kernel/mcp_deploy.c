#include "mcp_deploy.h"
#include "sha256.h"

typedef struct { char *b; size_t cap, len; } outp;

static void o_c(outp *o, char c) { if (o->len + 1u < o->cap) { o->b[o->len++] = c; o->b[o->len] = 0; } }
static void o_s(outp *o, const char *s) { while (*s) o_c(o, *s++); }
static void o_u(outp *o, uint32_t v)
{
    char t[12];
    int n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (n) o_c(o, t[--n]);
}

static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static size_t str_len(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
static void str_copy(char *d, size_t cap, const char *s)
{
    size_t i = 0;
    while (s[i] && i + 1u < cap) { d[i] = s[i]; i++; }
    d[i] = 0;
}
static int digest_eq(const uint8_t *a, const uint8_t *b)
{
    uint8_t d = 0;
    for (int i = 0; i < 32; i++) d |= (uint8_t)(a[i] ^ b[i]);
    return d == 0;
}
static int id_ok(const char *s)
{
    size_t n = str_len(s);
    if (n == 0 || n >= OS_DM_ID_MAX) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return 0;
    }
    return 1;
}

#define S_PKG "{\"type\":\"object\",\"properties\":{\"package\":{\"type\":\"string\"}},\"required\":[\"package\"],\"additionalProperties\":false}"
#define S_NONE "{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}"

const os_dm_contract os_dm_contracts[OS_DM_TOOL_COUNT] = {
    { "experimentalos_development_capabilities_v1", 1, S_NONE, OS_DM_CAP_DISCOVER, 0, 0 },
    { "experimentalos_pi5_compatibility_inspect_v1", 1, S_PKG, OS_DM_CAP_INSPECT, 0, 0 },
    { "experimentalos_application_build_package_v1", 1,
      "{\"type\":\"object\",\"properties\":{\"project\":{\"type\":\"string\"},\"profile\":{\"enum\":[\"native-release\",\"native-debug\"]}},\"required\":[\"project\",\"profile\"],\"additionalProperties\":false}",
      OS_DM_CAP_BUILD, 0, 4 },
    { "experimentalos_json_schema_validate_v1", 1,
      "{\"type\":\"object\",\"properties\":{\"schema\":{\"enum\":[\"manifest-v1\"]},\"document\":{\"type\":\"object\"}},\"required\":[\"schema\",\"document\"],\"additionalProperties\":false}",
      OS_DM_CAP_VALIDATE, 0, 0 },
    { "experimentalos_package_integrity_inspect_v1", 1, S_PKG, OS_DM_CAP_INSPECT, 0, 0 },
    { "experimentalos_package_export_local_v1", 1,
      "{\"type\":\"object\",\"properties\":{\"package\":{\"type\":\"string\"},\"destination\":{\"type\":\"string\"}},\"required\":[\"package\",\"destination\"],\"additionalProperties\":false}",
      OS_DM_CAP_EXPORT, 1, 0 },
    { "experimentalos_remote_node_status_v1", 1,
      "{\"type\":\"object\",\"properties\":{\"node\":{\"type\":\"string\"}},\"required\":[\"node\"],\"additionalProperties\":false}",
      OS_DM_CAP_NODE_STATUS, 0, 0 },
    { "experimentalos_deployment_propose_v1", 1,
      "{\"type\":\"object\",\"properties\":{\"package\":{\"type\":\"string\"},\"node\":{\"type\":\"string\"}},\"required\":[\"package\",\"node\"],\"additionalProperties\":false}",
      OS_DM_CAP_PROPOSE, 0, 0 },
    { "experimentalos_install_authorize_v1", 1,
      "{\"type\":\"object\",\"properties\":{\"plan\":{\"type\":\"integer\"}},\"required\":[\"plan\"],\"additionalProperties\":false}",
      OS_DM_CAP_AUTH_REQUEST, 1, 0 },
    { "experimentalos_deployment_verify_rollback_v1", 1,
      "{\"type\":\"object\",\"properties\":{\"node\":{\"type\":\"string\"},\"rollback\":{\"type\":\"boolean\"}},\"required\":[\"node\"],\"additionalProperties\":false}",
      OS_DM_CAP_ROLLBACK, 1, 0 },
};

void os_dm_init(os_dm_manager *dm)
{
    volatile uint8_t *p = (volatile uint8_t *)dm;
    for (size_t i = 0; i < sizeof *dm; i++) p[i] = 0;
    for (unsigned i = 0; i < OS_DM_MAX_NODES; i++) { dm->nodes[i].installed = -1; dm->nodes[i].previous = -1; }
}

int os_dm_find_package(const os_dm_manager *dm, const char *id)
{
    for (unsigned i = 0; i < OS_DM_MAX_PACKAGES; i++)
        if (dm->packages[i].used && str_eq(dm->packages[i].id, id)) return (int)i;
    return -1;
}

int os_dm_find_node(const os_dm_manager *dm, const char *id)
{
    for (unsigned i = 0; i < OS_DM_MAX_NODES; i++)
        if (dm->nodes[i].used && str_eq(dm->nodes[i].id, id)) return (int)i;
    return -1;
}

static void signature_for(const os_dm_manager *dm, int slot, const uint8_t digest[32], uint8_t out[32])
{
    os_sha256 h;
    uint8_t s = (uint8_t)slot;
    os_sha256_init(&h);
    os_sha256_update(&h, "EOS-PKG-SIG-V1", 14);
    os_sha256_update(&h, &s, 1);
    os_sha256_update(&h, dm->trusted_key[slot], 16);
    os_sha256_update(&h, digest, 32);
    os_sha256_final(&h, out);
}

static int digest_ok(const os_dm_package *p)
{
    uint8_t d[32];
    os_sha256_hash(p->payload, p->payload_len, d);
    return digest_eq(d, p->digest);
}

static int signature_ok(const os_dm_manager *dm, const os_dm_package *p)
{
    uint8_t sig[32];
    if (p->signer < 0 || p->signer > 1 || !dm->trusted_key_set[p->signer]) return 0;
    signature_for(dm, p->signer, p->digest, sig);
    return digest_eq(sig, p->signature);
}

int os_dm_package_verify(const os_dm_manager *dm, int package)
{
    if (!dm || package < 0 || package >= (int)OS_DM_MAX_PACKAGES || !dm->packages[package].used)
        return OS_DM_ERR_NOT_FOUND;
    const os_dm_package *p = &dm->packages[package];
    if (!digest_ok(p) || !signature_ok(dm, p)) return OS_DM_ERR_INTEGRITY;
    return OS_DM_OK;
}

static const char *compat_problem(const os_dm_package *p, const os_dm_node *n)
{
    if (!str_eq(p->arch, "aarch64")) return "architecture is not aarch64";
    if (n) {
        if (!str_eq(n->arch, p->arch)) return "node architecture mismatch";
        if (p->dependency[0] && !str_eq(p->dependency, n->base)) return "dependency not satisfied on node";
        if (p->size_kb > n->free_kb) return "insufficient node resources";
    }
    return 0;
}

int os_dm_provision_key(os_dm_manager *dm, int slot, const uint8_t key[16])
{
    if (!dm || !key || slot < 0 || slot > 1) return OS_DM_ERR_ARGUMENT;
    for (int i = 0; i < 16; i++) dm->trusted_key[slot][i] = key[i];
    dm->trusted_key_set[slot] = 1;
    return OS_DM_OK;
}

int os_dm_sign_package(os_dm_manager *dm, const char *package_id, int slot)
{
    int p;
    if (!dm || !package_id || slot < 0 || slot > 1 || !dm->trusted_key_set[slot]) return OS_DM_ERR_ARGUMENT;
    p = os_dm_find_package(dm, package_id);
    if (p < 0) return OS_DM_ERR_NOT_FOUND;
    dm->packages[p].signer = slot;
    signature_for(dm, slot, dm->packages[p].digest, dm->packages[p].signature);
    return OS_DM_OK;
}

int os_dm_import_real_package(os_dm_manager *dm, const char *id, uint32_t version, const char *arch,
                              const char *dependency, uint32_t size_kb, const uint8_t real_digest[32])
{
    char name[OS_DM_ID_MAX];
    size_t n = 0, il;
    if (!dm || !id || !arch || !dependency || !real_digest || !id_ok(id) || version == 0) return OS_DM_ERR_ARGUMENT;
    il = str_len(id);
    if (il + 12u >= OS_DM_ID_MAX) return OS_DM_ERR_ARGUMENT;
    for (size_t i = 0; i < il; i++) name[n++] = id[i];
    name[n++] = '-'; name[n++] = 'v';
    {
        char tmp[10];
        int k = 0;
        uint32_t v = version;
        do { tmp[k++] = (char)('0' + v % 10u); v /= 10u; } while (v);
        while (k) name[n++] = tmp[--k];
    }
    name[n] = 0;
    if (os_dm_find_package(dm, name) >= 0) return OS_DM_ERR_STATE;
    for (unsigned i = 0; i < OS_DM_MAX_PACKAGES; i++) {
        os_dm_package *p = &dm->packages[i];
        if (p->used) continue;
        p->used = 1; p->version = version; p->signer = -1; p->size_kb = size_kb;
        str_copy(p->id, sizeof p->id, name);
        str_copy(p->arch, sizeof p->arch, arch);
        str_copy(p->dependency, sizeof p->dependency, dependency);
        for (unsigned k = 0; k < 32u; k++) p->payload[k] = real_digest[k];
        p->payload_len = 32u;
        os_sha256_hash(p->payload, p->payload_len, p->digest);
        return (int)i;
    }
    return OS_DM_ERR_FULL;
}

int os_dm_enroll_node(os_dm_manager *dm, const char *id, const char *arch, const char *base,
                      uint32_t free_kb)
{
    if (!dm || !id || !arch || !base || !id_ok(id)) return OS_DM_ERR_ARGUMENT;
    if (os_dm_find_node(dm, id) >= 0) return OS_DM_ERR_STATE;
    for (unsigned i = 0; i < OS_DM_MAX_NODES; i++) {
        os_dm_node *n = &dm->nodes[i];
        if (n->used) continue;
        n->used = 1; n->enrolled = 1; n->online = 1;
        str_copy(n->id, sizeof n->id, id);
        str_copy(n->arch, sizeof n->arch, arch);
        str_copy(n->base, sizeof n->base, base);
        n->free_kb = free_kb; n->installed = -1; n->previous = -1;
        return (int)i;
    }
    return OS_DM_ERR_FULL;
}

int os_dm_set_rollback_policy(os_dm_manager *dm, const char *node_id, int authorized)
{
    int n = dm && node_id ? os_dm_find_node(dm, node_id) : -1;
    if (n < 0) return OS_DM_ERR_NOT_FOUND;
    dm->nodes[n].rollback_authorized = authorized ? 1 : 0;
    return OS_DM_OK;
}

int os_dm_recipient_approve(os_dm_manager *dm, int plan, const uint8_t digest[32],
                            const char *node_id, uint32_t version, uint64_t expiry, uint64_t now)
{
    if (!dm || !digest || !node_id || plan < 0 || plan >= (int)OS_DM_MAX_PLANS) return OS_DM_ERR_ARGUMENT;
    os_dm_plan *pl = &dm->plans[plan];
    if (pl->state != OS_DM_PLAN_REQUESTED) return OS_DM_ERR_STATE;
    const os_dm_package *pk = &dm->packages[pl->package];
    if (expiry <= now) return OS_DM_ERR_EXPIRED;
    if (!digest_eq(digest, pk->digest) || version != pk->version ||
        !str_eq(node_id, dm->nodes[pl->node].id)) return OS_DM_ERR_BINDING;
    for (int i = 0; i < 32; i++) pl->approved_digest[i] = digest[i];
    pl->approved_version = version;
    pl->expiry = expiry;
    pl->state = OS_DM_PLAN_APPROVED;
    return OS_DM_OK;
}

int os_dm_activate(os_dm_manager *dm, int plan, uint64_t now, int fault)
{
    if (!dm || plan < 0 || plan >= (int)OS_DM_MAX_PLANS) return OS_DM_ERR_ARGUMENT;
    os_dm_plan *pl = &dm->plans[plan];
    if (pl->state != OS_DM_PLAN_APPROVED) return OS_DM_ERR_STATE;
    if (now >= pl->expiry) return OS_DM_ERR_EXPIRED;
    os_dm_package *pk = &dm->packages[pl->package];
    os_dm_node *n = &dm->nodes[pl->node];
    if (os_dm_package_verify(dm, pl->package) != OS_DM_OK) return OS_DM_ERR_INTEGRITY;
    if (!digest_eq(pk->digest, pl->approved_digest) || pk->version != pl->approved_version) return OS_DM_ERR_BINDING;
    if (!n->enrolled || !n->online || compat_problem(pk, n)) return OS_DM_ERR_COMPAT;
    if (fault == OS_DM_FAULT_INTERRUPT) return OS_DM_ERR_FAULT; /* atomic: node untouched, plan stays resumable */
    if (fault == OS_DM_FAULT_INSTALL_FAIL) { pl->state = OS_DM_PLAN_FAILED; return OS_DM_ERR_FAULT; }
    n->previous = n->installed;
    n->installed = pl->package;
    n->free_kb -= pk->size_kb;
    pl->state = OS_DM_PLAN_ACTIVE;
    return OS_DM_OK;
}

static int arg_str(const char *t, size_t off, size_t len, const char *key, char *buf, size_t cap)
{
    return os_jrpc_member_string(t, off, len, key, buf, cap) && id_ok(buf);
}

#define HANDLER(name) static int name(void *ctx, const char *t, size_t off, size_t len, char *out, size_t cap)
#define DM ((os_dm_manager *)ctx)

HANDLER(h_capabilities)
{
    outp o = { out, cap, 0 };
    (void)ctx; (void)t; (void)off; (void)len;
    o_s(&o, "sdk=experimentalos-1;targets=aarch64-pi5,x86_64-vm;profiles=native-release,native-debug");
    return 0;
}

HANDLER(h_compat)
{
    outp o = { out, cap, 0 };
    char id[OS_DM_ID_MAX];
    if (!arg_str(t, off, len, "package", id, sizeof id)) return -1;
    int p = os_dm_find_package(DM, id);
    if (p < 0) { o_s(&o, "package not found"); return 1; }
    const char *why = compat_problem(&DM->packages[p], 0);
    if (why) { o_s(&o, "incompatible: "); o_s(&o, why); return 1; }
    o_s(&o, "compatible: aarch64");
    return 0;
}

HANDLER(h_build)
{
    outp o = { out, cap, 0 };
    char project[OS_DM_ID_MAX], profile[24];
    if (!arg_str(t, off, len, "project", project, sizeof project) ||
        !os_jrpc_member_string(t, off, len, "profile", profile, sizeof profile) ||
        !(str_eq(profile, "native-release") || str_eq(profile, "native-debug"))) return -1;
    uint32_t version = 1;
    for (unsigned i = 0; i < OS_DM_MAX_PACKAGES; i++) {
        const os_dm_package *q = &DM->packages[i];
        if (q->used && q->version >= version && str_len(q->id) > str_len(project) &&
            str_len(project) + 2u < OS_DM_ID_MAX) {
            size_t pl = str_len(project);
            int same = 1;
            for (size_t k = 0; k < pl; k++) if (q->id[k] != project[k]) same = 0;
            if (same && q->id[pl] == '-' && q->id[pl + 1u] == 'v') version = q->version + 1u;
        }
    }
    if (str_len(project) + 6u >= OS_DM_ID_MAX) return -1;
    for (unsigned i = 0; i < OS_DM_MAX_PACKAGES; i++) {
        os_dm_package *p = &DM->packages[i];
        if (p->used) continue;
        char *w = p->id;
        for (const char *s = project; *s; s++) *w++ = *s;
        *w++ = '-'; *w++ = 'v';
        *w++ = (char)('0' + version % 10u);
        *w = 0;
        p->used = 1; p->version = version; p->signer = -1; p->size_kb = 64;
        str_copy(p->arch, sizeof p->arch, "aarch64");
        const char *tag = "build:";
        uint32_t n = 0;
        for (const char *s = tag; *s && n < OS_DM_PAYLOAD_MAX; s++) p->payload[n++] = (uint8_t)*s;
        for (const char *s = project; *s && n < OS_DM_PAYLOAD_MAX; s++) p->payload[n++] = (uint8_t)*s;
        str_copy(p->dependency, sizeof p->dependency, "rros-base-1");
        for (const char *s = "|dep:rros-base-1"; *s && n < OS_DM_PAYLOAD_MAX; s++) p->payload[n++] = (uint8_t)*s;
        p->payload_len = n;
        os_sha256_hash(p->payload, n, p->digest);
        o_s(&o, "package="); o_s(&o, p->id); o_s(&o, ";signed=no");
        return 0;
    }
    o_s(&o, "package store full");
    return 1;
}

HANDLER(h_validate)
{
    outp o = { out, cap, 0 };
    char schema[16], name[OS_DM_ID_MAX], arch[12];
    size_t doff, dlen;
    int64_t version;
    (void)ctx;
    if (!os_jrpc_member_string(t, off, len, "schema", schema, sizeof schema) || !str_eq(schema, "manifest-v1") ||
        !os_jrpc_member_object(t, off, len, "document", &doff, &dlen)) return -1;
    if (!os_jrpc_member_string(t, doff, dlen, "name", name, sizeof name) || !id_ok(name)) { o_s(&o, "invalid: name"); return 1; }
    if (!os_jrpc_member_int(t, doff, dlen, "version", &version) || version < 1 || version > 9) { o_s(&o, "invalid: version"); return 1; }
    if (!os_jrpc_member_string(t, doff, dlen, "arch", arch, sizeof arch) ||
        !(str_eq(arch, "aarch64") || str_eq(arch, "x86_64"))) { o_s(&o, "invalid: arch"); return 1; }
    o_s(&o, "valid");
    return 0;
}

HANDLER(h_integrity)
{
    outp o = { out, cap, 0 };
    char id[OS_DM_ID_MAX];
    if (!arg_str(t, off, len, "package", id, sizeof id)) return -1;
    int p = os_dm_find_package(DM, id);
    if (p < 0) { o_s(&o, "package not found"); return 1; }
    const os_dm_package *pk = &DM->packages[p];
    int d = digest_ok(pk), s = signature_ok(DM, pk), c = compat_problem(pk, 0) == 0;
    o_s(&o, "digest="); o_s(&o, d ? "ok" : "MISMATCH");
    o_s(&o, ";signature="); o_s(&o, s ? "ok" : "unsigned-or-untrusted");
    o_s(&o, ";target="); o_s(&o, c ? "ok" : "incompatible");
    return (d && s && c) ? 0 : 1;
}

HANDLER(h_export)
{
    outp o = { out, cap, 0 };
    char id[OS_DM_ID_MAX], dest[64];
    if (!arg_str(t, off, len, "package", id, sizeof id) ||
        !os_jrpc_member_string(t, off, len, "destination", dest, sizeof dest)) return -1;
    if (dest[0] != '/' || dest[1] != 'e' || dest[2] != 'x' || dest[3] != 'p' || dest[4] != 'o' ||
        dest[5] != 'r' || dest[6] != 't' || dest[7] != '/' || dest[8] == 0) return -1;
    for (size_t i = 0; dest[i]; i++)
        if ((dest[i] == '.' && dest[i + 1u] == '.') || dest[i] == '\\') return -1;
    int p = os_dm_find_package(DM, id);
    if (p < 0 || os_dm_package_verify(DM, p) != OS_DM_OK) { o_s(&o, "export refused: package not verified"); return 1; }
    str_copy(DM->packages[p].exported_to, sizeof DM->packages[p].exported_to, dest);
    o_s(&o, "exported to "); o_s(&o, dest);
    return 0;
}

HANDLER(h_node_status)
{
    outp o = { out, cap, 0 };
    char id[16];
    if (!arg_str(t, off, len, "node", id, sizeof id)) return -1;
    int n = os_dm_find_node(DM, id);
    if (n < 0 || !DM->nodes[n].enrolled) { o_s(&o, "node not enrolled"); return 1; }
    o_s(&o, "online="); o_s(&o, DM->nodes[n].online ? "yes" : "no");
    o_s(&o, ";installed=");
    o_s(&o, DM->nodes[n].installed >= 0 ? DM->packages[DM->nodes[n].installed].id : "none");
    return 0;
}

HANDLER(h_propose)
{
    outp o = { out, cap, 0 };
    char pid[OS_DM_ID_MAX], nid[16];
    if (!arg_str(t, off, len, "package", pid, sizeof pid) || !arg_str(t, off, len, "node", nid, sizeof nid)) return -1;
    int p = os_dm_find_package(DM, pid), n = os_dm_find_node(DM, nid);
    if (p < 0 || n < 0) { o_s(&o, "package or node not found"); return 1; }
    if (os_dm_package_verify(DM, p) != OS_DM_OK) { o_s(&o, "refused: package not verified"); return 1; }
    const char *why = compat_problem(&DM->packages[p], &DM->nodes[n]);
    if (why) { o_s(&o, "refused: "); o_s(&o, why); return 1; }
    for (unsigned i = 0; i < OS_DM_MAX_PLANS; i++) {
        if (DM->plans[i].state != OS_DM_PLAN_UNUSED) continue;
        DM->plans[i].state = OS_DM_PLAN_PROPOSED;
        DM->plans[i].package = p;
        DM->plans[i].node = n;
        o_s(&o, "plan="); o_u(&o, i); o_s(&o, ";state=proposed;executes=no");
        return 0;
    }
    o_s(&o, "refused: plan quota exhausted");
    return 1;
}

HANDLER(h_authorize)
{
    outp o = { out, cap, 0 };
    int64_t plan;
    /* Approval can never arrive as tool input. */
    if (os_jrpc_has_member(t, off, len, "approved") || os_jrpc_has_member(t, off, len, "approval") ||
        os_jrpc_has_member(t, off, len, "digest") || os_jrpc_has_member(t, off, len, "expiry")) return -1;
    if (!os_jrpc_member_int(t, off, len, "plan", &plan) || plan < 0 || plan >= (int64_t)OS_DM_MAX_PLANS) return -1;
    os_dm_plan *pl = &DM->plans[plan];
    if (pl->state != OS_DM_PLAN_PROPOSED) { o_s(&o, "plan not in proposed state"); return 1; }
    pl->state = OS_DM_PLAN_REQUESTED;
    o_s(&o, "state=awaiting-recipient-approval");
    return 0;
}

HANDLER(h_rollback)
{
    outp o = { out, cap, 0 };
    char nid[16];
    int want = 0;
    if (!arg_str(t, off, len, "node", nid, sizeof nid)) return -1;
    (void)os_jrpc_member_bool(t, off, len, "rollback", &want);
    int n = os_dm_find_node(DM, nid);
    if (n < 0 || !DM->nodes[n].enrolled) { o_s(&o, "node not enrolled"); return 1; }
    os_dm_node *nd = &DM->nodes[n];
    if (nd->installed < 0) { o_s(&o, "nothing installed"); return 1; }
    int ok = os_dm_package_verify(DM, nd->installed) == OS_DM_OK;
    o_s(&o, "installed="); o_s(&o, DM->packages[nd->installed].id);
    o_s(&o, ";verified="); o_s(&o, ok ? "yes" : "NO");
    if (!want) return ok ? 0 : 1;
    if (!nd->rollback_authorized) { o_s(&o, ";rollback=refused-no-recipient-policy"); return 1; }
    if (nd->previous < 0 || os_dm_package_verify(DM, nd->previous) != OS_DM_OK) {
        o_s(&o, ";rollback=refused-no-verified-prior");
        return 1;
    }
    nd->installed = nd->previous;
    nd->previous = -1;
    o_s(&o, ";rollback=done;now="); o_s(&o, DM->packages[nd->installed].id);
    return 0;
}

int os_dm_register_tools(os_dm_manager *dm, os_mcp_registry *registry, os_mcp_server *server)
{
    static const os_mcp_tool_fn fns[OS_DM_TOOL_COUNT] = {
        h_capabilities, h_compat, h_build, h_validate, h_integrity,
        h_export, h_node_status, h_propose, h_authorize, h_rollback
    };
    if (!dm || !registry || !server) return OS_DM_ERR_ARGUMENT;
    for (unsigned i = 0; i < OS_DM_TOOL_COUNT; i++) {
        const os_dm_contract *c = &os_dm_contracts[i];
        int t = os_mcp_tool_register(registry, c->name, c->version, c->schema, str_len(c->schema),
                                     c->caps, c->side_effecting);
        if (t < 0) return t;
        if (os_mcp_server_bind(server, t, fns[i], dm, c->cost_ticks) != OS_MCP_OK) return OS_DM_ERR_ARGUMENT;
    }
    return OS_DM_OK;
}
