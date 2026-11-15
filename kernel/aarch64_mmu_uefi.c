#include "aarch64_mmu_uefi.h"

#define PE_SIGNATURE 0x00004550u
#define PE_MACHINE_ARM64 0xaa64u
#define PE_OPTIONAL_MAGIC_PE32_PLUS 0x020bu
#define PE_SECTION_MEM_EXECUTE 0x20000000u
#define PE_SECTION_MEM_READ 0x40000000u
#define PE_SECTION_MEM_WRITE 0x80000000u
#define PE_MAX_SECTIONS 96u
#define PE_MAX_IMAGE_SIZE (64u * 1024u * 1024u)
#define PE_MAX_IMAGE_PAGES \
    (PE_MAX_IMAGE_SIZE / OS_AARCH64_MMU_PAGE_SIZE)
#define EFI_STACK_DESCRIPTOR_MAX_SIZE (16u * 1024u * 1024u)
#define EFI_MEMORY_UC 0x0000000000000001ull
#define EFI_MEMORY_WC 0x0000000000000002ull
#define EFI_MEMORY_WT 0x0000000000000004ull
#define EFI_MEMORY_WB 0x0000000000000008ull
#define UART_VIRTUAL_ADDRESS 0x09000000ull
#define MMIO_VIRTUAL_BASE 0x0000f00000000000ull
#define MMIO_VIRTUAL_SIZE 0x10000000ull

static os_aarch64_mmu g_runtime_mmu;
static os_aarch64_mmio_space g_mmio_space;
static os_aarch64_mmu_live_range g_live_ranges[5];
static u8 g_image_page_attributes[PE_MAX_IMAGE_PAGES];
static u8 g_image_page_claimed[PE_MAX_IMAGE_PAGES];
static u64 g_image_virtual_base;
static u64 g_image_size;
static u64 g_stack_virtual_base;
static u64 g_stack_size;
static u64 g_pool_debug_virtual;
static u64 g_pool_debug_physical;
static int g_prepared;
static int g_installed;

static u16 read_le16(const u8 *bytes)
{
    return (u16)((u16)bytes[0] | ((u16)bytes[1] << 8));
}

static u32 read_le32(const u8 *bytes)
{
    return (u32)bytes[0] | ((u32)bytes[1] << 8)
        | ((u32)bytes[2] << 16) | ((u32)bytes[3] << 24);
}

static int bounded_range(usize offset, usize size, usize limit)
{
    return offset <= limit && size <= limit - offset;
}

static void clear_bytes(u8 *bytes, usize size)
{
    for (usize i = 0; i < size; ++i) {
        bytes[i] = 0u;
    }
}

static int memory_descriptor_for_physical_page(
    const void *memory_map,
    usize memory_map_size,
    usize descriptor_size,
    u64 physical_page,
    efi_memory_descriptor *found
)
{
    usize count = memory_map_size / descriptor_size;
    const u8 *bytes = (const u8 *)memory_map;
    for (usize i = 0; i < count; ++i) {
        const efi_memory_descriptor *descriptor =
            (const efi_memory_descriptor *)(bytes + i * descriptor_size);
        u64 descriptor_size_bytes =
            descriptor->number_of_pages * OS_AARCH64_MMU_PAGE_SIZE;
        if (physical_page >= descriptor->physical_start
            && physical_page - descriptor->physical_start
                < descriptor_size_bytes) {
            *found = *descriptor;
            return 1;
        }
    }
    return 0;
}

static int normal_memory_attributes(
    const efi_memory_descriptor *descriptor,
    u32 *attributes
)
{
    u64 cache_attributes = descriptor->attribute
        & (EFI_MEMORY_UC | EFI_MEMORY_WC | EFI_MEMORY_WT | EFI_MEMORY_WB);
    if (attributes == (u32 *)0 || cache_attributes == 0u
        || (cache_attributes & (cache_attributes - 1u)) != 0u) {
        return 0;
    }
    if ((cache_attributes & EFI_MEMORY_WB) != 0u) {
        *attributes = OS_AARCH64_MMU_NORMAL_WB;
    } else if ((cache_attributes & EFI_MEMORY_WT) != 0u) {
        *attributes = OS_AARCH64_MMU_NORMAL_WT;
    } else {
        *attributes = OS_AARCH64_MMU_NORMAL_NC;
    }
    return 1;
}

