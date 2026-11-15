#include "aarch64_mmu.h"

#define MMU_DESCRIPTOR_VALID 0x1ull
#define MMU_DESCRIPTOR_TABLE_OR_PAGE 0x2ull
#define MMU_DESCRIPTOR_TYPE_MASK 0x3ull
#define MMU_DESCRIPTOR_ATTR_INDEX_SHIFT 2u
#define MMU_DESCRIPTOR_AP_SHIFT 6u
#define MMU_DESCRIPTOR_SH_SHIFT 8u
#define MMU_DESCRIPTOR_AF (1ull << 10)
#define MMU_DESCRIPTOR_PXN (1ull << 53)
#define MMU_DESCRIPTOR_UXN (1ull << 54)
#define MMU_DESCRIPTOR_ADDRESS_MASK 0x0000fffffffff000ull
#define MMU_MAX_TABLES 128u

typedef struct {
    os_aarch64_mmu *mmu;
    usize count;
    usize initial_table_pages_used;
} os_aarch64_mmu_transaction;

static u64 physical_limit(u8 bits)
{
    return 1ull << bits;
}

static u64 *table_at(const os_aarch64_mmu *mmu, usize index)
{
    return mmu->table_pool + index * OS_AARCH64_MMU_TABLE_ENTRIES;
}

static u64 table_physical(const os_aarch64_mmu *mmu, usize index)
{
    return mmu->table_pool_physical_base
        + index * (u64)OS_AARCH64_MMU_PAGE_SIZE;
}

static int range_valid(u64 base, u64 size, u64 limit)
{
    return size != 0
        && (base & (OS_AARCH64_MMU_PAGE_SIZE - 1u)) == 0
        && (size & (OS_AARCH64_MMU_PAGE_SIZE - 1u)) == 0
        && base < limit
        && size <= limit - base;
}

int os_aarch64_mmu_init(
    os_aarch64_mmu *mmu,
    void *table_pool,
    u64 table_pool_physical_base,
    usize table_pool_size,
    u8 physical_address_bits
)
{
    if (mmu == (os_aarch64_mmu *)0
        || table_pool == (void *)0
        || ((usize)table_pool & (OS_AARCH64_MMU_PAGE_SIZE - 1u)) != 0
        || (table_pool_physical_base
            & (OS_AARCH64_MMU_PAGE_SIZE - 1u)) != 0
        || table_pool_size < OS_AARCH64_MMU_PAGE_SIZE
        || (table_pool_size & (OS_AARCH64_MMU_PAGE_SIZE - 1u)) != 0
        || table_pool_size / OS_AARCH64_MMU_PAGE_SIZE > MMU_MAX_TABLES
        || physical_address_bits < 32u
        || physical_address_bits > 48u
        || !range_valid(
            table_pool_physical_base,
            table_pool_size,
            physical_limit(physical_address_bits))) {
        return 0;
    }

    mmu->table_pool = (u64 *)table_pool;
    mmu->table_pool_physical_base = table_pool_physical_base;
    mmu->table_pool_pages = table_pool_size / OS_AARCH64_MMU_PAGE_SIZE;
    mmu->table_pages_used = 1;
    mmu->physical_address_bits = physical_address_bits;
    mmu->installed = 0u;
    for (usize i = 0; i < mmu->table_pool_pages
            * OS_AARCH64_MMU_TABLE_ENTRIES; ++i) {
        mmu->table_pool[i] = 0;
    }
    return 1;
}

static int table_index(
    const os_aarch64_mmu *mmu,
    u64 physical_address,
    usize *index
)
{
    if (physical_address < mmu->table_pool_physical_base) {
        return 0;
    }
    u64 offset = physical_address - mmu->table_pool_physical_base;
    if ((offset & (OS_AARCH64_MMU_PAGE_SIZE - 1u)) != 0
        || offset / OS_AARCH64_MMU_PAGE_SIZE >= mmu->table_pages_used) {
        return 0;
    }
    *index = (usize)(offset / OS_AARCH64_MMU_PAGE_SIZE);
    return 1;
}

