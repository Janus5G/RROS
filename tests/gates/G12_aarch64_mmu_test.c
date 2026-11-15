#include "aarch64_mmu.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition, label) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL G12-MMU %s at line %d\n", label, __LINE__); \
            return 1; \
        } \
    } while (0)

#define TEST_TABLE_PAGES 64u
#define TEST_TABLE_POOL_SIZE \
    (TEST_TABLE_PAGES * OS_AARCH64_MMU_PAGE_SIZE)

static u8 g_table_pool[TEST_TABLE_POOL_SIZE]
    __attribute__((aligned(OS_AARCH64_MMU_PAGE_SIZE)));

int main(void)
{
    os_aarch64_mmu mmu;
    CHECK(os_aarch64_mmu_init(
        &mmu, g_table_pool, 0x10000000u, sizeof(g_table_pool), 48u),
        "initialize bounded 48-bit table pool");
    CHECK(os_aarch64_mmu_root_physical(&mmu) == 0x10000000u,
        "expose physical root address");
    CHECK(!os_aarch64_mmu_init(
        &mmu, g_table_pool, 0x10000001u, sizeof(g_table_pool), 48u),
        "reject unaligned physical table pool");
    CHECK(!os_aarch64_mmu_init(
        &mmu, g_table_pool, 0x10000000u, sizeof(g_table_pool), 52u),
        "reject unsupported translation width");
    CHECK(os_aarch64_mmu_init(
        &mmu, g_table_pool, 0x10000000u, sizeof(g_table_pool), 48u),
        "reinitialize after invalid configuration");

    CHECK(os_aarch64_mmu_map(
        &mmu, 0x40000000u, 0x40000000u,
        OS_AARCH64_MMU_BLOCK_1G, OS_AARCH64_MMU_NORMAL_WB),
        "map aligned 1 GiB normal-memory block");
    u64 physical = 0;
    u64 descriptor = 0;
    CHECK(os_aarch64_mmu_translate(
        &mmu, 0x456789abu, &physical, &descriptor)
        && physical == 0x456789abu
        && ((descriptor >> 2) & 7u) == 1u
        && (descriptor & (1u << 10)) != 0,
        "translate normal block with WB and access flag");
    usize tables_after_block = mmu.table_pages_used;
    CHECK(!os_aarch64_mmu_map(
        &mmu, 0x40000000u, 0x80000000u,
        OS_AARCH64_MMU_PAGE_SIZE, OS_AARCH64_MMU_NORMAL_WB)
        && mmu.table_pages_used == tables_after_block
        && os_aarch64_mmu_translate(
            &mmu, 0x456789abu, &physical, 0)
        && physical == 0x456789abu,
        "reject overlapping mapping without modifying table");
    CHECK(!os_aarch64_mmu_unmap(
        &mmu, 0x40000000u, OS_AARCH64_MMU_PAGE_SIZE)
        && os_aarch64_mmu_translate(
            &mmu, 0x456789abu, &physical, 0),
        "reject partial unmap of block");
    CHECK(os_aarch64_mmu_unmap(
        &mmu, 0x40000000u, OS_AARCH64_MMU_BLOCK_1G)
        && !os_aarch64_mmu_translate(
            &mmu, 0x456789abu, &physical, 0),
        "unmap complete block");

    const u64 device_virtual = 0x0000f00000000000ull;
    const u64 device_physical = 0x0000007c00000000ull;
    CHECK(os_aarch64_mmu_map(
        &mmu, device_virtual, device_physical, 2u * OS_AARCH64_MMU_PAGE_SIZE,
        OS_AARCH64_MMU_DEVICE),
        "map device region at distinct virtual address");
    CHECK(os_aarch64_mmu_translate(
        &mmu, device_virtual + 0x1234u, &physical, &descriptor)
        && physical == device_physical + 0x1234u
        && ((descriptor >> 2) & 7u) == 0u
        && ((descriptor >> 8) & 3u) == 2u
        && (descriptor & (1ull << 53)) != 0
        && (descriptor & (1ull << 54)) != 0,
        "device mapping is nGnRnE-indexed, outer-shareable and execute-never");
    CHECK(!os_aarch64_mmu_map(
        &mmu, device_virtual + 0x1000u, device_physical + 0x1000u,
        OS_AARCH64_MMU_PAGE_SIZE,
        OS_AARCH64_MMU_DEVICE | OS_AARCH64_MMU_EXECUTABLE),
        "reject executable device mapping");
    CHECK(!os_aarch64_mmu_map(
        &mmu, device_virtual + 0x4000u, device_physical + 0x4000u,
        OS_AARCH64_MMU_PAGE_SIZE,
        OS_AARCH64_MMU_NORMAL_NC | OS_AARCH64_MMU_NORMAL_WT),
        "reject conflicting memory types");
    CHECK(os_aarch64_mmu_unmap(
        &mmu, device_virtual, 2u * OS_AARCH64_MMU_PAGE_SIZE)
        && !os_aarch64_mmu_translate(
            &mmu, device_virtual, &physical, 0),
        "unmap device pages");

    CHECK(os_aarch64_mmu_map(
        &mmu, 0x0000000200001000ull, 0x0000000200001000ull,
        OS_AARCH64_MMU_PAGE_SIZE,
        OS_AARCH64_MMU_NORMAL_NC | OS_AARCH64_MMU_READ_ONLY
            | OS_AARCH64_MMU_EXECUTABLE),
        "map read-only executable normal-NC page");
    CHECK(os_aarch64_mmu_translate(
        &mmu, 0x0000000200001000ull, &physical, &descriptor)
        && physical == 0x0000000200001000ull
        && ((descriptor >> 2) & 7u) == 2u
        && ((descriptor >> 6) & 3u) == 2u
        && (descriptor & (1ull << 53)) == 0
        && (descriptor & (1ull << 54)) != 0,
        "encode executable read-only normal-NC attributes");

    CHECK(!os_aarch64_mmu_map(
        &mmu, 0x1001u, 0x2000u, OS_AARCH64_MMU_PAGE_SIZE,
        OS_AARCH64_MMU_DEVICE),
        "reject unaligned virtual address");
    CHECK(!os_aarch64_mmu_map(
        &mmu, 0x3000u, 0x2001u, OS_AARCH64_MMU_PAGE_SIZE,
        OS_AARCH64_MMU_DEVICE),
        "reject unaligned physical address");
    CHECK(!os_aarch64_mmu_map(
        &mmu, 0x0000fffffffff000ull, 0x1000u,
        2u * OS_AARCH64_MMU_PAGE_SIZE, OS_AARCH64_MMU_DEVICE),
        "reject virtual range overflow");

    const u64 rollback_virtual = 0x0000e00000001000ull;
    CHECK(os_aarch64_mmu_map(
        &mmu, rollback_virtual + OS_AARCH64_MMU_PAGE_SIZE,
        0x9000u, OS_AARCH64_MMU_PAGE_SIZE, OS_AARCH64_MMU_DEVICE),
        "prepare collision after an unmapped page");
    usize tables_before_rollback = mmu.table_pages_used;
    CHECK(!os_aarch64_mmu_map(
        &mmu, rollback_virtual, 0x8000u,
        2u * OS_AARCH64_MMU_PAGE_SIZE, OS_AARCH64_MMU_DEVICE)
        && mmu.table_pages_used == tables_before_rollback
        && !os_aarch64_mmu_translate(
            &mmu, rollback_virtual, &physical, 0)
        && os_aarch64_mmu_translate(
            &mmu, rollback_virtual + OS_AARCH64_MMU_PAGE_SIZE,
            &physical, 0)
        && physical == 0x9000u,
        "rollback newly allocated tables after a later overlap");
    CHECK(os_aarch64_mmu_unmap(
        &mmu, rollback_virtual + OS_AARCH64_MMU_PAGE_SIZE,
        OS_AARCH64_MMU_PAGE_SIZE),
        "release rollback fixture mapping");

    CHECK(!os_aarch64_mmu_map(
        &mmu, 0x5000u, 0x0001000000000000ull,
        OS_AARCH64_MMU_PAGE_SIZE, OS_AARCH64_MMU_DEVICE),
        "reject physical address beyond configured width");

    u8 tiny_pool[OS_AARCH64_MMU_PAGE_SIZE]
        __attribute__((aligned(OS_AARCH64_MMU_PAGE_SIZE)));
    os_aarch64_mmu exhausted;
    CHECK(os_aarch64_mmu_init(
        &exhausted, tiny_pool, 0x20000000u, sizeof(tiny_pool), 48u),
        "initialize single-page table pool");
    CHECK(!os_aarch64_mmu_map(
        &exhausted, 0x0000004000000000ull, 0x1000u,
        OS_AARCH64_MMU_PAGE_SIZE, OS_AARCH64_MMU_DEVICE)
        && exhausted.table_pages_used == 1u
        && !os_aarch64_mmu_translate(
            &exhausted, 0x0000004000000000ull, &physical, 0),
        "rollback table exhaustion without partial mapping");

    puts("PASS G12-AARCH64-MMU: bounded stage-1 table construction and attributes");
    return 0;
}
