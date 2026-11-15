#include "memory.h"

#define OS_HEAP_GUARD_FREE 0x4f5346524545424full
#define OS_HEAP_GUARD_USED 0x4f53414c4c4f434bull
#define OS_HEAP_ALIGNMENT 16u

void *memset(void *destination, int value, usize size)
{
    volatile u8 *bytes = (volatile u8 *)destination;
    for (usize i = 0u; i < size; ++i) {
        bytes[i] = (u8)value;
    }
    return destination;
}

void *memcpy(void *destination, const void *source, usize size)
{
    volatile u8 *destination_bytes = (volatile u8 *)destination;
    const volatile u8 *source_bytes = (const volatile u8 *)source;
    for (usize i = 0u; i < size; ++i) {
        destination_bytes[i] = source_bytes[i];
    }
    return destination;
}

static usize align_up(usize value, usize alignment)
{
    usize mask = alignment - 1u;
    if (value > (~(usize)0) - mask) {
        return 0;
    }
    return (value + mask) & ~mask;
}

static usize heap_header_size(void)
{
    return align_up(sizeof(os_heap_block), OS_HEAP_ALIGNMENT);
}

static int range_end(u64 base, u64 size, u64 *end)
{
    if (size == 0 || base > (~(u64)0) - size) {
        return 0;
    }
    *end = base + size;
    return 1;
}

static int page_pool_valid(const os_page_pool *pool)
{
    return pool != (const os_page_pool *)0 && pool->base != (u8 *)0
        && pool->page_count != 0 && pool->page_count <= OS_MAX_POOL_PAGES;
}

