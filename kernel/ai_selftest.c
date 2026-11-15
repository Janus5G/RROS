#include "ai_worker.h"

/*
 * Shared host and guest self-test of the process-isolated AI worker.
 * Returns a null pointer on success, otherwise the failing check name.
 */

#define ARENA_SIZE (160u * 1024u)
static u8 arena[ARENA_SIZE] __attribute__((aligned(4096)));
static os_process_system processes;
static os_heap heap;
static os_ai_worker worker;

static usize build_model(u8 *image)
{
    /* Bigram model trained on "abcabcabc": a->b, b->c, c->a. */
    static const u8 name[] = {'c', 'y', 'c', 'l', 'e'};
    static const u8 edges[] = {'a', 'b', 3, 'b', 'c', 3, 'c', 'a', 2};
    usize pos = 16;
    u32 hash = 2166136261u;
    for (usize i = 0; i < sizeof name; ++i) {
        image[pos++] = name[i];
    }
    for (usize i = 0; i < sizeof edges; ++i) {
        image[pos++] = edges[i];
    }
    for (usize i = 16; i < pos; ++i) {
        hash ^= image[i];
        hash *= 16777619u;
    }
    image[0] = 'E'; image[1] = 'A'; image[2] = 'I'; image[3] = 'M';
    image[4] = 1; image[5] = 0;
    image[6] = (u8)sizeof name; image[7] = 0;
    image[8] = 3; image[9] = 0; image[10] = 0; image[11] = 0;
    for (int i = 0; i < 4; ++i) {
        image[12 + i] = (u8)(hash >> (8 * i));
    }
    return pos;
}

static int equal(const u8 *a, const char *b, usize n)
{
    for (usize i = 0; i < n; ++i) {
        if (a[i] != (u8)b[i]) {
            return 0;
        }
    }
    return 1;
}

#define CHECK(name, condition) do { if (!(condition)) { return name; } } while (0)

static os_ai_worker_msg make_request(u32 op, const char *model, int job)
{
    os_ai_worker_msg r;
    r.op = op;
    for (usize i = 0; i < OS_AI_MODEL_NAME_MAX; ++i) {
        r.model[i] = 0;
    }
    for (usize i = 0; model && model[i] && i + 1 < OS_AI_MODEL_NAME_MAX; ++i) {
        r.model[i] = model[i];
    }
    r.job = job;
    r.offset = 0;
    r.max_tokens = 0;
    r.input = (const u8 *)0;
    r.input_len = 0;
    r.output = (u8 *)0;
    r.output_cap = 0;
    return r;
}

