#include "../../kernel/ai_core.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(id, cond) do { \
    if (cond) printf("PASS %s\n", id); \
    else { printf("FAIL %s (line %d)\n", id, __LINE__); failures++; } \
} while (0)

static size_t build_model(uint8_t *img, const char *name, const char *train)
{
    unsigned counts[256][256];
    memset(counts, 0, sizeof counts);
    size_t n = strlen(train);
    for (size_t i = 0; i + 1 < n; i++)
        counts[(uint8_t)train[i]][(uint8_t)train[i + 1]]++;
    size_t nl = strlen(name), pos = 16 + nl, edges = 0;
    memcpy(img, "EAIM", 4);
    for (size_t p = 0; p < 256; p++)
        for (size_t q = 0; q < 256; q++)
            if (counts[p][q]) {
                img[pos++] = (uint8_t)p;
                img[pos++] = (uint8_t)q;
                img[pos++] = counts[p][q] > 255 ? 255 : (uint8_t)counts[p][q];
                edges++;
            }
    img[4] = 1; img[5] = 0; img[6] = (uint8_t)nl; img[7] = 0;
    for (int i = 0; i < 4; i++) img[8 + i] = (uint8_t)(edges >> (8 * i));
    memcpy(img + 16, name, nl);
    /* name sits before edges; edges were written after it above */
    uint32_t h = 2166136261u;
    for (size_t i = 16; i < pos; i++) { h ^= img[i]; h *= 16777619u; }
    for (int i = 0; i < 4; i++) img[12 + i] = (uint8_t)(h >> (8 * i));
    return pos;
}

