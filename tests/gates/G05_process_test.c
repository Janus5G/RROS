#include "process.h"

#include <pthread.h>
#include <stdio.h>

#define CHECK(condition, label) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s at line %d\n", label, __LINE__); \
            return 1; \
        } \
    } while (0)

typedef struct {
    os_process_system *system;
    u32 created[128];
} thread_test_context;

static void *create_threads(void *argument)
{
    thread_test_context *context = (thread_test_context *)argument;
    for (usize i = 0; i < 128; ++i) {
        context->created[i] = os_thread_create(context->system, 1);
    }
    return (void *)0;
}

int main(void)
{
    os_process_system system;
    os_process_system_init(&system);
    u32 root = os_process_create(&system, 0, 0x40000000u, 0x10000u);
    CHECK(root == 1, "root process creation");
    CHECK(os_process_create(&system, 99, 0x50000000u, 0x10000u) == 0,
          "reject nonexistent parent");
    u32 process = os_process_create(&system, root, 0x50000000u, 0x10000u);
    CHECK(process == 2, "child process creation");
    CHECK(os_virtual_reserve(
        &system.processes[1].address_space, 0x50000000u, OS_PAGE_SIZE,
        root, OS_VM_READ), "reserve foreign-owner region");
    CHECK(os_virtual_reserve(
        &system.processes[1].address_space, 0x50001000u, OS_PAGE_SIZE,
        process, OS_VM_READ | OS_VM_WRITE), "reserve process-owned region");

    u32 first = os_thread_create(&system, process);
    u32 second = os_thread_create(&system, process);
    CHECK(first != 0 && second != 0, "thread creation");
    CHECK(os_scheduler_next(&system) == first, "first runnable thread");
    CHECK(os_syscall_dispatch(
        &system, first, OS_SYSCALL_VALIDATE_RANGE,
        0x50001000u, 128, OS_VM_READ) == 0, "validate owned user range");
    CHECK(os_syscall_dispatch(
        &system, first, OS_SYSCALL_VALIDATE_RANGE,
        0x50000000u, 128, OS_VM_READ) == OS_SYSCALL_EFAULT,
        "reject foreign-owned range");
    CHECK(os_syscall_dispatch(
        &system, first, OS_SYSCALL_VALIDATE_RANGE,
        0x50001000u, 128, OS_VM_EXECUTE) == OS_SYSCALL_EFAULT,
        "reject unowned permissions");
    CHECK(os_syscall_dispatch(
        &system, first, 99, 0, 0, 0) == OS_SYSCALL_ENOSYS,
        "reject unknown syscall");
    CHECK(os_syscall_dispatch(
        &system, first, OS_SYSCALL_WAIT_THREAD,
        (u64)first + 0x100000000u, 0, 0) == OS_SYSCALL_EINVAL,
        "reject truncated thread identifier");
    CHECK(os_syscall_dispatch(
        &system, first, OS_SYSCALL_VALIDATE_RANGE,
        0x50001000u, 128, 0x100000001u) == OS_SYSCALL_EFAULT,
        "reject truncated permissions");
    CHECK(os_syscall_dispatch(
        &system, first, OS_SYSCALL_EXIT, 0x80000000u, 0, 0)
        == OS_SYSCALL_EINVAL, "reject oversized exit status");
    CHECK(os_syscall_dispatch(
        &system, first, OS_SYSCALL_YIELD, 0, 0, 0) == second,
        "yield to next runnable thread");
    CHECK(os_syscall_dispatch(
        &system, second, OS_SYSCALL_WAIT_THREAD, first, 0, 0)
        == OS_SYSCALL_BLOCKED, "wait for sibling");
    CHECK(os_thread_exit(&system, first, 7), "target thread exit");
    CHECK(system.threads[1].state == OS_THREAD_RUNNABLE,
          "wake sibling waiter");
    CHECK(os_scheduler_next(&system) == second, "resume waiting thread");
    CHECK(os_thread_sleep(&system, second), "thread sleep");
    CHECK(!os_thread_wake(&system, first), "cannot wake terminated thread");
    CHECK(os_thread_wake(&system, second), "wake sleeping thread");
    CHECK(os_scheduler_next(&system) == second, "reschedule woken thread");

    CHECK(os_syscall_dispatch(
        &system, second, OS_SYSCALL_EXIT, 12, 0, 0) == 0,
        "terminate process through syscall");
    CHECK(system.processes[1].state == OS_PROCESS_TERMINATED,
          "process termination");
    CHECK(system.processes[1].address_space.region_count == 0,
          "release process virtual regions");
    CHECK(system.threads[1].state == OS_THREAD_TERMINATED,
          "terminate remaining process threads");
    CHECK(os_thread_create(&system, process) == 0,
          "reject thread creation after termination");
    int process_exit_code = 0;
    CHECK(!os_process_reap(&system, root, process + 1u, &process_exit_code),
          "reject reaping a non-child");
    CHECK(os_process_reap(&system, root, process, &process_exit_code),
          "reap terminated child");
    CHECK(process_exit_code == 12, "preserve process exit status");
    u32 recycled = os_process_create(
        &system, root, 0x50000000u, 0x10000u);
    CHECK(recycled != 0 && recycled != process,
          "reuse process slot with a fresh identifier");

    os_process_system concurrent_system;
    os_process_system_init(&concurrent_system);
    CHECK(os_process_create(
        &concurrent_system, 0, 0x60000000u, 0x10000u) == 1,
        "concurrency process creation");
    thread_test_context contexts[4] = {
        {&concurrent_system, {0}},
        {&concurrent_system, {0}},
        {&concurrent_system, {0}},
        {&concurrent_system, {0}},
    };
    pthread_t workers[4];
    for (usize i = 0; i < 4; ++i) {
        CHECK(pthread_create(&workers[i], 0, create_threads, &contexts[i]) == 0,
              "start concurrent thread creator");
    }
    for (usize i = 0; i < 4; ++i) {
        CHECK(pthread_join(workers[i], 0) == 0, "join concurrent creator");
    }
    u32 thread_count = 0;
    for (usize i = 0; i < 4; ++i) {
        for (usize j = 0; j < 128; ++j) {
            if (contexts[i].created[j] != 0) {
                ++thread_count;
            }
        }
    }
    CHECK(thread_count == OS_PROCESS_MAX_THREADS,
          "bounded concurrent thread allocation");
    puts("PASS G05-PROCESS-HOST: scheduler model, syscall checks, lifecycle and concurrency");
    return 0;
}
