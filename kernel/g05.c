#include "g05.h"

static os_process_system g_process_system;
static u32 g_root_process;
static u32 g_process_ids[OS_G05_PROCESS_COUNT];
static u32 g_thread_ids[OS_G05_PROCESS_COUNT];
static u64 g_kernel_probe;
static u64 g_other_stack[OS_G05_PROCESS_COUNT];
static u64 g_fault_count;
static u64 g_invalid_range_count;
static u64 g_syscall_count;
static u64 g_fault_mask;
static u32 g_finish_error;
static int g_initialized;

int os_g05_initialize(
    u64 code_physical,
    u64 stack0_physical,
    u64 stack1_physical,
    u64 kernel_probe
)
{
    if (g_initialized || code_physical == 0 || stack0_physical == 0
        || stack1_physical == 0 || kernel_probe == 0
        || (code_physical & (OS_PAGE_SIZE - 1u)) != 0
        || (stack0_physical & (OS_PAGE_SIZE - 1u)) != 0
        || (stack1_physical & (OS_PAGE_SIZE - 1u)) != 0) {
        return 0;
    }

    os_process_system_init(&g_process_system);
    g_root_process = os_process_create(
        &g_process_system, 0, OS_G05_USER_CODE,
        OS_G05_USER_WINDOW_SIZE + OS_PAGE_SIZE
    );
    if (g_root_process == 0) {
        return 0;
    }

    g_kernel_probe = kernel_probe;
    g_other_stack[0] = stack1_physical;
    g_other_stack[1] = stack0_physical;
    for (usize i = 0; i < OS_G05_PROCESS_COUNT; ++i) {
        g_process_ids[i] = os_process_create(
            &g_process_system, g_root_process,
            OS_G05_USER_CODE, OS_G05_USER_WINDOW_SIZE + OS_PAGE_SIZE
        );
        if (g_process_ids[i] == 0
            || !os_virtual_reserve(
                &g_process_system.processes[i + 1u].address_space,
                OS_G05_USER_CODE, OS_PAGE_SIZE, g_process_ids[i],
                OS_VM_READ | OS_VM_EXECUTE)
            || !os_virtual_reserve(
                &g_process_system.processes[i + 1u].address_space,
                OS_G05_USER_STACK, OS_PAGE_SIZE, g_process_ids[i],
                OS_VM_READ | OS_VM_WRITE)) {
            return 0;
        }
        g_thread_ids[i] = os_thread_create(
            &g_process_system, g_process_ids[i]);
        if (g_thread_ids[i] == 0) {
            return 0;
        }
    }

    g_initialized = 1;
    return 1;
}

u32 os_g05_initial_thread(void)
{
    if (!g_initialized) {
        return 0;
    }
    return os_scheduler_next(&g_process_system);
}

u32 os_g05_thread_for_slot(usize slot)
{
    return slot < OS_G05_PROCESS_COUNT ? g_thread_ids[slot] : 0;
}

u32 os_g05_process_for_slot(usize slot)
{
    return slot < OS_G05_PROCESS_COUNT ? g_process_ids[slot] : 0;
}

u32 os_g05_schedule_syscall(
    u32 thread_id,
    u32 syscall_number,
    u64 argument0,
    u64 argument1,
    u64 argument2
)
{
    if (!g_initialized) {
        return 0;
    }
    ++g_syscall_count;
    long long result = os_syscall_dispatch(
        &g_process_system,
        thread_id,
        syscall_number,
        argument0,
        argument1,
        argument2
    );
    if (syscall_number == OS_SYSCALL_VALIDATE_RANGE
        && result == OS_SYSCALL_EFAULT) {
        ++g_invalid_range_count;
        return thread_id;
    }
    if (syscall_number == OS_SYSCALL_EXIT && result == 0) {
        return os_scheduler_next(&g_process_system);
    }
    if (syscall_number == OS_SYSCALL_YIELD && result > 0) {
        return (u32)result;
    }
    if (syscall_number == OS_SYSCALL_VALIDATE_RANGE && result == 0) {
        return thread_id;
    }
    return 0;
}

int os_g05_record_user_fault(u64 fault_address)
{
    if (!g_initialized) {
        return 0;
    }
    if (fault_address == g_kernel_probe) {
        g_fault_mask |= 1u;
        ++g_fault_count;
        return 1;
    }
    for (usize i = 0; i < OS_G05_PROCESS_COUNT; ++i) {
        if (fault_address == g_other_stack[i]) {
            g_fault_mask |= 2u;
            ++g_fault_count;
            return 1;
        }
    }
    return 0;
}

u64 os_g05_fault_count(void)
{
    return g_fault_count;
}

u64 os_g05_invalid_range_count(void)
{
    return g_invalid_range_count;
}

u64 os_g05_syscall_count(void)
{
    return g_syscall_count;
}

int os_g05_finish(void)
{
    g_finish_error = 0;
    if (!g_initialized) {
        g_finish_error = 1;
        return 0;
    }
    if (g_fault_count != 4
        || g_fault_mask != 3u || g_invalid_range_count != 2
        || g_syscall_count != 8) {
        g_finish_error = 2;
        return 0;
    }

    for (usize i = 0; i < OS_G05_PROCESS_COUNT; ++i) {
        os_process *process = &g_process_system.processes[i + 1u];
        os_thread *thread = &g_process_system.threads[i];
        if (process->state != OS_PROCESS_TERMINATED
            || process->live_threads != 0
            || process->address_space.region_count != 0
            || thread->state != OS_THREAD_TERMINATED
            || thread->process_id != g_process_ids[i]) {
            g_finish_error = 3;
            return 0;
        }
        int exit_code = 0;
        if (!os_process_reap(
                &g_process_system, g_root_process,
                g_process_ids[i], &exit_code)
            || exit_code != (int)(10u + i)) {
            g_finish_error = 4;
            return 0;
        }
    }
    return 1;
}

u32 os_g05_finish_error(void)
{
    return g_finish_error;
}
