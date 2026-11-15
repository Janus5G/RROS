#ifndef EXPERIMENTALOS_KERNEL_AI_CORE_H
#define EXPERIMENTALOS_KERNEL_AI_CORE_H

#include <stddef.h>
#include <stdint.h>

/*
 * Native AI Core service layer. It is deliberately free of privileged
 * kernel state: workers only see a private copy of one model and one job.
 * Model output is data; no API here executes commands, writes files or
 * touches hardware on behalf of a model.
 */

#define OS_AI_MAX_MODELS 4u
#define OS_AI_MAX_JOBS 8u
#define OS_AI_MAX_PROMPT 64u
#define OS_AI_MAX_OUTPUT 128u
#define OS_AI_MAX_EDGES 4096u
#define OS_AI_MODEL_NAME_MAX 24u
#define OS_AI_STEP_TOKENS 8u

#define OS_AI_CAP_REGISTER 0x1u
#define OS_AI_CAP_SUBMIT 0x2u
#define OS_AI_CAP_CANCEL 0x4u
#define OS_AI_CAP_READ 0x8u

enum os_ai_status {
    OS_AI_OK = 0,
    OS_AI_ERR_ARGUMENT = -1,
    OS_AI_ERR_DENIED = -2,
    OS_AI_ERR_FORMAT = -3,
    OS_AI_ERR_BUDGET = -4,
    OS_AI_ERR_FULL = -5,
    OS_AI_ERR_NOT_FOUND = -6,
    OS_AI_ERR_STATE = -7,
    OS_AI_ERR_DUPLICATE = -8
};

enum os_ai_job_state {
    OS_AI_JOB_FREE = 0,
    OS_AI_JOB_QUEUED,
    OS_AI_JOB_RUNNING,
    OS_AI_JOB_DONE,
    OS_AI_JOB_CANCELLED,
    OS_AI_JOB_FAILED
};

/* Edge: previous byte, next byte, weight. */
typedef struct {
    uint8_t prev;
    uint8_t next;
    uint8_t weight;
} os_ai_edge;

typedef struct {
    char name[OS_AI_MODEL_NAME_MAX];
    uint32_t memory_bytes;
    uint32_t edge_count;
    os_ai_edge edges[OS_AI_MAX_EDGES];
    uint8_t in_use;
} os_ai_model;

typedef struct {
    uint8_t state;
    uint8_t model_index;
    uint8_t prompt_len;
    uint8_t max_tokens;
    uint8_t prompt[OS_AI_MAX_PROMPT];
    uint8_t output[OS_AI_MAX_OUTPUT];
    uint16_t output_len;
    uint8_t last;
} os_ai_job;

typedef struct {
    os_ai_model models[OS_AI_MAX_MODELS];
    os_ai_job jobs[OS_AI_MAX_JOBS];
    uint32_t memory_budget;
    uint32_t memory_used;
} os_ai_service;

void os_ai_service_init(os_ai_service *service, uint32_t memory_budget);

/*
 * Model image "EAIM": magic, u16 version=1, u16 name_len, u32 edge_count,
 * u32 FNV-1a over name+edges, name bytes, then edge_count 3-byte edges.
 * Integers are little-endian.
 */
int os_ai_model_register(os_ai_service *service, uint32_t caps,
                         const uint8_t *image, size_t image_len);
int os_ai_model_unregister(os_ai_service *service, uint32_t caps,
                           const char *name);
int os_ai_job_submit(os_ai_service *service, uint32_t caps, const char *model,
                     const uint8_t *prompt, size_t prompt_len,
                     uint8_t max_tokens);
/* Runs at most OS_AI_STEP_TOKENS tokens of one job; returns tokens made. */
int os_ai_job_step(os_ai_service *service, int job);
int os_ai_job_cancel(os_ai_service *service, uint32_t caps, int job);
int os_ai_job_state(const os_ai_service *service, int job);
/* Incremental delivery: copies output from offset; returns bytes copied. */
int os_ai_job_read(const os_ai_service *service, uint32_t caps, int job,
                   size_t offset, uint8_t *out, size_t out_cap);
int os_ai_job_release(os_ai_service *service, int job);

#endif
