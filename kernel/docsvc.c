#include "docsvc.h"
#include "sha256.h"

static int char_ok(char c) { return c == '\n' || (c >= 32 && c < 127); }

int os_doc_set_text(os_document *doc, const char *name, const char *text, size_t length)
{
    size_t nl = 0;
    if (!doc || !name || (!text && length)) return OS_DOC_ERR_ARGUMENT;
    if (length > OS_DOC_MAX_BYTES) return OS_DOC_ERR_SIZE;
    while (name[nl]) nl++;
    if (nl == 0 || nl >= OS_DOC_NAME_MAX) return OS_DOC_ERR_ARGUMENT;
    for (size_t i = 0; i < length; i++) if (!char_ok(text[i])) return OS_DOC_ERR_CHARACTER;
    for (size_t i = 0; i <= nl; i++) doc->name[i] = name[i];
    for (size_t i = 0; i < length; i++) doc->text[i] = text[i];
    doc->length = length;
    return OS_DOC_OK;
}

int os_doc_save(const os_document *doc, os_storage_fs *fs, const char *path)
{
    if (!doc || !fs || !path) return OS_DOC_ERR_ARGUMENT;
    return os_storage_write(fs, path, doc->text, doc->length) ? OS_DOC_OK : OS_DOC_ERR_STORAGE;
}

int os_doc_load(os_document *doc, os_storage_fs *fs, const char *path, const char *name)
{
    usize size = 0, got = 0;
    static char staging[OS_DOC_MAX_BYTES];
    if (!doc || !fs || !path || !name) return OS_DOC_ERR_ARGUMENT;
    if (!os_storage_exists(fs, path, &size)) return OS_DOC_ERR_STORAGE;
    if (size > OS_DOC_MAX_BYTES) return OS_DOC_ERR_SIZE;
    if (size && !os_storage_read(fs, path, 0, staging, size, &got)) return OS_DOC_ERR_STORAGE;
    if (got != size) return OS_DOC_ERR_STORAGE;
    return os_doc_set_text(doc, name, staging, size);
}

static int add_line(os_doc_layout *l, size_t start, size_t len)
{
    if (l->lines >= OS_DOC_MAX_LINES) return 0;
    l->line_start[l->lines] = (uint16_t)start;
    l->line_len[l->lines] = (uint16_t)len;
    l->lines++;
    return 1;
}

int os_doc_layout_build(const os_document *doc, unsigned cols, unsigned rows, os_doc_layout *l)
{
    size_t i = 0, n;
    if (!doc || !l || cols < 20 || cols > 80 || rows < 5 || rows > 50) return OS_DOC_ERR_ARGUMENT;
    n = doc->length;
    l->lines = 0; l->cols = cols; l->rows = rows;
    while (n > 0 && i <= n) {
        size_t e = i, s;
        while (e < n && doc->text[e] != '\n') e++;
        if (e == n && i == n && n > 0) break;
        if (e == i) {
            if (!add_line(l, i, 0)) return OS_DOC_ERR_LAYOUT;
        } else {
            for (s = i; s < e;) {
                size_t rem = e - s, k;
                if (rem <= cols) { if (!add_line(l, s, rem)) return OS_DOC_ERR_LAYOUT; break; }
                k = cols;
                while (k > 0 && doc->text[s + k] != ' ') k--;
                if (k == 0) { if (!add_line(l, s, cols)) return OS_DOC_ERR_LAYOUT; s += cols; }
                else { if (!add_line(l, s, k)) return OS_DOC_ERR_LAYOUT; s += k + 1u; }
            }
        }
        if (e >= n) break;
        i = e + 1u;
    }
    l->pages = l->lines ? (l->lines + rows - 1u) / rows : 1u;
    if (l->pages > OS_DOC_MAX_PAGES) return OS_DOC_ERR_LAYOUT;
    return OS_DOC_OK;
}

