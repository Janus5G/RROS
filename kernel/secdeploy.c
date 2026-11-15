#include "secdeploy.h"
#include "chacha20.h"
#include "hmac_sha256.h"
#include "sha256.h"

static void copy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
}

static void wipe(void *p, size_t n)
{
    volatile uint8_t *b = (volatile uint8_t *)p;
    while (n--) {
        *b++ = 0;
    }
}

static int ct_equal(const uint8_t *a, const uint8_t *b, size_t n)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < n; i++) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return diff == 0;
}

static size_t bounded_len(const char *s, size_t max)
{
    size_t n = 0;
    while (n < max && s[n]) {
        n++;
    }
    return n;
}

static int str_eq(const char *a, const char *b, size_t max)
{
    size_t i = 0;
    while (i < max && a[i] && a[i] == b[i]) {
        i++;
    }
    return i == max || a[i] == b[i];
}

static int id_valid(const char *id, size_t max)
{
    if (!id) {
        return 0;
    }
    size_t n = bounded_len(id, max);
    if (n == 0 || n >= max) {
        return 0;
    }
    for (size_t i = 0; i < n; i++) {
        char c = id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return 0;
        }
    }
    return 1;
}

static void id_pad(uint8_t out[SECD_ID_MAX], const char *id)
{
    size_t n = bounded_len(id, SECD_ID_MAX - 1u);
    wipe(out, SECD_ID_MAX);
    copy(out, id, n);
}

static void put64(uint8_t *p, uint64_t v)
{
    for (unsigned i = 0; i < 8; i++) {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}

static void put32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; i++) {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}