static int ensure_child_table(
    os_aarch64_mmu *mmu,
    u64 *parent_slot,
    os_aarch64_mmu_transaction *transaction,
    u64 **child
)
{
    u64 entry = *parent_slot;
    if ((entry & MMU_DESCRIPTOR_VALID) != 0) {
        usize index = 0;
        if ((entry & MMU_DESCRIPTOR_TYPE_MASK)
                != (MMU_DESCRIPTOR_VALID | MMU_DESCRIPTOR_TABLE_OR_PAGE)
            || !table_index(
                mmu, entry & MMU_DESCRIPTOR_ADDRESS_MASK, &index)) {
            return 0;
        }
        *child = table_at(mmu, index);
        return 1;
    }

    if (mmu->table_pages_used >= mmu->table_pool_pages
        || transaction->count >= MMU_MAX_TABLES) {
        return 0;
    }
    usize index = mmu->table_pages_used++;
    u64 *new_table = table_at(mmu, index);
    for (usize i = 0; i < OS_AARCH64_MMU_TABLE_ENTRIES; ++i) {
        new_table[i] = 0;
    }
    transaction->mmu->rollback_slots[transaction->count] = parent_slot;
    transaction->mmu->rollback_previous[transaction->count] = entry;
    ++transaction->count;
    *parent_slot = (table_physical(mmu, index) & MMU_DESCRIPTOR_ADDRESS_MASK)
        | MMU_DESCRIPTOR_VALID | MMU_DESCRIPTOR_TABLE_OR_PAGE;
    *child = new_table;
    return 1;
}

static int find_leaf_slot(
    os_aarch64_mmu *mmu,
    u64 virtual_address,
    u32 target_level,
    os_aarch64_mmu_transaction *transaction,
    u64 **leaf_slot
)
{
    u64 *table = table_at(mmu, 0);
    for (u32 level = 0; level < target_level; ++level) {
        u32 shift = 39u - 9u * level;
        usize index = (usize)((virtual_address >> shift) & 0x1ffu);
        if (!ensure_child_table(
                mmu, &table[index], transaction, &table)) {
            return 0;
        }
    }
    u32 shift = 39u - 9u * target_level;
    usize index = (usize)((virtual_address >> shift) & 0x1ffu);
    *leaf_slot = &table[index];
    return 1;
}

static void rollback_tables(
    os_aarch64_mmu *mmu,
    os_aarch64_mmu_transaction *transaction
)
{
    while (transaction->count != 0) {
        --transaction->count;
        *transaction->mmu->rollback_slots[transaction->count] =
            transaction->mmu->rollback_previous[transaction->count];
    }
    for (usize i = transaction->initial_table_pages_used;
            i < mmu->table_pages_used; ++i) {
        u64 *table = table_at(mmu, i);
        for (usize j = 0; j < OS_AARCH64_MMU_TABLE_ENTRIES; ++j) {
            table[j] = 0;
        }
    }
    mmu->table_pages_used = transaction->initial_table_pages_used;
}

static u32 mapping_level(
    u64 virtual_address,
    u64 physical_address,
    u64 remaining
)
{
    if ((virtual_address & (OS_AARCH64_MMU_BLOCK_1G - 1u)) == 0
        && (physical_address & (OS_AARCH64_MMU_BLOCK_1G - 1u)) == 0
        && remaining >= OS_AARCH64_MMU_BLOCK_1G) {
        return 1u;
    }
    if ((virtual_address & (OS_AARCH64_MMU_BLOCK_2M - 1u)) == 0
        && (physical_address & (OS_AARCH64_MMU_BLOCK_2M - 1u)) == 0
        && remaining >= OS_AARCH64_MMU_BLOCK_2M) {
        return 2u;
    }
    return 3u;
}

static u64 level_size(u32 level)
{
    return 1ull << (39u - 9u * level);
}

