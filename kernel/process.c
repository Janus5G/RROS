#include "process.h"

static void system_lock(os_process_system *system)
{
    while (__atomic_exchange_n(&system->lock, 1u, __ATOMIC_ACQUIRE) != 0) {
    }
}

static void system_unlock(os_process_system *system)
{
    __atomic_store_n(&system->lock, 0u, __ATOMIC_RELEASE);
}

static os_process *find_process(os_process_system *system, u32 id)
{
    for (usize i = 0; i < OS_PROCESS_MAX_PROCESSES; ++i) {
        if (system->processes[i].id == id
            && system->processes[i].state != OS_PROCESS_FREE) {
            return &system->processes[i];
        }
    }
    return (os_process *)0;
}

static os_thread *find_thread(os_process_system *system, u32 id)
{
    for (usize i = 0; i < OS_PROCESS_MAX_THREADS; ++i) {
        if (system->threads[i].id == id
            && system->threads[i].state != OS_THREAD_FREE) {
            return &system->threads[i];
        }
    }
    return (os_thread *)0;
}

static u32 schedule_locked(os_process_system *system)
{
    if (system->current_thread_id != 0) {
        os_thread *current = find_thread(system, system->current_thread_id);
        if (current != (os_thread *)0
            && current->state == OS_THREAD_RUNNING) {
            current->state = OS_THREAD_RUNNABLE;
        }
    }
    system->current_thread_id = 0;

    for (usize offset = 0; offset < OS_PROCESS_MAX_THREADS; ++offset) {
        usize index = (system->scheduler_cursor + offset)
            % OS_PROCESS_MAX_THREADS;
        os_thread *thread = &system->threads[index];
        if (thread->state == OS_THREAD_RUNNABLE) {
            os_process *process = find_process(system, thread->process_id);
            if (process != (os_process *)0
                && process->state == OS_PROCESS_RUNNING) {
                thread->state = OS_THREAD_RUNNING;
                system->current_thread_id = thread->id;
                system->scheduler_cursor = (index + 1u)
                    % OS_PROCESS_MAX_THREADS;
                return thread->id;
            }
        }
    }
    return 0;
}

static void wake_waiters(os_process_system *system, u32 target_id)
{
    for (usize i = 0; i < OS_PROCESS_MAX_THREADS; ++i) {
        os_thread *thread = &system->threads[i];
        if (thread->state == OS_THREAD_WAITING
            && thread->waiting_for == target_id) {
            thread->waiting_for = 0;
            thread->state = OS_THREAD_RUNNABLE;
        }
    }
}

static void release_process_regions(os_process *process)
{
    process->address_space.region_count = 0;
}

static void terminate_process_locked(
    os_process_system *system,
    os_process *process,
    int exit_code
)
{
    if (process->state == OS_PROCESS_TERMINATED) {
        return;
    }
    process->state = OS_PROCESS_TERMINATED;
    process->exit_code = exit_code;
    process->live_threads = 0;
    release_process_regions(process);

    for (usize i = 0; i < OS_PROCESS_MAX_THREADS; ++i) {
        os_thread *thread = &system->threads[i];
        if (thread->process_id == process->id
            && thread->state != OS_THREAD_FREE
            && thread->state != OS_THREAD_TERMINATED) {
            thread->state = OS_THREAD_TERMINATED;
            thread->exit_code = exit_code;
            wake_waiters(system, thread->id);
            if (system->current_thread_id == thread->id) {
                system->current_thread_id = 0;
            }
        }
    }
}

void os_process_system_init(os_process_system *system)
{
    if (system == (os_process_system *)0) {
        return;
    }
    for (usize i = 0; i < sizeof(*system); ++i) {
        ((u8 *)system)[i] = 0;
    }
    system->next_process_id = 1;
    system->next_thread_id = 1;
}

u32 os_process_create(
    os_process_system *system,
    u32 parent_id,
    u64 address_base,
    u64 address_size
)
{
    if (system == (os_process_system *)0) {
        return 0;
    }
    system_lock(system);
    if (parent_id != 0) {
        os_process *parent = find_process(system, parent_id);
        if (parent == (os_process *)0
            || parent->state != OS_PROCESS_RUNNING) {
            system_unlock(system);
            return 0;
        }
    }

    os_process *slot = (os_process *)0;
    for (usize i = 0; i < OS_PROCESS_MAX_PROCESSES; ++i) {
        if (system->processes[i].state == OS_PROCESS_FREE) {
            slot = &system->processes[i];
            break;
        }
    }
    if (slot == (os_process *)0 || system->next_process_id == 0
        || !os_virtual_space_init(
            &slot->address_space, address_base, address_size)) {
        system_unlock(system);
        return 0;
    }
    slot->id = system->next_process_id++;
    slot->parent_id = parent_id;
    slot->state = OS_PROCESS_RUNNING;
    slot->exit_code = 0;
    slot->live_threads = 0;
    u32 id = slot->id;
    system_unlock(system);
    return id;
}