int os_doc_page_text(const os_document *doc, const os_doc_layout *l, unsigned page, char *out, size_t cap)
{
    size_t o = 0;
    unsigned first, last;
    char foot[32];
    size_t fl = 0;
    if (!doc || !l || !out || page == 0 || page > l->pages) return OS_DOC_ERR_RANGE;
    first = (page - 1u) * l->rows;
    last = first + l->rows < l->lines ? first + l->rows : l->lines;
    for (unsigned ln = first; ln < last; ln++) {
        if (o + l->line_len[ln] + 2u > cap) return OS_DOC_ERR_SIZE;
        for (unsigned k = 0; k < l->line_len[ln]; k++) out[o++] = doc->text[l->line_start[ln] + k];
        out[o++] = '\n';
    }
    {
        const char *a = "[page ", *b = " of ", *c = "]";
        unsigned vals[2] = { page, l->pages };
        for (const char *p = a; *p; p++) foot[fl++] = *p;
        for (unsigned v = 0; v < 2; v++) {
            char t[6];
            unsigned m = 0, x = vals[v];
            do { t[m++] = (char)('0' + x % 10u); x /= 10u; } while (x);
            while (m) foot[fl++] = t[--m];
            if (v == 0) for (const char *p = b; *p; p++) foot[fl++] = *p;
        }
        for (const char *p = c; *p; p++) foot[fl++] = *p;
        foot[fl++] = '\n';
    }
    if (o + fl + 1u > cap) return OS_DOC_ERR_SIZE;
    for (size_t k = 0; k < fl; k++) out[o++] = foot[k];
    out[o] = 0;
    return (int)o;
}

int os_print_init(os_print_spooler *sp, const os_print_backend *backend)
{
    if (!sp || !backend || !backend->name || !backend->submit) return OS_DOC_ERR_ARGUMENT;
    sp->backend = *backend;
    for (unsigned i = 0; i < OS_PRINT_JOBS; i++) sp->jobs[i].state = OS_PJ_NONE;
    return OS_DOC_OK;
}

static void compute_digest(const os_print_job *j, uint8_t out[32])
{
    uint8_t buf[32 + 20];
    uint32_t params[5];
    params[0] = j->cols; params[1] = j->rows; params[2] = j->first_page; params[3] = j->last_page; params[4] = j->copies;
    os_sha256_hash((const uint8_t *)j->doc.text, j->doc.length, buf);
    for (unsigned i = 0; i < 5; i++)
        for (unsigned k = 0; k < 4; k++) buf[32 + i * 4u + k] = (uint8_t)(params[i] >> (8u * k));
    os_sha256_hash(buf, sizeof buf, out);
}

int os_print_submit(os_print_spooler *sp, const os_document *doc, unsigned cols, unsigned rows,
                    unsigned first, unsigned last, unsigned copies)
{
    static os_doc_layout layout;
    int rc;
    if (!sp || !doc) return OS_DOC_ERR_ARGUMENT;
    rc = os_doc_layout_build(doc, cols, rows, &layout);
    if (rc != OS_DOC_OK) return rc;
    if (first == 0 || last < first || last > layout.pages || copies == 0 || copies > OS_PRINT_MAX_COPIES) return OS_DOC_ERR_RANGE;
    if ((last - first + 1u) * copies > 64u) return OS_DOC_ERR_RANGE;
    for (unsigned i = 0; i < OS_PRINT_JOBS; i++) {
        os_print_job *j = &sp->jobs[i];
        if (j->state == OS_PJ_QUEUED || j->state == OS_PJ_APPROVED) continue;
        j->doc = *doc;
        j->cols = cols; j->rows = rows; j->first_page = first; j->last_page = last; j->copies = copies;
        j->pages_done = 0;
        j->state = OS_PJ_QUEUED;
        compute_digest(j, j->digest);
        return (int)i;
    }
    return OS_DOC_ERR_FULL;
}

static os_print_job *job_at(os_print_spooler *sp, int job)
{
    return (sp && job >= 0 && job < (int)OS_PRINT_JOBS) ? &sp->jobs[job] : 0;
}