static uint64_t get64(const uint8_t *p)
{
    uint64_t v = 0;
    for (unsigned i = 0; i < 8; i++) {
        v |= (uint64_t)p[i] << (8 * i);
    }
    return v;
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int authorize(secd_node *n, const sec_token *t, uint32_t perm, uint64_t now, int *actor)
{
    if (sec_authorize(n->sec, t, perm, now) != SEC_OK) {
        return SECD_ERR_DENIED;
    }
    *actor = sec_session_principal(n->sec, t, now);
    return SECD_OK;
}

int secd_node_init(secd_node *node, sec_system *sec, const char *id, const char *arch, uint32_t quota_kb)
{
    if (!node || !sec || !id_valid(id, SECD_ID_MAX) || !arch || bounded_len(arch, sizeof node->arch) >= sizeof node->arch) {
        return SECD_ERR_ARGUMENT;
    }
    wipe(node, sizeof *node);
    node->sec = sec;
    copy(node->id, id, bounded_len(id, SECD_ID_MAX - 1u));
    copy(node->arch, arch, bounded_len(arch, sizeof node->arch - 1u));
    node->quota_kb = quota_kb;
    return SECD_OK;
}

void secd_key_id(const uint8_t key[16], uint8_t id[8])
{
    uint8_t digest[32];
    os_sha256_hash(key, 16, digest);
    copy(id, digest, 8);
}

static secd_key *find_key(secd_node *n, const uint8_t key_id[8])
{
    for (unsigned i = 0; i < SECD_MAX_KEYS; i++) {
        if (n->keys[i].used && ct_equal(n->keys[i].key_id, key_id, 8)) {
            return &n->keys[i];
        }
    }
    return (secd_key *)0;
}

static int key_usable(const secd_key *k, uint64_t now)
{
    return k && !k->revoked && now >= k->not_before && now < k->not_after;
}

int secd_key_add(secd_node *node, const sec_token *token, const uint8_t key[16], uint64_t not_before,
                 uint64_t not_after, uint64_t now)
{
    int actor = -1;
    uint8_t id[8];
    if (authorize(node, token, SEC_PERM_MANAGE_KEYS, now, &actor) != SECD_OK) {
        return SECD_ERR_DENIED;
    }
    if (not_after <= not_before) {
        return SECD_ERR_ARGUMENT;
    }
    secd_key_id(key, id);
    if (find_key(node, id)) {
        return SECD_ERR_ARGUMENT;
    }
    for (unsigned i = 0; i < SECD_MAX_KEYS; i++) {
        if (!node->keys[i].used) {
            node->keys[i].used = 1;
            node->keys[i].revoked = 0;
            copy(node->keys[i].key_id, id, 8);
            copy(node->keys[i].key, key, 16);
            node->keys[i].not_before = not_before;
            node->keys[i].not_after = not_after;
            sec_audit_log(node->sec, actor, SEC_A_KEY_ADD, SEC_OK, now);
            return SECD_OK;
        }
    }
    return SECD_ERR_FULL;
}

int secd_key_revoke(secd_node *node, const sec_token *token, const uint8_t key_id[8], uint64_t now)
{
    int actor = -1;
    if (authorize(node, token, SEC_PERM_MANAGE_KEYS, now, &actor) != SECD_OK) {
        return SECD_ERR_DENIED;
    }
    secd_key *k = find_key(node, key_id);
    if (!k) {
        return SECD_ERR_NOT_FOUND;
    }
    k->revoked = 1;   /* the key material stays only so the identity remains known as revoked */
    wipe(k->key, sizeof k->key);
    sec_audit_log(node->sec, actor, SEC_A_KEY_REVOKE, SEC_OK, now);
    return SECD_OK;
}

static secd_peer *find_peer(secd_node *n, const char *id)
{
    for (unsigned i = 0; i < SECD_MAX_PEERS; i++) {
        if (n->peers[i].used && str_eq(n->peers[i].id, id, SECD_ID_MAX)) {
            return &n->peers[i];
        }
    }
    return (secd_peer *)0;
}

int secd_peer_enroll(secd_node *node, const sec_token *token, const char *peer_id, const uint8_t psk[32],
                     uint64_t now)
{
    int actor = -1;
    uint8_t acc = 0;
    if (authorize(node, token, SEC_PERM_MANAGE_KEYS, now, &actor) != SECD_OK) {
        return SECD_ERR_DENIED;
    }
    for (unsigned i = 0; i < 32; i++) {
        acc |= psk[i];
    }
    if (!id_valid(peer_id, SECD_ID_MAX) || acc == 0 || find_peer(node, peer_id)) {
        return SECD_ERR_ARGUMENT;
    }
    for (unsigned i = 0; i < SECD_MAX_PEERS; i++) {
        if (!node->peers[i].used) {
            wipe(&node->peers[i], sizeof node->peers[i]);
            node->peers[i].used = 1;
            copy(node->peers[i].id, peer_id, bounded_len(peer_id, SECD_ID_MAX - 1u));
            copy(node->peers[i].psk, psk, 32);
            sec_audit_log(node->sec, actor, SEC_A_PEER_ENROLL, SEC_OK, now);
            return SECD_OK;
        }
    }
    return SECD_ERR_FULL;
}

int secd_peer_revoke(secd_node *node, const sec_token *token, const char *peer_id, uint64_t now)
{
    int actor = -1;
    if (authorize(node, token, SEC_PERM_MANAGE_KEYS, now, &actor) != SECD_OK) {
        return SECD_ERR_DENIED;
    }
    secd_peer *p = id_valid(peer_id, SECD_ID_MAX) ? find_peer(node, peer_id) : (secd_peer *)0;
    if (!p) {
        return SECD_ERR_NOT_FOUND;
    }
    p->revoked = 1;
    wipe(p->psk, sizeof p->psk);
    sec_audit_log(node->sec, actor, SEC_A_PEER_REVOKE, SEC_OK, now);
    return SECD_OK;
}

int secd_set_rollback_policy(secd_node *node, const sec_token *token, int authorized, uint64_t now)
{
    int actor = -1;
    if (authorize(node, token, SEC_PERM_MANAGE_KEYS, now, &actor) != SECD_OK) {
        return SECD_ERR_DENIED;
    }
    node->rollback_authorized = authorized ? 1 : 0;
    return SECD_OK;
}

/* ---------------- envelope and frame ---------------- */

static void envelope_signature(const uint8_t key[16], const uint8_t key_id[8], const uint8_t *inner, size_t inner_len,
                               uint8_t out[32])
{
    uint8_t msg[9 + 8 + 32];
    uint8_t digest[32];
    os_sha256_hash(inner, inner_len, digest);
    copy(msg, "SPK1-SIG", 9);
    copy(msg + 9, key_id, 8);
    copy(msg + 17, digest, 32);
    os_hmac_sha256(key, 16, msg, sizeof msg, out);
}

int secd_envelope_build(const uint8_t *inner, size_t inner_len, const uint8_t key[16], uint8_t *out, size_t cap,
                        size_t *out_len)
{
    uint8_t key_id[8];
    if (!inner || !key || !out || !out_len || inner_len == 0 || inner_len > OS_PKG_MAX_BYTES ||
        cap < SECD_ENVELOPE_HEADER + inner_len + 32u) {
        return SECD_ERR_ARGUMENT;
    }
    secd_key_id(key, key_id);
    copy(out, "SPK1", 4);
    out[4] = 1; out[5] = 0; out[6] = 0; out[7] = 0;
    copy(out + 8, key_id, 8);
    put32(out + 16, (uint32_t)inner_len);
    copy(out + SECD_ENVELOPE_HEADER, inner, inner_len);
    envelope_signature(key, key_id, inner, inner_len, out + SECD_ENVELOPE_HEADER + inner_len);
    *out_len = SECD_ENVELOPE_HEADER + inner_len + 32u;
    return SECD_OK;
}

static void derive(const uint8_t psk[32], const char *label, const uint8_t sender[SECD_ID_MAX],
                   const uint8_t recipient[SECD_ID_MAX], uint8_t out[32])
{
    uint8_t msg[4 + 2 * SECD_ID_MAX];
    copy(msg, label, 4);
    copy(msg + 4, sender, SECD_ID_MAX);
    copy(msg + 4 + SECD_ID_MAX, recipient, SECD_ID_MAX);
    os_hmac_sha256(psk, 32, msg, sizeof msg, out);
}

static void nonce_for(uint64_t seq, uint8_t nonce[12])
{
    wipe(nonce, 12);
    put64(nonce + 4, seq);
}

static void frame_tag(const uint8_t mac_key[32], const uint8_t *frame, size_t body_len, uint8_t out[32])
{
    /* HMAC over header and ciphertext; the buffer is the frame itself, so no copy is needed. */
    os_hmac_sha256(mac_key, 32, frame, body_len, out);
}

int secd_send(secd_node *node, const sec_token *token, const char *recipient, const uint8_t *plain, size_t length,
              uint8_t *frame, size_t cap, size_t *frame_len, uint64_t now)
{
    int actor = -1;
    uint8_t sender_id[SECD_ID_MAX], recipient_id[SECD_ID_MAX], enc[32], mac[32], nonce[12];
    if (authorize(node, token, SEC_PERM_SEND_PACKAGE, now, &actor) != SECD_OK) {
        return SECD_ERR_DENIED;
    }
    if (!plain || !frame || !frame_len || length == 0 || length > SECD_ENVELOPE_MAX ||
        cap < SECD_FRAME_HEADER + length + 32u || !id_valid(recipient, SECD_ID_MAX)) {
        return SECD_ERR_ARGUMENT;
    }
    secd_peer *peer = find_peer(node, recipient);
    if (!peer || peer->revoked) {
        return SECD_ERR_PEER;
    }
    uint64_t seq = ++node->send_seq;
    id_pad(sender_id, node->id);
    id_pad(recipient_id, recipient);
    copy(frame, "SFR1", 4);
    copy(frame + 4, sender_id, SECD_ID_MAX);
    copy(frame + 4 + SECD_ID_MAX, recipient_id, SECD_ID_MAX);
    put64(frame + 36, seq);
    put32(frame + 44, (uint32_t)length);
    derive(peer->psk, "ENC1", sender_id, recipient_id, enc);
    derive(peer->psk, "MAC1", sender_id, recipient_id, mac);
    nonce_for(seq, nonce);
    os_chacha20_xor(enc, nonce, 1, plain, frame + SECD_FRAME_HEADER, length);
    frame_tag(mac, frame, SECD_FRAME_HEADER + length, frame + SECD_FRAME_HEADER + length);
    *frame_len = SECD_FRAME_HEADER + length + 32u;
    wipe(enc, sizeof enc);
    wipe(mac, sizeof mac);
    sec_audit_log(node->sec, actor, SEC_A_SEND, SEC_OK, now);
    return SECD_OK;
}

/* ---------------- receive and stage ---------------- */

static int reject(secd_node *n, int actor, uint32_t action, int rc, uint64_t now)
{
    sec_audit_log(n->sec, actor, action, rc, now);
    return rc;
}

static int find_installed_idx(const secd_node *n, const char *id)
{
    for (unsigned i = 0; i < SECD_MAX_INSTALLED; i++) {
        if (n->installed[i].used && str_eq(n->installed[i].id, id, OS_PKG_ID_MAX)) {
            return (int)i;
        }
    }
    return -1;
}

int secd_find_installed(const secd_node *node, const char *id)
{
    if (!id_valid(id, OS_PKG_ID_MAX)) {
        return -1;
    }
    return find_installed_idx(node, id);
}

int secd_find_staged(const secd_node *node, const char *id, uint32_t version)
{
    if (!id_valid(id, OS_PKG_ID_MAX)) {
        return -1;
    }
    for (unsigned i = 0; i < SECD_MAX_STAGED; i++) {
        if (node->staged[i].used && node->staged[i].manifest.version == version &&
            str_eq(node->staged[i].manifest.id, id, OS_PKG_ID_MAX)) {
            return (int)i;
        }
    }
    return -1;
}

static uint32_t floor_version(const secd_node *n, const char *id)
{
    for (unsigned i = 0; i < SECD_MAX_INSTALLED; i++) {
        if (n->floors[i].used && str_eq(n->floors[i].id, id, OS_PKG_ID_MAX)) {
            return n->floors[i].version;
        }
    }
    return 0;
}

static secd_floor *floor_slot(secd_node *n, const char *id)
{
    secd_floor *free_slot = (secd_floor *)0;
    for (unsigned i = 0; i < SECD_MAX_INSTALLED; i++) {
        if (n->floors[i].used && str_eq(n->floors[i].id, id, OS_PKG_ID_MAX)) {
            return &n->floors[i];
        }
        if (!n->floors[i].used && !free_slot) {
            free_slot = &n->floors[i];
        }
    }
    return free_slot;
}

int secd_receive(secd_node *node, const sec_token *token, const uint8_t *frame, size_t length, uint64_t now)
{
    int actor = -1;
    uint8_t self_id[SECD_ID_MAX], sender_id[SECD_ID_MAX], enc[32], mac[32], tag[32], nonce[12];
    if (authorize(node, token, SEC_PERM_REQUEST_INSTALL, now, &actor) != SECD_OK) {
        return SECD_ERR_DENIED;
    }
    if (sec_charge_request(node->sec, token, now) != SEC_OK) {
        return reject(node, actor, SEC_A_FRAME_REJECT, SECD_ERR_QUOTA, now);
    }
    if (node->journal_valid) {
        return reject(node, actor, SEC_A_FRAME_REJECT, SECD_ERR_STATE, now);
    }
    if (!frame || length < SECD_FRAME_HEADER + 1u + 32u || length > SECD_FRAME_MAX || frame[0] != 'S' ||
        frame[1] != 'F' || frame[2] != 'R' || frame[3] != '1') {
        return reject(node, actor, SEC_A_FRAME_REJECT, SECD_ERR_FORMAT, now);
    }
    uint32_t body = get32(frame + 44);
    if (body == 0 || body > SECD_ENVELOPE_MAX || length != SECD_FRAME_HEADER + (size_t)body + 32u) {
        return reject(node, actor, SEC_A_FRAME_REJECT, SECD_ERR_FORMAT, now);
    }
    id_pad(self_id, node->id);
    copy(sender_id, frame + 4, SECD_ID_MAX);
    if (!ct_equal(frame + 4 + SECD_ID_MAX, self_id, SECD_ID_MAX) || sender_id[SECD_ID_MAX - 1] != 0) {
        return reject(node, actor, SEC_A_FRAME_REJECT, SECD_ERR_PEER, now);
    }
    secd_peer *peer = find_peer(node, (const char *)sender_id);
    if (!peer || peer->revoked) {
        return reject(node, actor, SEC_A_FRAME_REJECT, SECD_ERR_PEER, now);
    }
    derive(peer->psk, "MAC1", sender_id, self_id, mac);
    frame_tag(mac, frame, SECD_FRAME_HEADER + body, tag);
    int tag_ok = ct_equal(tag, frame + SECD_FRAME_HEADER + body, 32);
    wipe(mac, sizeof mac);
    if (!tag_ok) {
        return reject(node, actor, SEC_A_FRAME_REJECT, SECD_ERR_AUTH, now);
    }
    uint64_t seq = get64(frame + 36);
    if (seq <= peer->last_seq) {
        return reject(node, actor, SEC_A_FRAME_REJECT, SECD_ERR_REPLAY, now);
    }
    peer->last_seq = seq;
    derive(peer->psk, "ENC1", sender_id, self_id, enc);
    nonce_for(seq, nonce);
    os_chacha20_xor(enc, nonce, 1, frame + SECD_FRAME_HEADER, node->scratch, body);
    wipe(enc, sizeof enc);

    const uint8_t *env = node->scratch;
    if (body < SECD_ENVELOPE_HEADER + 1u + 32u || env[0] != 'S' || env[1] != 'P' || env[2] != 'K' || env[3] != '1' ||
        env[4] != 1 || env[5] != 0 || env[6] != 0 || env[7] != 0) {
        return reject(node, actor, SEC_A_PKG_REJECT, SECD_ERR_FORMAT, now);
    }
    uint32_t inner_len = get32(env + 16);
    if (inner_len == 0 || inner_len > OS_PKG_MAX_BYTES || body != SECD_ENVELOPE_HEADER + inner_len + 32u) {
        return reject(node, actor, SEC_A_PKG_REJECT, SECD_ERR_FORMAT, now);
    }
    const uint8_t *inner = env + SECD_ENVELOPE_HEADER;
    secd_key *key = find_key(node, env + 8);
    if (!key_usable(key, now)) {
        return reject(node, actor, SEC_A_PKG_REJECT, SECD_ERR_KEY, now);
    }
    uint8_t sig[32];
    envelope_signature(key->key, key->key_id, inner, inner_len, sig);
    if (!ct_equal(sig, inner + inner_len, 32)) {
        return reject(node, actor, SEC_A_PKG_REJECT, SECD_ERR_SIGNATURE, now);
    }
    os_pkg_view view;
    int rc = os_pkg_inspect(inner, inner_len, &view);
    if (rc != OS_PKG_OK) {
        return reject(node, actor, SEC_A_PKG_REJECT, rc == OS_PKG_ERR_INTEGRITY ? SECD_ERR_INTEGRITY : SECD_ERR_MANIFEST, now);
    }
    const os_pkg_manifest *m = &view.manifest;
    if (m->version == 0 || !id_valid(m->id, OS_PKG_ID_MAX)) {
        return reject(node, actor, SEC_A_PKG_REJECT, SECD_ERR_MANIFEST, now);
    }
    if (!str_eq(m->arch, "any", sizeof m->arch) && !str_eq(m->arch, node->arch, sizeof m->arch)) {
        return reject(node, actor, SEC_A_PKG_REJECT, SECD_ERR_COMPAT, now);
    }
    if (m->dependency[0]) {
        int d = find_installed_idx(node, m->dependency);
        if (d < 0 || node->installed[d].pending) {
            return reject(node, actor, SEC_A_PKG_REJECT, SECD_ERR_DEPENDENCY, now);
        }
    }
    int existing = find_installed_idx(node, m->id);
    uint32_t floor = floor_version(node, m->id);
    if (existing >= 0 && node->installed[existing].version > floor) {
        floor = node->installed[existing].version;
    }
    if (m->version <= floor) {
        return reject(node, actor, SEC_A_PKG_REJECT, SECD_ERR_VERSION, now);
    }
    uint64_t need = (uint64_t)node->used_kb + m->size_kb - (existing >= 0 ? node->installed[existing].size_kb : 0u);
    if (need > node->quota_kb) {
        return reject(node, actor, SEC_A_PKG_REJECT, SECD_ERR_QUOTA, now);
    }
    secd_staged *slot = (secd_staged *)0;
    for (unsigned i = 0; i < SECD_MAX_STAGED; i++) {
        if (node->staged[i].used && str_eq(node->staged[i].manifest.id, m->id, OS_PKG_ID_MAX)) {
            slot = &node->staged[i];
            break;
        }
        if (!node->staged[i].used && !slot) {
            slot = &node->staged[i];
        }
    }
    if (!slot) {
        return reject(node, actor, SEC_A_PKG_REJECT, SECD_ERR_FULL, now);
    }
    slot->used = 1;
    slot->manifest = *m;
    copy(slot->digest, view.digest, 32);
    copy(slot->key_id, key->key_id, 8);
    sec_audit_log(node->sec, actor, SEC_A_PKG_STAGE, SEC_OK, now);
    return SECD_OK;
}

/* ---------------- approval, install, rollback, recovery ---------------- */

int secd_approve(secd_node *node, const sec_token *token, const char *id, uint32_t version, const uint8_t digest[32],
                 uint64_t expiry, uint64_t now)
{
    int actor = -1;
    if (authorize(node, token, SEC_PERM_APPROVE_INSTALL, now, &actor) != SECD_OK) {
        return SECD_ERR_DENIED;
    }
    int s = secd_find_staged(node, id, version);
    if (s < 0) {
        return SECD_ERR_NOT_FOUND;
    }
    if (!ct_equal(node->staged[s].digest, digest, 32) || expiry <= now) {
        return reject(node, actor, SEC_A_APPROVE, SECD_ERR_APPROVAL, now);
    }
    secd_approval *slot = (secd_approval *)0;
    for (unsigned i = 0; i < SECD_MAX_APPROVALS; i++) {
        if (node->approvals[i].used && node->approvals[i].version == version &&
            str_eq(node->approvals[i].id, id, OS_PKG_ID_MAX)) {
            slot = &node->approvals[i];
            break;
        }
        if (!node->approvals[i].used && !slot) {
            slot = &node->approvals[i];
        }
    }
    if (!slot) {
        return SECD_ERR_FULL;
    }
    slot->used = 1;
    copy(slot->id, id, bounded_len(id, OS_PKG_ID_MAX - 1u));
    slot->id[bounded_len(id, OS_PKG_ID_MAX - 1u)] = 0;
    slot->version = version;
    copy(slot->digest, digest, 32);
    slot->expiry = expiry;
    sec_audit_log(node->sec, actor, SEC_A_APPROVE, SEC_OK, now);
    return SECD_OK;
}

int secd_discard(secd_node *node, const sec_token *token, const char *id, uint32_t version, uint64_t now)
{
    int actor = -1;
    if (authorize(node, token, SEC_PERM_REQUEST_INSTALL, now, &actor) != SECD_OK) {
        return SECD_ERR_DENIED;
    }
    int s = secd_find_staged(node, id, version);
    if (s < 0) {
        return SECD_ERR_NOT_FOUND;
    }
    for (unsigned i = 0; i < SECD_MAX_APPROVALS; i++) {
        if (node->approvals[i].used && node->approvals[i].version == version &&
            str_eq(node->approvals[i].id, id, OS_PKG_ID_MAX)) {
            wipe(&node->approvals[i], sizeof node->approvals[i]);
        }
    }
    wipe(&node->staged[s], sizeof node->staged[s]);
    sec_audit_log(node->sec, actor, SEC_A_PKG_REJECT, SEC_OK, now);
    return SECD_OK;
}

static void snapshot(secd_node *n)
{
    copy(n->journal, n->installed, sizeof n->installed);
    n->journal_used_kb = n->used_kb;
    n->journal_valid = 1;
}

static void restore(secd_node *n)
{
    copy(n->installed, n->journal, sizeof n->installed);
    n->used_kb = n->journal_used_kb;
    n->journal_valid = 0;
}

int secd_install(secd_node *node, const sec_token *token, const char *id, uint32_t version, uint64_t now, int fault)
{
    int actor = -1;
    if (authorize(node, token, SEC_PERM_REQUEST_INSTALL, now, &actor) != SECD_OK) {
        return SECD_ERR_DENIED;
    }
    if (sec_charge_request(node->sec, token, now) != SEC_OK) {
        return reject(node, actor, SEC_A_INSTALL_FAIL, SECD_ERR_QUOTA, now);
    }
    if (node->journal_valid) {
        return reject(node, actor, SEC_A_INSTALL_FAIL, SECD_ERR_STATE, now);
    }
    int s = secd_find_staged(node, id, version);
    if (s < 0) {
        return reject(node, actor, SEC_A_INSTALL_FAIL, SECD_ERR_NOT_FOUND, now);
    }
    secd_staged *st = &node->staged[s];
    secd_approval *ap = (secd_approval *)0;
    for (unsigned i = 0; i < SECD_MAX_APPROVALS; i++) {
        if (node->approvals[i].used && node->approvals[i].version == version &&
            str_eq(node->approvals[i].id, id, OS_PKG_ID_MAX) &&
            ct_equal(node->approvals[i].digest, st->digest, 32) && now < node->approvals[i].expiry) {
            ap = &node->approvals[i];
        }
    }
    if (!ap) {
        return reject(node, actor, SEC_A_INSTALL_FAIL, SECD_ERR_APPROVAL, now);
    }
    if (!key_usable(find_key(node, st->key_id), now)) {
        return reject(node, actor, SEC_A_INSTALL_FAIL, SECD_ERR_KEY, now);
    }
    const os_pkg_manifest *m = &st->manifest;
    if (m->dependency[0]) {
        int d = find_installed_idx(node, m->dependency);
        if (d < 0 || node->installed[d].pending) {
            return reject(node, actor, SEC_A_INSTALL_FAIL, SECD_ERR_DEPENDENCY, now);
        }
    }
    int existing = find_installed_idx(node, m->id);
    uint32_t floor = floor_version(node, m->id);
    if (existing >= 0 && node->installed[existing].version > floor) {
        floor = node->installed[existing].version;
    }
    if (m->version <= floor) {
        return reject(node, actor, SEC_A_INSTALL_FAIL, SECD_ERR_VERSION, now);
    }
    uint64_t need = (uint64_t)node->used_kb + m->size_kb - (existing >= 0 ? node->installed[existing].size_kb : 0u);
    if (need > node->quota_kb) {
        return reject(node, actor, SEC_A_INSTALL_FAIL, SECD_ERR_QUOTA, now);
    }
    int slot = existing;
    for (unsigned i = 0; slot < 0 && i < SECD_MAX_INSTALLED; i++) {
        if (!node->installed[i].used) {
            slot = (int)i;
        }
    }
    secd_floor *fl = floor_slot(node, m->id);
    if (slot < 0 || !fl) {
        return reject(node, actor, SEC_A_INSTALL_FAIL, SECD_ERR_FULL, now);
    }
    if (fault == SECD_FAULT_INTERRUPT_BEFORE) {
        return reject(node, actor, SEC_A_INSTALL_FAIL, SECD_ERR_FAULT, now);
    }

    snapshot(node);
    secd_installed *in = &node->installed[slot];
    if (existing >= 0) {
        in->previous = in->version;
        in->previous_size_kb = in->size_kb;
        copy(in->previous_digest, in->digest, 32);
    } else {
        wipe(in, sizeof *in);
        in->used = 1;
        copy(in->id, m->id, bounded_len(m->id, OS_PKG_ID_MAX - 1u));
    }
    in->version = m->version;
    in->size_kb = m->size_kb;
    copy(in->digest, st->digest, 32);
    copy(in->key_id, st->key_id, 8);
    in->pending = 1;
    node->used_kb = (uint32_t)need;
    if (fault == SECD_FAULT_CRASH_MID) {
        return SECD_ERR_FAULT;   /* simulated power loss: journal and pending flag survive for secd_recover */
    }
    if (fault == SECD_FAULT_VERIFY_FAIL) {
        restore(node);
        ap->used = 0;
        return reject(node, actor, SEC_A_INSTALL_FAIL, SECD_ERR_FAULT, now);
    }
    in->pending = 0;
    node->journal_valid = 0;
    fl->used = 1;
    copy(fl->id, m->id, bounded_len(m->id, OS_PKG_ID_MAX - 1u));
    fl->id[bounded_len(m->id, OS_PKG_ID_MAX - 1u)] = 0;
    fl->version = m->version;
    ap->used = 0;
    st->used = 0;
    sec_audit_log(node->sec, actor, SEC_A_INSTALL_OK, SEC_OK, now);
    return SECD_OK;
}

int secd_rollback(secd_node *node, const sec_token *token, const char *id, uint64_t now)
{
    int actor = -1;
    if (authorize(node, token, SEC_PERM_ROLLBACK, now, &actor) != SECD_OK) {
        return SECD_ERR_DENIED;
    }
    if (!node->rollback_authorized) {
        return reject(node, actor, SEC_A_ROLLBACK, SECD_ERR_POLICY, now);
    }
    if (node->journal_valid) {
        return reject(node, actor, SEC_A_ROLLBACK, SECD_ERR_STATE, now);
    }
    int i = secd_find_installed(node, id);
    if (i < 0 || node->installed[i].previous == 0) {
        return reject(node, actor, SEC_A_ROLLBACK, SECD_ERR_NOT_FOUND, now);
    }
    secd_installed *in = &node->installed[i];
    node->used_kb = node->used_kb - in->size_kb + in->previous_size_kb;
    in->version = in->previous;
    in->size_kb = in->previous_size_kb;
    copy(in->digest, in->previous_digest, 32);
    in->previous = 0;
    in->previous_size_kb = 0;
    wipe(in->previous_digest, 32);
    sec_audit_log(node->sec, actor, SEC_A_ROLLBACK, SEC_OK, now);
    return SECD_OK;
}

int secd_recover(secd_node *node, const sec_token *token, uint64_t now)
{
    int actor = -1;
    if (authorize(node, token, SEC_PERM_RECOVER, now, &actor) != SECD_OK) {
        return SECD_ERR_DENIED;
    }
    if (!node->journal_valid) {
        return SECD_ERR_STATE;
    }
    restore(node);
    sec_audit_log(node->sec, actor, SEC_A_RECOVER, SEC_OK, now);
    return SECD_OK;
}

unsigned secd_count_revoked_installed(const secd_node *node)
{
    unsigned count = 0;
    for (unsigned i = 0; i < SECD_MAX_INSTALLED; i++) {
        if (!node->installed[i].used) {
            continue;
        }
        for (unsigned k = 0; k < SECD_MAX_KEYS; k++) {
            if (node->keys[k].used && node->keys[k].revoked &&
                ct_equal(node->keys[k].key_id, node->installed[i].key_id, 8)) {
                count++;
                break;
            }
        }
    }
    return count;
}
