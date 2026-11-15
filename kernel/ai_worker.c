#include "ai_worker.h"

static const os_process *find_process_const(const os_process_system *system, u32 id)
{
    for (usize i = 0; i < OS_PROCESS_MAX_PROCESSES; ++i) {
        const os_process *p = &system->processes[i];
        if (p->state != OS_PROCESS_FREE && p->id == id) {
            return p;
        }
    }
    return (const os_process *)0;
}

static int process_running(const os_process_system *system, u32 id)
{
    const os_process *p = id ? find_process_const(system, id) : (const os_process *)0;
    return p != (const os_process *)0 && p->state == OS_PROCESS_RUNNING;
}

int os_ai_worker_alive(const os_ai_worker *worker)
{
    return worker != (const os_ai_worker *)0 && worker->alive
        && process_running(worker->processes, worker->process_id);
}

static void erase(void *pointer, usize size)
{
    volatile u8 *bytes = (volatile u8 *)pointer;
    for (usize i = 0; i < size; ++i) {
        bytes[i] = 0;
    }
}

int os_ai_worker_start(os_ai_worker *worker, os_process_system *processes,
                       os_heap *heap, u32 parent_id, u64 address_base,
                       u64 address_size, u32 memory_budget)
{
    if (worker == (os_ai_worker *)0 || processes == (os_process_system *)0
        || heap == (os_heap *)0 || parent_id == 0
        || !process_running(processes, parent_id)) {
        return 0;
    }
    erase(worker, sizeof *worker);
    worker->processes = processes;
    worker->heap = heap;
    worker->parent_id = parent_id;
    worker->process_id = os_process_create(
        processes, parent_id, address_base, address_size);
    if (worker->process_id == 0) {
        return 0;
    }
    worker->thread_id = os_thread_create(processes, worker->process_id);
    const u64 region_size = ((u64)sizeof(os_ai_service) + OS_PAGE_SIZE - 1u) & ~(u64)(OS_PAGE_SIZE - 1u);
    void *memory = os_heap_alloc(heap, (usize)region_size + OS_PAGE_SIZE, worker->process_id);
    if (worker->thread_id == 0 || memory == (void *)0) {
        goto fail;
    }
    worker->raw_memory = memory;
    u64 base = ((u64)(usize)memory + OS_PAGE_SIZE - 1u) & ~(u64)(OS_PAGE_SIZE - 1u);
    worker->service = (os_ai_service *)(usize)base;
    worker->region_size = region_size;
    os_process *self = (os_process *)0;
    for (usize i = 0; i < OS_PROCESS_MAX_PROCESSES; ++i) {
        if (processes->processes[i].state != OS_PROCESS_FREE
            && processes->processes[i].id == worker->process_id) {
            self = &processes->processes[i];
        }
    }
    /* The worker's own address window must cover its private service memory. */
    if (self == (os_process *)0
        || !os_virtual_reserve(&self->address_space, base,
                               region_size, worker->process_id,
                               OS_VM_READ | OS_VM_WRITE)) {
        goto fail;
    }
    os_ai_service_init(worker->service, memory_budget);
    worker->alive = 1;
    return 1;
fail:
    if (memory != (void *)0) {
        erase(memory, (usize)region_size + OS_PAGE_SIZE);
        (void)os_heap_free(heap, memory, worker->process_id);
    }
    (void)os_process_terminate(processes, worker->process_id, 1);
    int exit_code = 0;
    (void)os_process_reap(processes, parent_id, worker->process_id, &exit_code);
    worker->alive = 0;
    worker->service = (os_ai_service *)0;
    worker->raw_memory = (void *)0;
    return 0;
}

static os_ai_worker_entry *find_grant(os_ai_worker *worker, u32 client_id)
{
    for (usize i = 0; i < OS_AI_WORKER_MAX_CLIENTS; ++i) {
        if (worker->grants[i].client_id == client_id && client_id != 0) {
            return &worker->grants[i];
        }
    }
    return (os_ai_worker_entry *)0;
}

