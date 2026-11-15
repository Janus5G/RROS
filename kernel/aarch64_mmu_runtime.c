#include "aarch64_mmu_runtime.h"

#define MMU_ADDRESS_MASK 0x0000fffffffff000ull
#define MMU_MAIR_VALUE 0x00000000bb44ff00ull
#define MMU_VERIFY_MAX_PAGES 65536u

_Static_assert(
    sizeof(efi_memory_descriptor) == OS_AARCH64_MMU_EFI_DESCRIPTOR_MIN_SIZE,
    "UEFI memory descriptor layout must match revision 1");
_Static_assert(
    sizeof(os_aarch64_mmu_arch_state) == 72u,
    "AArch64 MMU state layout must match the assembly snapshot");

static int range_end(u64 base, u64 size, u64 *end)
{
    if (size == 0 || size > ~(u64)0 - base) {
        return 0;
    }
    *end = base + size;
    return 1;
}

int os_aarch64_mmu_validate_efi_memory_map(
    const void *memory_map,
    usize memory_map_size,
    usize descriptor_size,
    u32 descriptor_version
)
{
    if (memory_map == (const void *)0
        || ((usize)memory_map & 7u) != 0u
        || descriptor_version != OS_AARCH64_MMU_EFI_DESCRIPTOR_VERSION
        || descriptor_size < sizeof(efi_memory_descriptor)
        || (descriptor_size & 7u) != 0u
        || memory_map_size == 0u
        || memory_map_size % descriptor_size != 0u) {
        return 0;
    }

    usize descriptor_count = memory_map_size / descriptor_size;
    if (descriptor_count > 4096u) {
        return 0;
    }
    const u8 *bytes = (const u8 *)memory_map;
    for (usize i = 0; i < descriptor_count; ++i) {
        const efi_memory_descriptor *current =
            (const efi_memory_descriptor *)(bytes + i * descriptor_size);
        u64 current_size = 0;
        u64 current_end = 0;
        if (current->type > 14u
            || (current->physical_start
                & (OS_AARCH64_MMU_PAGE_SIZE - 1u)) != 0u
            || (current->virtual_start != 0u
                && (current->virtual_start
                    & (OS_AARCH64_MMU_PAGE_SIZE - 1u)) != 0u)
            || current->number_of_pages == 0u
            || current->number_of_pages
                > (~(u64)0) / OS_AARCH64_MMU_PAGE_SIZE) {
            return 0;
        }
        current_size =
            current->number_of_pages * OS_AARCH64_MMU_PAGE_SIZE;
        if (!range_end(current->physical_start, current_size, &current_end)) {
            return 0;
        }

        for (usize j = 0; j < i; ++j) {
            const efi_memory_descriptor *previous =
                (const efi_memory_descriptor *)(bytes + j * descriptor_size);
            u64 previous_size =
                previous->number_of_pages * OS_AARCH64_MMU_PAGE_SIZE;
            u64 previous_end = previous->physical_start + previous_size;
            if (current->physical_start < previous_end
                && previous->physical_start < current_end) {
                return 0;
            }
        }
    }
    return 1;
}

int os_aarch64_mmu_find_efi_descriptor(
    const void *memory_map,
    usize memory_map_size,
    usize descriptor_size,
    u64 physical_address,
    u64 size,
    efi_memory_descriptor *descriptor
)
{
    u64 requested_end = 0;
    if (descriptor == (efi_memory_descriptor *)0
        || !range_end(physical_address, size, &requested_end)
        || !os_aarch64_mmu_validate_efi_memory_map(
            memory_map, memory_map_size, descriptor_size,
            OS_AARCH64_MMU_EFI_DESCRIPTOR_VERSION)) {
        return 0;
    }

    usize descriptor_count = memory_map_size / descriptor_size;
    const u8 *bytes = (const u8 *)memory_map;
    for (usize i = 0; i < descriptor_count; ++i) {
        const efi_memory_descriptor *candidate =
            (const efi_memory_descriptor *)(bytes + i * descriptor_size);
        u64 candidate_size =
            candidate->number_of_pages * OS_AARCH64_MMU_PAGE_SIZE;
        u64 candidate_end = candidate->physical_start + candidate_size;
        if (physical_address >= candidate->physical_start
            && requested_end <= candidate_end) {
            *descriptor = *candidate;
            return 1;
        }
    }
    return 0;
}