u32 os_thread_create(os_process_system *system, u32 process_id)
{
    if (system == (os_process_system *)0) {
        return 0;
    }
    system_lock(system);
    os_process *process = find_process(system, process_id);
    os_thread *slot = (os_thread *)0;
    if (process == (os_process *)0
        || process->state != OS_PROCESS_RUNNING
        || system->next_thread_id == 0) {
        system_unlock(system);
        return 0;
    }
    for (usize i = 0; i < OS_PROCESS_MAX_THREADS; ++i) {
        if (system->threads[i].state == OS_THREAD_FREE) {
            slot = &system->threads[i];
            break;
        }
    }
    if (slot == (os_thread *)0) {
        system_unlock(system);
        return 0;
    }
    slot->id = system->next_thread_id++;
    slot->process_id = process_id;
    slot->waiting_for = 0;
    slot->state = OS_THREAD_RUNNABLE;
    slot->exit_code = 0;
    ++process->live_threads;
    u32 id = slot->id;
    system_unlock(system);
    return id;
}

u32 os_scheduler_next(os_process_system *system)
{
    if (system == (os_process_system *)0) {
        return 0;
    }
    system_lock(system);
    u32 id = schedule_locked(system);
    system_unlock(system);
    return id;
}

int os_thread_sleep(os_process_system *system, u32 thread_id)
{
    if (system == (os_process_system *)0) {
        return 0;
    }
    system_lock(system);
    os_thread *thread = find_thread(system, thread_id);
    if (thread == (os_thread *)0 || thread->state != OS_THREAD_RUNNING
        || system->current_thread_id != thread_id) {
        system_unlock(system);
        return 0;
    }
    thread->state = OS_THREAD_SLEEPING;
    system->current_thread_id = 0;
    (void)schedule_locked(system);
    system_unlock(system);
    return 1;
}

int os_thread_wake(os_process_system *system, u32 thread_id)
{
    if (system == (os_process_system *)0) {
        return 0;
    }
    system_lock(system);
    os_thread *thread = find_thread(system, thread_id);
    if (thread == (os_thread *)0 || thread->state != OS_THREAD_SLEEPING) {
        system_unlock(system);
        return 0;
    }
    thread->state = OS_THREAD_RUNNABLE;
    system_unlock(system);
    return 1;
}

int os_thread_wait(
    os_process_system *system,
    u32 waiter_id,
    u32 target_id
)
{
    if (system == (os_process_system *)0 || waiter_id == target_id) {
        return 0;
    }
    system_lock(system);
    os_thread *waiter = find_thread(system, waiter_id);
    os_thread *target = find_thread(system, target_id);
    if (waiter == (os_thread *)0 || target == (os_thread *)0
        || waiter->state != OS_THREAD_RUNNING
        || system->current_thread_id != waiter_id
        || waiter->process_id != target->process_id
        || target->state == OS_THREAD_FREE) {
        system_unlock(system);
        return 0;
    }
    if (target->state == OS_THREAD_TERMINATED) {
        system_unlock(system);
        return 1;
    }
    waiter->state = OS_THREAD_WAITING;
    waiter->waiting_for = target_id;
    system->current_thread_id = 0;
    (void)schedule_locked(system);
    system_unlock(system);
    return 1;
}

int os_thread_exit(os_process_system *system, u32 thread_id, int exit_code)
{
    if (system == (os_process_system *)0) {
        return 0;
    }
    system_lock(system);
    os_thread *thread = find_thread(system, thread_id);
    if (thread == (os_thread *)0
        || thread->state == OS_THREAD_TERMINATED) {
        system_unlock(system);
        return 0;
    }
    os_process *process = find_process(system, thread->process_id);
    if (process == (os_process *)0
        || process->state != OS_PROCESS_RUNNING
        || process->live_threads == 0) {
        system_unlock(system);
        return 0;
    }
    thread->state = OS_THREAD_TERMINATED;
    thread->exit_code = exit_code;
    --process->live_threads;
    wake_waiters(system, thread_id);
    if (system->current_thread_id == thread_id) {
        system->current_thread_id = 0;
    }
    if (process->live_threads == 0) {
        terminate_process_locked(system, process, exit_code);
    }
    system_unlock(system);
    return 1;
}

