#ifndef EXPERIMENTALOS_KERNEL_G05_H
#define EXPERIMENTALOS_KERNEL_G05_H

#include "process.h"

#define OS_G05_USER_CODE 0x100000000ull
#define OS_G05_USER_STACK (OS_G05_USER_CODE + OS_PAGE_SIZE)
#define OS_G05_USER_WINDOW_SIZE (2u * OS_PAGE_SIZE)
#define OS_G05_PROCESS_COUNT 2u

int os_g05_initialize(
    u64 code_physical,
    u64 stack0_physical,
    u64 stack1_physical,
    u64 kernel_probe
);
u32 os_g05_initial_thread(void);
u32 os_g05_thread_for_slot(usize slot);
u32 os_g05_process_for_slot(usize slot);
u32 os_g05_schedule_syscall(
    u32 thread_id,
    u32 syscall_number,
    u64 argument0,
    u64 argument1,
    u64 argument2
);
int os_g05_record_user_fault(u64 fault_address);
int os_g05_finish(void);
u32 os_g05_finish_error(void);
u64 os_g05_fault_count(void);
u64 os_g05_invalid_range_count(void);
u64 os_g05_syscall_count(void);

#endif