static int range_contains(
    const os_aarch64_mmu_live_range *range,
    u64 address,
    u64 size
)
{
    u64 range_limit = 0;
    u64 requested_limit = 0;
    return range_end(range->virtual_base, range->size, &range_limit)
        && range_end(address, size, &requested_limit)
        && address >= range->virtual_base
        && requested_limit <= range_limit;
}

static int live_ranges_contain(
    const os_aarch64_mmu_live_range *ranges,
    usize range_count,
    u64 address,
    u64 size
)
{
    for (usize i = 0; i < range_count; ++i) {
        if (range_contains(&ranges[i], address, size)) {
            return 1;
        }
    }
    return 0;
}

static int validate_live_range(
    const os_aarch64_mmu *mmu,
    const os_aarch64_mmu_live_range *range,
    usize *page_count
)
{
    u64 end = 0;
    if (range == (const os_aarch64_mmu_live_range *)0
        || (range->virtual_base
            & (OS_AARCH64_MMU_PAGE_SIZE - 1u)) != 0u
        || (range->size & (OS_AARCH64_MMU_PAGE_SIZE - 1u)) != 0u
        || !range_end(range->virtual_base, range->size, &end)
        || end > (1ull << 48)
        || range->size / OS_AARCH64_MMU_PAGE_SIZE
            > MMU_VERIFY_MAX_PAGES - *page_count) {
        return 0;
    }

    for (u64 address = range->virtual_base; address < end;
            address += OS_AARCH64_MMU_PAGE_SIZE) {
        u64 active_physical = arm_mmu_translate_current(address);
        u64 table_physical = 0;
        if (active_physical == ~(u64)0
            || !os_aarch64_mmu_translate(
                mmu, address, &table_physical, (u64 *)0)
            || active_physical != table_physical) {
            return 0;
        }
    }
    *page_count += range->size / OS_AARCH64_MMU_PAGE_SIZE;
    return 1;
}

static int physical_address_bits(u64 mmfr0, u8 *bits, u32 *encoding)
{
    static const u8 widths[6] = {32u, 36u, 40u, 42u, 44u, 48u};
    u32 supported = (u32)(mmfr0 & 0xfu);
    if (supported > 6u || bits == (u8 *)0
        || encoding == (u32 *)0) {
        return 0;
    }
    if (supported > 5u) {
        *bits = 48u;
        *encoding = 5u;
    } else {
        *bits = widths[supported];
        *encoding = supported;
    }
    return 1;
}

static u64 make_tcr(u32 current_el, u32 physical_encoding)
{
    u64 tcr = 16u
        | (1ull << 8)
        | (1ull << 10)
        | (3ull << 12);
    if (current_el == 1u) {
        tcr |= (16ull << 16) | (1ull << 23);
        tcr |= (u64)physical_encoding << 32;
    } else {
        tcr |= (u64)physical_encoding << 16;
    }
    return tcr;
}