static int attributes_valid(u32 attributes)
{
    const u32 supported = OS_AARCH64_MMU_DEVICE
        | OS_AARCH64_MMU_READ_ONLY
        | OS_AARCH64_MMU_EXECUTABLE
        | OS_AARCH64_MMU_NORMAL_NC
        | OS_AARCH64_MMU_NORMAL_WT;
    return (attributes & ~supported) == 0
        && !((attributes & OS_AARCH64_MMU_NORMAL_NC)
            && (attributes & OS_AARCH64_MMU_NORMAL_WT))
        && !((attributes & OS_AARCH64_MMU_DEVICE)
            && (attributes & (OS_AARCH64_MMU_NORMAL_NC
                | OS_AARCH64_MMU_NORMAL_WT
                | OS_AARCH64_MMU_EXECUTABLE)));
}

static u64 page_descriptor(u64 physical_address, u32 attributes, u32 level)
{
    u64 descriptor = (physical_address & MMU_DESCRIPTOR_ADDRESS_MASK)
        | MMU_DESCRIPTOR_VALID | MMU_DESCRIPTOR_AF;
    if (level == 3u) {
        descriptor |= MMU_DESCRIPTOR_TABLE_OR_PAGE;
    }
    u32 attribute_index = (attributes & OS_AARCH64_MMU_DEVICE) != 0 ? 0u
        : ((attributes & OS_AARCH64_MMU_NORMAL_NC) != 0 ? 2u
            : ((attributes & OS_AARCH64_MMU_NORMAL_WT) != 0 ? 3u : 1u));
    descriptor |= (u64)attribute_index << MMU_DESCRIPTOR_ATTR_INDEX_SHIFT;
    descriptor |= (attributes & OS_AARCH64_MMU_READ_ONLY) != 0
        ? 2ull << MMU_DESCRIPTOR_AP_SHIFT
        : 0;
    descriptor |= (attributes & OS_AARCH64_MMU_DEVICE) != 0
        ? 2ull << MMU_DESCRIPTOR_SH_SHIFT
        : 3ull << MMU_DESCRIPTOR_SH_SHIFT;
    if ((attributes & OS_AARCH64_MMU_EXECUTABLE) == 0) {
        descriptor |= MMU_DESCRIPTOR_PXN;
    }
    descriptor |= MMU_DESCRIPTOR_UXN;
    return descriptor;
}

int os_aarch64_mmu_map(
    os_aarch64_mmu *mmu,
    u64 virtual_base,
    u64 physical_base,
    u64 size,
    u32 attributes
)
{
    const u64 virtual_limit = 1ull << 48;
    if (mmu == (os_aarch64_mmu *)0
        || mmu->table_pool == (u64 *)0
        || !range_valid(virtual_base, size, virtual_limit)
        || !range_valid(
            physical_base, size,
            physical_limit(mmu->physical_address_bits))
        || !attributes_valid(attributes)) {
        return 0;
    }

    os_aarch64_mmu_transaction transaction;
    transaction.mmu = mmu;
    transaction.count = 0;
    transaction.initial_table_pages_used = mmu->table_pages_used;

    u64 virtual_address = virtual_base;
    u64 physical_address = physical_base;
    u64 remaining = size;
    while (remaining != 0) {
        u32 level = mapping_level(
            virtual_address, physical_address, remaining);
        u64 chunk_size = level_size(level);
        u64 *slot = (u64 *)0;
        if (!find_leaf_slot(
                mmu, virtual_address, level, &transaction, &slot)
            || (*slot & MMU_DESCRIPTOR_VALID) != 0) {
            rollback_tables(mmu, &transaction);
            return 0;
        }
        virtual_address += chunk_size;
        physical_address += chunk_size;
        remaining -= chunk_size;
    }

    virtual_address = virtual_base;
    physical_address = physical_base;
    remaining = size;
    os_aarch64_mmu_transaction no_allocations;
    no_allocations.mmu = mmu;
    no_allocations.count = 0;
    no_allocations.initial_table_pages_used = mmu->table_pages_used;
    while (remaining != 0) {
        u32 level = mapping_level(
            virtual_address, physical_address, remaining);
        u64 chunk_size = level_size(level);
        u64 *slot = (u64 *)0;
        if (!find_leaf_slot(
                mmu, virtual_address, level, &no_allocations, &slot)) {
            return 0;
        }
        *slot = page_descriptor(physical_address, attributes, level);
        virtual_address += chunk_size;
        physical_address += chunk_size;
        remaining -= chunk_size;
    }
    return 1;
}

