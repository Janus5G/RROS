#ifndef EXPERIMENTALOS_KERNEL_MEMORY_H
#define EXPERIMENTALOS_KERNEL_MEMORY_H

#include "efi.h"

#define OS_PAGE_SIZE 4096u
#define OS_MAX_POOL_PAGES 64u
#define OS_MAX_VIRTUAL_REGIONS 32u
#define OS_OWNER_RESERVED 0xffffffffu

#define OS_VM_READ 0x1u
#define OS_VM_WRITE 0x2u
#define OS_VM_EXECUTE 0x4u

typedef struct {
    u8 *base;
    u64 physical_base;
    usize page_count;
    u8 has_physical_base;
    u32 lock;
    u32 owners[OS_MAX_POOL_PAGES];
} os_page_pool;

typedef struct os_heap_block os_heap_block;

struct os_heap_block {
    usize size;
    u32 owner;
    u32 allocated;
    u64 guard;
    os_heap_block *previous;
    os_heap_block *next;
};

typedef struct {
    u8 *base;
    usize size;
    os_heap_block *first;
} os_heap;

typedef struct {
    u64 base;
    u64 size;
    u32 owner;
    u32 permissions;
} os_virtual_region;

typedef struct {
    u64 base;
    u64 size;
    usize region_count;
    os_virtual_region regions[OS_MAX_VIRTUAL_REGIONS];
} os_virtual_space;

/* Initialize before publication; backing storage and metadata must outlive users.
 * Mutators are nonblocking: contention returns NULL/0, leaving ownership intact.
 * An owner must quiesce CPU/device use before freeing and retry a busy release.
 * Do not reinitialize a published pool. Concurrent free_count is advisory.
 */
int os_page_pool_init(
    os_page_pool *pool,
    void *base,
    usize page_count,
    u64 reserved_mask
);
int os_page_pool_init_physical(
    os_page_pool *pool,
    void *base,
    u64 physical_base,
    usize page_count,
    u64 reserved_mask
);
void *os_page_alloc(os_page_pool *pool, u32 owner);
void *os_page_alloc_contiguous(
    os_page_pool *pool,
    u32 owner,
    usize page_count,
    usize alignment_pages,
    u64 *physical_address
);
int os_page_free(os_page_pool *pool, void *page, u32 owner);
int os_page_free_contiguous(
    os_page_pool *pool,
    void *base,
    usize page_count,
    u32 owner
);
usize os_page_free_count(const os_page_pool *pool);

int os_heap_init(os_heap *heap, void *base, usize size);
void *os_heap_alloc(os_heap *heap, usize size, u32 owner);
int os_heap_free(os_heap *heap, void *pointer, u32 owner);

int os_virtual_space_init(os_virtual_space *space, u64 base, u64 size);
int os_virtual_reserve(
    os_virtual_space *space,
    u64 base,
    u64 size,
    u32 owner,
    u32 permissions
);
int os_virtual_release(os_virtual_space *space, u64 base, u32 owner);

int os_memory_self_test(void *arena, usize arena_size);

#endif