int os_process_terminate(
    os_process_system *system,
    u32 process_id,
    int exit_code
)
{
    if (system == (os_process_system *)0) {
        return 0;
    }
    system_lock(system);
    os_process *process = find_process(system, process_id);
    if (process == (os_process *)0
        || process->state != OS_PROCESS_RUNNING) {
        system_unlock(system);
        return 0;
    }
    terminate_process_locked(system, process, exit_code);
    system_unlock(system);
    return 1;
}

int os_process_reap(
    os_process_system *system,
    u32 parent_id,
    u32 process_id,
    int *exit_code
)
{
    if (system == (os_process_system *)0 || exit_code == (int *)0) {
        return 0;
    }
    system_lock(system);
    os_process *process = find_process(system, process_id);
    if (process == (os_process *)0
        || process->parent_id != parent_id
        || process->state != OS_PROCESS_TERMINATED) {
        system_unlock(system);
        return 0;
    }

    *exit_code = process->exit_code;
    for (usize i = 0; i < OS_PROCESS_MAX_THREADS; ++i) {
        os_thread *thread = &system->threads[i];
        if (thread->process_id == process_id
            && thread->state == OS_THREAD_TERMINATED) {
            thread->id = 0;
            thread->process_id = 0;
            thread->waiting_for = 0;
            thread->state = OS_THREAD_FREE;
            thread->exit_code = 0;
        }
    }
    for (usize i = 0; i < sizeof(*process); ++i) {
        ((u8 *)process)[i] = 0;
    }
    system_unlock(system);
    return 1;
}

static int range_is_owned(
    const os_process *process,
    u64 base,
    u64 size,
    u32 permissions
)
{
    if (size == 0 || base > (~(u64)0) - size
        || (permissions & ~(OS_VM_READ | OS_VM_WRITE | OS_VM_EXECUTE)) != 0
        || permissions == 0
        || (permissions & (OS_VM_WRITE | OS_VM_EXECUTE))
            == (OS_VM_WRITE | OS_VM_EXECUTE)) {
        return 0;
    }
    u64 end = base + size;
    for (usize i = 0; i < process->address_space.region_count; ++i) {
        const os_virtual_region *region =
            &process->address_space.regions[i];
        if (region->owner == process->id
            && region->base <= (~(u64)0) - region->size
            && (region->permissions & permissions) == permissions
            && base >= region->base
            && end >= base
            && end <= region->base + region->size) {
            return 1;
        }
    }
    return 0;
}

long long os_syscall_dispatch(
    os_process_system *system,
    u32 thread_id,
    u32 syscall_number,
    u64 argument0,
    u64 argument1,
    u64 argument2
)
{
    if (system == (os_process_system *)0) {
        return OS_SYSCALL_EINVAL;
    }
    system_lock(system);
    os_thread *thread = find_thread(system, thread_id);
    if (thread == (os_thread *)0
        || thread->state != OS_THREAD_RUNNING
        || system->current_thread_id != thread_id) {
        system_unlock(system);
        return OS_SYSCALL_EINVAL;
    }
    os_process *process = find_process(system, thread->process_id);
    if (process == (os_process *)0
        || process->state != OS_PROCESS_RUNNING) {
        system_unlock(system);
        return OS_SYSCALL_EINVAL;
    }

    long long result;
    switch (syscall_number) {
    case OS_SYSCALL_YIELD:
        thread->state = OS_THREAD_RUNNABLE;
        system->current_thread_id = 0;
        result = (long long)schedule_locked(system);
        break;
    case OS_SYSCALL_EXIT:
        if (argument0 > 0x7fffffffu) {
            result = OS_SYSCALL_EINVAL;
            break;
        }
        terminate_process_locked(system, process, (int)argument0);
        result = 0;
        break;
    case OS_SYSCALL_WAIT_THREAD: {
        if (argument0 > 0xffffffffu) {
            result = OS_SYSCALL_EINVAL;
            break;
        }
        os_thread *target = find_thread(system, (u32)argument0);
        if (target == (os_thread *)0
            || target->process_id != process->id
            || target->id == thread->id) {
            result = OS_SYSCALL_EINVAL;
        } else if (target->state == OS_THREAD_TERMINATED) {
            result = target->exit_code;
        } else {
            thread->state = OS_THREAD_WAITING;
            thread->waiting_for = target->id;
            system->current_thread_id = 0;
            (void)schedule_locked(system);
            result = OS_SYSCALL_BLOCKED;
        }
        break;
    }
    case OS_SYSCALL_VALIDATE_RANGE:
        if (argument2 > 0xffffffffu) {
            result = OS_SYSCALL_EFAULT;
            break;
        }
        result = range_is_owned(
            process, argument0, argument1, (u32)argument2)
            ? 0 : OS_SYSCALL_EFAULT;
        break;
    default:
        result = OS_SYSCALL_ENOSYS;
        break;
    }
    system_unlock(system);
    return result;
}