static int configured_physical_bits(u64 mmfr0, u8 *bits)
{
    static const u8 widths[6] = {32u, 36u, 40u, 42u, 44u, 48u};
    u32 encoding = (u32)(mmfr0 & 0xfu);
    if (bits == (u8 *)0 || encoding > 6u) {
        return 0;
    }
    *bits = encoding > 5u ? 48u : widths[encoding];
    return 1;
}

static int map_current_page(
    const void *memory_map,
    usize memory_map_size,
    usize descriptor_size,
    u64 virtual_page,
    u32 attributes
)
{
    u64 physical_page = arm_mmu_translate_current(virtual_page);
    efi_memory_descriptor descriptor;
    u32 memory_attributes = 0;
    if (physical_page == ~(u64)0
        || (physical_page & (OS_AARCH64_MMU_PAGE_SIZE - 1u)) != 0u
        || !memory_descriptor_for_physical_page(
            memory_map, memory_map_size, descriptor_size,
            physical_page, &descriptor)
        || descriptor.type < 1u || descriptor.type > 4u
        || !normal_memory_attributes(&descriptor, &memory_attributes)) {
        return 0;
    }
    return os_aarch64_mmu_map(
        &g_runtime_mmu, virtual_page, physical_page,
        OS_AARCH64_MMU_PAGE_SIZE, attributes | memory_attributes);
}

static int map_dma_pool(
    const void *memory_map,
    usize memory_map_size,
    usize descriptor_size,
    u64 virtual_base,
    u64 size
)
{
    const u64 maximum_size =
        (u64)OS_MAX_POOL_PAGES * OS_AARCH64_MMU_PAGE_SIZE;
    if (virtual_base == 0u || size == 0u || size > maximum_size
        || (virtual_base & (OS_AARCH64_MMU_PAGE_SIZE - 1u)) != 0u
        || (size & (OS_AARCH64_MMU_PAGE_SIZE - 1u)) != 0u
        || virtual_base >= (1ull << 48)
        || size > (1ull << 48) - virtual_base) {
        return 0;
    }

    u64 physical_base = arm_mmu_translate_current(virtual_base);
    if (physical_base == ~(u64)0
        || (physical_base & (OS_AARCH64_MMU_PAGE_SIZE - 1u)) != 0u
        || size > ~(u64)0 - physical_base) {
        return 0;
    }
    u32 pool_memory_attributes = 0u;
    for (u64 offset = 0u; offset < size;
            offset += OS_AARCH64_MMU_PAGE_SIZE) {
        u64 virtual_page = virtual_base + offset;
        u64 physical_page = arm_mmu_translate_current(virtual_page);
        efi_memory_descriptor descriptor;
        u32 page_memory_attributes = 0u;
        if (physical_page != physical_base + offset
            || !memory_descriptor_for_physical_page(
                memory_map, memory_map_size, descriptor_size,
                physical_page, &descriptor)
            || descriptor.type != 2u
            || !normal_memory_attributes(
                &descriptor, &page_memory_attributes)
            || (offset != 0u
                && page_memory_attributes != pool_memory_attributes)
            || !map_current_page(
                memory_map, memory_map_size, descriptor_size,
                virtual_page, 0u)) {
            return 0;
        }
        pool_memory_attributes = page_memory_attributes;
    }
    g_live_ranges[4].virtual_base = virtual_base;
    g_live_ranges[4].size = size;
    return 1;
}

