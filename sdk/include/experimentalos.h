#ifndef EXPERIMENTALOS_SDK_H
#define EXPERIMENTALOS_SDK_H

#include "../../kernel/abi.h"

static inline long os_app_syscall3(
    u32 syscall_number,
    u64 argument0,
    u64 argument1,
    u64 argument2
)
{
#if defined(__x86_64__)
    long result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "0"((long)syscall_number),
          "D"(argument0), "S"(argument1), "d"(argument2)
        : "memory", "cc"
    );
    return result;
#elif defined(__aarch64__)
    register u64 x0 __asm__("x0") = argument0;
    register u64 x1 __asm__("x1") = argument1;
    register u64 x2 __asm__("x2") = argument2;
    register u64 x8 __asm__("x8") = syscall_number;
    __asm__ volatile(
        "svc #0"
        : "+r"(x0)
        : "r"(x1), "r"(x2), "r"(x8)
        : "memory", "cc"
    );
    return (long)x0;
#else
#error "ExperimentalOS native SDK supports only x86-64 and AArch64"
#endif
}

static inline long os_app_yield(void)
{
    return os_app_syscall3(OS_SYSCALL_YIELD, 0, 0, 0);
}

static inline long os_app_exit(u32 exit_code)
{
    return os_app_syscall3(OS_SYSCALL_EXIT, exit_code, 0, 0);
}

static inline long os_app_wait_thread(u32 thread_id)
{
    return os_app_syscall3(OS_SYSCALL_WAIT_THREAD, thread_id, 0, 0);
}

static inline long os_app_validate_memory(
    const void *address,
    u32 size,
    u32 permissions
)
{
    return os_app_syscall3(
        OS_SYSCALL_VALIDATE_RANGE,
        (u64)(usize)address,
        size,
        permissions
    );
}

#endif