void os_print_job_digest(const os_print_spooler *sp, int job, uint8_t d[32])
{
    const os_print_job *j = job >= 0 && job < (int)OS_PRINT_JOBS && sp ? &sp->jobs[job] : 0;
    for (unsigned i = 0; i < 32; i++) d[i] = j ? j->digest[i] : 0;
}

int os_print_approve(os_print_spooler *sp, int job, const uint8_t digest[32])
{
    os_print_job *j = job_at(sp, job);
    uint8_t diff = 0;
    if (!j || !digest) return OS_DOC_ERR_ARGUMENT;
    if (j->state != OS_PJ_QUEUED) return OS_DOC_ERR_STATE;
    for (unsigned i = 0; i < 32; i++) diff |= (uint8_t)(digest[i] ^ j->digest[i]);
    if (diff) return OS_DOC_ERR_APPROVAL;
    j->state = OS_PJ_APPROVED;
    return OS_DOC_OK;
}

int os_print_cancel(os_print_spooler *sp, int job)
{
    os_print_job *j = job_at(sp, job);
    if (!j) return OS_DOC_ERR_ARGUMENT;
    if (j->state != OS_PJ_QUEUED && j->state != OS_PJ_APPROVED) return OS_DOC_ERR_STATE;
    j->state = OS_PJ_CANCELLED;
    return OS_DOC_OK;
}

int os_print_state(const os_print_spooler *sp, int job)
{
    return (sp && job >= 0 && job < (int)OS_PRINT_JOBS) ? sp->jobs[job].state : -1;
}

int os_print_run(os_print_spooler *sp, int job)
{
    static os_doc_layout layout;
    char page[OS_PRINT_MAX_PAGE_BYTES];
    os_print_job *j = job_at(sp, job);
    uint8_t d[32], diff = 0;
    if (!j) return OS_DOC_ERR_ARGUMENT;
    if (j->state != OS_PJ_APPROVED) return j->state == OS_PJ_QUEUED ? OS_DOC_ERR_APPROVAL : OS_DOC_ERR_STATE;
    compute_digest(j, d);
    for (unsigned i = 0; i < 32; i++) diff |= (uint8_t)(d[i] ^ j->digest[i]);
    j->state = OS_PJ_FAILED; /* approval is single-use: any early exit leaves the job failed, never re-runnable */
    if (diff) return OS_DOC_ERR_APPROVAL;
    if (os_doc_layout_build(&j->doc, j->cols, j->rows, &layout) != OS_DOC_OK) return OS_DOC_ERR_LAYOUT;
    for (unsigned c = 1; c <= j->copies; c++) {
        for (unsigned p = j->first_page; p <= j->last_page; p++) {
            int n = os_doc_page_text(&j->doc, &layout, p, page, sizeof page);
            if (n < 0) return n;
            if (sp->backend.submit(sp->backend.ctx, (unsigned)job, c, p, page, (size_t)n) != 0) return OS_DOC_ERR_BACKEND;
            j->pages_done++;
        }
    }
    j->state = OS_PJ_DONE;
    return OS_DOC_OK;
}

static int hw_submit(void *ctx, unsigned job, unsigned copy, unsigned page, const char *text, size_t length)
{
    (void)ctx; (void)job; (void)copy; (void)page; (void)text; (void)length;
    return -1;
}

const os_print_backend os_print_backend_hardware_unavailable = { "hardware-unavailable", hw_submit, 0 };

static int preview_submit(void *ctx, unsigned job, unsigned copy, unsigned page, const char *text, size_t length)
{
    os_print_preview *s = (os_print_preview *)ctx;
    (void)job; (void)copy; (void)page;
    if (!s || s->length + length + 1u > sizeof s->data) return -1;
    for (size_t i = 0; i < length; i++) s->data[s->length++] = text[i];
    s->data[s->length++] = '\f';
    s->data[s->length] = 0;
    s->pages++;
    return 0;
}

void os_print_backend_preview(os_print_backend *b, os_print_preview *sink)
{
    sink->length = 0; sink->pages = 0; sink->data[0] = 0;
    b->name = "memory-preview"; b->submit = preview_submit; b->ctx = sink;
}
