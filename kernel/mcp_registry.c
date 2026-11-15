#include "mcp_registry.h"
#include "sha256.h"

static int name_valid(const char *name, size_t *len)
{
    size_t n = 0;
    while (name[n]) {
        char c = name[n];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return 0;
        if (++n >= OS_MCP_TOOL_NAME_MAX) return 0;
    }
    *len = n;
    return n > 0;
}

static int name_equal(const char *a, const char *b)
{
    for (size_t i = 0; i < OS_MCP_TOOL_NAME_MAX; i++) {
        if (a[i] != b[i]) return 0;
        if (!a[i]) return 1;
    }
    return 0;
}

static int digest_equal(const uint8_t *a, const uint8_t *b)
{
    uint8_t diff = 0;
    for (unsigned i = 0; i < 32; i++) diff |= (uint8_t)(a[i] ^ b[i]);
    return diff == 0;
}

static void zero(void *p, size_t n)
{
    volatile uint8_t *b = (volatile uint8_t *)p;
    for (size_t i = 0; i < n; i++) b[i] = 0;
}

void os_mcp_registry_init(os_mcp_registry *r)
{
    if (r) zero(r, sizeof *r);
}

void os_mcp_tool_fingerprint(const char *name, uint32_t version, const char *schema,
                             size_t schema_len, uint8_t out[32])
{
    static const char tag[] = "EOS-MCP-TOOL-V1";
    os_sha256 h;
    uint8_t v[4] = { (uint8_t)version, (uint8_t)(version >> 8), (uint8_t)(version >> 16),
                     (uint8_t)(version >> 24) };
    uint8_t sep = 0;
    size_t n = 0;
    while (name[n]) n++;
    os_sha256_init(&h);
    os_sha256_update(&h, tag, sizeof tag);
    os_sha256_update(&h, name, n);
    os_sha256_update(&h, &sep, 1);
    os_sha256_update(&h, v, 4);
    os_sha256_update(&h, schema, schema_len);
    os_sha256_final(&h, out);
}

static void audit(os_mcp_registry *r, int event, int tool, int result)
{
    os_mcp_audit_entry *e;
    os_sha256 h;
    uint8_t rec[8];
    if (r->audit_count >= OS_MCP_AUDIT_MAX) return;
    e = &r->audit[r->audit_count];
    e->sequence = r->audit_count + 1u;
    e->event = (uint8_t)event;
    e->tool = (int8_t)tool;
    e->result = (int16_t)result;
    rec[0] = (uint8_t)e->sequence; rec[1] = (uint8_t)(e->sequence >> 8);
    rec[2] = (uint8_t)(e->sequence >> 16); rec[3] = (uint8_t)(e->sequence >> 24);
    rec[4] = e->event; rec[5] = (uint8_t)e->tool;
    rec[6] = (uint8_t)e->result; rec[7] = (uint8_t)((uint16_t)e->result >> 8);
    os_sha256_init(&h);
    os_sha256_update(&h, r->audit_head, 32);
    os_sha256_update(&h, rec, sizeof rec);
    os_sha256_final(&h, e->hash);
    for (unsigned i = 0; i < 32; i++) r->audit_head[i] = e->hash[i];
    r->audit_count++;
}

int os_mcp_audit_verify(const os_mcp_registry *r)
{
    if (!r || r->audit_count > OS_MCP_AUDIT_MAX) return r ? OS_MCP_ERR_FULL : OS_MCP_ERR_ARGUMENT;
    uint8_t prev[32] = {0};
    for (uint32_t i = 0; i < r->audit_count; i++) {
        const os_mcp_audit_entry *e = &r->audit[i];
        os_sha256 h;
        uint8_t rec[8], digest[32];
        rec[0] = (uint8_t)e->sequence; rec[1] = (uint8_t)(e->sequence >> 8);
        rec[2] = (uint8_t)(e->sequence >> 16); rec[3] = (uint8_t)(e->sequence >> 24);
        rec[4] = e->event; rec[5] = (uint8_t)e->tool;
        rec[6] = (uint8_t)e->result; rec[7] = (uint8_t)((uint16_t)e->result >> 8);
        os_sha256_init(&h);
        os_sha256_update(&h, prev, 32);
        os_sha256_update(&h, rec, sizeof rec);
        os_sha256_final(&h, digest);
        if (!digest_equal(digest, e->hash) || e->sequence != i + 1u) return OS_MCP_ERR_FINGERPRINT;
        for (unsigned k = 0; k < 32; k++) prev[k] = digest[k];
    }
    return digest_equal(prev, r->audit_head) ? OS_MCP_OK : OS_MCP_ERR_FINGERPRINT;
}

static int find(const os_mcp_registry *r, const char *name)
{
    for (uint32_t i = 0; i < OS_MCP_MAX_TOOLS; i++)
        if (r->tools[i].state != OS_MCP_TOOL_UNUSED && name_equal(r->tools[i].name, name))
            return (int)i;
    return -1;
}

static int tool_ok(const os_mcp_registry *r, int tool)
{
    return r && tool >= 0 && tool < (int)OS_MCP_MAX_TOOLS &&
           r->tools[tool].state != OS_MCP_TOOL_UNUSED;
}

static void current_fingerprint(const os_mcp_tool *t, uint8_t out[32])
{
    os_mcp_tool_fingerprint(t->name, t->version, t->schema, t->schema_len, out);
}