static int map_image_pages(
    const void *memory_map,
    usize memory_map_size,
    usize descriptor_size,
    u64 image_base,
    u64 image_size,
    usize *mapped_size
)
{
    const u8 *image = (const u8 *)(usize)image_base;
    if ((image_base & (OS_AARCH64_MMU_PAGE_SIZE - 1u)) != 0u
        || image_base >= (1ull << 48)
        || image_size < 64u
        || image_size > PE_MAX_IMAGE_SIZE
        || image_size > (1ull << 48) - image_base
        || read_le16(image) != 0x5a4du) {
        return 0;
    }

    u32 pe_offset = read_le32(image + 0x3cu);
    if (!bounded_range(pe_offset, 24u, (usize)image_size)
        || read_le32(image + pe_offset) != PE_SIGNATURE) {
        return 0;
    }
    const u8 *file_header = image + pe_offset + 4u;
    u16 machine = read_le16(file_header);
    u16 section_count = read_le16(file_header + 2u);
    u16 optional_header_size = read_le16(file_header + 16u);
    usize optional_offset = (usize)pe_offset + 24u;
    if (machine != PE_MACHINE_ARM64 || section_count == 0u
        || section_count > PE_MAX_SECTIONS
        || optional_header_size < 64u
        || !bounded_range(
            optional_offset, optional_header_size, (usize)image_size)) {
        return 0;
    }
    const u8 *optional_header = image + optional_offset;
    if (read_le16(optional_header) != PE_OPTIONAL_MAGIC_PE32_PLUS) {
        return 0;
    }
    u32 size_of_image = read_le32(optional_header + 56u);
    u32 size_of_headers = read_le32(optional_header + 60u);
    if (size_of_image == 0u || size_of_image > image_size
        || size_of_image > PE_MAX_IMAGE_SIZE
        || size_of_headers == 0u || size_of_headers > size_of_image) {
        return 0;
    }
    usize page_count = ((usize)size_of_image
        + OS_AARCH64_MMU_PAGE_SIZE - 1u)
        / OS_AARCH64_MMU_PAGE_SIZE;
    usize rounded_size = page_count * OS_AARCH64_MMU_PAGE_SIZE;
    if (page_count > PE_MAX_IMAGE_PAGES || rounded_size > image_size) {
        return 0;
    }

    clear_bytes(g_image_page_attributes, page_count);
    clear_bytes(g_image_page_claimed, page_count);
    usize header_pages = ((usize)size_of_headers
        + OS_AARCH64_MMU_PAGE_SIZE - 1u)
        / OS_AARCH64_MMU_PAGE_SIZE;
    for (usize page = 0; page < header_pages; ++page) {
        g_image_page_attributes[page] =
            (u8)(OS_AARCH64_MMU_NORMAL_WB
                | OS_AARCH64_MMU_READ_ONLY | 1u);
        g_image_page_claimed[page] = 1u;
    }

    usize sections_offset = optional_offset + optional_header_size;
    usize section_table_size = (usize)section_count * 40u;
    if (!bounded_range(
            sections_offset, section_table_size, (usize)image_size)) {
        return 0;
    }
    for (usize i = 0; i < section_count; ++i) {
        const u8 *section = image + sections_offset + i * 40u;
        u32 virtual_size = read_le32(section + 8u);
        u32 virtual_address = read_le32(section + 12u);
        u32 raw_size = read_le32(section + 16u);
        u32 characteristics = read_le32(section + 36u);
        u32 section_size = virtual_size > raw_size
            ? virtual_size : raw_size;
        if (section_size == 0u) {
            continue;
        }
        if ((characteristics & PE_SECTION_MEM_READ) == 0u
            || ((characteristics & PE_SECTION_MEM_EXECUTE) != 0u
                && (characteristics & PE_SECTION_MEM_WRITE) != 0u)
            || virtual_address > size_of_image
            || section_size > size_of_image - virtual_address) {
            return 0;
        }
        usize first_page = virtual_address / OS_AARCH64_MMU_PAGE_SIZE;
        usize page_limit = ((usize)virtual_address + section_size
            + OS_AARCH64_MMU_PAGE_SIZE - 1u)
            / OS_AARCH64_MMU_PAGE_SIZE;
        if (first_page < header_pages || page_limit > page_count) {
            return 0;
        }
        u32 attributes = OS_AARCH64_MMU_NORMAL_WB;
        if ((characteristics & PE_SECTION_MEM_WRITE) == 0u) {
            attributes |= OS_AARCH64_MMU_READ_ONLY;
        }
        if ((characteristics & PE_SECTION_MEM_EXECUTE) != 0u) {
            attributes |= OS_AARCH64_MMU_EXECUTABLE;
        }
        for (usize page = first_page; page < page_limit; ++page) {
            if (g_image_page_claimed[page] != 0u) {
                return 0;
            }
            g_image_page_attributes[page] = (u8)(attributes + 1u);
            g_image_page_claimed[page] = 1u;
        }
    }

    for (usize page = 0; page < page_count; ++page) {
        u32 attributes = g_image_page_attributes[page] == 0u
            ? OS_AARCH64_MMU_NORMAL_WB | OS_AARCH64_MMU_READ_ONLY
            : (u32)g_image_page_attributes[page] - 1u;
        u64 virtual_page = image_base
            + page * (u64)OS_AARCH64_MMU_PAGE_SIZE;
        if (!map_current_page(
                memory_map, memory_map_size, descriptor_size,
                virtual_page, attributes)) {
            return 0;
        }
    }
    *mapped_size = rounded_size;
    return 1;
}

