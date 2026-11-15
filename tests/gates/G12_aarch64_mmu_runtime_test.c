#include "aarch64_mmu_runtime.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition, label) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL G12-MMU-RUNTIME %s at line %d\n", \
                label, __LINE__); \
            return 1; \
        } \
    } while (0)

#define TEST_TABLE_PAGES 64u
#define TEST_TABLE_POOL_SIZE \
    (TEST_TABLE_PAGES * OS_AARCH64_MMU_PAGE_SIZE)
#define CODE_VIRTUAL 0x40000000ull
#define CODE_PHYSICAL 0x50000000ull
#define STACK_VIRTUAL 0x60000000ull
#define STACK_PHYSICAL 0x70000000ull
#define TABLE_PHYSICAL 0x30000000ull

static u8 g_table_pool[TEST_TABLE_POOL_SIZE]
    __attribute__((aligned(OS_AARCH64_MMU_PAGE_SIZE)));
static os_aarch64_mmu g_mmu;
static os_aarch64_mmu_arch_state g_arch_state;
static int g_active;
static int g_force_stack_failure;
static u32 g_install_calls;
static u32 g_invalidation_calls;

int arm_mmu_read_state(os_aarch64_mmu_arch_state *state)
{
    if (state == (os_aarch64_mmu_arch_state *)0) {
        return 0;
    }
    *state = g_arch_state;
    return 1;
}

u64 arm_mmu_translate_current(u64 virtual_address)
{
    if (g_active) {
        if (g_force_stack_failure
            && virtual_address >= STACK_VIRTUAL
            && virtual_address < STACK_VIRTUAL + OS_AARCH64_MMU_PAGE_SIZE) {
            return ~(u64)0;
        }
        u64 physical = 0;
        return os_aarch64_mmu_translate(
                &g_mmu, virtual_address, &physical, (u64 *)0)
            ? physical : ~(u64)0;
    }

    if (virtual_address >= CODE_VIRTUAL
        && virtual_address < CODE_VIRTUAL + OS_AARCH64_MMU_PAGE_SIZE) {
        return CODE_PHYSICAL + virtual_address - CODE_VIRTUAL;
    }
    if (virtual_address >= STACK_VIRTUAL
        && virtual_address < STACK_VIRTUAL + OS_AARCH64_MMU_PAGE_SIZE) {
        return STACK_PHYSICAL + virtual_address - STACK_VIRTUAL;
    }
    u64 table_virtual = (u64)(usize)g_table_pool;
    if (virtual_address >= table_virtual
        && virtual_address < table_virtual + sizeof(g_table_pool)) {
        return TABLE_PHYSICAL + virtual_address - table_virtual;
    }
    return ~(u64)0;
}

int arm_mmu_install(u64 ttbr0, u64 tcr, u64 mair, u64 current_el)
{
    ++g_install_calls;
    if (current_el != 1u && current_el != 2u) {
        return 0;
    }
    g_active = ttbr0 == os_aarch64_mmu_root_physical(&g_mmu);
    g_arch_state.current_el = current_el;
    g_arch_state.ttbr0 = ttbr0;
    g_arch_state.tcr = tcr;
    g_arch_state.mair = mair;
    return 1;
}

void arm_mmu_invalidate_current(void)
{
    ++g_invalidation_calls;
}