int os_mcp_tool_register(os_mcp_registry *r, const char *name, uint32_t version,
                         const char *schema, size_t schema_len,
                         uint32_t required_caps, int side_effecting)
{
    size_t n;
    if (!r || !name || (!schema && schema_len)) return OS_MCP_ERR_ARGUMENT;
    if (!name_valid(name, &n) || version == 0 || schema_len == 0 || schema_len > OS_MCP_SCHEMA_MAX)
        return OS_MCP_ERR_NAME;
    if (find(r, name) >= 0) return OS_MCP_ERR_DUPLICATE;
    for (uint32_t i = 0; i < OS_MCP_MAX_TOOLS; i++) {
        os_mcp_tool *t = &r->tools[i];
        if (t->state != OS_MCP_TOOL_UNUSED) continue;
        zero(t, sizeof *t);
        for (size_t k = 0; k < n; k++) t->name[k] = name[k];
        for (size_t k = 0; k < schema_len; k++) t->schema[k] = schema[k];
        t->schema_len = (uint16_t)schema_len;
        t->version = version;
        t->required_caps = required_caps;
        t->side_effecting = side_effecting ? 1u : 0u;
        t->state = OS_MCP_TOOL_PENDING;
        audit(r, OS_MCP_AUDIT_REGISTER, (int)i, OS_MCP_OK);
        return (int)i;
    }
    return OS_MCP_ERR_FULL;
}

int os_mcp_tool_approve(os_mcp_registry *r, int tool, const uint8_t expected[32], uint64_t expiry)
{
    uint8_t fp[32];
    if (!tool_ok(r, tool) || !expected || expiry == 0) return OS_MCP_ERR_ARGUMENT;
    os_mcp_tool *t = &r->tools[tool];
    if (t->state == OS_MCP_TOOL_REVOKED) return OS_MCP_ERR_NOT_APPROVED;
    current_fingerprint(t, fp);
    if (!digest_equal(fp, expected)) {
        audit(r, OS_MCP_AUDIT_APPROVE, tool, OS_MCP_ERR_FINGERPRINT);
        return OS_MCP_ERR_FINGERPRINT;
    }
    for (unsigned i = 0; i < 32; i++) t->approved_fingerprint[i] = fp[i];
    t->approval_expiry = expiry;
    t->state = OS_MCP_TOOL_APPROVED;
    audit(r, OS_MCP_AUDIT_APPROVE, tool, OS_MCP_OK);
    return OS_MCP_OK;
}

int os_mcp_tool_revoke(os_mcp_registry *r, int tool)
{
    if (!tool_ok(r, tool)) return OS_MCP_ERR_ARGUMENT;
    r->tools[tool].state = OS_MCP_TOOL_REVOKED;
    audit(r, OS_MCP_AUDIT_REVOKE, tool, OS_MCP_OK);
    return OS_MCP_OK;
}

int os_mcp_tool_update_schema(os_mcp_registry *r, int tool, const char *schema, size_t schema_len)
{
    if (!tool_ok(r, tool) || !schema || schema_len == 0 || schema_len > OS_MCP_SCHEMA_MAX)
        return OS_MCP_ERR_ARGUMENT;
    os_mcp_tool *t = &r->tools[tool];
    for (size_t k = 0; k < schema_len; k++) t->schema[k] = schema[k];
    t->schema_len = (uint16_t)schema_len;
    /* A changed schema is a new identity: prior approval no longer covers it. */
    if (t->state == OS_MCP_TOOL_APPROVED) t->state = OS_MCP_TOOL_PENDING;
    audit(r, OS_MCP_AUDIT_SCHEMA_CHANGE, tool, OS_MCP_OK);
    return OS_MCP_OK;
}

int os_mcp_authorize_call(os_mcp_registry *r, const char *name, uint32_t caller_caps,
                          uint64_t now, uint64_t nonce, int owner_confirmed)
{
    uint8_t fp[32];
    int tool, result = OS_MCP_OK;
    if (!r || !name || nonce == 0) return OS_MCP_ERR_ARGUMENT;
    /* Fail closed: an action that cannot be audited is not allowed. */
    if (r->audit_count + 2u > OS_MCP_AUDIT_MAX) return OS_MCP_ERR_FULL;
    tool = find(r, name);
    if (tool < 0) return OS_MCP_ERR_NOT_FOUND;
    const os_mcp_tool *t = &r->tools[tool];
    current_fingerprint(t, fp);
    if (t->state != OS_MCP_TOOL_APPROVED) result = OS_MCP_ERR_NOT_APPROVED;
    else if (!digest_equal(fp, t->approved_fingerprint)) result = OS_MCP_ERR_FINGERPRINT;
    else if (now >= t->approval_expiry) result = OS_MCP_ERR_EXPIRED;
    else if ((caller_caps & t->required_caps) != t->required_caps) result = OS_MCP_ERR_CAPABILITY;
    else if (t->side_effecting && !owner_confirmed) result = OS_MCP_ERR_SIDE_EFFECT;
    else {
        for (unsigned i = 0; i < OS_MCP_NONCE_WINDOW; i++)
            if (r->nonces[i] == nonce) { result = OS_MCP_ERR_REPLAY; break; }
    }
    if (result == OS_MCP_OK) {
        r->nonces[r->nonce_next++ % OS_MCP_NONCE_WINDOW] = nonce;
        audit(r, OS_MCP_AUDIT_CALL_ALLOWED, tool, OS_MCP_OK);
        return tool;
    }
    audit(r, OS_MCP_AUDIT_CALL_DENIED, tool, result);
    return result;
}