static int locate_leaf(
    const os_aarch64_mmu *mmu,
    u64 virtual_address,
    u64 **leaf_slot,
    u32 *leaf_level
)
{
    u64 *table = table_at(mmu, 0);
    for (u32 level = 0; level < 4u; ++level) {
        u32 shift = 39u - 9u * level;
        usize index = (usize)((virtual_address >> shift) & 0x1ffu);
        u64 *slot = &table[index];
        u64 entry = *slot;
        if ((entry & MMU_DESCRIPTOR_VALID) == 0) {
            return 0;
        }
        u64 type = entry & MMU_DESCRIPTOR_TYPE_MASK;
        if (level == 3u || type !=
                (MMU_DESCRIPTOR_VALID | MMU_DESCRIPTOR_TABLE_OR_PAGE)) {
            if (level == 0u || (level == 3u && type != 3u)
                || (level < 3u && type != 1u)) {
                return 0;
            }
            *leaf_slot = slot;
            *leaf_level = level;
            return 1;
        }
        usize child_index = 0;
        if (!table_index(
                mmu, entry & MMU_DESCRIPTOR_ADDRESS_MASK, &child_index)) {
            return 0;
        }
        table = table_at(mmu, child_index);
    }
    return 0;
}

int os_aarch64_mmu_unmap(
    os_aarch64_mmu *mmu,
    u64 virtual_base,
    u64 size
)
{
    if (mmu == (os_aarch64_mmu *)0
        || mmu->table_pool == (u64 *)0
        || !range_valid(virtual_base, size, 1ull << 48)) {
        return 0;
    }

    u64 address = virtual_base;
    u64 remaining = size;
    while (remaining != 0) {
        u64 *slot = (u64 *)0;
        u32 level = 0;
        if (!locate_leaf(mmu, address, &slot, &level)) {
            return 0;
        }
        u64 chunk_size = level_size(level);
        if ((address & (chunk_size - 1u)) != 0
            || remaining < chunk_size) {
            return 0;
        }
        address += chunk_size;
        remaining -= chunk_size;
    }

    address = virtual_base;
    remaining = size;
    while (remaining != 0) {
        u64 *slot = (u64 *)0;
        u32 level = 0;
        if (!locate_leaf(mmu, address, &slot, &level)) {
            return 0;
        }
        u64 chunk_size = level_size(level);
        *slot = 0;
        address += chunk_size;
        remaining -= chunk_size;
    }
    return 1;
}

int os_aarch64_mmu_translate(
    const os_aarch64_mmu *mmu,
    u64 virtual_address,
    u64 *physical_address,
    u64 *descriptor
)
{
    u64 *slot = (u64 *)0;
    u32 level = 0;
    if (mmu == (const os_aarch64_mmu *)0
        || mmu->table_pool == (u64 *)0
        || physical_address == (u64 *)0
        || virtual_address >= (1ull << 48)
        || !locate_leaf(mmu, virtual_address, &slot, &level)) {
        return 0;
    }
    u64 size = level_size(level);
    *physical_address = (*slot & MMU_DESCRIPTOR_ADDRESS_MASK)
        | (virtual_address & (size - 1u));
    if (descriptor != (u64 *)0) {
        *descriptor = *slot;
    }
    return 1;
}

u64 os_aarch64_mmu_root_physical(const os_aarch64_mmu *mmu)
{
    return mmu == (const os_aarch64_mmu *)0
        || mmu->table_pool == (u64 *)0
        ? 0 : mmu->table_pool_physical_base;
}