static int map_stack_descriptor(
    const void *memory_map,
    usize memory_map_size,
    usize descriptor_size,
    const os_aarch64_mmu_arch_state *state
)
{
    u64 probe = state->sp == 0u ? 0u : state->sp - 1u;
    u64 physical_probe = arm_mmu_translate_current(probe);
    efi_memory_descriptor descriptor;
    if (physical_probe == ~(u64)0
        || !os_aarch64_mmu_find_efi_descriptor(
            memory_map, memory_map_size, descriptor_size,
            physical_probe & ~(u64)(OS_AARCH64_MMU_PAGE_SIZE - 1u),
            OS_AARCH64_MMU_PAGE_SIZE, &descriptor)
        || (descriptor.type != 2u && descriptor.type != 4u)) {
        return 0;
    }
    u64 descriptor_size_bytes =
        descriptor.number_of_pages * OS_AARCH64_MMU_PAGE_SIZE;
    u32 stack_attributes = 0;
    if (descriptor_size_bytes > EFI_STACK_DESCRIPTOR_MAX_SIZE
        || physical_probe < descriptor.physical_start
        || !normal_memory_attributes(&descriptor, &stack_attributes)) {
        return 0;
    }
    u64 virtual_page = state->sp
        & ~(u64)(OS_AARCH64_MMU_PAGE_SIZE - 1u);
    u64 physical_page = physical_probe
        & ~(u64)(OS_AARCH64_MMU_PAGE_SIZE - 1u);
    u64 physical_offset = physical_page - descriptor.physical_start;
    if (virtual_page < physical_offset) {
        return 0;
    }
    u64 virtual_base = virtual_page - physical_offset;
    if ((virtual_base & (OS_AARCH64_MMU_PAGE_SIZE - 1u)) != 0u
        || virtual_base >= (1ull << 48)
        || descriptor_size_bytes > (1ull << 48) - virtual_base) {
        return 0;
    }

    for (u64 offset = 0; offset < descriptor_size_bytes;
            offset += OS_AARCH64_MMU_PAGE_SIZE) {
        u64 current_virtual = virtual_base + offset;
        u64 current_physical = arm_mmu_translate_current(current_virtual);
        if (current_physical != descriptor.physical_start + offset
            || !os_aarch64_mmu_map(
                &g_runtime_mmu, current_virtual,
                descriptor.physical_start + offset,
                OS_AARCH64_MMU_PAGE_SIZE, stack_attributes)) {
            return 0;
        }
    }
    g_stack_virtual_base = virtual_base;
    g_stack_size = descriptor_size_bytes;
    return 1;
}