const char *os_ai_core_self_test(void)
{
    u8 image[16 + 8 + 9 + 8];
    usize image_len = build_model(image);
    u8 out[OS_AI_MAX_OUTPUT];
    const u32 all = OS_AI_CAP_REGISTER | OS_AI_CAP_SUBMIT | OS_AI_CAP_CANCEL
                  | OS_AI_CAP_READ;

    os_process_system_init(&processes);
    CHECK("G13-GUEST-HEAP", os_heap_init(&heap, arena, ARENA_SIZE));
    u32 parent = os_process_create(&processes, 0, 0x100000u, 0x10000u);
    u32 client_a = os_process_create(&processes, parent, 0x200000u, 0x10000u);
    u32 client_b = os_process_create(&processes, parent, 0x300000u, 0x10000u);
    CHECK("G13-GUEST-PROCESSES", parent && client_a && client_b);

    CHECK("G13-GUEST-WORKER-START",
          os_ai_worker_start(&worker, &processes, &heap, parent, (u64)(usize)arena,
                             ARENA_SIZE, 512u));
    CHECK("G13-GUEST-WORKER-ALIVE", os_ai_worker_alive(&worker)
          && worker.process_id != parent && worker.process_id != client_a);

    /* Capability enforcement and owner-only grants. */
    os_ai_worker_msg reg = make_request(OS_AI_OP_REGISTER, 0, 0);
    reg.input = image;
    reg.input_len = image_len;
    CHECK("G13-GUEST-UNGRANTED-DENIED",
          os_ai_worker_request(&worker, client_a, &reg) == OS_AI_ERR_DENIED);
    CHECK("G13-GUEST-GRANT-OWNER-ONLY",
          os_ai_worker_grant(&worker, client_a, client_a, all) == OS_AI_ERR_DENIED);
    CHECK("G13-GUEST-GRANT-A", os_ai_worker_grant(&worker, parent, client_a, all) == OS_AI_OK);
    CHECK("G13-GUEST-GRANT-B", os_ai_worker_grant(&worker, parent, client_b,
          OS_AI_CAP_SUBMIT | OS_AI_CAP_READ) == OS_AI_OK);
    CHECK("G13-GUEST-GRANT-UNKNOWN-CLIENT",
          os_ai_worker_grant(&worker, parent, 99u, all) == OS_AI_ERR_DENIED);
    CHECK("G13-GUEST-LIMITED-CAP-DENIED",
          os_ai_worker_request(&worker, client_b, &reg) == OS_AI_ERR_DENIED);
    CHECK("G13-GUEST-REGISTER", os_ai_worker_request(&worker, client_a, &reg) >= 0);

    /* Memory isolation between worker and clients. */
    CHECK("G13-GUEST-MEMORY-ISOLATED",
          !os_ai_worker_memory_reachable(&worker, client_a)
          && !os_ai_worker_memory_reachable(&worker, client_b)
          && !os_ai_worker_memory_reachable(&worker, parent));
    CHECK("G13-GUEST-MEMORY-OWNED",
          os_ai_worker_memory_reachable(&worker, worker.process_id));
    CHECK("G13-GUEST-HEAP-OWNER",
          !os_heap_free(&heap, worker.raw_memory, client_a));

    /* Job submit, incremental read, per-client job ownership. */
    static const u8 prompt[] = {'a'};
    os_ai_worker_msg submit = make_request(OS_AI_OP_SUBMIT, "cycle", 0);
    submit.input = prompt;
    submit.input_len = 1;
    submit.max_tokens = 7;
    int job_a = os_ai_worker_request(&worker, client_a, &submit);
    CHECK("G13-GUEST-SUBMIT", job_a >= 0);
    os_ai_worker_msg state = make_request(OS_AI_OP_STATE, 0, job_a);
    CHECK("G13-GUEST-STATE-QUEUED", os_ai_worker_request(&worker, client_a, &state) == OS_AI_JOB_QUEUED);
    CHECK("G13-GUEST-PUMP", os_ai_worker_pump(&worker) == 7);
    CHECK("G13-GUEST-STATE-DONE", os_ai_worker_request(&worker, client_a, &state) == OS_AI_JOB_DONE);
    os_ai_worker_msg read = make_request(OS_AI_OP_READ, 0, job_a);
    read.output = out;
    read.output_cap = sizeof out;
    CHECK("G13-GUEST-INFERENCE",
          os_ai_worker_request(&worker, client_a, &read) == 7 && equal(out, "bcabcab", 7));
    read.offset = 5;
    CHECK("G13-GUEST-INCREMENTAL",
          os_ai_worker_request(&worker, client_a, &read) == 2 && equal(out, "ab", 2));
    read.offset = 0;
    CHECK("G13-GUEST-CROSS-CLIENT-READ-DENIED",
          os_ai_worker_request(&worker, client_b, &read) == OS_AI_WORKER_ERR_OWNER);
    os_ai_worker_msg cancel_a = make_request(OS_AI_OP_CANCEL, 0, job_a);
    CHECK("G13-GUEST-CROSS-CLIENT-CANCEL-DENIED",
          os_ai_worker_request(&worker, client_b, &cancel_a) == OS_AI_WORKER_ERR_OWNER);

    /* Oversized input is rejected before any worker state changes. */
    os_ai_worker_msg big = make_request(OS_AI_OP_REGISTER, 0, 0);
    big.input = image;
    big.input_len = OS_AI_WORKER_MAX_DATA + 1u;
    CHECK("G13-GUEST-OVERSIZE-REJECTED",
          os_ai_worker_request(&worker, client_a, &big) == OS_AI_WORKER_ERR_SIZE);

    /* Bounded cancellation. */
    submit.max_tokens = 100;
    int job_b = os_ai_worker_request(&worker, client_b, &submit);
    CHECK("G13-GUEST-SUBMIT-B", job_b >= 0 && job_b != job_a);
    CHECK("G13-GUEST-PUMP-PARTIAL", os_ai_worker_pump(&worker) == (int)OS_AI_STEP_TOKENS);
    os_ai_worker_msg cancel_b = make_request(OS_AI_OP_CANCEL, 0, job_b);
    CHECK("G13-GUEST-CANCEL-NEEDS-CAP",
          os_ai_worker_request(&worker, client_b, &cancel_b) == OS_AI_ERR_DENIED);
    CHECK("G13-GUEST-CANCEL-BY-GRANT", os_ai_worker_grant(&worker, parent, client_b,
          OS_AI_CAP_SUBMIT | OS_AI_CAP_READ | OS_AI_CAP_CANCEL) == OS_AI_OK
          && os_ai_worker_request(&worker, client_b, &cancel_b) == OS_AI_OK);
    CHECK("G13-GUEST-CANCELLED-IDLE", os_ai_worker_pump(&worker) == 0);

    /* Revocation removes access and that client's jobs. */
    CHECK("G13-GUEST-REVOKE", os_ai_worker_grant(&worker, parent, client_b, 0) == OS_AI_OK
          && os_ai_worker_request(&worker, client_b, &state) == OS_AI_ERR_DENIED);

    /* Worker crash containment: clients get a clean error, parent cleans up. */
    CHECK("G13-GUEST-CRASH", os_process_terminate(&processes, worker.process_id, 9));
    CHECK("G13-GUEST-DEAD-REQUEST",
          os_ai_worker_request(&worker, client_a, &state) == OS_AI_WORKER_ERR_DEAD
          && os_ai_worker_pump(&worker) == OS_AI_WORKER_ERR_DEAD);
    CHECK("G13-GUEST-CLIENT-SURVIVES", processes.processes[0].state == OS_PROCESS_RUNNING);
    CHECK("G13-GUEST-STOP-OWNER-ONLY", !os_ai_worker_stop(&worker, client_a));
    CHECK("G13-GUEST-STOP", os_ai_worker_stop(&worker, parent));
    CHECK("G13-GUEST-MEMORY-ERASED", worker.service == (os_ai_service *)0);

    /* Restart yields a clean worker with no retained registry or jobs. */
    CHECK("G13-GUEST-RESTART",
          os_ai_worker_start(&worker, &processes, &heap, parent, (u64)(usize)arena,
                             ARENA_SIZE, 512u)
          && os_ai_worker_grant(&worker, parent, client_a, all) == OS_AI_OK);
    CHECK("G13-GUEST-RESTART-CLEAN",
          os_ai_worker_request(&worker, client_a, &submit) == OS_AI_ERR_NOT_FOUND);
    CHECK("G13-GUEST-FINAL-STOP", os_ai_worker_stop(&worker, parent));
    return (const char *)0;
}
