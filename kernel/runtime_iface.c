#include "runtime_iface.h"

static const os_runtime_desc table[] = {
    { "eosvm", OS_EOSVM_VERSION, OS_RT_AVAILABLE, 1, "bounded stack VM with assembler; text output only" },
    { "native-app", 1u, OS_RT_AVAILABLE, 0, "G09 native application image via the isolated process path" },
    { "c-sdk", 1u, OS_RT_AVAILABLE, 0, "host cross-compiled SDK examples (x86-64 and AArch64)" },
    { "python", 1u, OS_RT_PENDING, 0, "no guest interpreter; host-side tooling only (compatibility-pending)" },
};

static int str_eq(const char *a, const char *b) { size_t i = 0; while (a[i] && a[i] == b[i]) i++; return a[i] == b[i]; }

size_t os_runtime_count(void) { return sizeof table / sizeof table[0]; }
const os_runtime_desc *os_runtime_at(size_t index) { return index < os_runtime_count() ? &table[index] : (const os_runtime_desc *)0; }

const os_runtime_desc *os_runtime_find(const char *name)
{
    if (!name) return (const os_runtime_desc *)0;
    for (size_t i = 0; i < os_runtime_count(); i++) if (str_eq(table[i].name, name)) return &table[i];
    return (const os_runtime_desc *)0;
}

int os_runtime_execute(const char *name, const uint8_t *program, size_t length, uint32_t fuel,
                       os_eosvm_result *result)
{
    const os_runtime_desc *d;
    if (!name || !program || !result) return OS_RT_ERR_ARGUMENT;
    d = os_runtime_find(name);
    if (!d) return OS_RT_ERR_UNKNOWN;
    if (d->state != OS_RT_AVAILABLE) return OS_RT_ERR_UNAVAILABLE;
    if (!d->guest_executable) return OS_RT_ERR_NOT_EXECUTABLE;
    return os_eosvm_run(program, length, fuel, result) == OS_EVM_OK ? OS_RT_OK : OS_RT_ERR_RUN;
}
