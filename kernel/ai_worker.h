#ifndef EXPERIMENTALOS_KERNEL_AI_WORKER_H
#define EXPERIMENTALOS_KERNEL_AI_WORKER_H

#include "ai_core.h"
#include "process.h"

/*
 * Process-isolated AI inference worker.
 *
 * The worker is its own RROS process. Its service state lives in heap memory
 * owned by the worker process and reserved in the worker's address space, so
 * no other process can validate or reach it through the syscall interface.
 * Clients only exchange bounded request/response messages with copy-in and
 * copy-out; they never receive pointers into worker memory. Access is
 * capability-based, granted per client process by the worker's parent, and
 * every job is bound to the client that submitted it.
 */

#define OS_AI_WORKER_MAX_CLIENTS 4u
#define OS_AI_WORKER_MAX_DATA 1024u

enum os_ai_worker_op {
    OS_AI_OP_REGISTER = 1,
    OS_AI_OP_UNREGISTER,
    OS_AI_OP_SUBMIT,
    OS_AI_OP_CANCEL,
    OS_AI_OP_READ,
    OS_AI_OP_STATE,
    OS_AI_OP_RELEASE
};

enum os_ai_worker_status {
    OS_AI_WORKER_ERR_DEAD = -20,
    OS_AI_WORKER_ERR_CLIENT = -21,
    OS_AI_WORKER_ERR_SIZE = -22,
    OS_AI_WORKER_ERR_OWNER = -23
};

typedef struct {
    u32 op;
    char model[OS_AI_MODEL_NAME_MAX];
    int job;
    usize offset;
    u8 max_tokens;
    const u8 *input;
    usize input_len;
    u8 *output;
    usize output_cap;
} os_ai_worker_msg;

typedef struct {
    u32 client_id;
    u32 caps;
} os_ai_worker_entry;

typedef struct {
    os_process_system *processes;
    os_heap *heap;
    os_ai_service *service;
    void *raw_memory;
    u64 region_size;
    u32 parent_id;
    u32 process_id;
    u32 thread_id;
    u8 alive;
    os_ai_worker_entry grants[OS_AI_WORKER_MAX_CLIENTS];
    u32 job_owner[OS_AI_MAX_JOBS];
    u32 total_steps;
} os_ai_worker;

int os_ai_worker_start(os_ai_worker *worker, os_process_system *processes,
                       os_heap *heap, u32 parent_id, u64 address_base,
                       u64 address_size, u32 memory_budget);
/* Only the worker's parent may grant or revoke client capabilities. */
int os_ai_worker_grant(os_ai_worker *worker, u32 requester_id, u32 client_id,
                       u32 caps);
int os_ai_worker_request(os_ai_worker *worker, u32 client_id,
                         const os_ai_worker_msg *request);
/* Runs bounded inference for queued jobs; returns tokens produced. */
int os_ai_worker_pump(os_ai_worker *worker);
/* Parent-only stop: terminates the process, frees and erases worker memory. */
int os_ai_worker_stop(os_ai_worker *worker, u32 requester_id);
int os_ai_worker_alive(const os_ai_worker *worker);
/* 1 if `client_id` could validate the worker's service memory (must be 0). */
int os_ai_worker_memory_reachable(const os_ai_worker *worker, u32 client_id);
/* Process-isolation self-test shared by host and guests; null means PASS. */
const char *os_ai_core_self_test(void);

#endif
