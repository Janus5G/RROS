/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _XOPEN_SOURCE 700
#include "pi5_probe_support.h"
#include "memory.h"
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static char con_text[512], uart_text[512];
static usize con_used;
static efi_status con_result;
static efi_status console_write(efi_text_output_protocol *self, const u16 *text)
{
    (void)self;
    for (; *text; ++text) {
        CHECK(con_used + 1u < sizeof(con_text));
        con_text[con_used++] = (char)*text;
    }
    con_text[con_used] = 0;
    return con_result;
}

static void uart_write(const char *text)
{
    CHECK(strlen(text) < sizeof(uart_text));
    strcpy(uart_text, text);
}

static void test_uart(void)
{
    efi_text_output_protocol console = {0, console_write};
    char text[291];
    memset(text, 'x', sizeof(text) - 1u);
    text[sizeof(text) - 1u] = 0;
    for (unsigned mode = 0; mode < 4; ++mode) {
        con_used = 0;
        con_text[0] = uart_text[0] = 0;
        con_result = mode == 2 ? (1ull << 63) | 7u : 0u;
        os_pi5_probe_output(mode == 1 ? 0 : &console,
                            mode == 3 ? 0 : uart_write, text);
        if (mode != 1) CHECK(strcmp(con_text, text) == 0);
        if (mode != 3) CHECK(strcmp(uart_text, text) == 0);
    }
    puts("PASS UART: full multi-chunk fan-out, absent sinks and console error");
}

static void test_map(void)
{
    _Alignas(8) unsigned char bytes[128] = {0};
    efi_memory_descriptor d = {7, 0, 0x1000, 0, 257, 0};
    memcpy(bytes, &d, sizeof(d));
    os_pi5_probe_memory_summary result;
    CHECK(os_pi5_probe_summarize_memory_map(bytes, sizeof(d), sizeof(d), 1, &result));
    CHECK(result.conventional_pages == 257 && result.highest == 0x102000);
    /* A misaligned second descriptor must be rejected before any typed read. */
    memcpy(bytes + sizeof(d) + 1u, &d, sizeof(d));
    result = (os_pi5_probe_memory_summary){0};
    CHECK(!os_pi5_probe_summarize_memory_map(bytes, 2u * (sizeof(d) + 1u),
                                            sizeof(d) + 1u, 1, &result));
    CHECK(result.conventional_pages == 0 && result.highest == 0);
    const usize strides[] = {0, sizeof(d) - 1u, sizeof(d)};
    for (usize i = 0; i < sizeof(strides) / sizeof(strides[0]); ++i) {
        CHECK(!os_pi5_probe_summarize_memory_map(bytes, sizeof(d) - 1u,
                                                strides[i], 1, &result));
    }
    CHECK(!os_pi5_probe_summarize_memory_map(bytes + 1, sizeof(d), sizeof(d), 1, &result));
    CHECK(!os_pi5_probe_summarize_memory_map(bytes, 0, sizeof(d), 1, &result));
    CHECK(!os_pi5_probe_summarize_memory_map(bytes, sizeof(d), sizeof(d), 0, &result));
    d.number_of_pages = ~(u64)0 / OS_PAGE_SIZE + 1u;
    memcpy(bytes, &d, sizeof(d));
    CHECK(!os_pi5_probe_summarize_memory_map(bytes, sizeof(d), sizeof(d), 1, &result));
    CHECK(result.highest == 0 && result.conventional_pages == 0);
    d.number_of_pages = 1;
    d.physical_start = ~(u64)0 & ~(u64)(OS_PAGE_SIZE - 1u);
    memcpy(bytes, &d, sizeof(d));
    CHECK(!os_pi5_probe_summarize_memory_map(bytes, sizeof(d), sizeof(d), 1, &result));
    puts("PASS MAP: stride, truncation, alignment, version and arithmetic rejection");
}

#define THREADS 8u
#define ROUNDS 3000u
static _Alignas(OS_PAGE_SIZE) u8 arena[16u * OS_PAGE_SIZE];
static os_page_pool pool;
static pthread_barrier_t barrier;
static void *allocations[THREADS];
static usize lengths[THREADS];

static void rendezvous(void)
{
    int status = pthread_barrier_wait(&barrier);
    CHECK(status == 0 || status == PTHREAD_BARRIER_SERIAL_THREAD);
}

