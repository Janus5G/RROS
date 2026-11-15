#include "compat_registry.h"

#define RF_REV "b0dc46f860faedb6f00a36b2c636a2b31c2b290e"
#define CN_REV "3ce209ccd2aeb52094fbaade4429632eb17d3340"
#define PR_REV "2270d347e93a3863582362474ddc333a557a90a6"
#define CP_REV "9ec06fba945f443ab57f25d2507fda42c02e1a4c"

static const os_compat_adapter adapters[] = {
    { "chromaneural-identity", 1, "Janus5G/ChromaNeural", CN_REV, "Apache-2.0",
      OS_COMPAT_SOFTWARE_VERIFIED, OS_COMPAT_CAP_VERIFY_DATA, 4096u, 0u, 1,
      "Verifies the nodeId binding (sha256 of the raw Ed25519 key); Ed25519 signatures and TLS certificates are not verified." },
    { "chromaneural-cnrb", 1, "Janus5G/ChromaNeural", CN_REV, "Apache-2.0",
      OS_COMPAT_SOFTWARE_VERIFIED, OS_COMPAT_CAP_VERIFY_DATA, 1052672u, 0u, 1,
      "CNRB v1 container verified byte-for-byte against the upstream transport.py; data only, never executed." },
    { "chromaneural-mcp-limits", 1, "Janus5G/ChromaNeural", CN_REV, "Apache-2.0",
      OS_COMPAT_PENDING, OS_COMPAT_CAP_VERIFY_DATA, 32768u, 4u, 1,
      "Limit profile only; tool-definition fingerprint canonicalization and live approval are not implemented." },
    { "chromaneural-node-admission", 1, "Janus5G/ChromaNeural", CN_REV, "Apache-2.0",
      OS_COMPAT_PENDING, 0u, 0u, 0u, 1,
      "Upstream marks live node admission and LAN interoperability NOT VERIFIED and remote execution disabled; no Ed25519 or TLS here." },
    { "roboforge-mcp", 1, "Janus5G/RoboForge", RF_REV, "MIT",
      OS_COMPAT_SOFTWARE_VERIFIED, OS_COMPAT_CAP_READ_METADATA | OS_COMPAT_CAP_LOOPBACK_CLIENT, 32768u, 4u, 1,
      "G15 loopback client adapter verified against a real local gateway; no ChromaNeural client test." },
    { "prisme-symbols", 1, "Janus5G/PRISME", PR_REV, "All rights reserved (no licence granted)",
      OS_COMPAT_DISCREPANCY, OS_COMPAT_CAP_VERIFY_DATA, 0u, 0u, 1,
      "Public README documents five channels, four levels and 10 bits per flash; the 28-symbol definition is not in public sources. No upstream code or vectors used." },
    { "chromaplex-data-tags", 1, "Janus5G/chromaplex-os", CP_REV, "MIT",
      OS_COMPAT_SOFTWARE_VERIFIED, OS_COMPAT_CAP_VERIFY_DATA, 1048576u, 0u, 1,
      "CPL/CPA are accepted only as inert tagged CNRB data; nothing is compiled or executed." },
    { "chromaplex-compiler", 1, "Janus5G/chromaplex-os", CP_REV, "MIT",
      OS_COMPAT_PENDING, 0u, 0u, 0u, 1,
      "Compiler is a Python tool; requires a Python runtime (G17) and independent verification." },
};

size_t os_compat_count(void) { return sizeof adapters / sizeof adapters[0]; }

const os_compat_adapter *os_compat_at(size_t index)
{
    return index < os_compat_count() ? &adapters[index] : 0;
}

static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static size_t cstr_len(const char *s) { size_t n = 0; while (s[n]) n++; return n; }

const os_compat_adapter *os_compat_find(const char *id)
{
    if (!id) return 0;
    for (size_t i = 0; i < os_compat_count(); i++)
        if (str_eq(adapters[i].id, id)) return &adapters[i];
    return 0;
}

int os_compat_validate_all(void)
{
    for (size_t i = 0; i < os_compat_count(); i++) {
        const os_compat_adapter *a = &adapters[i];
        if (!a->id || !a->id[0] || a->version == 0u || !a->upstream || !a->license || !a->note) return -1;
        if (cstr_len(a->revision) != 40u) return -2;
        if (a->capabilities & ~OS_COMPAT_CAP_ALLOWED) return -3;
        if (!a->preserves_upstream_authorization) return -4;
        if (a->status < OS_COMPAT_SOFTWARE_VERIFIED || a->status > OS_COMPAT_DISCREPANCY) return -5;
        if ((a->capabilities & OS_COMPAT_CAP_LOOPBACK_CLIENT) && a->max_calls == 0u) return -6;
        if (a->status != OS_COMPAT_SOFTWARE_VERIFIED && (a->capabilities & OS_COMPAT_CAP_LOOPBACK_CLIENT)) return -7;
        for (size_t j = i + 1u; j < os_compat_count(); j++)
            if (str_eq(a->id, adapters[j].id)) return -8;
    }
    return 0;
}

int os_compat_usable(const char *id)
{
    const os_compat_adapter *a = os_compat_find(id);
    return a && a->status == OS_COMPAT_SOFTWARE_VERIFIED;
}

int os_prisme_symbol_valid(unsigned color, unsigned level)
{
    return color < OS_PRISME_DATA_COLORS && level < OS_PRISME_LEVELS;
}

int os_prisme_channel_is_data(unsigned channel)
{
    return channel < OS_PRISME_DATA_COLORS;
}