static int page_pool_try_lock(os_page_pool *pool)
{
    u32 expected = 0;
    /* Never spin behind an interrupted allocator on the same CPU. */
    return __atomic_compare_exchange_n(
        &pool->lock, &expected, 1u, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

static void page_pool_unlock(os_page_pool *pool)
{
    __atomic_store_n(&pool->lock, 0u, __ATOMIC_RELEASE);
}

int os_page_pool_init(
    os_page_pool *pool,
    void *base,
    usize page_count,
    u64 reserved_mask
)
{
    if (!os_page_pool_init_physical(
            pool, base, 0, page_count, reserved_mask)) {
        return 0;
    }
    pool->has_physical_base = 0;
    return 1;
}

int os_page_pool_init_physical(
    os_page_pool *pool,
    void *base,
    u64 physical_base,
    usize page_count,
    u64 reserved_mask
)
{
    if (pool == (os_page_pool *)0 || base == (void *)0
        || page_count == 0 || page_count > OS_MAX_POOL_PAGES
        || ((usize)base & (OS_PAGE_SIZE - 1u)) != 0
        || (physical_base & (OS_PAGE_SIZE - 1u)) != 0
        || (usize)base > (~(usize)0) - page_count * OS_PAGE_SIZE
        || page_count > ((~(u64)0 - physical_base) / OS_PAGE_SIZE)
        || (page_count < OS_MAX_POOL_PAGES
            && (reserved_mask >> page_count) != 0)) {
        return 0;
    }

    pool->base = (u8 *)base;
    pool->physical_base = physical_base;
    pool->page_count = page_count;
    pool->has_physical_base = 1;
    pool->lock = 0;
    for (usize i = 0; i < page_count; ++i) {
        __atomic_store_n(&pool->owners[i],
            (reserved_mask & ((u64)1u << i)) != 0 ? OS_OWNER_RESERVED : 0,
            __ATOMIC_RELAXED);
    }
    return 1;
}

void *os_page_alloc(os_page_pool *pool, u32 owner)
{
    if (!page_pool_valid(pool)
        || owner == 0 || owner == OS_OWNER_RESERVED
        || !page_pool_try_lock(pool)) {
        return (void *)0;
    }

    for (usize i = 0; i < pool->page_count; ++i) {
        if (__atomic_load_n(&pool->owners[i], __ATOMIC_ACQUIRE) == 0) {
            u8 *page = pool->base + i * OS_PAGE_SIZE;
            for (usize offset = 0; offset < OS_PAGE_SIZE; ++offset) {
                page[offset] = 0;
            }
            __atomic_store_n(&pool->owners[i], owner, __ATOMIC_RELEASE);
            page_pool_unlock(pool);
            return page;
        }
    }
    page_pool_unlock(pool);
    return (void *)0;
}

void *os_page_alloc_contiguous(
    os_page_pool *pool,
    u32 owner,
    usize page_count,
    usize alignment_pages,
    u64 *physical_address
)
{
    if (physical_address != (u64 *)0) {
        *physical_address = 0;
    }
    if (!page_pool_valid(pool)
        || !pool->has_physical_base || owner == 0
        || owner == OS_OWNER_RESERVED || page_count == 0
        || page_count > pool->page_count || alignment_pages == 0
        || alignment_pages > (~(usize)0) / OS_PAGE_SIZE
        || (alignment_pages & (alignment_pages - 1u)) != 0
        || physical_address == (u64 *)0 || !page_pool_try_lock(pool)) {
        return (void *)0;
    }
    for (usize start = 0; start <= pool->page_count - page_count; ++start) {
        u64 address = pool->physical_base + start * OS_PAGE_SIZE;
        if ((address & (alignment_pages * OS_PAGE_SIZE - 1u)) != 0) {
            continue;
        }
        usize page = 0;
        while (page < page_count
               && __atomic_load_n(&pool->owners[start + page], __ATOMIC_ACQUIRE) == 0) {
            ++page;
        }
        if (page != page_count) {
            continue;
        }
        for (page = 0; page < page_count; ++page) {
            u8 *bytes = pool->base + (start + page) * OS_PAGE_SIZE;
            for (usize offset = 0; offset < OS_PAGE_SIZE; ++offset) {
                bytes[offset] = 0;
            }
            __atomic_store_n(&pool->owners[start + page], owner, __ATOMIC_RELEASE);
        }
        *physical_address = address;
        page_pool_unlock(pool);
        return pool->base + start * OS_PAGE_SIZE;
    }
    page_pool_unlock(pool);
    return (void *)0;
}

int os_page_free(os_page_pool *pool, void *page, u32 owner)
{
    if (!page_pool_valid(pool)
        || page == (void *)0 || owner == 0 || owner == OS_OWNER_RESERVED) {
        return 0;
    }

    usize base = (usize)pool->base;
    usize address = (usize)page;
    usize pool_size = pool->page_count * OS_PAGE_SIZE;
    if (address < base || address - base >= pool_size
        || ((address - base) & (OS_PAGE_SIZE - 1u)) != 0) {
        return 0;
    }

    usize index = (address - base) / OS_PAGE_SIZE;
    if (!page_pool_try_lock(pool)) {
        return 0;
    }
    if (__atomic_load_n(&pool->owners[index], __ATOMIC_ACQUIRE) != owner) {
        page_pool_unlock(pool);
        return 0;
    }
    __atomic_store_n(&pool->owners[index], 0u, __ATOMIC_RELEASE);
    page_pool_unlock(pool);
    return 1;
}

int os_page_free_contiguous(
    os_page_pool *pool,
    void *base_pointer,
    usize page_count,
    u32 owner
)
{
    if (!page_pool_valid(pool)
        || base_pointer == (void *)0 || page_count == 0 || owner == 0
        || owner == OS_OWNER_RESERVED) {
        return 0;
    }
    usize base = (usize)pool->base;
    usize address = (usize)base_pointer;
    usize pool_size = pool->page_count * OS_PAGE_SIZE;
    if (address < base || address - base >= pool_size
        || ((address - base) & (OS_PAGE_SIZE - 1u)) != 0) {
        return 0;
    }
    usize start = (address - base) / OS_PAGE_SIZE;
    if (page_count > pool->page_count - start || !page_pool_try_lock(pool)) {
        return 0;
    }
    for (usize page = 0; page < page_count; ++page) {
        if (__atomic_load_n(&pool->owners[start + page], __ATOMIC_ACQUIRE) != owner) {
            page_pool_unlock(pool);
            return 0;
        }
    }
    for (usize page = 0; page < page_count; ++page) {
        __atomic_store_n(&pool->owners[start + page], 0u, __ATOMIC_RELEASE);
    }
    page_pool_unlock(pool);
    return 1;
}

usize os_page_free_count(const os_page_pool *pool)
{
    if (!page_pool_valid(pool)) {
        return 0;
    }
    usize free_count = 0;
    for (usize i = 0; i < pool->page_count; ++i) {
        if (__atomic_load_n(&pool->owners[i], __ATOMIC_ACQUIRE) == 0) {
            ++free_count;
        }
    }
    return free_count;
}

static int heap_block_valid(const os_heap *heap, const os_heap_block *block)
{
    if (heap == (const os_heap *)0 || heap->base == (u8 *)0
        || block == (const os_heap_block *)0) {
        return 0;
    }

    usize base = (usize)heap->base;
    usize address = (usize)block;
    usize header = heap_header_size();
    if (header == 0 || address < base || address - base > heap->size
        || heap->size - (address - base) < header) {
        return 0;
    }
    if (block->size > heap->size - (address - base) - header
        || (block->size & (OS_HEAP_ALIGNMENT - 1u)) != 0
        || block->allocated > 1u) {
        return 0;
    }
    if (block->allocated != 0) {
        return block->owner != 0 && block->guard == OS_HEAP_GUARD_USED;
    }
    return block->owner == 0 && block->guard == OS_HEAP_GUARD_FREE;
}

static int heap_layout_valid(const os_heap *heap)
{
    if (heap == (const os_heap *)0 || heap->first == (os_heap_block *)0) {
        return 0;
    }

    os_heap_block *block = heap->first;
    os_heap_block *previous = (os_heap_block *)0;
    usize expected = (usize)heap->base;
    usize header = heap_header_size();
    usize limit = heap->size / header + 1u;
    for (usize count = 0; block != (os_heap_block *)0; ++count) {
        if (count >= limit || (usize)block != expected
            || !heap_block_valid(heap, block) || block->previous != previous) {
            return 0;
        }
        usize address = (usize)block;
        usize span = header + block->size;
        if (span > heap->size - (address - (usize)heap->base)) {
            return 0;
        }
        expected = address + span;
        previous = block;
        block = block->next;
    }
    return expected == (usize)heap->base + heap->size;
}

int os_heap_init(os_heap *heap, void *base, usize size)
{
    usize header = heap_header_size();
    if (heap == (os_heap *)0 || base == (void *)0 || header == 0
        || ((usize)base & (OS_HEAP_ALIGNMENT - 1u)) != 0
        || size < header + OS_HEAP_ALIGNMENT
        || (usize)base > (~(usize)0) - size) {
        return 0;
    }

    usize usable_size = size & ~(OS_HEAP_ALIGNMENT - 1u);
    if (usable_size < header + OS_HEAP_ALIGNMENT) {
        return 0;
    }
    heap->base = (u8 *)base;
    heap->size = usable_size;
    heap->first = (os_heap_block *)base;
    heap->first->size = usable_size - header;
    heap->first->owner = 0;
    heap->first->allocated = 0;
    heap->first->guard = OS_HEAP_GUARD_FREE;
    heap->first->previous = (os_heap_block *)0;
    heap->first->next = (os_heap_block *)0;
    return 1;
}

void *os_heap_alloc(os_heap *heap, usize size, u32 owner)
{
    if (heap == (os_heap *)0 || size == 0 || owner == 0
        || owner == OS_OWNER_RESERVED || !heap_layout_valid(heap)) {
        return (void *)0;
    }
    usize requested = align_up(size, OS_HEAP_ALIGNMENT);
    usize header = heap_header_size();
    if (requested == 0 || header == 0) {
        return (void *)0;
    }

    for (os_heap_block *block = heap->first;
         block != (os_heap_block *)0;
         block = block->next) {
        if (block->allocated != 0 || block->size < requested) {
            continue;
        }
        usize remainder = block->size - requested;
        if (remainder >= header + OS_HEAP_ALIGNMENT) {
            os_heap_block *split = (os_heap_block *)(
                (u8 *)block + header + requested
            );
            split->size = remainder - header;
            split->owner = 0;
            split->allocated = 0;
            split->guard = OS_HEAP_GUARD_FREE;
            split->previous = block;
            split->next = block->next;
            if (split->next != (os_heap_block *)0) {
                split->next->previous = split;
            }
            block->next = split;
            block->size = requested;
        }
        block->owner = owner;
        block->allocated = 1;
        block->guard = OS_HEAP_GUARD_USED;
        return (u8 *)block + header;
    }
    return (void *)0;
}

int os_heap_free(os_heap *heap, void *pointer, u32 owner)
{
    if (heap == (os_heap *)0 || pointer == (void *)0 || owner == 0
        || owner == OS_OWNER_RESERVED || !heap_layout_valid(heap)) {
        return 0;
    }

    usize header = heap_header_size();
    os_heap_block *block = heap->first;
    while (block != (os_heap_block *)0) {
        if ((u8 *)block + header == (u8 *)pointer) {
            break;
        }
        block = block->next;
    }
    if (block == (os_heap_block *)0 || block->allocated == 0
        || block->owner != owner) {
        return 0;
    }

    block->allocated = 0;
    block->owner = 0;
    block->guard = OS_HEAP_GUARD_FREE;
    if (block->next != (os_heap_block *)0 && block->next->allocated == 0) {
        block->size += header + block->next->size;
        block->next = block->next->next;
        if (block->next != (os_heap_block *)0) {
            block->next->previous = block;
        }
    }
    if (block->previous != (os_heap_block *)0
        && block->previous->allocated == 0) {
        os_heap_block *previous = block->previous;
        previous->size += header + block->size;
        previous->next = block->next;
        if (previous->next != (os_heap_block *)0) {
            previous->next->previous = previous;
        }
    }
    return heap_layout_valid(heap);
}

int os_virtual_space_init(os_virtual_space *space, u64 base, u64 size)
{
    u64 end = 0;
    if (space == (os_virtual_space *)0
        || (base & (OS_PAGE_SIZE - 1u)) != 0
        || (size & (OS_PAGE_SIZE - 1u)) != 0
        || !range_end(base, size, &end)) {
        return 0;
    }
    (void)end;
    space->base = base;
    space->size = size;
    space->region_count = 0;
    for (usize i = 0; i < OS_MAX_VIRTUAL_REGIONS; ++i) {
        space->regions[i].base = 0;
        space->regions[i].size = 0;
        space->regions[i].owner = 0;
        space->regions[i].permissions = 0;
    }
    return 1;
}

int os_virtual_reserve(
    os_virtual_space *space,
    u64 base,
    u64 size,
    u32 owner,
    u32 permissions
)
{
    u64 end = 0;
    u64 space_end = 0;
    if (space == (os_virtual_space *)0 || owner == 0
        || owner == OS_OWNER_RESERVED || size == 0
        || (base & (OS_PAGE_SIZE - 1u)) != 0
        || (size & (OS_PAGE_SIZE - 1u)) != 0
        || permissions == 0
        || (permissions & ~(OS_VM_READ | OS_VM_WRITE | OS_VM_EXECUTE)) != 0
        || (permissions & (OS_VM_WRITE | OS_VM_EXECUTE))
            == (OS_VM_WRITE | OS_VM_EXECUTE)
        || !range_end(base, size, &end)
        || !range_end(space->base, space->size, &space_end)
        || base < space->base || end > space_end
        || space->region_count >= OS_MAX_VIRTUAL_REGIONS) {
        return 0;
    }

    usize free_slot = OS_MAX_VIRTUAL_REGIONS;
    for (usize i = 0; i < OS_MAX_VIRTUAL_REGIONS; ++i) {
        os_virtual_region *region = &space->regions[i];
        if (region->size == 0) {
            if (free_slot == OS_MAX_VIRTUAL_REGIONS) {
                free_slot = i;
            }
            continue;
        }
        u64 region_end = 0;
        if (!range_end(region->base, region->size, &region_end)
            || (base < region_end && region->base < end)) {
            return 0;
        }
    }
    if (free_slot == OS_MAX_VIRTUAL_REGIONS) {
        return 0;
    }
    space->regions[free_slot].base = base;
    space->regions[free_slot].size = size;
    space->regions[free_slot].owner = owner;
    space->regions[free_slot].permissions = permissions;
    ++space->region_count;
    return 1;
}

int os_virtual_release(os_virtual_space *space, u64 base, u32 owner)
{
    if (space == (os_virtual_space *)0 || owner == 0
        || owner == OS_OWNER_RESERVED) {
        return 0;
    }
    for (usize i = 0; i < OS_MAX_VIRTUAL_REGIONS; ++i) {
        os_virtual_region *region = &space->regions[i];
        if (region->size != 0 && region->base == base) {
            if (region->owner != owner) {
                return 0;
            }
            region->base = 0;
            region->size = 0;
            region->owner = 0;
            region->permissions = 0;
            --space->region_count;
            return 1;
        }
    }
    return 0;
}

int os_memory_self_test(void *arena, usize arena_size)
{
    if (arena == (void *)0 || ((usize)arena & (OS_PAGE_SIZE - 1u)) != 0
        || arena_size < 12u * OS_PAGE_SIZE
        || (arena_size & (OS_PAGE_SIZE - 1u)) != 0) {
        return 1;
    }

    os_page_pool pages;
    if (!os_page_pool_init_physical(
            &pages, arena, 0x80000000u, 8u, 1u)) {
        return 2;
    }
    void *page = os_page_alloc(&pages, 1);
    if (page == (void *)0 || page != (u8 *)arena + OS_PAGE_SIZE
        || os_page_alloc(&pages, 0) != (void *)0
        || os_page_free(&pages, page, 2)) {
        return 3;
    }
    if (!os_page_free(&pages, page, 1)
        || os_page_free(&pages, page, 1)
        || os_page_free_count(&pages) != 7) {
        return 4;
    }
    u64 physical_address = 0;
    void *contiguous = os_page_alloc_contiguous(
        &pages, 2u, 2u, 2u, &physical_address);
    if (contiguous != (u8 *)arena + 2u * OS_PAGE_SIZE
        || physical_address != 0x80000000u + 2u * OS_PAGE_SIZE
        || os_page_alloc_contiguous(
            &pages, 3u, 2u, 0u, &physical_address) != (void *)0
        || !os_page_free_contiguous(&pages, contiguous, 2u, 2u)
        || os_page_free_contiguous(&pages, contiguous, 2u, 2u)
        || os_page_free_count(&pages) != 7u) {
        return 11;
    }

    os_heap heap;
    u8 *heap_base = (u8 *)arena + 8u * OS_PAGE_SIZE;
    usize heap_size = arena_size - 8u * OS_PAGE_SIZE;
    if (!os_heap_init(&heap, heap_base, heap_size)) {
        return 5;
    }
    void *first = os_heap_alloc(&heap, 31, 1);
    void *second = os_heap_alloc(&heap, OS_PAGE_SIZE + 1u, 2);
    if (first == (void *)0 || second == (void *)0
        || ((usize)first & (OS_HEAP_ALIGNMENT - 1u)) != 0
        || os_heap_free(&heap, first, 2)) {
        return 6;
    }
    if (!os_heap_free(&heap, first, 1)
        || !os_heap_free(&heap, second, 2)
        || os_heap_free(&heap, second, 2)) {
        return 7;
    }
    void *whole_heap = os_heap_alloc(&heap, heap_size / 2u, 3);
    if (whole_heap == (void *)0 || !os_heap_free(&heap, whole_heap, 3)) {
        return 8;
    }

    os_virtual_space virtual_space;
    const u64 virtual_base = 0x40000000u;
    if (!os_virtual_space_init(&virtual_space, virtual_base, 0x100000u)
        || !os_virtual_reserve(
            &virtual_space, virtual_base, OS_PAGE_SIZE, 1,
            OS_VM_READ | OS_VM_EXECUTE)
        || !os_virtual_reserve(
            &virtual_space, virtual_base + OS_PAGE_SIZE, OS_PAGE_SIZE, 2,
            OS_VM_READ | OS_VM_WRITE)
        || os_virtual_reserve(
            &virtual_space, virtual_base, OS_PAGE_SIZE, 3, OS_VM_READ)
        || os_virtual_reserve(
            &virtual_space, virtual_base + 2u * OS_PAGE_SIZE, OS_PAGE_SIZE, 3,
            OS_VM_READ | OS_VM_WRITE | OS_VM_EXECUTE)
        || os_virtual_reserve(
            &virtual_space, virtual_base + 0x100000u, OS_PAGE_SIZE, 3,
            OS_VM_READ)
        || os_virtual_release(&virtual_space, virtual_base, 2)) {
        return 9;
    }
    if (!os_virtual_release(&virtual_space, virtual_base, 1)
        || !os_virtual_release(
            &virtual_space, virtual_base + OS_PAGE_SIZE, 2)
        || virtual_space.region_count != 0) {
        return 10;
    }
    return 0;
}