static void *allocate_worker(void *arg)
{
    usize index = (usize)arg;
    for (unsigned round = 0; round < ROUNDS; ++round) {
        rendezvous();
        u64 physical = 0;
        usize count = (index & 1u) + 1u;
        void *p = count == 1 ? os_page_alloc(&pool, (u32)index + 1u)
            : os_page_alloc_contiguous(&pool, (u32)index + 1u, count, 2, &physical);
        if (p) {
            if (count == 2) {
                CHECK((physical & (2u * OS_PAGE_SIZE - 1u)) == 0);
                CHECK(physical == 0x100000u + (usize)p - (usize)arena);
            }
            memset(p, (int)index + 1, count * OS_PAGE_SIZE);
        }
        allocations[index] = p;
        lengths[index] = count * OS_PAGE_SIZE;
        CHECK(os_page_free_count(&pool) <= 16);
        rendezvous();
        rendezvous();
        /* A competing operation may temporarily hold the nonblocking allocator. */
        unsigned attempts = 0;
        while (p && !(count == 1
                ? os_page_free(&pool, p, (u32)index + 1u)
                : os_page_free_contiguous(&pool, p, count, (u32)index + 1u))) {
            CHECK(++attempts < 1000000u);
            sched_yield();
        }
    }
    return 0;
}

static void test_concurrent(void)
{
    CHECK(os_page_pool_init_physical(&pool, arena, 0x100000, 16, 0));
    pthread_t workers[THREADS];
    CHECK(pthread_barrier_init(&barrier, 0, THREADS + 1u) == 0);
    for (usize i = 0; i < THREADS; ++i)
        CHECK(pthread_create(&workers[i], 0, allocate_worker, (void *)i) == 0);
    for (unsigned round = 0; round < ROUNDS; ++round) {
        rendezvous();
        rendezvous();
        unsigned successful = 0;
        for (usize i = 0; i < THREADS; ++i) {
            if (!allocations[i]) continue;
            ++successful;
            for (usize j = 0; j < i; ++j) {
                if (!allocations[j]) continue;
                usize a = (usize)allocations[i], b = (usize)allocations[j];
                CHECK(a + lengths[i] <= b || b + lengths[j] <= a);
            }
            const u8 *bytes = allocations[i];
            for (usize n = 0; n < lengths[i]; ++n) CHECK(bytes[n] == i + 1u);
        }
        CHECK(successful > 0);
        rendezvous();
    }
    for (usize i = 0; i < THREADS; ++i) CHECK(pthread_join(workers[i], 0) == 0);
    CHECK(pthread_barrier_destroy(&barrier) == 0);
    CHECK(os_page_free_count(&pool) == 16);
    puts("PASS OWNERSHIP: 24000 mixed concurrent requests, disjoint live ranges and data");
}

static void test_pool_bounds(void)
{
    CHECK(os_page_pool_init_physical(&pool, arena, 0x100000, 16, 0));
    u64 physical = 9;
    pool.page_count = OS_MAX_POOL_PAGES + 1u;
    CHECK(!os_page_alloc_contiguous(&pool, 1, 1, 1, &physical));
    CHECK(physical == 0);
    pool.page_count = 16;
    void *p = os_page_alloc_contiguous(&pool, 1, 2, 2, &physical);
    CHECK(p == arena);
    CHECK(!os_page_free_contiguous(&pool, p, 2, 2));
    CHECK(os_page_free_count(&pool) == 14);
    __atomic_store_n(&pool.lock, 1u, __ATOMIC_RELAXED);
    CHECK(!os_page_alloc(&pool, 3));
    CHECK(!os_page_alloc_contiguous(&pool, 3, 1, 1, &physical));
    CHECK(physical == 0);
    CHECK(!os_page_free(&pool, p, 1));
    CHECK(!os_page_free_contiguous(&pool, p, 2, 1));
    CHECK(os_page_free_count(&pool) == 14);
    __atomic_store_n(&pool.lock, 0u, __ATOMIC_RELEASE);
    CHECK(os_page_free_contiguous(&pool, p, 2, 1));
    CHECK(!os_page_free_contiguous(&pool, p, 2, 1));
    CHECK(!os_page_pool_init(&pool, (void *)(~(usize)0 & ~(usize)(OS_PAGE_SIZE - 1u)),
                             2, 0));
    puts("PASS OWNERSHIP: invalid pool bounds, wrong owner and double free");
}

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    if (!strcmp(argv[1], "uart")) test_uart();
    else if (!strcmp(argv[1], "map")) test_map();
    else if (!strcmp(argv[1], "ownership")) test_concurrent();
    else if (!strcmp(argv[1], "bounds")) test_pool_bounds();
    else CHECK(0);
    return 0;
}