int os_ai_worker_grant(os_ai_worker *worker, u32 requester_id, u32 client_id,
                       u32 caps)
{
    if (!os_ai_worker_alive(worker) || requester_id != worker->parent_id
        || client_id == 0 || client_id == worker->process_id
        || (caps & ~(u32)(OS_AI_CAP_REGISTER | OS_AI_CAP_SUBMIT
                          | OS_AI_CAP_CANCEL | OS_AI_CAP_READ)) != 0
        || !process_running(worker->processes, client_id)) {
        return OS_AI_ERR_DENIED;
    }
    os_ai_worker_entry *grant = find_grant(worker, client_id);
    if (grant == (os_ai_worker_entry *)0) {
        for (usize i = 0; i < OS_AI_WORKER_MAX_CLIENTS; ++i) {
            if (worker->grants[i].client_id == 0) {
                grant = &worker->grants[i];
                break;
            }
        }
    }
    if (grant == (os_ai_worker_entry *)0) {
        return OS_AI_ERR_FULL;
    }
    if (caps == 0) {
        for (usize j = 0; j < OS_AI_MAX_JOBS; ++j) {
            if (worker->job_owner[j] == client_id) {
                (void)os_ai_job_cancel(worker->service, OS_AI_CAP_CANCEL, (int)j);
                (void)os_ai_job_release(worker->service, (int)j);
                worker->job_owner[j] = 0;
            }
        }
        grant->client_id = 0;
        grant->caps = 0;
        return OS_AI_OK;
    }
    grant->client_id = client_id;
    grant->caps = caps;
    return OS_AI_OK;
}

static int job_index_ok(const os_ai_worker *worker, u32 client_id, int job)
{
    return job >= 0 && job < (int)OS_AI_MAX_JOBS
        && worker->job_owner[job] == client_id;
}

int os_ai_worker_request(os_ai_worker *worker, u32 client_id,
                         const os_ai_worker_msg *request)
{
    if (worker == (os_ai_worker *)0 || request == (const os_ai_worker_msg *)0) {
        return OS_AI_ERR_ARGUMENT;
    }
    if (!os_ai_worker_alive(worker)) {
        return OS_AI_WORKER_ERR_DEAD;
    }
    if (!process_running(worker->processes, client_id)) {
        return OS_AI_WORKER_ERR_CLIENT;
    }
    os_ai_worker_entry *grant = find_grant(worker, client_id);
    if (grant == (os_ai_worker_entry *)0) {
        return OS_AI_ERR_DENIED;
    }
    u32 caps = grant->caps;
    if (request->input_len > OS_AI_WORKER_MAX_DATA
        || (request->input_len != 0 && request->input == (const u8 *)0)) {
        return OS_AI_WORKER_ERR_SIZE;
    }

    /* Copy-in: the worker never keeps a reference to client memory. */
    u8 staged[OS_AI_WORKER_MAX_DATA];
    for (usize i = 0; i < request->input_len; ++i) {
        staged[i] = request->input[i];
    }
    char model[OS_AI_MODEL_NAME_MAX];
    for (usize i = 0; i < OS_AI_MODEL_NAME_MAX; ++i) {
        model[i] = request->model[i];
    }
    model[OS_AI_MODEL_NAME_MAX - 1u] = 0;

    os_ai_service *service = worker->service;
    int result;
    switch (request->op) {
    case OS_AI_OP_REGISTER:
        result = os_ai_model_register(service, caps, staged, request->input_len);
        break;
    case OS_AI_OP_UNREGISTER:
        result = os_ai_model_unregister(service, caps, model);
        break;
    case OS_AI_OP_SUBMIT:
        result = os_ai_job_submit(service, caps, model, staged,
                                  request->input_len, request->max_tokens);
        if (result >= 0) {
            worker->job_owner[result] = client_id;
        }
        break;
    case OS_AI_OP_CANCEL:
        result = job_index_ok(worker, client_id, request->job)
            ? os_ai_job_cancel(service, caps, request->job)
            : OS_AI_WORKER_ERR_OWNER;
        break;
    case OS_AI_OP_STATE:
        result = job_index_ok(worker, client_id, request->job)
            ? os_ai_job_state(service, request->job)
            : OS_AI_WORKER_ERR_OWNER;
        break;
    case OS_AI_OP_READ: {
        if (!job_index_ok(worker, client_id, request->job)) {
            result = OS_AI_WORKER_ERR_OWNER;
            break;
        }
        if (request->output_cap != 0 && request->output == (u8 *)0) {
            result = OS_AI_ERR_ARGUMENT;
            break;
        }
        u8 window[OS_AI_MAX_OUTPUT];
        usize cap = request->output_cap > OS_AI_MAX_OUTPUT
            ? OS_AI_MAX_OUTPUT : request->output_cap;
        result = os_ai_job_read(service, caps, request->job, request->offset,
                                window, cap);
        for (int i = 0; i < result; ++i) {
            request->output[i] = window[i];
        }
        break;
    }
    case OS_AI_OP_RELEASE:
        if (!job_index_ok(worker, client_id, request->job)) {
            result = OS_AI_WORKER_ERR_OWNER;
            break;
        }
        result = os_ai_job_release(service, request->job);
        if (result == OS_AI_OK) {
            worker->job_owner[request->job] = 0;
        }
        break;
    default:
        result = OS_AI_ERR_ARGUMENT;
        break;
    }
    erase(staged, sizeof staged);
    return result;
}