static int prepare_installer(os_aarch64_mmu_live_range *ranges)
{
    u64 table_virtual = (u64)(usize)g_table_pool;
    if (!os_aarch64_mmu_init(
            &g_mmu, g_table_pool, TABLE_PHYSICAL,
            sizeof(g_table_pool), 48u)
        || !os_aarch64_mmu_map(
            &g_mmu, CODE_VIRTUAL, CODE_PHYSICAL,
            OS_AARCH64_MMU_PAGE_SIZE,
            OS_AARCH64_MMU_NORMAL_WB | OS_AARCH64_MMU_EXECUTABLE)
        || !os_aarch64_mmu_map(
            &g_mmu, STACK_VIRTUAL, STACK_PHYSICAL,
            OS_AARCH64_MMU_PAGE_SIZE, OS_AARCH64_MMU_NORMAL_WB)
        || !os_aarch64_mmu_map(
            &g_mmu, table_virtual, TABLE_PHYSICAL,
            sizeof(g_table_pool), OS_AARCH64_MMU_NORMAL_WB)) {
        return 0;
    }
    ranges[0].virtual_base = CODE_VIRTUAL;
    ranges[0].size = OS_AARCH64_MMU_PAGE_SIZE;
    ranges[1].virtual_base = STACK_VIRTUAL;
    ranges[1].size = OS_AARCH64_MMU_PAGE_SIZE;
    ranges[2].virtual_base = table_virtual;
    ranges[2].size = sizeof(g_table_pool);
    g_arch_state.current_el = 1u;
    g_arch_state.ttbr0 = 0x10000000u;
    g_arch_state.tcr = 0x1234u;
    g_arch_state.mair = 0x5678u;
    g_arch_state.sctlr = 1u;
    g_arch_state.sp = STACK_VIRTUAL + 0x800u;
    g_arch_state.pc = CODE_VIRTUAL + 0x100u;
    g_arch_state.mmfr0 = 5u;
    g_arch_state.vbar = CODE_VIRTUAL + 0x800u;
    g_active = 0;
    g_force_stack_failure = 0;
    g_install_calls = 0u;
    return 1;
}

static int test_memory_map(void)
{
    efi_memory_descriptor descriptors[2];
    memset(descriptors, 0, sizeof(descriptors));
    descriptors[0].type = 2u;
    descriptors[0].physical_start = 0x1000u;
    descriptors[0].number_of_pages = 3u;
    descriptors[1].type = 11u;
    descriptors[1].physical_start = 0x10000u;
    descriptors[1].number_of_pages = 2u;
    CHECK(os_aarch64_mmu_validate_efi_memory_map(
        descriptors, sizeof(descriptors), sizeof(descriptors[0]),
        OS_AARCH64_MMU_EFI_DESCRIPTOR_VERSION),
        "validate disjoint revision-1 UEFI descriptors");
    efi_memory_descriptor found;
    CHECK(os_aarch64_mmu_find_efi_descriptor(
        descriptors, sizeof(descriptors), sizeof(descriptors[0]),
        0x2000u, 0x1000u, &found)
        && found.type == 2u,
        "find descriptor covering a complete physical range");
    CHECK(!os_aarch64_mmu_find_efi_descriptor(
        descriptors, sizeof(descriptors), sizeof(descriptors[0]),
        0x3000u, 0x2000u, &found),
        "reject a physical range crossing descriptor bounds");
    CHECK(!os_aarch64_mmu_validate_efi_memory_map(
        descriptors, sizeof(descriptors), sizeof(descriptors[0]), 0u),
        "reject unsupported descriptor revisions");
    descriptors[1].physical_start = 0x2000u;
    CHECK(!os_aarch64_mmu_validate_efi_memory_map(
        descriptors, sizeof(descriptors), sizeof(descriptors[0]),
        OS_AARCH64_MMU_EFI_DESCRIPTOR_VERSION),
        "reject overlapping physical descriptors");
    descriptors[1].physical_start = 0x10000u;
    descriptors[0].number_of_pages = ~(u64)0;
    CHECK(!os_aarch64_mmu_validate_efi_memory_map(
        descriptors, sizeof(descriptors), sizeof(descriptors[0]),
        OS_AARCH64_MMU_EFI_DESCRIPTOR_VERSION),
        "reject descriptor extent overflow");
    return 0;
}

static int test_verified_install(void)
{
    os_aarch64_mmu_live_range ranges[3];
    CHECK(prepare_installer(ranges), "prepare non-identity live ranges");
    CHECK(os_aarch64_mmu_install_verified(&g_mmu, ranges, 3u),
        "install only after exact active translations match");
    CHECK(g_active && g_install_calls == 1u
        && g_arch_state.current_el == 1u
        && g_arch_state.ttbr0 == TABLE_PHYSICAL
        && g_arch_state.mair == 0x00000000bb44ff00ull
        && (g_arch_state.tcr & 0x3fu) == 16u,
        "program the validated TTBR, MAIR and 48-bit TCR configuration");
    CHECK(arm_mmu_translate_current(CODE_VIRTUAL + 0x20u)
            == CODE_PHYSICAL + 0x20u
        && arm_mmu_translate_current(STACK_VIRTUAL + 0x20u)
            == STACK_PHYSICAL + 0x20u,
        "preserve non-identity code and stack translations");

    CHECK(prepare_installer(ranges), "prepare rollback fixture");
    g_force_stack_failure = 1;
    CHECK(!os_aarch64_mmu_install_verified(&g_mmu, ranges, 3u)
        && !g_active && g_install_calls == 2u
        && g_arch_state.ttbr0 == 0x10000000u
        && g_arch_state.tcr == 0x1234u
        && g_arch_state.mair == 0x5678u,
        "restore firmware translation registers after failed post-install validation");

    CHECK(prepare_installer(ranges), "prepare missing-range fixture");
    CHECK(!os_aarch64_mmu_install_verified(&g_mmu, ranges, 2u)
        && g_install_calls == 0u && !g_active,
        "leave active firmware state untouched when table-pool coverage is absent");
    return 0;
}

