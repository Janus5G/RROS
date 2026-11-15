#ifndef EXPERIMENTALOS_KERNEL_COMPAT_REGISTRY_H
#define EXPERIMENTALOS_KERNEL_COMPAT_REGISTRY_H

/*
 * Registry of separately versioned compatibility adapters for the Janus5G
 * ecosystem. Entries are data: they grant no authority, preserve upstream
 * authorization models and never block unrelated OS functionality when an
 * optional dependency is unavailable.
 */

#include <stddef.h>

enum os_compat_status {
    OS_COMPAT_SOFTWARE_VERIFIED = 1, /* host/fixture verified; never hardware or end-to-end */
    OS_COMPAT_PENDING,               /* required dependency or live upstream verification missing */
    OS_COMPAT_DISCREPANCY            /* upstream sources disagree with the master-plan definition */
};

/* Least-authority capability bits an adapter may declare. */
#define OS_COMPAT_CAP_VERIFY_DATA 0x1u
#define OS_COMPAT_CAP_READ_METADATA 0x2u
#define OS_COMPAT_CAP_LOOPBACK_CLIENT 0x4u
#define OS_COMPAT_CAP_ALLOWED (OS_COMPAT_CAP_VERIFY_DATA | OS_COMPAT_CAP_READ_METADATA | OS_COMPAT_CAP_LOOPBACK_CLIENT)

typedef struct {
    const char *id;
    unsigned version;
    const char *upstream;
    const char *revision;
    const char *license;
    int status;
    unsigned capabilities;
    size_t max_message_bytes;
    unsigned max_calls;
    int preserves_upstream_authorization;
    const char *note;
} os_compat_adapter;

size_t os_compat_count(void);
const os_compat_adapter *os_compat_at(size_t index);
const os_compat_adapter *os_compat_find(const char *id);
/* Returns 0 when every entry satisfies the structural and least-authority rules. */
int os_compat_validate_all(void);
/* Only software-verified adapters are usable; pending ones are inert, not errors. */
int os_compat_usable(const char *id);

/*
 * PRISME protocol shape from the master plan: four data colours, seven
 * intensity levels each, 28 data symbols; UV is reserved for control. Only
 * structure is validated: no byte encoding is defined because it could not be
 * verified against public upstream sources.
 */
#define OS_PRISME_DATA_COLORS 4u
#define OS_PRISME_LEVELS 7u
#define OS_PRISME_DATA_SYMBOLS 28u
int os_prisme_symbol_valid(unsigned color, unsigned level);
int os_prisme_channel_is_data(unsigned channel); /* channel 4 (UV) is control only */

#endif