int os_ai_worker_pump(os_ai_worker *worker)
{
    if (!os_ai_worker_alive(worker)) {
        return OS_AI_WORKER_ERR_DEAD;
    }
    int produced = 0;
    for (usize j = 0; j < OS_AI_MAX_JOBS; ++j) {
        int state = os_ai_job_state(worker->service, (int)j);
        if (state != OS_AI_JOB_QUEUED && state != OS_AI_JOB_RUNNING) {
            continue;
        }
        int made = os_ai_job_step(worker->service, (int)j);
        if (made > 0) {
            produced += made;
            worker->total_steps += 1u;
        }
    }
    return produced;
}

int os_ai_worker_stop(os_ai_worker *worker, u32 requester_id)
{
    if (worker == (os_ai_worker *)0 || worker->service == (os_ai_service *)0
        || requester_id != worker->parent_id) {
        return 0;
    }
    int ok = 1;
    int exit_code = 0;
    if (process_running(worker->processes, worker->process_id)) {
        ok = os_process_terminate(worker->processes, worker->process_id, 0);
    }
    erase(worker->raw_memory, (usize)worker->region_size + OS_PAGE_SIZE);
    ok = os_heap_free(worker->heap, worker->raw_memory, worker->process_id) && ok;
    ok = os_process_reap(worker->processes, worker->parent_id,
                         worker->process_id, &exit_code) && ok;
    worker->service = (os_ai_service *)0;
    worker->raw_memory = (void *)0;
    worker->alive = 0;
    erase(worker->grants, sizeof worker->grants);
    erase(worker->job_owner, sizeof worker->job_owner);
    return ok;
}

int os_ai_worker_memory_reachable(const os_ai_worker *worker, u32 client_id)
{
    if (worker == (const os_ai_worker *)0 || worker->service == (os_ai_service *)0) {
        return 0;
    }
    const os_process *client = find_process_const(worker->processes, client_id);
    if (client == (const os_process *)0) {
        return 0;
    }
    u64 base = (u64)(usize)worker->service;
    u64 end = base + worker->region_size;
    for (usize i = 0; i < client->address_space.region_count; ++i) {
        const os_virtual_region *r = &client->address_space.regions[i];
        if (base < r->base + r->size && r->base < end) {
            return 1;
        }
    }
    return 0;
}
