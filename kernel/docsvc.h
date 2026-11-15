#ifndef EXPERIMENTALOS_KERNEL_DOCSVC_H
#define EXPERIMENTALOS_KERNEL_DOCSVC_H

#include <stddef.h>
#include <stdint.h>
#include "storage.h"

/*
 * Document and printing services v1. Documents are bounded printable-ASCII text with deterministic
 * word-wrap pagination. Printing is an architecture only: a spooler with owner-approved jobs and an injected
 * backend. The only backends provided are an in-memory preview sink and a hardware stub that always fails
 * closed (no printer driver exists). Print authority is never granted to a job without approval bound to the
 * job's SHA-256; approval is a host/owner action, not reachable from the browser or MCP services.
 */
#define OS_DOC_MAX_BYTES 8192u
#define OS_DOC_NAME_MAX 24u
#define OS_DOC_MAX_LINES 512u
#define OS_DOC_MAX_PAGES 32u
#define OS_PRINT_JOBS 2u
#define OS_PRINT_MAX_COPIES 5u
#define OS_PRINT_MAX_PAGE_BYTES 4200u

enum os_doc_status {
    OS_DOC_OK = 0, OS_DOC_ERR_ARGUMENT = -1, OS_DOC_ERR_SIZE = -2, OS_DOC_ERR_CHARACTER = -3, OS_DOC_ERR_STORAGE = -4,
    OS_DOC_ERR_LAYOUT = -5, OS_DOC_ERR_STATE = -6, OS_DOC_ERR_APPROVAL = -7, OS_DOC_ERR_BACKEND = -8,
    OS_DOC_ERR_FULL = -9, OS_DOC_ERR_RANGE = -10, OS_DOC_ERR_NOT_FOUND = -11
};

typedef struct {
    char name[OS_DOC_NAME_MAX];
    char text[OS_DOC_MAX_BYTES];
    size_t length;
} os_document;

typedef struct {
    uint16_t line_start[OS_DOC_MAX_LINES];
    uint16_t line_len[OS_DOC_MAX_LINES];
    unsigned lines, cols, rows, pages;
} os_doc_layout;

int os_doc_set_text(os_document *doc, const char *name, const char *text, size_t length);
int os_doc_save(const os_document *doc, os_storage_fs *fs, const char *path);
int os_doc_load(os_document *doc, os_storage_fs *fs, const char *path, const char *name);
int os_doc_layout_build(const os_document *doc, unsigned cols, unsigned rows, os_doc_layout *layout);
/* Writes page (1-based) lines plus a "[page n of N]" footer; returns the length or a negative status. */
int os_doc_page_text(const os_document *doc, const os_doc_layout *layout, unsigned page, char *out, size_t capacity);

typedef struct {
    const char *name;
    int (*submit)(void *ctx, unsigned job, unsigned copy, unsigned page, const char *text, size_t length);
    void *ctx;
} os_print_backend;

enum os_print_state { OS_PJ_NONE = 0, OS_PJ_QUEUED, OS_PJ_APPROVED, OS_PJ_DONE, OS_PJ_FAILED, OS_PJ_CANCELLED };

typedef struct {
    uint8_t state;
    os_document doc;
    unsigned cols, rows, first_page, last_page, copies, pages_done;
    uint8_t digest[32];
} os_print_job;

typedef struct {
    os_print_backend backend;
    os_print_job jobs[OS_PRINT_JOBS];
} os_print_spooler;

int os_print_init(os_print_spooler *sp, const os_print_backend *backend);
/* Snapshots the document; the job is QUEUED and cannot print until approved. Returns the job id. */
int os_print_submit(os_print_spooler *sp, const os_document *doc, unsigned cols, unsigned rows,
                    unsigned first_page, unsigned last_page, unsigned copies);
void os_print_job_digest(const os_print_spooler *sp, int job, uint8_t digest[32]);
int os_print_approve(os_print_spooler *sp, int job, const uint8_t digest[32]);
int os_print_cancel(os_print_spooler *sp, int job);
int os_print_run(os_print_spooler *sp, int job);
int os_print_state(const os_print_spooler *sp, int job);

/* Backends: an in-memory preview sink and a hardware stub that fails closed. */
typedef struct { char data[OS_PRINT_MAX_PAGE_BYTES * 4u]; size_t length; unsigned pages; } os_print_preview;
extern const os_print_backend os_print_backend_hardware_unavailable;
void os_print_backend_preview(os_print_backend *backend, os_print_preview *sink);

#endif
