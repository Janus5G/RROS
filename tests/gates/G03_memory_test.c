#include "memory.h"

#include <stdio.h>

#define CHECK(condition, label) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s at line %d\n", label, __LINE__); \
            return 1; \
        } \
    } while (0)

static u8 arena[16u * OS_PAGE_SIZE] __attribute__((aligned(OS_PAGE_SIZE)));

int main(void)
{
    for (usize i = 0; i < sizeof(arena); ++i) {
        arena[i] = 0xa5;
    }

    CHECK(!os_page_pool_init((os_page_pool *)0, arena, 4, 0),
          "null page pool");
    CHECK(!os_page_pool_init((os_page_pool *)arena, arena, 65, 0),
          "page-count overflow");

    os_page_pool pool;
    CHECK(os_page_pool_init(&pool, arena, 4, 0x2), "page pool init");
    CHECK(os_page_free_count(&pool) == 3, "reserved page excluded");
    void *first_page = os_page_alloc(&pool, 10);
    void *second_page = os_page_alloc(&pool, 20);
    CHECK(first_page == arena, "first available page");
    CHECK(second_page == arena + 2u * OS_PAGE_SIZE,
          "reserved page skipped");
    CHECK(((u8 *)first_page)[0] == 0, "allocated page cleared");
    CHECK(!os_page_free(&pool, first_page, 20), "wrong page owner");
    CHECK(pool.owners[0] == 10, "wrong-owner free preserves ownership");
    CHECK(!os_page_free(&pool, arena + 1u, 10), "unaligned page free");
    CHECK(!os_page_free(&pool, arena + OS_PAGE_SIZE, 10),
          "reserved page cannot be freed");
    CHECK(os_page_free(&pool, first_page, 10), "valid page free");
    CHECK(!os_page_free(&pool, first_page, 10), "double page free");
    CHECK(os_page_free(&pool, second_page, 20), "second page free");
    CHECK(os_page_free_count(&pool) == 3, "page count restored");

    os_heap heap;
    void *heap_base = arena + 4u * OS_PAGE_SIZE;
    usize heap_size = 8u * OS_PAGE_SIZE;
    CHECK(!os_heap_init(&heap, heap_base, 1), "undersized heap");
    CHECK(os_heap_init(&heap, heap_base, heap_size), "heap init");
    CHECK(os_heap_alloc(&heap, 0, 1) == (void *)0, "zero allocation");
    CHECK(os_heap_alloc(&heap, (usize)-8, 1) == (void *)0,
          "allocation size overflow");
    void *first = os_heap_alloc(&heap, 31, 1);
    void *second = os_heap_alloc(&heap, OS_PAGE_SIZE + 1u, 2);
    CHECK(first != (void *)0 && second != (void *)0, "heap allocations");
    CHECK(((usize)first & 15u) == 0 && ((usize)second & 15u) == 0,
          "heap alignment");
    CHECK(!os_heap_free(&heap, first, 2), "wrong heap owner");
    CHECK(os_heap_free(&heap, first, 1), "first heap free");
    CHECK(os_heap_free(&heap, second, 2), "second heap free");
    CHECK(!os_heap_free(&heap, second, 2), "double heap free");
    usize header_size = (sizeof(os_heap_block) + 15u) & ~(usize)15u;
    void *whole_heap = os_heap_alloc(&heap, heap_size - header_size, 3);
    CHECK(whole_heap != (void *)0, "coalesced heap capacity");
    CHECK(os_heap_free(&heap, whole_heap, 3), "coalesced heap free");

    os_virtual_space space;
    const u64 base = 0x40000000u;
    CHECK(!os_virtual_space_init(&space, base + 1u, 0x10000u),
          "unaligned virtual space");
    CHECK(os_virtual_space_init(&space, base, 0x10000u),
          "virtual space init");
    CHECK(!os_virtual_reserve(
        &space, base, OS_PAGE_SIZE, 0, OS_VM_READ), "zero virtual owner");
    CHECK(!os_virtual_reserve(
        &space, base, 0, 1, OS_VM_READ), "zero virtual range");
    CHECK(!os_virtual_reserve(
        &space, base, OS_PAGE_SIZE, 1,
        OS_VM_READ | OS_VM_WRITE | OS_VM_EXECUTE), "writable executable");
    CHECK(os_virtual_reserve(
        &space, base, OS_PAGE_SIZE, 1, OS_VM_READ | OS_VM_EXECUTE),
        "executable virtual region");
    CHECK(!os_virtual_reserve(
        &space, base, OS_PAGE_SIZE, 2, OS_VM_READ), "overlapping virtual range");
    CHECK(!os_virtual_reserve(
        &space, base + 0x10000u, OS_PAGE_SIZE, 2, OS_VM_READ),
        "out-of-range virtual region");
    CHECK(!os_virtual_release(&space, base, 2), "wrong virtual owner");
    CHECK(space.region_count == 1, "wrong-owner release preserves mapping");
    CHECK(os_virtual_release(&space, base, 1), "valid virtual release");
    CHECK(!os_virtual_release(&space, base, 1), "double virtual release");

    CHECK(os_memory_self_test(arena, sizeof(arena)) == 0,
          "guest memory self-test");
    CHECK(os_memory_self_test((void *)0, sizeof(arena)) != 0,
          "null self-test arena");
    puts("PASS G03-MEMORY-HOST: reserved pages, ownership, heap and virtual ranges");
    return 0;
}
