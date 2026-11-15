#include "ai_core.h"

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | (rd16(p + 2) << 16); }

static uint32_t fnv1a(uint32_t h, const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static int name_equal(const char *a, const char *b)
{
    for (size_t i = 0; i < OS_AI_MODEL_NAME_MAX; i++) {
        if (a[i] != b[i]) return 0;
        if (a[i] == 0) return 1;
    }
    return 0;
}

static int find_model(const os_ai_service *s, const char *name)
{
    for (uint32_t i = 0; i < OS_AI_MAX_MODELS; i++)
        if (s->models[i].in_use && name_equal(s->models[i].name, name)) return (int)i;
    return -1;
}

static int job_valid(const os_ai_service *s, int job)
{
    return s && job >= 0 && job < (int)OS_AI_MAX_JOBS &&
           s->jobs[job].state != OS_AI_JOB_FREE;
}

void os_ai_service_init(os_ai_service *s, uint32_t memory_budget)
{
    if (!s) return;
    uint8_t *raw = (uint8_t *)s;
    for (size_t i = 0; i < sizeof *s; i++) raw[i] = 0;
    s->memory_budget = memory_budget;
}

int os_ai_model_register(os_ai_service *s, uint32_t caps, const uint8_t *image,
                         size_t len)
{
    if (!s || !image) return OS_AI_ERR_ARGUMENT;
    if (!(caps & OS_AI_CAP_REGISTER)) return OS_AI_ERR_DENIED;
    if (len < 16u || image[0] != 'E' || image[1] != 'A' || image[2] != 'I' ||
        image[3] != 'M' || rd16(image + 4) != 1u)
        return OS_AI_ERR_FORMAT;
    uint32_t name_len = rd16(image + 6);
    uint32_t edges = rd32(image + 8);
    uint32_t checksum = rd32(image + 12);
    if (name_len == 0 || name_len >= OS_AI_MODEL_NAME_MAX || edges == 0 ||
        edges > OS_AI_MAX_EDGES)
        return OS_AI_ERR_FORMAT;
    if ((size_t)len != 16u + name_len + (size_t)edges * 3u) return OS_AI_ERR_FORMAT;
    const uint8_t *body = image + 16;
    if (fnv1a(2166136261u, body, name_len + (size_t)edges * 3u) != checksum)
        return OS_AI_ERR_FORMAT;
    for (uint32_t i = 0; i < name_len; i++)
        if (body[i] < 0x21 || body[i] > 0x7e) return OS_AI_ERR_FORMAT;

    char name[OS_AI_MODEL_NAME_MAX] = {0};
    for (uint32_t i = 0; i < name_len; i++) name[i] = (char)body[i];
    if (find_model(s, name) >= 0) return OS_AI_ERR_DUPLICATE;
    uint32_t memory = edges * 3u + name_len;
    if (memory > s->memory_budget - s->memory_used) return OS_AI_ERR_BUDGET;
    int slot = -1;
    for (uint32_t i = 0; i < OS_AI_MAX_MODELS; i++)
        if (!s->models[i].in_use) { slot = (int)i; break; }
    if (slot < 0) return OS_AI_ERR_FULL;

    os_ai_model *m = &s->models[slot];
    for (size_t i = 0; i < sizeof *m; i++) ((uint8_t *)m)[i] = 0;
    for (uint32_t i = 0; i < OS_AI_MODEL_NAME_MAX; i++) m->name[i] = name[i];
    const uint8_t *e = body + name_len;
    for (uint32_t i = 0; i < edges; i++) {
        m->edges[i].prev = e[i * 3u];
        m->edges[i].next = e[i * 3u + 1u];
        m->edges[i].weight = e[i * 3u + 2u];
    }
    m->edge_count = edges;
    m->memory_bytes = memory;
    m->in_use = 1;
    s->memory_used += memory;
    return slot;
}

int os_ai_model_unregister(os_ai_service *s, uint32_t caps, const char *name)
{
    if (!s || !name) return OS_AI_ERR_ARGUMENT;
    if (!(caps & OS_AI_CAP_REGISTER)) return OS_AI_ERR_DENIED;
    int idx = find_model(s, name);
    if (idx < 0) return OS_AI_ERR_NOT_FOUND;
    for (uint32_t j = 0; j < OS_AI_MAX_JOBS; j++) {
        uint8_t st = s->jobs[j].state;
        if ((st == OS_AI_JOB_QUEUED || st == OS_AI_JOB_RUNNING) &&
            s->jobs[j].model_index == idx)
            return OS_AI_ERR_STATE;
    }
    s->memory_used -= s->models[idx].memory_bytes;
    for (size_t i = 0; i < sizeof s->models[idx]; i++)
        ((uint8_t *)&s->models[idx])[i] = 0;
    return OS_AI_OK;
}

int os_ai_job_submit(os_ai_service *s, uint32_t caps, const char *model,
                     const uint8_t *prompt, size_t plen, uint8_t max_tokens)
{
    if (!s || !model || (!prompt && plen)) return OS_AI_ERR_ARGUMENT;
    if (!(caps & OS_AI_CAP_SUBMIT)) return OS_AI_ERR_DENIED;
    if (plen == 0 || plen > OS_AI_MAX_PROMPT || max_tokens == 0 ||
        max_tokens > OS_AI_MAX_OUTPUT)
        return OS_AI_ERR_ARGUMENT;
    int m = find_model(s, model);
    if (m < 0) return OS_AI_ERR_NOT_FOUND;
    for (uint32_t i = 0; i < OS_AI_MAX_JOBS; i++) {
        os_ai_job *j = &s->jobs[i];
        if (j->state != OS_AI_JOB_FREE) continue;
        for (size_t k = 0; k < sizeof *j; k++) ((uint8_t *)j)[k] = 0;
        for (size_t k = 0; k < plen; k++) j->prompt[k] = prompt[k];
        j->prompt_len = (uint8_t)plen;
        j->max_tokens = max_tokens;
        j->model_index = (uint8_t)m;
        j->last = prompt[plen - 1u];
        j->state = OS_AI_JOB_QUEUED;
        return (int)i;
    }
    return OS_AI_ERR_FULL;
}

/* Worker: sees only an immutable model and one byte of context. */
static int worker_next(const os_ai_model *m, uint8_t prev, uint8_t *out)
{
    int best = -1;
    uint8_t best_weight = 0;
    for (uint32_t i = 0; i < m->edge_count; i++) {
        const os_ai_edge *e = &m->edges[i];
        if (e->prev != prev || e->weight == 0) continue;
        if (best < 0 || e->weight > best_weight ||
            (e->weight == best_weight && e->next < (uint8_t)best)) {
            best = e->next;
            best_weight = e->weight;
        }
    }
    if (best < 0) return 0;
    *out = (uint8_t)best;
    return 1;
}

int os_ai_job_step(os_ai_service *s, int job)
{
    if (!job_valid(s, job)) return OS_AI_ERR_ARGUMENT;
    os_ai_job *j = &s->jobs[job];
    if (j->state == OS_AI_JOB_QUEUED) j->state = OS_AI_JOB_RUNNING;
    if (j->state != OS_AI_JOB_RUNNING) return OS_AI_ERR_STATE;
    const os_ai_model *m = &s->models[j->model_index];
    if (!m->in_use) {
        j->state = OS_AI_JOB_FAILED;
        return OS_AI_ERR_NOT_FOUND;
    }
    int made = 0;
    while (made < (int)OS_AI_STEP_TOKENS) {
        uint8_t next;
        if (j->output_len >= j->max_tokens || !worker_next(m, j->last, &next)) {
            j->state = OS_AI_JOB_DONE;
            break;
        }
        j->output[j->output_len++] = next;
        j->last = next;
        made++;
    }
    if (j->state == OS_AI_JOB_RUNNING && j->output_len >= j->max_tokens)
        j->state = OS_AI_JOB_DONE;
    return made;
}

int os_ai_job_cancel(os_ai_service *s, uint32_t caps, int job)
{
    if (!job_valid(s, job)) return OS_AI_ERR_ARGUMENT;
    if (!(caps & OS_AI_CAP_CANCEL)) return OS_AI_ERR_DENIED;
    os_ai_job *j = &s->jobs[job];
    if (j->state != OS_AI_JOB_QUEUED && j->state != OS_AI_JOB_RUNNING)
        return OS_AI_ERR_STATE;
    j->state = OS_AI_JOB_CANCELLED;
    return OS_AI_OK;
}

int os_ai_job_state(const os_ai_service *s, int job)
{
    return job_valid(s, job) ? (int)s->jobs[job].state : OS_AI_ERR_ARGUMENT;
}

int os_ai_job_read(const os_ai_service *s, uint32_t caps, int job, size_t offset,
                   uint8_t *out, size_t cap)
{
    if (!job_valid(s, job) || (!out && cap)) return OS_AI_ERR_ARGUMENT;
    if (!(caps & OS_AI_CAP_READ)) return OS_AI_ERR_DENIED;
    const os_ai_job *j = &s->jobs[job];
    if (offset > j->output_len) return OS_AI_ERR_ARGUMENT;
    size_t n = j->output_len - offset;
    if (n > cap) n = cap;
    for (size_t i = 0; i < n; i++) out[i] = j->output[offset + i];
    return (int)n;
}

int os_ai_job_release(os_ai_service *s, int job)
{
    if (!job_valid(s, job)) return OS_AI_ERR_ARGUMENT;
    uint8_t st = s->jobs[job].state;
    if (st == OS_AI_JOB_QUEUED || st == OS_AI_JOB_RUNNING) return OS_AI_ERR_STATE;
    for (size_t i = 0; i < sizeof s->jobs[job]; i++)
        ((uint8_t *)&s->jobs[job])[i] = 0;
    return OS_AI_OK;
}