int os_aarch64_mmu_install_verified(
    os_aarch64_mmu *mmu,
    const os_aarch64_mmu_live_range *live_ranges,
    usize live_range_count
)
{
    if (mmu == (os_aarch64_mmu *)0
        || mmu->table_pool == (u64 *)0
        || mmu->installed != 0u
        || live_ranges == (const os_aarch64_mmu_live_range *)0
        || live_range_count == 0u
        || live_range_count > OS_AARCH64_MMU_LIVE_RANGE_MAX) {
        return 0;
    }

    os_aarch64_mmu_arch_state old_state;
    if (!arm_mmu_read_state(&old_state)
        || (old_state.current_el != 1u && old_state.current_el != 2u)
        || (old_state.sctlr & 1u) == 0u) {
        return 0;
    }

    u64 pc_physical = arm_mmu_translate_current(old_state.pc);
    u64 stack_probe = old_state.sp == 0u ? 0u : old_state.sp - 1u;
    u64 stack_physical = old_state.sp == 0u
        ? ~(u64)0 : arm_mmu_translate_current(stack_probe);
    u64 vbar_physical = arm_mmu_translate_current(old_state.vbar);
    if (pc_physical == ~(u64)0 || stack_physical == ~(u64)0
        || vbar_physical == ~(u64)0
        || !live_ranges_contain(
            live_ranges, live_range_count, old_state.pc, 1u)
        || !live_ranges_contain(
            live_ranges, live_range_count, stack_probe, 1u)
        || !live_ranges_contain(
            live_ranges, live_range_count, old_state.vbar, 0x800u)) {
        return 0;
    }

    u64 table_pool_size = mmu->table_pool_pages
        * (u64)OS_AARCH64_MMU_PAGE_SIZE;
    int table_pool_covered = 0;
    usize verified_pages = 0;
    for (usize i = 0; i < live_range_count; ++i) {
        if (range_contains(
                &live_ranges[i], (u64)(usize)mmu->table_pool,
                table_pool_size)) {
            table_pool_covered = 1;
        }
        if (!validate_live_range(
                mmu, &live_ranges[i], &verified_pages)) {
            return 0;
        }
    }
    if (!table_pool_covered
        || !live_ranges_contain(
            live_ranges, live_range_count, old_state.pc, 1u)
        || !live_ranges_contain(
            live_ranges, live_range_count, stack_probe, 1u)) {
        return 0;
    }

    u64 table_pool_physical = 0;
    if (arm_mmu_translate_current((u64)(usize)mmu->table_pool)
            != mmu->table_pool_physical_base
        || !os_aarch64_mmu_translate(
            mmu, (u64)(usize)mmu->table_pool,
            &table_pool_physical, (u64 *)0)
        || table_pool_physical != mmu->table_pool_physical_base) {
        return 0;
    }

    u8 supported_bits = 0;
    u32 physical_encoding = 0;
    if (!physical_address_bits(
            old_state.mmfr0, &supported_bits, &physical_encoding)
        || supported_bits != mmu->physical_address_bits) {
        return 0;
    }

    u64 new_tcr = make_tcr(
        (u32)old_state.current_el, physical_encoding);
    if (!arm_mmu_install(
            os_aarch64_mmu_root_physical(mmu),
            new_tcr, MMU_MAIR_VALUE, old_state.current_el)) {
        return 0;
    }

    os_aarch64_mmu_arch_state new_state;
    if (!arm_mmu_read_state(&new_state)
        || new_state.current_el != old_state.current_el
        || (new_state.ttbr0 & MMU_ADDRESS_MASK)
            != os_aarch64_mmu_root_physical(mmu)
        || new_state.tcr != new_tcr
        || new_state.mair != MMU_MAIR_VALUE
        || new_state.vbar != old_state.vbar
        || arm_mmu_translate_current(old_state.pc) != pc_physical
        || arm_mmu_translate_current(stack_probe) != stack_physical
        || arm_mmu_translate_current(old_state.vbar) != vbar_physical) {
        (void)arm_mmu_install(
            old_state.ttbr0, old_state.tcr, old_state.mair,
            old_state.current_el);
        return 0;
    }
    verified_pages = 0;
    for (usize i = 0; i < live_range_count; ++i) {
        if (!validate_live_range(
                mmu, &live_ranges[i], &verified_pages)) {
            (void)arm_mmu_install(
                old_state.ttbr0, old_state.tcr, old_state.mair,
                old_state.current_el);
            return 0;
        }
    }
    mmu->installed = 1u;
    return 1;
}

static void mmio_lock(os_aarch64_mmio_space *space)
{
    while (__atomic_exchange_n(&space->lock, 1u, __ATOMIC_ACQUIRE) != 0u) {
        __asm__ volatile("" : : : "memory");
    }
}

static void mmio_unlock(os_aarch64_mmio_space *space)
{
    __atomic_store_n(&space->lock, 0u, __ATOMIC_RELEASE);
}