static int uart_region_matches(
    const os_device_tree *tree,
    u64 physical_page
)
{
    if (tree == (const os_device_tree *)0) {
        return 0;
    }
    for (usize i = 0; i < tree->node_count; ++i) {
        const os_device_tree_node *node = &tree->nodes[i];
        if (!node->enabled
            || !os_device_tree_node_has_compatible(node, "arm,pl011")) {
            continue;
        }
        for (usize j = 0; j < node->region_count; ++j) {
            const os_device_tree_region *region = &node->regions[j];
            if (region->translated && region->size != 0u
                && physical_page >= region->address
                && physical_page - region->address < region->size) {
                return 1;
            }
        }
    }
#ifdef EXPERIMENTALOS_QEMU_VIRT
    return tree->node_count == 0u
        && physical_page
            == (UART_VIRTUAL_ADDRESS
                & ~(u64)(OS_AARCH64_MMU_PAGE_SIZE - 1u));
#else
    return 0;
#endif
}

static int authorize_tree_mmio_regions(const os_device_tree *tree)
{
    if (tree == (const os_device_tree *)0
        || tree->node_count > OS_DEVICE_TREE_MAX_NODES) {
        return 0;
    }

    usize authorized_count = 0u;
    for (usize i = 0; i < tree->node_count; ++i) {
        const os_device_tree_node *node = &tree->nodes[i];
        if (!node->enabled) {
            continue;
        }
        if (node->region_count > OS_DEVICE_TREE_MAX_REGIONS
            || node->assigned_region_count > OS_DEVICE_TREE_MAX_REGIONS) {
            return 0;
        }
        for (usize j = 0; j < node->region_count; ++j) {
            const os_device_tree_region *region = &node->regions[j];
            if (region->translated && region->size != 0u) {
                if (!os_aarch64_mmio_space_authorize(
                        &g_mmio_space, region->address, region->size,
                        (u32)i)) {
                    return 0;
                }
                ++authorized_count;
            }
        }
        for (usize j = 0; j < node->assigned_region_count; ++j) {
            const os_device_tree_region *region =
                &node->assigned_regions[j];
            if (region->translated && region->size != 0u) {
                if (!os_aarch64_mmio_space_authorize(
                        &g_mmio_space, region->address, region->size,
                        (u32)i)) {
                    return 0;
                }
                ++authorized_count;
            }
        }
    }
#ifdef EXPERIMENTALOS_QEMU_VIRT
    if (tree->node_count == 0u) {
        return os_aarch64_mmio_space_authorize(
            &g_mmio_space, UART_VIRTUAL_ADDRESS,
            OS_AARCH64_MMU_PAGE_SIZE, 0xffffffffu);
    }
#endif
    return authorized_count != 0u;
}

