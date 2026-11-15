#ifndef EXPERIMENTALOS_KERNEL_RUNTIME_IFACE_H
#define EXPERIMENTALOS_KERNEL_RUNTIME_IFACE_H

#include <stddef.h>
#include <stdint.h>
#include "eosvm.h"

/* Versioned language-runtime interface table. Only AVAILABLE guest runtimes can execute programs. */
enum os_runtime_state { OS_RT_AVAILABLE = 1, OS_RT_PENDING = 2 };
enum os_runtime_status { OS_RT_OK = 0, OS_RT_ERR_ARGUMENT = -1, OS_RT_ERR_UNKNOWN = -2,
                         OS_RT_ERR_UNAVAILABLE = -3, OS_RT_ERR_NOT_EXECUTABLE = -4, OS_RT_ERR_RUN = -5 };

typedef struct {
    const char *name;
    uint32_t version;
    int state;
    int guest_executable;   /* 1 when os_runtime_execute can run it inside the OS */
    const char *note;
} os_runtime_desc;

size_t os_runtime_count(void);
const os_runtime_desc *os_runtime_at(size_t index);
const os_runtime_desc *os_runtime_find(const char *name);
int os_runtime_execute(const char *name, const uint8_t *program, size_t length, uint32_t fuel,
                       os_eosvm_result *result);

#endif