static int find_mmio_gap(
    const os_aarch64_mmio_space *space,
    u64 size,
    u64 *virtual_base
)
{
    u64 candidate = space->virtual_base;
    u64 window_end = space->virtual_base + space->virtual_size;
    for (;;) {
        u64 next_start = window_end;
        u64 next_end = window_end;
        for (usize i = 0; i < OS_AARCH64_MMIO_MAX_MAPPINGS; ++i) {
            const os_aarch64_mmio_mapping *mapping = &space->mappings[i];
            u64 mapping_end = mapping->virtual_base + mapping->virtual_size;
            if (mapping->active && mapping->virtual_base >= candidate
                && mapping->virtual_base < next_start) {
                next_start = mapping->virtual_base;
                next_end = mapping_end;
            }
        }
        if (size <= next_start - candidate) {
            *virtual_base = candidate;
            return 1;
        }
        if (next_start == window_end) {
            return 0;
        }
        candidate = next_end;
        if (candidate > window_end) {
            return 0;
        }
    }
}

static int find_authorized_mmio_owner(
    const os_aarch64_mmio_space *space,
    u64 physical_base,
    u64 mapped_size,
    u32 *owner
)
{
    u64 mapped_end = 0;
    if (owner == (u32 *)0
        || !range_end(physical_base, mapped_size, &mapped_end)) {
        return 0;
    }

    int found = 0;
    u64 smallest_authorized_size = ~(u64)0;
    for (usize i = 0; i < space->authorized_count; ++i) {
        const os_aarch64_mmio_authorized_range *range =
            &space->authorized[i];
        u64 authorized_end = 0;
        if (!range_end(range->physical_base, range->size, &authorized_end)) {
            return 0;
        }
        u64 authorized_page_base = range->physical_base
            & ~(u64)(OS_AARCH64_MMU_PAGE_SIZE - 1u);
        u64 authorized_page_end = (authorized_end
            + OS_AARCH64_MMU_PAGE_SIZE - 1u)
            & ~(u64)(OS_AARCH64_MMU_PAGE_SIZE - 1u);
        if (physical_base >= authorized_page_base
            && mapped_end <= authorized_page_end
            && range->size < smallest_authorized_size) {
            found = 1;
            smallest_authorized_size = range->size;
            *owner = range->owner;
        }
    }
    return found;
}

static int mmio_map_region(
    void *context,
    u64 physical_address,
    u32 size,
    volatile u32 **mapped_base
)
{
    os_aarch64_mmio_space *space = (os_aarch64_mmio_space *)context;
    if (space == (os_aarch64_mmio_space *)0
        || mapped_base == (volatile u32 **)0
        || size == 0u
        || physical_address > ~(u64)0 - size) {
        return 0;
    }

    u64 physical_offset =
        physical_address & (OS_AARCH64_MMU_PAGE_SIZE - 1u);
    u64 physical_base = physical_address - physical_offset;
    u64 span = physical_offset + size;
    if (span > ~(u64)0 - (OS_AARCH64_MMU_PAGE_SIZE - 1u)) {
        return 0;
    }
    u64 mapped_size = (span + OS_AARCH64_MMU_PAGE_SIZE - 1u)
        & ~(u64)(OS_AARCH64_MMU_PAGE_SIZE - 1u);

    mmio_lock(space);
    u32 owner = 0u;
    if (space->active == 0u || space->mmu == (os_aarch64_mmu *)0) {
        mmio_unlock(space);
        return 0;
    }
    if (!find_authorized_mmio_owner(
            space, physical_base, mapped_size, &owner)) {
        mmio_unlock(space);
        return 0;
    }
    usize slot = OS_AARCH64_MMIO_MAX_MAPPINGS;
    for (usize i = 0; i < OS_AARCH64_MMIO_MAX_MAPPINGS; ++i) {
        if (!space->mappings[i].active) {
            slot = i;
            break;
        }
    }
    u64 virtual_base = 0;
    if (slot == OS_AARCH64_MMIO_MAX_MAPPINGS
        || mapped_size > space->virtual_size
        || !find_mmio_gap(space, mapped_size, &virtual_base)
        || !os_aarch64_mmu_map(
            space->mmu, virtual_base, physical_base, mapped_size,
            OS_AARCH64_MMU_DEVICE)) {
        mmio_unlock(space);
        return 0;
    }

    space->mappings[slot].virtual_base = virtual_base;
    space->mappings[slot].virtual_size = mapped_size;
    space->mappings[slot].physical_base = physical_base;
    space->mappings[slot].mapped_size = mapped_size;
    space->mappings[slot].returned_address =
        virtual_base + physical_offset;
    space->mappings[slot].requested_size = size;
    space->mappings[slot].owner = owner;
    space->mappings[slot].active = 1u;
    arm_mmu_invalidate_current();
    *mapped_base = (volatile u32 *)(usize)
        space->mappings[slot].returned_address;
    mmio_unlock(space);
    return 1;
}