u32 os_aarch64_mmu_uefi_prepare(
    const efi_loaded_image_protocol *loaded_image,
    void *table_pool_virtual_pointer,
    u64 table_pool_size,
    void *dma_pool_virtual_pointer,
    u64 dma_pool_size,
    const os_device_tree *tree,
    const void *memory_map,
    usize memory_map_size,
    usize descriptor_size,
    u32 descriptor_version
)
{
    if (g_prepared || loaded_image == (const efi_loaded_image_protocol *)0
        || table_pool_virtual_pointer == (void *)0
        || (u64)(usize)table_pool_virtual_pointer >= (1ull << 48)
        || table_pool_size < OS_AARCH64_MMU_PAGE_SIZE
        || table_pool_size > 128u * (u64)OS_AARCH64_MMU_PAGE_SIZE
        || table_pool_size
            > (1ull << 48) - (u64)(usize)table_pool_virtual_pointer
        || (table_pool_size & (OS_AARCH64_MMU_PAGE_SIZE - 1u)) != 0u
        || ((usize)table_pool_virtual_pointer
            & (OS_AARCH64_MMU_PAGE_SIZE - 1u)) != 0u
        || dma_pool_virtual_pointer == (void *)0
        || dma_pool_size == 0u
        || tree == (const os_device_tree *)0
        || loaded_image->image_base == (void *)0
        || loaded_image->image_size == 0u
        || !os_aarch64_mmu_validate_efi_memory_map(
            memory_map, memory_map_size, descriptor_size,
            descriptor_version)) {
        return OS_AARCH64_MMU_UEFI_PREPARE_INVALID_INPUT;
    }

    os_aarch64_mmu_arch_state state;
    u8 physical_bits = 0;
    if (!arm_mmu_read_state(&state)
        || (state.current_el != 1u && state.current_el != 2u)
        || (state.sctlr & 1u) == 0u
        || !configured_physical_bits(state.mmfr0, &physical_bits)) {
        return OS_AARCH64_MMU_UEFI_PREPARE_ARCH_STATE;
    }

    u64 table_pool_virtual = (u64)(usize)table_pool_virtual_pointer;
    u64 table_pool_physical =
        arm_mmu_translate_current(table_pool_virtual);
    g_pool_debug_virtual = table_pool_virtual;
    g_pool_debug_physical = table_pool_physical;
    if (table_pool_physical == ~(u64)0) {
        return OS_AARCH64_MMU_UEFI_PREPARE_TABLE_POOL;
    }
    if (table_pool_physical != table_pool_virtual) {
        return OS_AARCH64_MMU_UEFI_PREPARE_TABLE_POOL_NONIDENTITY;
    }
    if ((table_pool_physical
        & (OS_AARCH64_MMU_PAGE_SIZE - 1u)) != 0u) {
        return OS_AARCH64_MMU_UEFI_PREPARE_TABLE_POOL_LAYOUT;
    }
    for (u64 offset = 0; offset < table_pool_size;
            offset += OS_AARCH64_MMU_PAGE_SIZE) {
        if (arm_mmu_translate_current(table_pool_virtual + offset)
            != table_pool_physical + offset) {
            return OS_AARCH64_MMU_UEFI_PREPARE_TABLE_POOL_NONCONTIGUOUS;
        }
    }
    if (!os_aarch64_mmu_init(
            &g_runtime_mmu, table_pool_virtual_pointer, table_pool_physical,
            (usize)table_pool_size, physical_bits)) {
        return OS_AARCH64_MMU_UEFI_PREPARE_TABLE_POOL_LAYOUT;
    }

    usize image_mapped_size = 0;
    g_image_virtual_base = (u64)(usize)loaded_image->image_base;
    if (!map_image_pages(
            memory_map, memory_map_size, descriptor_size,
            g_image_virtual_base, loaded_image->image_size,
            &image_mapped_size)) {
        return OS_AARCH64_MMU_UEFI_PREPARE_IMAGE;
    }
    if (!map_stack_descriptor(
            memory_map, memory_map_size, descriptor_size, &state)) {
        return OS_AARCH64_MMU_UEFI_PREPARE_STACK;
    }

    for (u64 offset = 0; offset < table_pool_size;
            offset += OS_AARCH64_MMU_PAGE_SIZE) {
        if (!map_current_page(
                memory_map, memory_map_size, descriptor_size,
                table_pool_virtual + offset, OS_AARCH64_MMU_NORMAL_WB)) {
            return OS_AARCH64_MMU_UEFI_PREPARE_TABLE_POOL_RESOURCES;
        }
    }
    if (!map_dma_pool(
            memory_map, memory_map_size, descriptor_size,
            (u64)(usize)dma_pool_virtual_pointer, dma_pool_size)) {
        return OS_AARCH64_MMU_UEFI_PREPARE_DMA_POOL;
    }

    u64 uart_virtual_page = UART_VIRTUAL_ADDRESS
        & ~(u64)(OS_AARCH64_MMU_PAGE_SIZE - 1u);
    u64 uart_physical = arm_mmu_translate_current(UART_VIRTUAL_ADDRESS);
    if (uart_physical == ~(u64)0
        || !uart_region_matches(
            tree, uart_physical
                & ~(u64)(OS_AARCH64_MMU_PAGE_SIZE - 1u))
        || !os_aarch64_mmu_map(
            &g_runtime_mmu, uart_virtual_page,
            uart_physical & ~(u64)(OS_AARCH64_MMU_PAGE_SIZE - 1u),
            OS_AARCH64_MMU_PAGE_SIZE, OS_AARCH64_MMU_DEVICE)) {
        return OS_AARCH64_MMU_UEFI_PREPARE_UART;
    }

    g_image_size = image_mapped_size;
    g_live_ranges[0].virtual_base = g_image_virtual_base;
    g_live_ranges[0].size = g_image_size;
    g_live_ranges[1].virtual_base = g_stack_virtual_base;
    g_live_ranges[1].size = g_stack_size;
    g_live_ranges[2].virtual_base = uart_virtual_page;
    g_live_ranges[2].size = OS_AARCH64_MMU_PAGE_SIZE;
    g_live_ranges[3].virtual_base = table_pool_virtual;
    g_live_ranges[3].size = table_pool_size;
    if (!os_aarch64_mmio_space_init(
            &g_mmio_space, &g_runtime_mmu,
            MMIO_VIRTUAL_BASE, MMIO_VIRTUAL_SIZE)
        || !authorize_tree_mmio_regions(tree)) {
        return OS_AARCH64_MMU_UEFI_PREPARE_MMIO_WINDOW;
    }
    g_prepared = 1;
    return OS_AARCH64_MMU_UEFI_PREPARE_OK;
}

