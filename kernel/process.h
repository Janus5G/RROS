#ifndef EXPERIMENTALOS_KERNEL_PROCESS_H
#define EXPERIMENTALOS_KERNEL_PROCESS_H

#include "memory.h"
#include "abi.h"

#define OS_PROCESS_MAX_PROCESSES 8u
#define OS_PROCESS_MAX_THREADS 32u

#define OS_SYSCALL_BLOCKED (-4096LL)
#define OS_SYSCALL_EINVAL (-22LL)
#define OS_SYSCALL_EFAULT (-14LL)
#define OS_SYSCALL_ENOSYS (-38LL)

typedef enum {
    OS_PROCESS_FREE = 0,
    OS_PROCESS_RUNNING,
    OS_PROCESS_TERMINATED
} os_process_state;

typedef enum {
    OS_THREAD_FREE = 0,
    OS_THREAD_RUNNABLE,
    OS_THREAD_RUNNING,
    OS_THREAD_SLEEPING,
    OS_THREAD_WAITING,
    OS_THREAD_TERMINATED
} os_thread_state;

typedef struct {
    u32 id;
    u32 parent_id;
    os_process_state state;
    int exit_code;
    usize live_threads;
    os_virtual_space address_space;
} os_process;

typedef struct {
    u32 id;
    u32 process_id;
    u32 waiting_for;
    os_thread_state state;
    int exit_code;
} os_thread;

typedef struct {
    u32 lock;
    u32 next_process_id;
    u32 next_thread_id;
    u32 current_thread_id;
    usize scheduler_cursor;
    os_process processes[OS_PROCESS_MAX_PROCESSES];
    os_thread threads[OS_PROCESS_MAX_THREADS];
} os_process_system;

void os_process_system_init(os_process_system *system);
u32 os_process_create(
    os_process_system *system,
    u32 parent_id,
    u64 address_base,
    u64 address_size
);
u32 os_thread_create(os_process_system *system, u32 process_id);
u32 os_scheduler_next(os_process_system *system);
int os_thread_sleep(os_process_system *system, u32 thread_id);
int os_thread_wake(os_process_system *system, u32 thread_id);
int os_thread_wait(
    os_process_system *system,
    u32 waiter_id,
    u32 target_id
);
int os_thread_exit(os_process_system *system, u32 thread_id, int exit_code);
int os_process_terminate(
    os_process_system *system,
    u32 process_id,
    int exit_code
);
int os_process_reap(
    os_process_system *system,
    u32 parent_id,
    u32 process_id,
    int *exit_code
);
long long os_syscall_dispatch(
    os_process_system *system,
    u32 thread_id,
    u32 syscall_number,
    u64 argument0,
    u64 argument1,
    u64 argument2
);

#endif