static void mmio_unmap_region(
    void *context,
    volatile u32 *mapped_base,
    u32 size
)
{
    os_aarch64_mmio_space *space = (os_aarch64_mmio_space *)context;
    if (space == (os_aarch64_mmio_space *)0
        || mapped_base == (volatile u32 *)0 || size == 0u) {
        return;
    }

    mmio_lock(space);
    u64 address = (u64)(usize)mapped_base;
    for (usize i = 0; i < OS_AARCH64_MMIO_MAX_MAPPINGS; ++i) {
        os_aarch64_mmio_mapping *mapping = &space->mappings[i];
        if (mapping->active
            && mapping->requested_size == size
            && address == mapping->returned_address) {
            if (os_aarch64_mmu_unmap(
                    space->mmu, mapping->virtual_base,
                    mapping->mapped_size)) {
                arm_mmu_invalidate_current();
                mapping->active = 0u;
            }
            break;
        }
    }
    mmio_unlock(space);
}

int os_aarch64_mmio_space_init(
    os_aarch64_mmio_space *space,
    os_aarch64_mmu *mmu,
    u64 virtual_base,
    u64 virtual_size
)
{
    u64 end = 0;
    if (space == (os_aarch64_mmio_space *)0
        || mmu == (os_aarch64_mmu *)0
        || mmu->table_pool == (u64 *)0
        || (virtual_base & (OS_AARCH64_MMU_PAGE_SIZE - 1u)) != 0u
        || (virtual_size & (OS_AARCH64_MMU_PAGE_SIZE - 1u)) != 0u
        || !range_end(virtual_base, virtual_size, &end)
        || end > (1ull << 48)) {
        return 0;
    }
    space->mmu = mmu;
    space->virtual_base = virtual_base;
    space->virtual_size = virtual_size;
    space->active = 0u;
    space->lock = 0u;
    space->authorized_count = 0u;
    for (usize i = 0; i < OS_AARCH64_MMIO_MAX_MAPPINGS; ++i) {
        space->mappings[i].active = 0u;
    }
    return 1;
}

int os_aarch64_mmio_space_authorize(
    os_aarch64_mmio_space *space,
    u64 physical_base,
    u64 size,
    u32 owner
)
{
    u64 end = 0;
    if (space == (os_aarch64_mmio_space *)0
        || space->mmu == (os_aarch64_mmu *)0
        || space->active != 0u
        || !range_end(physical_base, size, &end)
        || end > (1ull << 48)) {
        return 0;
    }
    for (usize i = 0; i < space->authorized_count; ++i) {
        const os_aarch64_mmio_authorized_range *range =
            &space->authorized[i];
        if (range->physical_base == physical_base
            && range->size == size && range->owner == owner) {
            return 1;
        }
    }
    if (space->authorized_count == OS_AARCH64_MMIO_MAX_AUTHORIZED) {
        return 0;
    }
    os_aarch64_mmio_authorized_range *range =
        &space->authorized[space->authorized_count++];
    range->physical_base = physical_base;
    range->size = size;
    range->owner = owner;
    return 1;
}

int os_aarch64_mmio_space_activate(os_aarch64_mmio_space *space)
{
    if (space == (os_aarch64_mmio_space *)0
        || space->mmu == (os_aarch64_mmu *)0
        || space->mmu->table_pool == (u64 *)0
        || space->mmu->installed == 0u
        || space->authorized_count == 0u
        || space->active != 0u) {
        return 0;
    }
    space->active = 1u;
    return 1;
}

int os_aarch64_mmio_space_get_mapper(
    os_aarch64_mmio_space *space,
    os_pi5_mmio_mapper *mapper
)
{
    if (space == (os_aarch64_mmio_space *)0
        || mapper == (os_pi5_mmio_mapper *)0
        || space->mmu == (os_aarch64_mmu *)0) {
        return 0;
    }
    mapper->context = space;
    mapper->map_region = mmio_map_region;
    mapper->unmap_region = mmio_unmap_region;
    return 1;
}