int main(void)
{
    static os_ai_service svc;
    static uint8_t img[16 + 32 + 3 * 4096];
    const uint32_t all = OS_AI_CAP_REGISTER | OS_AI_CAP_SUBMIT |
                         OS_AI_CAP_CANCEL | OS_AI_CAP_READ;
    os_ai_service_init(&svc, 1024);

    size_t len = build_model(img, "cycle", "abcabcabcabc");
    CHECK("G13-REGISTER-DENIED", os_ai_model_register(&svc, 0, img, len) == OS_AI_ERR_DENIED);
    CHECK("G13-REGISTER-OK", os_ai_model_register(&svc, all, img, len) >= 0);
    CHECK("G13-REGISTER-DUPLICATE", os_ai_model_register(&svc, all, img, len) == OS_AI_ERR_DUPLICATE);

    img[len - 1] ^= 1;
    CHECK("G13-CHECKSUM-REJECT", os_ai_model_register(&svc, all, img, len) == OS_AI_ERR_FORMAT);
    img[len - 1] ^= 1;
    CHECK("G13-TRUNCATED-REJECT", os_ai_model_register(&svc, all, img, len - 1) == OS_AI_ERR_FORMAT);
    uint8_t bad[16] = {'X', 'A', 'I', 'M'};
    CHECK("G13-MAGIC-REJECT", os_ai_model_register(&svc, all, bad, sizeof bad) == OS_AI_ERR_FORMAT);

    static os_ai_service tight;
    os_ai_service_init(&tight, 8);
    CHECK("G13-BUDGET-REJECT", os_ai_model_register(&tight, all, img, len) == OS_AI_ERR_BUDGET && tight.memory_used == 0);

    /* Real deterministic inference: greedy continuation of the bigram model. */
    int job = os_ai_job_submit(&svc, all, "cycle", (const uint8_t *)"a", 1, 7);
    CHECK("G13-SUBMIT", job >= 0 && os_ai_job_state(&svc, job) == OS_AI_JOB_QUEUED);
    CHECK("G13-SUBMIT-DENIED", os_ai_job_submit(&svc, OS_AI_CAP_READ, "cycle", (const uint8_t *)"a", 1, 3) == OS_AI_ERR_DENIED);
    CHECK("G13-SUBMIT-UNKNOWN-MODEL", os_ai_job_submit(&svc, all, "nope", (const uint8_t *)"a", 1, 3) == OS_AI_ERR_NOT_FOUND);
    CHECK("G13-SUBMIT-BOUNDS", os_ai_job_submit(&svc, all, "cycle", (const uint8_t *)"a", 0, 3) == OS_AI_ERR_ARGUMENT &&
                               os_ai_job_submit(&svc, all, "cycle", (const uint8_t *)"a", 1, 0) == OS_AI_ERR_ARGUMENT &&
                               os_ai_job_submit(&svc, all, "cycle", (const uint8_t *)"a", OS_AI_MAX_PROMPT + 1, 3) == OS_AI_ERR_ARGUMENT);
    CHECK("G13-UNREGISTER-BUSY", os_ai_model_unregister(&svc, all, "cycle") == OS_AI_ERR_STATE);

    uint8_t out[OS_AI_MAX_OUTPUT + 1];
    CHECK("G13-STEP", os_ai_job_step(&svc, job) == 7 && os_ai_job_state(&svc, job) == OS_AI_JOB_DONE);
    int n = os_ai_job_read(&svc, all, job, 0, out, sizeof out);
    CHECK("G13-INFERENCE-OUTPUT", n == 7 && memcmp(out, "bcabcab", 7) == 0);
    CHECK("G13-INCREMENTAL-READ", os_ai_job_read(&svc, all, job, 5, out, 8) == 2 && memcmp(out, "ab", 2) == 0);
    CHECK("G13-READ-DENIED", os_ai_job_read(&svc, OS_AI_CAP_SUBMIT, job, 0, out, 4) == OS_AI_ERR_DENIED);
    CHECK("G13-READ-OFFSET-BOUNDS", os_ai_job_read(&svc, all, job, 8, out, 4) == OS_AI_ERR_ARGUMENT);

    /* Incremental delivery across steps and early termination at a dead end. */
    size_t l2 = build_model(img, "tiny", "xyz");
    CHECK("G13-REGISTER-SECOND", os_ai_model_register(&svc, all, img, l2) >= 0);
    int j2 = os_ai_job_submit(&svc, all, "tiny", (const uint8_t *)"x", 1, 50);
    CHECK("G13-DEADEND-STOPS", os_ai_job_step(&svc, j2) == 2 &&
          os_ai_job_state(&svc, j2) == OS_AI_JOB_DONE &&
          os_ai_job_read(&svc, all, j2, 0, out, 8) == 2 && memcmp(out, "yz", 2) == 0);

    int j3 = os_ai_job_submit(&svc, all, "cycle", (const uint8_t *)"a", 1, 20);
    CHECK("G13-STEP-BOUNDED", os_ai_job_step(&svc, j3) == (int)OS_AI_STEP_TOKENS &&
          os_ai_job_state(&svc, j3) == OS_AI_JOB_RUNNING);
    CHECK("G13-CANCEL-DENIED", os_ai_job_cancel(&svc, OS_AI_CAP_SUBMIT, j3) == OS_AI_ERR_DENIED);
    CHECK("G13-CANCEL", os_ai_job_cancel(&svc, all, j3) == OS_AI_OK &&
          os_ai_job_state(&svc, j3) == OS_AI_JOB_CANCELLED);
    CHECK("G13-CANCELLED-NO-PROGRESS", os_ai_job_step(&svc, j3) == OS_AI_ERR_STATE);
    CHECK("G13-CANCEL-PARTIAL-KEPT", os_ai_job_read(&svc, all, j3, 0, out, 32) == (int)OS_AI_STEP_TOKENS);
    CHECK("G13-CANCEL-TWICE-REJECTED", os_ai_job_cancel(&svc, all, j3) == OS_AI_ERR_STATE);

    /* Job table exhaustion, release and model unregister. */
    int first = -1, full = 0;
    for (unsigned i = 0; i < OS_AI_MAX_JOBS + 2; i++) {
        int r = os_ai_job_submit(&svc, all, "cycle", (const uint8_t *)"a", 1, 3);
        if (r == OS_AI_ERR_FULL) full = 1;
        else if (r >= 0 && first < 0) first = r;
    }
    CHECK("G13-JOB-TABLE-BOUNDED", full);
    CHECK("G13-RELEASE-ACTIVE-REJECTED", first >= 0 && os_ai_job_release(&svc, first) == OS_AI_ERR_STATE);
    for (unsigned j = 0; j < OS_AI_MAX_JOBS; j++) {
        if (os_ai_job_state(&svc, (int)j) == OS_AI_JOB_QUEUED)
            os_ai_job_cancel(&svc, all, (int)j);
        os_ai_job_release(&svc, (int)j);
    }
    uint32_t used = svc.memory_used;
    CHECK("G13-UNREGISTER", os_ai_model_unregister(&svc, all, "cycle") == OS_AI_OK && svc.memory_used < used);
    CHECK("G13-UNREGISTER-MISSING", os_ai_model_unregister(&svc, all, "cycle") == OS_AI_ERR_NOT_FOUND);
    CHECK("G13-NULL-ARGS", os_ai_job_step(NULL, 0) == OS_AI_ERR_ARGUMENT &&
          os_ai_job_state(&svc, 99) == OS_AI_ERR_ARGUMENT);

    if (failures) { printf("G13 host tests: %d failure(s)\n", failures); return 1; }
    printf("PASS G13-AI-CORE: registry, bounded jobs, cancellation, capability checks and inference\n");
    return 0;
}