static int test_mmio_mapper(void)
{
    os_aarch64_mmu_live_range ranges[3];
    CHECK(prepare_installer(ranges), "prepare MMIO mapper table");
    CHECK(os_aarch64_mmu_install_verified(&g_mmu, ranges, 3u),
        "install MMIO mapper page tables safely");
    os_aarch64_mmio_space space;
    os_pi5_mmio_mapper mapper;
    CHECK(os_aarch64_mmio_space_init(
        &space, &g_mmu, 0x0000f00000000000ull, 0x10000u),
        "initialize bounded non-identity MMIO window");
    CHECK(os_aarch64_mmio_space_get_mapper(&space, &mapper),
        "expose mapper callbacks");
    volatile u32 *mapped = (volatile u32 *)0;
    CHECK(!mapper.map_region(
        mapper.context, 0x80000123u, 0x1000u, &mapped),
        "reject mapping before page tables are active");
    CHECK(os_aarch64_mmio_space_authorize(
        &space, 0x80000000u, 0x2000u, 7u),
        "authorize a bounded physical device range");
    CHECK(os_aarch64_mmio_space_activate(&space),
        "activate MMIO mapping service");
    CHECK(!mapper.map_region(
        mapper.context, 0x81000000u, 0x1000u, &mapped),
        "reject a physical range not authorized by platform discovery");
    CHECK(!mapper.map_region(
        mapper.context, 0x80000000u, 0x3000u, &mapped),
        "reject a mapping whose page-rounded extent exceeds ownership bounds");
    CHECK(!os_aarch64_mmio_space_authorize(
        &space, 0x90000000u, 0x1000u, 8u),
        "reject authorization changes after activation");
    CHECK(mapper.map_region(
        mapper.context, 0x80000123u, 0x1000u, &mapped),
        "map an unaligned physical MMIO range");
    u64 translated = 0;
    CHECK(os_aarch64_mmu_translate(
        &g_mmu, (u64)(usize)mapped, &translated, (u64 *)0)
        && translated == 0x80000123u
        && space.mappings[0].owner == 7u
        && g_invalidation_calls == 1u,
        "preserve requested offset and invalidate the new mapping");
    mapper.unmap_region(mapper.context, mapped, 0x0fffu);
    CHECK(os_aarch64_mmu_translate(
        &g_mmu, (u64)(usize)mapped, &translated, (u64 *)0),
        "reject an unmap with mismatched ownership size");
    mapper.unmap_region(mapper.context, mapped, 0x1000u);
    CHECK(!os_aarch64_mmu_translate(
        &g_mmu, (u64)(usize)mapped, &translated, (u64 *)0)
        && g_invalidation_calls == 2u,
        "release only the exact owned mapping and invalidate the teardown");
    CHECK(mapper.map_region(
        mapper.context, 0x80000123u, 0x1000u, &mapped),
        "reuse released virtual aperture space");
    mapper.unmap_region(mapper.context, mapped, 0x1000u);
    CHECK(!mapper.map_region(
        mapper.context, 0x0001000000000000ull, 0x1000u, &mapped),
        "reject unsupported physical-address width");
    return 0;
}

int main(void)
{
    CHECK(test_memory_map() == 0, "UEFI memory-map fixture");
    CHECK(test_verified_install() == 0, "verified installation fixture");
    CHECK(test_mmio_mapper() == 0, "active MMIO mapper fixture");
    puts("PASS G12-AARCH64-MMU-RUNTIME: guarded installation and MMIO mapping");
    return 0;
}