int os_aarch64_mmu_uefi_install(void)
{
    if (!g_prepared || g_installed
        || !os_aarch64_mmu_install_verified(
            &g_runtime_mmu, g_live_ranges, 5u)
        || !os_aarch64_mmio_space_activate(&g_mmio_space)) {
        return 0;
    }
    g_installed = 1;
    return 1;
}

int os_aarch64_mmu_uefi_get_mapper(os_pi5_mmio_mapper *mapper)
{
    return g_installed
        && os_aarch64_mmio_space_get_mapper(&g_mmio_space, mapper);
}

int os_aarch64_mmu_uefi_mmio_self_test(void)
{
    os_pi5_mmio_mapper mapper;
    volatile u32 *mapped = (volatile u32 *)0;
    u64 physical = arm_mmu_translate_current(UART_VIRTUAL_ADDRESS);
    if (!g_installed || physical == ~(u64)0
        || !os_aarch64_mmu_uefi_get_mapper(&mapper)
        || !mapper.map_region(
            mapper.context, physical, sizeof(u32), &mapped)) {
        return 0;
    }

    int mapping_valid = arm_mmu_translate_current((u64)(usize)mapped)
        == physical;
    mapper.unmap_region(mapper.context, mapped, sizeof(u32));
    return mapping_valid
        && arm_mmu_translate_current((u64)(usize)mapped) == ~(u64)0;
}

void os_aarch64_mmu_uefi_get_pool_translation(
    u64 *virtual_address,
    u64 *physical_address
)
{
    if (virtual_address != (u64 *)0) {
        *virtual_address = g_pool_debug_virtual;
    }
    if (physical_address != (u64 *)0) {
        *physical_address = g_pool_debug_physical;
    }
}

int os_aarch64_mmu_uefi_get_loaded_image(
    void *image_handle,
    efi_boot_services *boot_services,
    const efi_loaded_image_protocol **loaded_image
)
{
    static const efi_guid loaded_image_guid = {
        0x5b1b31a1u, 0x9562u, 0x11d2u,
        {0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b},
    };
    if (image_handle == (void *)0
        || boot_services == (efi_boot_services *)0
        || boot_services->handle_protocol == (efi_handle_protocol)0
        || loaded_image == (const efi_loaded_image_protocol **)0) {
        return 0;
    }
    void *interface = (void *)0;
    if (boot_services->handle_protocol(
            image_handle, &loaded_image_guid, &interface) != 0
        || interface == (void *)0) {
        return 0;
    }
    *loaded_image = (const efi_loaded_image_protocol *)interface;
    return (*loaded_image)->image_base != (void *)0
        && (*loaded_image)->image_size != 0u;
}
