#include "device_tree.h"

#define FDT_MAGIC 0xd00dfeedu
#define FDT_HEADER_SIZE 40u
#define FDT_TOKEN_BEGIN_NODE 1u
#define FDT_TOKEN_END_NODE 2u
#define FDT_TOKEN_PROPERTY 3u
#define FDT_TOKEN_NOP 4u
#define FDT_TOKEN_END 9u
#define OS_FDT_MAX_UEFI_TABLES 128u

typedef struct {
    u16 node_index;
    u32 child_address_cells;
    u32 child_size_cells;
    u32 reg_address_cells;
    u32 reg_size_cells;
    u8 ranges_present;
    u8 ranges_identity;
    u8 range_count;
    const u8 *ranges_data;
} fdt_node_frame;

static u32 read_be32(const u8 *data)
{
    return ((u32)data[0] << 24) | ((u32)data[1] << 16)
        | ((u32)data[2] << 8) | data[3];
}

int os_device_tree_get_gic_interrupt(
    const os_device_tree *tree,
    const os_device_tree_node *node,
    usize interrupt_index,
    os_device_tree_gic_interrupt *interrupt
)
{
    if (interrupt != (os_device_tree_gic_interrupt *)0) {
        interrupt->interrupt_id = 0;
        interrupt->flags = 0;
        interrupt->is_peripheral = 0;
    }
    if (tree == (const os_device_tree *)0
        || node == (const os_device_tree_node *)0
        || interrupt == (os_device_tree_gic_interrupt *)0
        || node->interrupt_parent_index >= tree->node_count
        || node->interrupt_cell_count > OS_DEVICE_TREE_MAX_INTERRUPT_CELLS
        || node->interrupt_cell_count % 3u != 0
        || interrupt_index >= node->interrupt_cell_count / 3u) {
        return 0;
    }
    const os_device_tree_node *controller =
        &tree->nodes[node->interrupt_parent_index];
    if (!os_device_tree_node_has_compatible(controller, "arm,gic-400")) {
        return 0;
    }
    if (controller->interrupt_specifier_cells != 3u) {
        return 0;
    }
    usize cell = interrupt_index * 3u;
    u32 type = node->interrupt_cells[cell];
    u32 number = node->interrupt_cells[cell + 1u];
    u32 flags = node->interrupt_cells[cell + 2u];
    if ((type == 0u && number > 987u)
        || (type == 1u && number > 15u)
        || type > 1u || (flags != 1u && flags != 2u
            && flags != 4u && flags != 8u)) {
        return 0;
    }
    interrupt->is_peripheral = (u8)(type == 0u);
    interrupt->interrupt_id = (type == 0u ? 32u : 16u) + number;
    interrupt->flags = flags;
    return 1;
}

int os_device_tree_get_rp1_interrupt(
    const os_device_tree *tree,
    const os_device_tree_node *node,
    usize interrupt_index,
    os_device_tree_interrupt *interrupt
)
{
    if (interrupt != (os_device_tree_interrupt *)0) {
        interrupt->interrupt_id = 0u;
        interrupt->flags = 0u;
    }
    if (tree == (const os_device_tree *)0
        || node == (const os_device_tree_node *)0
        || interrupt == (os_device_tree_interrupt *)0
        || node->interrupt_parent_index >= tree->node_count
        || node->interrupt_cell_count == 0u
        || node->interrupt_cell_count > OS_DEVICE_TREE_MAX_INTERRUPT_CELLS
        || node->interrupt_cell_count % 2u != 0u
        || interrupt_index >= node->interrupt_cell_count / 2u) {
        return 0;
    }
    const os_device_tree_node *controller =
        &tree->nodes[node->interrupt_parent_index];
    if (!controller->interrupt_controller
        || controller->interrupt_specifier_cells != 2u) {
        return 0;
    }
    usize cell = interrupt_index * 2u;
    u32 interrupt_id = node->interrupt_cells[cell];
    u32 flags = node->interrupt_cells[cell + 1u];
    if (interrupt_id >= OS_DEVICE_TREE_RP1_INTERRUPT_COUNT
        || (flags != 1u && flags != 2u && flags != 3u
            && flags != 4u && flags != 8u)) {
        return 0;
    }
    interrupt->interrupt_id = interrupt_id;
    interrupt->flags = flags;
    return 1;
}

int os_device_tree_get_clock_frequency(
    const os_device_tree *tree,
    const os_device_tree_node *node,
    usize clock_index,
    u32 *frequency_hz
)
{
    if (frequency_hz != (u32 *)0) {
        *frequency_hz = 0;
    }
    if (tree == (const os_device_tree *)0
        || node == (const os_device_tree_node *)0
        || frequency_hz == (u32 *)0
        || clock_index >= node->clock_reference_count) {
        return 0;
    }
    const os_device_tree_reference *reference =
        &node->clock_references[clock_index];
    if (reference->provider_index >= tree->node_count
        || reference->argument_count != 0u) {
        return 0;
    }
    const os_device_tree_node *provider =
        &tree->nodes[reference->provider_index];
    if (!os_device_tree_node_has_compatible(provider, "fixed-clock")
        || provider->fixed_clock_frequency_hz == 0) {
        return 0;
    }
    *frequency_hz = provider->fixed_clock_frequency_hz;
    return 1;
}

static u64 read_be_cells(const u8 *data, u32 count)
{
    u64 value = 0;
    for (u32 cell = 0; cell < count; ++cell) {
        value = (value << 32) | read_be32(data + cell * 4u);
    }
    return value;
}

static u64 read_address_cells(const u8 *data, u32 count, u32 *flags)
{
    if (count == 3u) {
        *flags = read_be32(data);
        return read_be_cells(data + 4u, 2u);
    }
    *flags = 0;
    return read_be_cells(data, count);
}

static int range_valid(usize offset, usize size, usize limit)
{
    return offset <= limit && size <= limit - offset;
}

static int bytes_equal(const u8 *bytes, const char *text, usize length)
{
    for (usize i = 0; i < length; ++i) {
        if (bytes[i] != (u8)text[i]) {
            return 0;
        }
    }
    return 1;
}
static usize align_four(usize offset)
{
    return (offset + 3u) & ~(usize)3u;
}

static void clear_tree(os_device_tree *tree)
{
    volatile u8 *bytes = (volatile u8 *)tree;
    for (usize i = 0; i < sizeof(*tree); ++i) {
        bytes[i] = 0;
    }
}

static int copy_string(
    char *destination,
    usize capacity,
    const u8 *source,
    usize length
)
{
    if (length == 0 || length >= capacity || source[length - 1u] != 0) {
        return 0;
    }
    for (usize i = 0; i + 1u < length; ++i) {
        if (source[i] == 0) {
            return 0;
        }
        destination[i] = (char)source[i];
    }
    destination[length - 1u] = '\0';
    return 1;
}

static int valid_string_list(const u8 *data, usize size, usize *first_length)
{
    usize start = 0;
    usize strings = 0;
    while (start < size) {
        usize end = start;
        while (end < size && data[end] != 0) {
            ++end;
        }
        if (end == start || end == size) {
            return 0;
        }
        if (strings == 0) {
            *first_length = end - start + 1u;
        }
        ++strings;
        start = end + 1u;
    }
    return strings != 0;
}

int os_device_tree_node_has_compatible(
    const os_device_tree_node *node,
    const char *compatible
)
{
    if (node == (const os_device_tree_node *)0
        || compatible == (const char *)0 || compatible[0] == '\0') {
        return 0;
    }
    usize compatible_length = 0;
    while (compatible[compatible_length] != '\0') {
        ++compatible_length;
    }
    if (node->compatible_list_size == 0) {
        usize length = 0;
        while (node->compatible[length] != '\0') {
            ++length;
        }
        return length == compatible_length
            && bytes_equal((const u8 *)node->compatible,
                compatible, length);
    }
    if (node->compatible_list_size > sizeof(node->compatible_list)) {
        return 0;
    }
    usize start = 0;
    while (start < node->compatible_list_size) {
        usize end = start;
        while (end < node->compatible_list_size
            && node->compatible_list[end] != '\0') {
            ++end;
        }
        if (end == node->compatible_list_size) {
            return 0;
        }
        if (end - start == compatible_length
            && bytes_equal((const u8 *)node->compatible_list + start,
                compatible, compatible_length)) {
            return 1;
        }
        start = end + 1u;
    }
    return 0;
}

int os_device_tree_translate_dma_address(
    const os_device_tree_node *node,
    u64 dma_address,
    u32 address_flags,
    u64 size,
    u64 *system_address
)
{
    if (system_address != (u64 *)0) {
        *system_address = 0;
    }
    if (node == (const os_device_tree_node *)0
        || system_address == (u64 *)0 || size == 0
        || dma_address > (~(u64)0) - size
        || !node->dma_ranges_present
        || node->dma_range_count > OS_DEVICE_TREE_MAX_DMA_RANGES) {
        return 0;
    }
    if (node->dma_ranges_identity) {
        *system_address = dma_address;
        return 1;
    }
    for (usize i = 0; i < node->dma_range_count; ++i) {
        const os_device_tree_dma_range *range = &node->dma_ranges[i];
        if ((address_flags & 0xff000000u)
                != (range->child_flags & 0xff000000u)
            || dma_address < range->child_address) {
            continue;
        }
        u64 offset = dma_address - range->child_address;
        if (offset > range->size || size > range->size - offset
            || range->parent_address > (~(u64)0) - offset) {
            continue;
        }
        *system_address = range->parent_address + offset;
        return 1;
    }
    return 0;
}

int os_device_tree_translate_system_dma_address(
    const os_device_tree_node *node,
    u64 system_address,
    u32 address_flags,
    u64 size,
    u64 *dma_address
)
{
    if (dma_address != (u64 *)0) {
        *dma_address = 0;
    }
    if (node == (const os_device_tree_node *)0
        || dma_address == (u64 *)0 || size == 0
        || system_address > (~(u64)0) - size
        || !node->dma_ranges_present
        || node->dma_range_count > OS_DEVICE_TREE_MAX_DMA_RANGES) {
        return 0;
    }
    if (node->dma_ranges_identity) {
        *dma_address = system_address;
        return 1;
    }
    int found = 0;
    u64 translated = 0;
    for (usize i = 0; i < node->dma_range_count; ++i) {
        const os_device_tree_dma_range *range = &node->dma_ranges[i];
        if ((address_flags & 0xff000000u)
                != (range->child_flags & 0xff000000u)
            || system_address < range->parent_address) {
            continue;
        }
        u64 offset = system_address - range->parent_address;
        if (offset > range->size || size > range->size - offset
            || range->child_address > (~(u64)0) - offset) {
            continue;
        }
        u64 candidate = range->child_address + offset;
        if (found && candidate != translated) {
            return 0;
        }
        translated = candidate;
        found = 1;
    }
    if (!found) {
        return 0;
    }
    *dma_address = translated;
    return 1;
}

static int find_string(
    const u8 *strings,
    usize size,
    u32 offset,
    const u8 **name,
    usize *name_length
)
{
    if (offset >= size) {
        return 0;
    }
    usize length = 0;
    while ((usize)offset + length < size
        && strings[offset + length] != 0) {
        ++length;
    }
    if ((usize)offset + length >= size) {
        return 0;
    }
    *name = strings + offset;
    *name_length = length;
    return 1;
}

static int validate_reservations(
    const u8 *blob,
    usize total_size,
    usize offset,
    usize structure_start,
    usize strings_start
)
{
    if ((offset & 7u) != 0 || offset < FDT_HEADER_SIZE) {
        return 0;
    }
    while (range_valid(offset, 16u, total_size)
        && offset + 16u <= structure_start
        && offset + 16u <= strings_start) {
        u64 address = read_be_cells(blob + offset, 2);
        u64 size = read_be_cells(blob + offset + 8u, 2);
        offset += 16u;
        if (address == 0 && size == 0) {
            return 1;
        }
        if (size == 0 || address > (~(u64)0) - size) {
            return 0;
        }
    }
    return 0;
}

static int translate_address(
    const fdt_node_frame *stack,
    usize parent_depth,
    u64 *address,
    u32 *address_flags,
    u64 size,
    u8 *translated
)
{
    *translated = 1;
    while (parent_depth > 1u) {
        const fdt_node_frame *bus = &stack[parent_depth - 1u];
        if (!bus->ranges_present) {
            *translated = 0;
            return 1;
        }
        if (bus->ranges_identity) {
            --parent_depth;
            continue;
        }
        int matched = 0;
        u32 tuple_cells = bus->child_address_cells
            + bus->reg_address_cells + bus->child_size_cells;
        usize tuple_size = (usize)tuple_cells * 4u;
        for (usize i = 0; i < bus->range_count; ++i) {
            const u8 *tuple = bus->ranges_data + i * tuple_size;
            u32 child_address_flags = 0;
            u32 parent_address_flags = 0;
            u64 child_address = read_address_cells(
                tuple, bus->child_address_cells, &child_address_flags);
            u64 parent_address = read_address_cells(
                tuple + bus->child_address_cells * 4u,
                bus->reg_address_cells, &parent_address_flags);
            u64 range_size = read_be_cells(
                tuple + (bus->child_address_cells
                    + bus->reg_address_cells) * 4u,
                bus->child_size_cells);
            if ((*address_flags & 0xff000000u)
                    != (child_address_flags & 0xff000000u)
                || *address < child_address) {
                continue;
            }
            u64 offset = *address - child_address;
            if (offset > range_size || size > range_size - offset
                || parent_address > (~(u64)0) - offset) {
                continue;
            }
            *address = parent_address + offset;
            *address_flags = parent_address_flags;
            matched = 1;
            break;
        }
        if (!matched) {
            *translated = 0;
            return 1;
        }
        --parent_depth;
    }
    return 1;
}

static int set_regions(
    os_device_tree_region *regions,
    u8 *count_out,
    const u8 *data,
    usize size,
    u32 address_cells,
    u32 size_cells,
    const fdt_node_frame *stack,
    usize parent_depth,
    int assigned_addresses
)
{
    u32 tuple_cells = address_cells + size_cells;
    usize tuple_size = (usize)tuple_cells * 4u;
    if (address_cells == 0 || address_cells > 3
        || size_cells > 2 || tuple_size == 0
        || size == 0 || size % tuple_size != 0
        || size / tuple_size > OS_DEVICE_TREE_MAX_REGIONS) {
        return 0;
    }
    usize region_count = size / tuple_size;
    for (usize i = 0; i < region_count; ++i) {
        const u8 *tuple = data + i * tuple_size;
        os_device_tree_region *region = &regions[i];
        region->address = read_address_cells(
            tuple, address_cells, &region->address_flags);
        region->size = size_cells == 0
            ? 0 : read_be_cells(tuple + address_cells * 4u, size_cells);
        if (region->size != 0
            && region->address > (~(u64)0) - region->size) {
            return 0;
        }
        if (address_cells == 3u && !assigned_addresses) {
            region->translated = 0;
        } else if (parent_depth != 0
            && !translate_address(
                stack, parent_depth, &region->address,
                &region->address_flags,
                region->size, &region->translated)) {
            return 0;
        }
        if (parent_depth == 0
            && !(address_cells == 3u && !assigned_addresses)) {
            region->translated = 1;
        }
    }
    *count_out = (u8)region_count;
    return 1;
}

static int set_ranges(
    os_device_tree_node *node,
    fdt_node_frame *frame,
    const u8 *data,
    usize size
)
{
    node->ranges_present = 1u;
    frame->ranges_present = 1;
    if (size == 0) {
        node->ranges_identity = 1u;
        frame->ranges_identity = 1;
        return 1;
    }
    u32 tuple_cells = frame->child_address_cells
        + frame->reg_address_cells + frame->child_size_cells;
    usize tuple_size = (usize)tuple_cells * 4u;
    if (frame->child_address_cells == 0
        || frame->child_address_cells > 3
        || frame->reg_address_cells == 0
        || frame->reg_address_cells > 3
        || frame->child_size_cells == 0
        || frame->child_size_cells > 2
        || tuple_size == 0 || size % tuple_size != 0
        || size / tuple_size > OS_DEVICE_TREE_MAX_RANGES) {
        return 0;
    }
    usize range_count = size / tuple_size;
    for (usize i = 0; i < range_count; ++i) {
        const u8 *tuple = data + i * tuple_size;
        u32 child_address_flags = 0;
        u32 parent_address_flags = 0;
        u64 child_address = read_address_cells(
            tuple, frame->child_address_cells,
            &child_address_flags);
        u64 parent_address = read_address_cells(
            tuple + frame->child_address_cells * 4u,
            frame->reg_address_cells, &parent_address_flags);
        u64 range_size = read_be_cells(
            tuple + (frame->child_address_cells
                + frame->reg_address_cells) * 4u,
            frame->child_size_cells);
        if (range_size == 0
            || child_address > (~(u64)0) - range_size
            || parent_address > (~(u64)0) - range_size) {
            return 0;
        }
        os_device_tree_range *range = &node->ranges[i];
        range->child_address = child_address;
        range->parent_address = parent_address;
        range->size = range_size;
        range->child_flags = child_address_flags;
        range->parent_flags = parent_address_flags;
    }
    node->range_count = (u8)range_count;
    frame->range_count = (u8)range_count;
    frame->ranges_data = data;
    return 1;
}

static int set_dma_ranges(
    os_device_tree_node *node,
    const fdt_node_frame *frame,
    const u8 *data,
    usize size
)
{
    node->dma_ranges_present = 1;
    if (size == 0) {
        node->dma_ranges_identity = 1;
        return 1;
    }
    u32 tuple_cells = frame->child_address_cells
        + frame->reg_address_cells + frame->child_size_cells;
    usize tuple_size = (usize)tuple_cells * 4u;
    if (frame->child_address_cells == 0
        || frame->child_address_cells > 3
        || frame->reg_address_cells == 0
        || frame->reg_address_cells > 3
        || frame->child_size_cells == 0
        || frame->child_size_cells > 2
        || tuple_size == 0 || size % tuple_size != 0
        || size / tuple_size > OS_DEVICE_TREE_MAX_DMA_RANGES) {
        return 0;
    }
    usize range_count = size / tuple_size;
    for (usize i = 0; i < range_count; ++i) {
        const u8 *tuple = data + i * tuple_size;
        os_device_tree_dma_range *range = &node->dma_ranges[i];
        range->child_address = read_address_cells(
            tuple, frame->child_address_cells, &range->child_flags);
        range->parent_address = read_address_cells(
            tuple + frame->child_address_cells * 4u,
            frame->reg_address_cells, &range->parent_flags);
        range->size = read_be_cells(
            tuple + (frame->child_address_cells
                + frame->reg_address_cells) * 4u,
            frame->child_size_cells);
        if (range->size == 0
            || range->child_address > (~(u64)0) - range->size
            || range->parent_address > (~(u64)0) - range->size) {
            return 0;
        }
    }
    node->dma_range_count = (u8)range_count;
    return 1;
}

static int parse_property(
    os_device_tree_node *node,
    fdt_node_frame *frame,
    const fdt_node_frame *stack,
    usize depth,
    const u8 *name,
    usize name_length,
    const u8 *data,
    usize data_length
)
{
    if (name_length == 10 && bytes_equal(name, "compatible", 10)) {
        usize first_length = 0;
        if (!valid_string_list(data, data_length, &first_length)
            || data_length > sizeof(node->compatible_list)) {
            return 0;
        }
        if (!copy_string(
                node->compatible,
                sizeof(node->compatible),
                data,
                first_length)) {
            return 0;
        }
        for (usize i = 0; i < data_length; ++i) {
            node->compatible_list[i] = (char)data[i];
        }
        node->compatible_list_size = (u16)data_length;
        return 1;
    }
    if (name_length == 6 && bytes_equal(name, "status", 6)) {
        if (data_length < 2 || data[data_length - 1u] != 0) {
            return 0;
        }
        for (usize i = 0; i + 1u < data_length; ++i) {
            if (data[i] == 0) {
                return 0;
            }
        }
        node->enabled = (data_length == 5
                && bytes_equal(data, "okay", 4))
            || (data_length == 3 && bytes_equal(data, "ok", 2));
        return 1;
    }
    if (name_length == 7 && bytes_equal(name, "dr_mode", 7)) {
        if (node->usb_dr_mode_present || data_length < 2u
            || data[data_length - 1u] != 0u) {
            return 0;
        }
        for (usize i = 0; i + 1u < data_length; ++i) {
            if (data[i] == 0u) {
                return 0;
            }
        }
        node->usb_dr_mode_present = 1u;
        if (data_length == 5u && bytes_equal(data, "host", 4u)) {
            node->usb_dr_mode = OS_DEVICE_TREE_USB_DR_MODE_HOST;
        } else if (data_length == 11u
            && bytes_equal(data, "peripheral", 10u)) {
            node->usb_dr_mode = OS_DEVICE_TREE_USB_DR_MODE_PERIPHERAL;
        } else if (data_length == 4u
            && bytes_equal(data, "otg", 3u)) {
            node->usb_dr_mode = OS_DEVICE_TREE_USB_DR_MODE_OTG;
        } else {
            node->usb_dr_mode = OS_DEVICE_TREE_USB_DR_MODE_UNSUPPORTED;
        }
        return 1;
    }
    if (name_length == 9 && bytes_equal(name, "skip-init", 9)) {
        if (data_length != 0u || node->skip_init) {
            return 0;
        }
        node->skip_init = 1u;
        return 1;
    }
    if (name_length == 15
        && bytes_equal(name, "remote-endpoint", 15)) {
        if (data_length != sizeof(u32)
            || node->remote_endpoint_present) {
            return 0;
        }
        node->remote_endpoint_phandle = read_be32(data);
        if (node->remote_endpoint_phandle == 0u
            || node->remote_endpoint_phandle == 0xffffffffu) {
            return 0;
        }
        node->remote_endpoint_present = 1u;
        return 1;
    }
    if (name_length == 10 && bytes_equal(name, "data-lanes", 10)) {
        if (data_length == 0u || data_length % sizeof(u32) != 0u
            || data_length / sizeof(u32) > OS_DEVICE_TREE_MAX_DATA_LANES
            || node->data_lanes_present) {
            return 0;
        }
        node->data_lane_count = (u8)(data_length / sizeof(u32));
        for (usize i = 0u; i < node->data_lane_count; ++i) {
            node->data_lanes[i] = read_be32(data + i * sizeof(u32));
        }
        node->data_lanes_present = 1u;
        return 1;
    }
    if (name_length == 11 && bytes_equal(name, "clock-lanes", 11)) {
        if (data_length != sizeof(u32) || node->clock_lane_present) {
            return 0;
        }
        node->clock_lane = read_be32(data);
        node->clock_lane_present = 1u;
        return 1;
    }
    if (name_length == 14 && bytes_equal(name, "#address-cells", 14)) {
        if (data_length != 4) {
            return 0;
        }
        u32 value = read_be32(data);
        if (value > 3) {
            return 0;
        }
        frame->child_address_cells = value;
        return 1;
    }
    if (name_length == 11 && bytes_equal(name, "#size-cells", 11)) {
        if (data_length != 4) {
            return 0;
        }
        u32 value = read_be32(data);
        if (value > 2) {
            return 0;
        }
        frame->child_size_cells = value;
        return 1;
    }
    if ((name_length == 7 && bytes_equal(name, "phandle", 7))
        || (name_length == 13 && bytes_equal(name, "linux,phandle", 13))) {
        if (data_length != 4u) {
            return 0;
        }
        u32 value = read_be32(data);
        if (value == 0 || value == 0xffffffffu
            || (node->phandle != 0 && node->phandle != value)) {
            return 0;
        }
        node->phandle = value;
        return 1;
    }
    if (name_length == 16
        && bytes_equal(name, "interrupt-parent", 16)) {
        if (data_length != 4u) {
            return 0;
        }
        node->interrupt_parent_phandle = read_be32(data);
        if (node->interrupt_parent_phandle == 0
            || node->interrupt_parent_phandle == 0xffffffffu) {
            return 0;
        }
        node->interrupt_parent_present = 1;
        return 1;
    }
    if (name_length == 10 && bytes_equal(name, "msi-parent", 10)) {
        if (data_length != 4u) {
            return 0;
        }
        node->msi_parent_phandle = read_be32(data);
        if (node->msi_parent_phandle == 0
            || node->msi_parent_phandle == 0xffffffffu) {
            return 0;
        }
        node->msi_parent_present = 1u;
        return 1;
    }
    if (name_length == 10 && bytes_equal(name, "msi-ranges", 10)) {
        if (node->msi_ranges_present || data_length != 5u * sizeof(u32)) {
            return 0;
        }
        for (usize i = 0; i < 5u; ++i) {
            node->msi_range_cells[i] = read_be32(data + i * sizeof(u32));
        }
        node->msi_ranges_present = 1u;
        return 1;
    }
    if (name_length == 14 && bytes_equal(name, "msi-controller", 14)) {
        if (data_length != 0u || node->msi_controller) {
            return 0;
        }
        node->msi_controller = 1u;
        return 1;
    }
    if (name_length == 20
        && bytes_equal(name, "interrupt-controller", 20)) {
        if (data_length != 0u || node->interrupt_controller) {
            return 0;
        }
        node->interrupt_controller = 1u;
        return 1;
    }
    if (name_length == 15
        && bytes_equal(name, "brcm,msi-offset", 15)) {
        if (data_length != 4u || node->msi_offset_present) {
            return 0;
        }
        node->msi_offset = read_be32(data);
        node->msi_offset_present = 1u;
        return 1;
    }
    if (name_length == 16
        && bytes_equal(name, "#interrupt-cells", 16)) {
        if (data_length != 4u) {
            return 0;
        }
        u32 value = read_be32(data);
        if (value == 0 || value > 4u) {
            return 0;
        }
        node->interrupt_specifier_cells = (u8)value;
        return 1;
    }
    if (name_length == 11 && bytes_equal(name, "#gpio-cells", 11)) {
        if (data_length != 4u || node->gpio_specifier_cells_present) {
            return 0;
        }
        u32 value = read_be32(data);
        if (value == 0u || value > OS_DEVICE_TREE_MAX_REFERENCE_ARGS) {
            return 0;
        }
        node->gpio_specifier_cells = (u8)value;
        node->gpio_specifier_cells_present = 1u;
        return 1;
    }
    if (name_length == 21
        && bytes_equal(name, "brcm,gpio-bank-widths", 21)) {
        if (data_length == 0u || data_length % 4u != 0u
            || data_length / 4u > OS_DEVICE_TREE_MAX_GPIO_BANKS
            || node->gpio_bank_width_count != 0u) {
            return 0;
        }
        node->gpio_bank_width_count = (u8)(data_length / 4u);
        for (usize i = 0; i < node->gpio_bank_width_count; ++i) {
            u32 width = read_be32(data + i * 4u);
            if (width > 32u) {
                return 0;
            }
            node->gpio_bank_widths[i] = width;
        }
        return 1;
    }
    if ((name_length == 11 && bytes_equal(name, "vmmc-supply", 11))
        || (name_length == 12 && bytes_equal(name, "vqmmc-supply", 12))) {
        if (data_length != 4u) {
            return 0;
        }
        u32 phandle = read_be32(data);
        if (phandle == 0u || phandle == 0xffffffffu) {
            return 0;
        }
        if (name[0] == 'v') {
            if (name[1] == 'm') {
                if (node->vmmc_supply_present) {
                    return 0;
                }
                node->vmmc_supply_phandle = phandle;
                node->vmmc_supply_present = 1u;
            } else {
                if (node->vqmmc_supply_present) {
                    return 0;
                }
                node->vqmmc_supply_phandle = phandle;
                node->vqmmc_supply_present = 1u;
            }
        }
        return 1;
    }
    if (name_length == 17
        && bytes_equal(name, "local-mac-address", 17)) {
        if (data_length != sizeof(node->local_mac_address)
            || node->local_mac_address_present) {
            return 0;
        }
        for (usize i = 0; i < sizeof(node->local_mac_address); ++i) {
            node->local_mac_address[i] = data[i];
        }
        node->local_mac_address_present = 1u;
        return 1;
    }
    if (name_length == 10 && bytes_equal(name, "phy-handle", 10)) {
        if (data_length != sizeof(u32) || node->phy_handle_present) {
            return 0;
        }
        node->phy_handle_phandle = read_be32(data);
        if (node->phy_handle_phandle == 0u
            || node->phy_handle_phandle == 0xffffffffu) {
            return 0;
        }
        node->phy_handle_present = 1u;
        return 1;
    }
    if (name_length == 15
        && bytes_equal(name, "phy-reset-gpios", 15)) {
        if (data_length == 0u || data_length % sizeof(u32) != 0u
            || data_length / sizeof(u32)
                > OS_DEVICE_TREE_MAX_REGULATOR_CELLS
            || node->phy_reset_gpio_cell_count != 0u) {
            return 0;
        }
        node->phy_reset_gpio_cell_count =
            (u8)(data_length / sizeof(u32));
        for (usize i = 0; i < node->phy_reset_gpio_cell_count; ++i) {
            node->phy_reset_gpio_cells[i] =
                read_be32(data + i * sizeof(u32));
        }
        return 1;
    }
    if (name_length == 18
        && bytes_equal(name, "phy-reset-duration", 18)) {
        if (data_length != sizeof(u32)
            || node->phy_reset_duration_present) {
            return 0;
        }
        node->phy_reset_duration_ms = read_be32(data);
        if (node->phy_reset_duration_ms == 0u) {
            return 0;
        }
        node->phy_reset_duration_present = 1u;
        return 1;
    }
    if (name_length == 9 && bytes_equal(name, "bus-width", 9)) {
        if (data_length != sizeof(u32) || node->bus_width_present) {
            return 0;
        }
        u32 value = read_be32(data);
        if (value != 1u && value != 4u && value != 8u) {
            return 0;
        }
        node->bus_width = (u8)value;
        node->bus_width_present = 1u;
        return 1;
    }
    if (name_length == 13 && bytes_equal(name, "non-removable", 13)) {
        if (data_length != 0u || node->non_removable) {
            return 0;
        }
        node->non_removable = 1u;
        return 1;
    }
    if (name_length == 4 && bytes_equal(name, "phys", 4)) {
        if (data_length == 0u || data_length % sizeof(u32) != 0u) {
            return 0;
        }
        node->usb_phy_reference_present = 1u;
        return 1;
    }
    if (name_length == 9 && bytes_equal(name, "phy-names", 9)) {
        usize first_length = 0u;
        if (!valid_string_list(data, data_length, &first_length)) {
            return 0;
        }
        node->usb_phy_reference_present = 1u;
        return 1;
    }
    if (name_length == 7 && bytes_equal(name, "usb-phy", 7)) {
        if (data_length != sizeof(u32)
            || read_be32(data) == 0u
            || read_be32(data) == 0xffffffffu) {
            return 0;
        }
        node->usb_phy_reference_present = 1u;
        return 1;
    }
    if (name_length == 13 && bytes_equal(name, "power-domains", 13)) {
        if (data_length == 0u || data_length % sizeof(u32) != 0u) {
            return 0;
        }
        node->usb_power_domain_reference_present = 1u;
        return 1;
    }
    if (name_length == 18
        && bytes_equal(name, "power-domain-names", 18)) {
        usize first_length = 0u;
        if (!valid_string_list(data, data_length, &first_length)) {
            return 0;
        }
        node->usb_power_domain_reference_present = 1u;
        return 1;
    }
    if (name_length == 12 && bytes_equal(name, "usb-psy-name", 12)) {
        usize first_length = 0u;
        if (node->usb_power_supply_name_present
            || !valid_string_list(data, data_length, &first_length)
            || first_length != data_length) {
            return 0;
        }
        node->usb_power_supply_name_present = 1u;
        return 1;
    }
    if ((name_length == 11 && bytes_equal(name, "vbus-supply", 11))
        || (name_length == 16
            && (bytes_equal(name, "usb2-vbus-supply", 16)
                || bytes_equal(name, "usb3-vbus-supply", 16)))) {
        if (data_length != sizeof(u32)
            || read_be32(data) == 0u
            || read_be32(data) == 0xffffffffu) {
            return 0;
        }
        node->usb_vbus_supply_present = 1u;
        return 1;
    }
    if ((name_length == 23
            && (bytes_equal(name, "regulator-min-microvolt", 23)
                || bytes_equal(name, "regulator-max-microvolt", 23)))
        || (name_length == 26
            && bytes_equal(name, "regulator-settling-time-us", 26))
        || (name_length == 16
            && bytes_equal(name, "startup-delay-us", 16))) {
        if (data_length != 4u) {
            return 0;
        }
        u32 value = read_be32(data);
        if (name_length == 26) {
            if (node->regulator_settling_present || value == 0u) {
                return 0;
            }
            node->regulator_settling_time_us = value;
            node->regulator_settling_present = 1u;
        } else if (name_length == 16) {
            if (node->regulator_startup_delay_present) {
                return 0;
            }
            node->regulator_startup_delay_us = value;
            node->regulator_startup_delay_present = 1u;
        } else if (name[11] == 'i') {
            if (node->regulator_min_present || value == 0u) {
                return 0;
            }
            node->regulator_min_microvolt = value;
            node->regulator_min_present = 1u;
        } else {
            if (node->regulator_max_present || value == 0u) {
                return 0;
            }
            node->regulator_max_microvolt = value;
            node->regulator_max_present = 1u;
        }
        return 1;
    }
    if (name_length == 8 && bytes_equal(name, "gpio-hog", 8)) {
        node->gpio_hog_present = 1u;
        return 1;
    }
    if ((name_length == 5 && bytes_equal(name, "gpios", 5))
        || (name_length == 4 && bytes_equal(name, "gpio", 4))) {
        if (data_length == 0u || data_length % 4u != 0u
            || data_length / 4u > OS_DEVICE_TREE_MAX_REGULATOR_CELLS
            || node->regulator_gpio_cell_count != 0u) {
            return 0;
        }
        node->regulator_gpio_cell_count = (u8)(data_length / 4u);
        for (usize i = 0; i < node->regulator_gpio_cell_count; ++i) {
            node->regulator_gpio_cells[i] = read_be32(data + i * 4u);
        }
        return 1;
    }
    if (name_length == 8 && bytes_equal(name, "cd-gpios", 8)) {
        if (data_length == 0u || data_length % 4u != 0u
            || data_length / 4u > OS_DEVICE_TREE_MAX_REGULATOR_CELLS
            || node->card_detect_gpio_cell_count != 0u) {
            return 0;
        }
        node->card_detect_gpio_cell_count = (u8)(data_length / 4u);
        for (usize i = 0; i < node->card_detect_gpio_cell_count; ++i) {
            node->card_detect_gpio_cells[i] = read_be32(data + i * 4u);
        }
        return 1;
    }
    if (name_length == 6 && bytes_equal(name, "states", 6)) {
        if (data_length == 0u || data_length % 4u != 0u
            || data_length / 4u > OS_DEVICE_TREE_MAX_REGULATOR_CELLS
            || node->regulator_state_cell_count != 0u) {
            return 0;
        }
        node->regulator_state_cell_count = (u8)(data_length / 4u);
        for (usize i = 0; i < node->regulator_state_cell_count; ++i) {
            node->regulator_state_cells[i] = read_be32(data + i * 4u);
        }
        return 1;
    }
    if ((name_length == 17 && bytes_equal(name, "regulator-boot-on", 17))
        || (name_length == 19
            && bytes_equal(name, "regulator-always-on", 19))
        || (name_length == 18
            && bytes_equal(name, "enable-active-high", 18))) {
        if (data_length != 0u) {
            return 0;
        }
        u8 *flag = name_length == 17
            ? &node->regulator_boot_on
            : (name_length == 19
                ? &node->regulator_always_on
                : &node->regulator_enable_active_high);
        if (*flag) {
            return 0;
        }
        *flag = 1u;
        return 1;
    }
    if ((name_length == 12 && bytes_equal(name, "#clock-cells", 12))
        || (name_length == 12 && bytes_equal(name, "#reset-cells", 12))) {
        if (data_length != 4u) {
            return 0;
        }
        u32 value = read_be32(data);
        if (value > OS_DEVICE_TREE_MAX_REFERENCE_ARGS) {
            return 0;
        }
        if (name[1] == 'c') {
            node->clock_specifier_cells = (u8)value;
        } else {
            node->reset_specifier_cells = (u8)value;
        }
        return 1;
    }
    if (name_length == 10 && bytes_equal(name, "#dma-cells", 10)) {
        if (data_length != 4u || node->dma_specifier_cells_present) {
            return 0;
        }
        u32 value = read_be32(data);
        if (value > OS_DEVICE_TREE_MAX_REFERENCE_ARGS) {
            return 0;
        }
        node->dma_specifier_cells = (u8)value;
        node->dma_specifier_cells_present = 1u;
        return 1;
    }
    if (name_length == 12 && bytes_equal(name, "#iommu-cells", 12)) {
        if (data_length != 4u || node->iommu_specifier_cells_present) {
            return 0;
        }
        u32 value = read_be32(data);
        if (value > OS_DEVICE_TREE_MAX_REFERENCE_ARGS) {
            return 0;
        }
        node->iommu_specifier_cells = (u8)value;
        node->iommu_specifier_cells_present = 1u;
        return 1;
    }
    if (name_length == 16
        && bytes_equal(name, "snps,dma-targets", 16)) {
        if (data_length != 4u || node->dma_target_count_present) {
            return 0;
        }
        node->dma_target_count = read_be32(data);
        if (node->dma_target_count == 0u) {
            return 0;
        }
        node->dma_target_count_present = 1u;
        return 1;
    }
    if (name_length == 12 && bytes_equal(name, "dma-channels", 12)) {
        if (data_length != 4u || node->dma_channel_count_present) {
            return 0;
        }
        node->dma_channel_count = read_be32(data);
        if (node->dma_channel_count == 0u) {
            return 0;
        }
        node->dma_channel_count_present = 1u;
        return 1;
    }
    if (name_length == 16 && bytes_equal(name, "snps,dma-masters", 16)) {
        if (data_length != 4u || node->dma_master_count_present) {
            return 0;
        }
        node->dma_master_count = read_be32(data);
        if (node->dma_master_count == 0u
            || node->dma_master_count > OS_DEVICE_TREE_MAX_DMA_CHANNELS) {
            return 0;
        }
        node->dma_master_count_present = 1u;
        return 1;
    }
    if (name_length == 15 && bytes_equal(name, "snps,data-width", 15)) {
        if (data_length != 4u || node->dma_data_width_present) {
            return 0;
        }
        node->dma_data_width = read_be32(data);
        if (node->dma_data_width > 7u) {
            return 0;
        }
        node->dma_data_width_present = 1u;
        return 1;
    }
    if (name_length == 15 && bytes_equal(name, "snps,block-size", 15)) {
        if (data_length != OS_DEVICE_TREE_MAX_DMA_CHANNELS * 4u
            || node->dma_block_size_count != 0u) {
            return 0;
        }
        for (usize i = 0u; i < OS_DEVICE_TREE_MAX_DMA_CHANNELS; ++i) {
            u32 block_size = read_be32(data + i * 4u);
            if (block_size == 0u) {
                return 0;
            }
            node->dma_block_sizes[i] = block_size;
        }
        node->dma_block_size_count = OS_DEVICE_TREE_MAX_DMA_CHANNELS;
        return 1;
    }
    if (name_length == 22
        && bytes_equal(name, "snps,axi-max-burst-len", 22)) {
        if (data_length == sizeof(u32) && node->dma_axi_max_burst_len_count == 0u) {
            u32 scalar_burst = read_be32(data);
            if (scalar_burst == 0u || scalar_burst > 256u) {
                return 0;
            }
            /* The upstream binding is a scalar that applies to every channel. */
            for (usize i = 0u; i < OS_DEVICE_TREE_MAX_DMA_CHANNELS; ++i) {
                node->dma_axi_max_burst_lengths[i] = scalar_burst;
            }
            node->dma_axi_max_burst_len_count =
                OS_DEVICE_TREE_MAX_DMA_CHANNELS;
            return 1;
        }
        if (data_length != OS_DEVICE_TREE_MAX_DMA_CHANNELS * 4u
            || node->dma_axi_max_burst_len_count != 0u) {
            return 0;
        }
        for (usize i = 0u; i < OS_DEVICE_TREE_MAX_DMA_CHANNELS; ++i) {
            u32 burst_length = read_be32(data + i * 4u);
            if (burst_length == 0u || burst_length > 256u) {
                return 0;
            }
            node->dma_axi_max_burst_lengths[i] = burst_length;
        }
        node->dma_axi_max_burst_len_count =
            OS_DEVICE_TREE_MAX_DMA_CHANNELS;
        return 1;
    }
    if (name_length == 15
        && bytes_equal(name, "clock-frequency", 15)) {
        if (data_length != 4u) {
            return 0;
        }
        node->fixed_clock_frequency_hz = read_be32(data);
        return node->fixed_clock_frequency_hz != 0;
    }
    if (name_length == 22
        && bytes_equal(name, "i2c-scl-rising-time-ns", 22)) {
        if (data_length != 4u || node->i2c_scl_rising_time_present) {
            return 0;
        }
        node->i2c_scl_rising_time_ns = read_be32(data);
        if (node->i2c_scl_rising_time_ns == 0u) {
            return 0;
        }
        node->i2c_scl_rising_time_present = 1u;
        return 1;
    }
    if (name_length == 23
        && bytes_equal(name, "i2c-scl-falling-time-ns", 23)) {
        if (data_length != 4u || node->i2c_scl_falling_time_present) {
            return 0;
        }
        node->i2c_scl_falling_time_ns = read_be32(data);
        if (node->i2c_scl_falling_time_ns == 0u) {
            return 0;
        }
        node->i2c_scl_falling_time_present = 1u;
        return 1;
    }
    if (name_length == 9 && bytes_equal(name, "num-lanes", 9)) {
        if (data_length != 4u) {
            return 0;
        }
        node->pcie_num_lanes = read_be32(data);
        node->pcie_num_lanes_present = 1u;
        return 1;
    }
    if (name_length == 3 && bytes_equal(name, "reg", 3)) {
        return set_regions(
            node->regions, &node->region_count,
            data, data_length,
            frame->reg_address_cells, frame->reg_size_cells,
            stack, depth > 1u ? depth - 1u : 0u, 0);
    }
    if (name_length == 18
        && bytes_equal(name, "assigned-addresses", 18)) {
        return set_regions(
            node->assigned_regions, &node->assigned_region_count,
            data, data_length,
            frame->reg_address_cells, frame->reg_size_cells,
            stack, depth > 1u ? depth - 1u : 0u, 1);
    }
    if (name_length == 6 && bytes_equal(name, "ranges", 6)) {
        return set_ranges(node, frame, data, data_length);
    }
    if (name_length == 10 && bytes_equal(name, "dma-ranges", 10)) {
        return set_dma_ranges(node, frame, data, data_length);
    }
    if (name_length == 10 && bytes_equal(name, "interrupts", 10)) {
        if (data_length == 0 || data_length % 4u != 0
            || data_length / 4u > OS_DEVICE_TREE_MAX_INTERRUPT_CELLS) {
            return 0;
        }
        node->interrupt_cell_count = (u8)(data_length / 4u);
        for (usize i = 0; i < node->interrupt_cell_count; ++i) {
            node->interrupt_cells[i] = read_be32(data + i * 4u);
        }
        return 1;
    }
    if ((name_length == 6 && bytes_equal(name, "clocks", 6))
        || (name_length == 6 && bytes_equal(name, "resets", 6))) {
        if (data_length == 0 || data_length % 4u != 0
            || data_length / 4u > OS_DEVICE_TREE_MAX_REFERENCE_CELLS) {
            return 0;
        }
        u8 *cell_count = name[0] == 'c'
            ? &node->clock_cell_count : &node->reset_cell_count;
        u32 *cells = name[0] == 'c'
            ? node->clock_cells : node->reset_cells;
        *cell_count = (u8)(data_length / 4u);
        for (usize i = 0; i < *cell_count; ++i) {
            cells[i] = read_be32(data + i * 4u);
        }
        return 1;
    }
    if (name_length == 4 && bytes_equal(name, "dmas", 4)) {
        if (data_length == 0u || data_length % 4u != 0u
            || data_length / 4u > OS_DEVICE_TREE_MAX_REFERENCE_CELLS
            || node->dma_cell_count != 0u) {
            return 0;
        }
        node->dma_cell_count = (u8)(data_length / 4u);
        for (usize i = 0u; i < node->dma_cell_count; ++i) {
            node->dma_cells[i] = read_be32(data + i * 4u);
        }
        return 1;
    }
    if (name_length == 6 && bytes_equal(name, "iommus", 6)) {
        if (data_length == 0u || data_length % 4u != 0u
            || data_length / 4u > OS_DEVICE_TREE_MAX_REFERENCE_CELLS
            || node->iommu_cell_count != 0u) {
            return 0;
        }
        node->iommu_cell_count = (u8)(data_length / 4u);
        for (usize i = 0u; i < node->iommu_cell_count; ++i) {
            node->iommu_cells[i] = read_be32(data + i * 4u);
        }
        return 1;
    }
    return 1;
}

static u16 find_phandle(const os_device_tree *tree, u32 phandle)
{
    for (usize i = 0; i < tree->node_count; ++i) {
        if (tree->nodes[i].phandle == phandle) {
            return (u16)i;
        }
    }
    return OS_DEVICE_TREE_NO_PARENT;
}

static int resolve_endpoint_references(os_device_tree *tree)
{
    for (usize i = 0u; i < tree->node_count; ++i) {
        os_device_tree_node *node = &tree->nodes[i];
        if (!node->remote_endpoint_present) {
            continue;
        }
        u16 endpoint_index = find_phandle(
            tree, node->remote_endpoint_phandle);
        if (endpoint_index == OS_DEVICE_TREE_NO_PARENT
            || endpoint_index == i) {
            return 0;
        }
        node->remote_endpoint_index = endpoint_index;
    }
    return 1;
}

static int resolve_gpio_specifier(
    const os_device_tree *tree,
    const u32 *cells,
    u8 cell_count,
    u16 *provider_index_out,
    u32 *pin_out,
    u32 *flags_out
)
{
    if (provider_index_out != (u16 *)0) {
        *provider_index_out = OS_DEVICE_TREE_NO_PARENT;
    }
    if (pin_out != (u32 *)0) {
        *pin_out = 0u;
    }
    if (flags_out != (u32 *)0) {
        *flags_out = 0u;
    }
    if (tree == (const os_device_tree *)0
        || cells == (const u32 *)0
        || provider_index_out == (u16 *)0
        || pin_out == (u32 *)0
        || flags_out == (u32 *)0
        || cell_count < 2u) {
        return 0;
    }
    u16 provider_index = find_phandle(tree, cells[0]);
    if (provider_index == OS_DEVICE_TREE_NO_PARENT) {
        return 0;
    }
    const os_device_tree_node *provider = &tree->nodes[provider_index];
    if (!provider->gpio_specifier_cells_present
        || provider->gpio_specifier_cells != 2u
        || cell_count != 3u) {
        return 0;
    }
    u32 pin = cells[1];
    u32 bank = pin / 32u;
    u32 bit = pin % 32u;
    if (provider->gpio_bank_width_count != 0u
        && (bank >= provider->gpio_bank_width_count
            || bit >= provider->gpio_bank_widths[bank])) {
        return 0;
    }
    *provider_index_out = provider_index;
    *pin_out = pin;
    *flags_out = cells[2];
    return 1;
}

static int resolve_gpio_and_supply_references(os_device_tree *tree)
{
    for (usize i = 0; i < tree->node_count; ++i) {
        os_device_tree_node *node = &tree->nodes[i];
        if (node->vmmc_supply_present) {
            node->vmmc_supply_index = find_phandle(
                tree, node->vmmc_supply_phandle);
            if (node->vmmc_supply_index == OS_DEVICE_TREE_NO_PARENT) {
                return 0;
            }
        }
        if (node->vqmmc_supply_present) {
            node->vqmmc_supply_index = find_phandle(
                tree, node->vqmmc_supply_phandle);
            if (node->vqmmc_supply_index == OS_DEVICE_TREE_NO_PARENT) {
                return 0;
            }
        }
        if (node->phy_handle_present) {
            node->phy_node_index = find_phandle(
                tree, node->phy_handle_phandle);
            if (node->phy_node_index == OS_DEVICE_TREE_NO_PARENT) {
                return 0;
            }
        }
        if (node->phy_reset_gpio_cell_count != 0u) {
            if (!resolve_gpio_specifier(
                    tree, node->phy_reset_gpio_cells,
                    node->phy_reset_gpio_cell_count,
                    &node->phy_reset_gpio_provider_index,
                    &node->phy_reset_gpio_pin,
                    &node->phy_reset_gpio_flags)) {
                return 0;
            }
            node->phy_reset_gpio_resolved = 1u;
        }
        /* gpio-hog "gpios" are bare pin specifiers without a provider phandle. */
        if (node->regulator_gpio_cell_count == 0u
            || node->gpio_hog_present) {
            continue;
        }
        usize cursor = 0u;
        while (cursor < node->regulator_gpio_cell_count) {
            u16 provider_index = find_phandle(
                tree, node->regulator_gpio_cells[cursor++]);
            if (provider_index == OS_DEVICE_TREE_NO_PARENT) {
                return 0;
            }
            os_device_tree_node *provider = &tree->nodes[provider_index];
            if (!provider->gpio_specifier_cells_present
                || provider->gpio_specifier_cells
                    > node->regulator_gpio_cell_count - cursor) {
                return 0;
            }
            u32 pin = node->regulator_gpio_cells[cursor];
            u32 bank = pin / 32u;
            u32 bit = pin % 32u;
            if (provider->gpio_specifier_cells == 0u
                || (provider->gpio_bank_width_count != 0u
                    && (bank >= provider->gpio_bank_width_count
                        || bit >= provider->gpio_bank_widths[bank]))) {
                return 0;
            }
            if (cursor == 1u) {
                node->regulator_gpio_provider_index = provider_index;
                node->regulator_gpio_pin = pin;
                node->regulator_gpio_flags =
                    provider->gpio_specifier_cells > 1u
                    ? node->regulator_gpio_cells[cursor + 1u] : 0u;
            }
            cursor += provider->gpio_specifier_cells;
        }
        node->regulator_gpio_resolved = 1u;
    }
    for (usize i = 0; i < tree->node_count; ++i) {
        os_device_tree_node *node = &tree->nodes[i];
        if (node->card_detect_gpio_cell_count == 0u) {
            continue;
        }
        usize cursor = 0u;
        while (cursor < node->card_detect_gpio_cell_count) {
            u16 provider_index = find_phandle(
                tree, node->card_detect_gpio_cells[cursor++]);
            if (provider_index == OS_DEVICE_TREE_NO_PARENT) {
                return 0;
            }
            os_device_tree_node *provider = &tree->nodes[provider_index];
            if (!provider->gpio_specifier_cells_present
                || provider->gpio_specifier_cells
                    > node->card_detect_gpio_cell_count - cursor) {
                return 0;
            }
            u32 pin = node->card_detect_gpio_cells[cursor];
            u32 bank = pin / 32u;
            u32 bit = pin % 32u;
            if (provider->gpio_specifier_cells == 0u
                || (provider->gpio_bank_width_count != 0u
                    && (bank >= provider->gpio_bank_width_count
                        || bit >= provider->gpio_bank_widths[bank]))) {
                return 0;
            }
            if (cursor == 1u) {
                node->card_detect_gpio_provider_index = provider_index;
                node->card_detect_gpio_pin = pin;
                node->card_detect_gpio_flags =
                    provider->gpio_specifier_cells > 1u
                    ? node->card_detect_gpio_cells[cursor + 1u] : 0u;
            }
            cursor += provider->gpio_specifier_cells;
        }
        node->card_detect_gpio_resolved = 1u;
    }
    return 1;
}

int os_device_tree_get_bcm2712_sd_power(
    const os_device_tree *tree,
    const os_device_tree_node *sdhci_node,
    os_device_tree_bcm2712_sd_power *power
)
{
    if (power != (os_device_tree_bcm2712_sd_power *)0) {
        power->gpio_node_index = OS_DEVICE_TREE_NO_PARENT;
        power->card_enable_pin = 0u;
        power->io_voltage_select_pin = 0u;
        power->card_detect_pin = 0u;
        power->card_detect_flags = 0u;
    }
    int node_belongs_to_tree = 0;
    if (tree != (const os_device_tree *)0
        && tree->node_count <= OS_DEVICE_TREE_MAX_NODES
        && sdhci_node != (const os_device_tree_node *)0) {
        for (usize i = 0; i < tree->node_count; ++i) {
            if (sdhci_node == &tree->nodes[i]) {
                node_belongs_to_tree = 1;
                break;
            }
        }
    }
    if (tree == (const os_device_tree *)0
        || sdhci_node == (const os_device_tree_node *)0
        || power == (os_device_tree_bcm2712_sd_power *)0
        || !node_belongs_to_tree
        || !sdhci_node->enabled
        || (!os_device_tree_node_has_compatible(
                sdhci_node, "brcm,bcm2712-sdhci")
            && !os_device_tree_node_has_compatible(
                sdhci_node, "brcm,sdhci-brcmstb"))
        || !sdhci_node->vmmc_supply_present
        || !sdhci_node->vqmmc_supply_present
        || sdhci_node->card_detect_gpio_cell_count != 3u
        || !sdhci_node->card_detect_gpio_resolved
        || sdhci_node->vmmc_supply_index >= tree->node_count
        || sdhci_node->vqmmc_supply_index >= tree->node_count) {
        return 0;
    }
    const os_device_tree_node *card =
        &tree->nodes[sdhci_node->vmmc_supply_index];
    const os_device_tree_node *io =
        &tree->nodes[sdhci_node->vqmmc_supply_index];
    if (!card->enabled || !io->enabled
        || !os_device_tree_node_has_compatible(card, "regulator-fixed")
        || !os_device_tree_node_has_compatible(io, "regulator-gpio")
        || !card->regulator_min_present || !card->regulator_max_present
        || card->regulator_min_microvolt != 3300000u
        || card->regulator_max_microvolt != 3300000u
        || !card->regulator_boot_on
        || !card->regulator_enable_active_high
        || !card->regulator_gpio_resolved
        || card->regulator_gpio_cell_count != 3u
        || card->regulator_gpio_flags != 0u
        || !io->regulator_min_present || !io->regulator_max_present
        || io->regulator_min_microvolt != 1800000u
        || io->regulator_max_microvolt != 3300000u
        || !io->regulator_boot_on || !io->regulator_always_on
        || !io->regulator_settling_present
        || io->regulator_settling_time_us
            != 5000u
        || !io->regulator_gpio_resolved
        || io->regulator_gpio_cell_count != 3u
        || io->regulator_gpio_flags != 0u
        || io->regulator_state_cell_count != 4u
        || io->regulator_state_cells[0] != 1800000u
        || io->regulator_state_cells[1] != 1u
        || io->regulator_state_cells[2] != 3300000u
        || io->regulator_state_cells[3] != 0u
        || card->regulator_gpio_provider_index
            != io->regulator_gpio_provider_index
        || sdhci_node->card_detect_gpio_provider_index
            != card->regulator_gpio_provider_index
        || sdhci_node->card_detect_gpio_pin != 5u
        || sdhci_node->card_detect_gpio_flags != 1u
        || card->regulator_gpio_pin != 4u
        || io->regulator_gpio_pin != 3u) {
        return 0;
    }
    u16 gpio_index = card->regulator_gpio_provider_index;
    const os_device_tree_node *gpio = &tree->nodes[gpio_index];
    if (!gpio->enabled
        || (!os_device_tree_node_has_compatible(
                gpio, "brcm,bcm7445-gpio")
            && !os_device_tree_node_has_compatible(
                gpio, "brcm,brcmstb-gpio"))
        || !gpio->gpio_specifier_cells_present
        || gpio->gpio_specifier_cells != 2u
        || gpio->gpio_bank_width_count != 2u
        || gpio->gpio_bank_widths[0] != 17u
        || gpio->gpio_bank_widths[1] != 6u
        || gpio->region_count != 1u
        || !gpio->regions[0].translated
        || gpio->regions[0].address != 0x7d517c00ull
        || gpio->regions[0].size != 0x40u) {
        return 0;
    }
    power->gpio_node_index = gpio_index;
    power->card_enable_pin = card->regulator_gpio_pin;
    power->io_voltage_select_pin = io->regulator_gpio_pin;
    power->card_detect_pin = sdhci_node->card_detect_gpio_pin;
    power->card_detect_flags = sdhci_node->card_detect_gpio_flags;
    return 1;
}

static int resolve_provider_list(
    os_device_tree *tree,
    os_device_tree_node *node,
    int is_clock
)
{
    u8 cell_count = is_clock
        ? node->clock_cell_count : node->reset_cell_count;
    u32 *cells = is_clock ? node->clock_cells : node->reset_cells;
    u8 *reference_count = is_clock
        ? &node->clock_reference_count : &node->reset_reference_count;
    os_device_tree_reference *references = is_clock
        ? node->clock_references : node->reset_references;
    usize cursor = 0;
    while (cursor < cell_count) {
        if (*reference_count >= OS_DEVICE_TREE_MAX_REFERENCES) {
            return 0;
        }
        u32 phandle = cells[cursor++];
        if (phandle == 0 || phandle == 0xffffffffu) {
            return 0;
        }
        u16 provider_index = find_phandle(tree, phandle);
        if (provider_index == OS_DEVICE_TREE_NO_PARENT) {
            return 0;
        }
        const os_device_tree_node *provider = &tree->nodes[provider_index];
        u8 argument_count = is_clock
            ? provider->clock_specifier_cells
            : provider->reset_specifier_cells;
        if (argument_count > OS_DEVICE_TREE_MAX_REFERENCE_ARGS
            || argument_count > cell_count - cursor) {
            return 0;
        }
        os_device_tree_reference *reference =
            &references[(*reference_count)++];
        reference->provider_index = provider_index;
        reference->argument_count = argument_count;
        for (usize i = 0; i < argument_count; ++i) {
            reference->arguments[i] = cells[cursor++];
        }
    }
    return 1;
}

static int resolve_dma_provider_list(
    os_device_tree *tree,
    os_device_tree_node *node
)
{
    usize cursor = 0u;
    while (cursor < node->dma_cell_count) {
        if (node->dma_reference_count >= OS_DEVICE_TREE_MAX_REFERENCES) {
            return 0;
        }
        u32 phandle = node->dma_cells[cursor++];
        if (phandle == 0u || phandle == 0xffffffffu) {
            return 0;
        }
        u16 provider_index = find_phandle(tree, phandle);
        if (provider_index == OS_DEVICE_TREE_NO_PARENT) {
            return 0;
        }
        const os_device_tree_node *provider = &tree->nodes[provider_index];
        u8 argument_count = provider->dma_specifier_cells;
        if (!provider->dma_specifier_cells_present
            || argument_count > OS_DEVICE_TREE_MAX_REFERENCE_ARGS
            || argument_count > node->dma_cell_count - cursor) {
            return 0;
        }
        os_device_tree_reference *reference =
            &node->dma_references[node->dma_reference_count++];
        reference->provider_index = provider_index;
        reference->argument_count = argument_count;
        for (usize i = 0u; i < argument_count; ++i) {
            reference->arguments[i] = node->dma_cells[cursor++];
        }
    }
    return 1;
}

static int resolve_iommu_provider_list(
    os_device_tree *tree,
    os_device_tree_node *node
)
{
    usize cursor = 0u;
    while (cursor < node->iommu_cell_count) {
        if (node->iommu_reference_count >= OS_DEVICE_TREE_MAX_REFERENCES) {
            return 0;
        }
        u32 phandle = node->iommu_cells[cursor++];
        if (phandle == 0u || phandle == 0xffffffffu) {
            return 0;
        }
        u16 provider_index = find_phandle(tree, phandle);
        if (provider_index == OS_DEVICE_TREE_NO_PARENT) {
            return 0;
        }
        const os_device_tree_node *provider = &tree->nodes[provider_index];
        u8 argument_count = provider->iommu_specifier_cells;
        if (!provider->iommu_specifier_cells_present
            || argument_count > OS_DEVICE_TREE_MAX_REFERENCE_ARGS
            || argument_count > node->iommu_cell_count - cursor) {
            return 0;
        }
        os_device_tree_reference *reference =
            &node->iommu_references[node->iommu_reference_count++];
        reference->provider_index = provider_index;
        reference->argument_count = argument_count;
        for (usize i = 0u; i < argument_count; ++i) {
            reference->arguments[i] = node->iommu_cells[cursor++];
        }
    }
    return 1;
}

static int resolve_interrupt_parents(os_device_tree *tree)
{
    for (usize i = 0; i < tree->node_count; ++i) {
        const os_device_tree_node *node = &tree->nodes[i];
        if (node->phandle == 0) {
            continue;
        }
        for (usize other = i + 1u; other < tree->node_count; ++other) {
            if (tree->nodes[other].phandle == node->phandle) {
                return 0;
            }
        }
    }
    for (usize i = 0; i < tree->node_count; ++i) {
        if (!resolve_provider_list(tree, &tree->nodes[i], 1)
            || !resolve_provider_list(tree, &tree->nodes[i], 0)
            || !resolve_dma_provider_list(tree, &tree->nodes[i])
            || !resolve_iommu_provider_list(tree, &tree->nodes[i])) {
            return 0;
        }
    }
    for (usize i = 0; i < tree->node_count; ++i) {
        os_device_tree_node *node = &tree->nodes[i];
        u32 parent_phandle = 0;
        u16 ancestor_index = (u16)i;
        for (;;) {
            const os_device_tree_node *ancestor =
                &tree->nodes[ancestor_index];
            if (ancestor->interrupt_parent_present) {
                parent_phandle = ancestor->interrupt_parent_phandle;
                break;
            }
            if (ancestor->parent_index == OS_DEVICE_TREE_NO_PARENT) {
                break;
            }
            ancestor_index = ancestor->parent_index;
        }
        if (parent_phandle == 0) {
            continue;
        }
        u16 resolved_index = OS_DEVICE_TREE_NO_PARENT;
        for (usize candidate = 0; candidate < tree->node_count; ++candidate) {
            if (tree->nodes[candidate].phandle == parent_phandle) {
                resolved_index = (u16)candidate;
                break;
            }
        }
        if (resolved_index == OS_DEVICE_TREE_NO_PARENT) {
            return 0;
        }
        node->interrupt_parent_index = resolved_index;
        if (node->interrupt_cell_count != 0) {
            u8 specifier_cells =
                tree->nodes[resolved_index].interrupt_specifier_cells;
            if (specifier_cells == 0
                || node->interrupt_cell_count % specifier_cells != 0) {
                return 0;
            }
        }
    }
    return 1;
}

static int resolve_msi_references(os_device_tree *tree)
{
    for (usize i = 0; i < tree->node_count; ++i) {
        os_device_tree_node *node = &tree->nodes[i];
        if (node->msi_parent_present) {
            u16 provider_index = find_phandle(
                tree, node->msi_parent_phandle);
            if (provider_index == OS_DEVICE_TREE_NO_PARENT
                || !tree->nodes[provider_index].msi_controller) {
                return 0;
            }
            node->msi_parent_index = provider_index;
        }
        if (!node->msi_controller) {
            if (node->msi_ranges_present || node->msi_offset_present) {
                return 0;
            }
            continue;
        }
        if (!node->msi_ranges_present) {
            /* brcm,bcm2712-msix style controllers describe SPIs via interrupts. */
            continue;
        }
        u16 provider_index = find_phandle(
            tree, node->msi_range_cells[0]);
        if (provider_index == OS_DEVICE_TREE_NO_PARENT
            || tree->nodes[provider_index].interrupt_specifier_cells != 3u
            || node->msi_range_cells[1] != 0u
            || node->msi_range_cells[2] >= 1020u
            || node->msi_range_cells[4] == 0u
            || node->msi_range_cells[4] > 1020u - node->msi_range_cells[2]
            || node->msi_offset > 64u
            || node->msi_range_cells[4] > 64u - node->msi_offset) {
            return 0;
        }
        node->msi_range_parent_index = provider_index;
        node->msi_range_base = node->msi_range_cells[2];
        node->msi_range_flags = node->msi_range_cells[3];
        node->msi_range_count = node->msi_range_cells[4];
    }
    return 1;
}

int os_device_tree_parse(
    const void *blob_pointer,
    usize blob_size,
    os_device_tree *tree
)
{
    if (tree == (os_device_tree *)0) {
        return 0;
    }
    clear_tree(tree);
    if (blob_pointer == (const void *)0
        || blob_size < FDT_HEADER_SIZE
        || blob_size > OS_DEVICE_TREE_MAX_BLOB_SIZE) {
        return 0;
    }
    const u8 *blob = (const u8 *)blob_pointer;
    if (read_be32(blob) != FDT_MAGIC) {
        return 0;
    }
    usize total_size = read_be32(blob + 4u);
    usize structure_offset = read_be32(blob + 8u);
    usize strings_offset = read_be32(blob + 12u);
    usize reservations_offset = read_be32(blob + 16u);
    u32 version = read_be32(blob + 20u);
    u32 last_compatible_version = read_be32(blob + 24u);
    usize strings_size = read_be32(blob + 32u);
    usize structure_size = read_be32(blob + 36u);
    if (total_size < FDT_HEADER_SIZE || total_size > blob_size
        || total_size > OS_DEVICE_TREE_MAX_BLOB_SIZE
        || version < 16 || version > 17
        || last_compatible_version > version
        || structure_size < 4
        || (structure_offset & 3u) != 0
        || !range_valid(structure_offset, structure_size, total_size)
        || !range_valid(strings_offset, strings_size, total_size)
        || structure_offset < FDT_HEADER_SIZE
        || strings_offset < FDT_HEADER_SIZE
        || !(structure_offset + structure_size <= strings_offset
            || strings_offset + strings_size <= structure_offset)
        || !validate_reservations(
            blob, total_size, reservations_offset,
            structure_offset, strings_offset)) {
        return 0;
    }

    const u8 *structure = blob + structure_offset;
    const u8 *strings = blob + strings_offset;
    fdt_node_frame stack[16];
    volatile u8 *stack_bytes = (volatile u8 *)stack;
    for (usize i = 0; i < sizeof(stack); ++i) {
        stack_bytes[i] = 0;
    }
    usize depth = 0;
    usize cursor = 0;
    int root_closed = 0;
    int end_seen = 0;
    while (cursor < structure_size) {
        if (!range_valid(cursor, 4u, structure_size)) {
            clear_tree(tree);
            return 0;
        }
        u32 token = read_be32(structure + cursor);
        cursor += 4u;
        if (token == FDT_TOKEN_BEGIN_NODE) {
            if (root_closed || depth >= 16
                || tree->node_count >= OS_DEVICE_TREE_MAX_NODES) {
                clear_tree(tree);
                return 0;
            }
            usize start = cursor;
            while (cursor < structure_size && structure[cursor] != 0) {
                ++cursor;
            }
            if (cursor >= structure_size) {
                clear_tree(tree);
                return 0;
            }
            usize name_length = cursor - start;
            if (name_length >= OS_DEVICE_TREE_NAME_MAX
                || (depth == 0 && (name_length != 0
                    || tree->node_count != 0))) {
                clear_tree(tree);
                return 0;
            }
            ++cursor;
            cursor = align_four(cursor);
            if (cursor > structure_size) {
                clear_tree(tree);
                return 0;
            }

            usize node_index = tree->node_count++;
            os_device_tree_node *node = &tree->nodes[node_index];
            node->parent_index = depth == 0
                ? OS_DEVICE_TREE_NO_PARENT : stack[depth - 1u].node_index;
            node->interrupt_parent_index = OS_DEVICE_TREE_NO_PARENT;
            node->remote_endpoint_index = OS_DEVICE_TREE_NO_PARENT;
            node->msi_parent_index = OS_DEVICE_TREE_NO_PARENT;
            node->msi_range_parent_index = OS_DEVICE_TREE_NO_PARENT;
            node->vmmc_supply_index = OS_DEVICE_TREE_NO_PARENT;
            node->vqmmc_supply_index = OS_DEVICE_TREE_NO_PARENT;
            node->regulator_gpio_provider_index = OS_DEVICE_TREE_NO_PARENT;
            node->card_detect_gpio_provider_index =
                OS_DEVICE_TREE_NO_PARENT;
            node->enabled = 1;
            if (name_length != 0) {
                for (usize i = 0; i < name_length; ++i) {
                    node->name[i] = (char)structure[start + i];
                }
                node->name[name_length] = '\0';
            }
            fdt_node_frame *frame = &stack[depth];
            volatile u8 *frame_bytes = (volatile u8 *)frame;
            for (usize i = 0; i < sizeof(*frame); ++i) {
                frame_bytes[i] = 0;
            }
            frame->node_index = (u16)node_index;
            frame->child_address_cells = depth == 0
                ? 2u : stack[depth - 1u].child_address_cells;
            frame->child_size_cells = depth == 0
                ? 1u : stack[depth - 1u].child_size_cells;
            frame->reg_address_cells = depth == 0
                ? 0u : stack[depth - 1u].child_address_cells;
            frame->reg_size_cells = depth == 0
                ? 0u : stack[depth - 1u].child_size_cells;
            /*
             * FDT does not order properties: #address-cells and #size-cells
             * may follow reg/ranges (QEMU emits them last). Apply them before
             * any property of this node is interpreted. Structure errors are
             * left to the main loop, which re-validates every token.
             */
            for (usize scan = cursor; range_valid(scan, 4u, structure_size);) {
                u32 scan_token = read_be32(structure + scan);
                scan += 4u;
                if (scan_token == FDT_TOKEN_NOP) {
                    continue;
                }
                if (scan_token != FDT_TOKEN_PROPERTY
                    || !range_valid(scan, 8u, structure_size)) {
                    break;
                }
                usize scan_length = read_be32(structure + scan);
                u32 scan_name_offset = read_be32(structure + scan + 4u);
                scan += 8u;
                if (!range_valid(scan, scan_length, structure_size)) {
                    break;
                }
                const u8 *scan_name = (const u8 *)0;
                usize scan_name_length = 0;
                if (scan_length == 4u
                    && find_string(strings, strings_size, scan_name_offset,
                        &scan_name, &scan_name_length)) {
                    u32 cells = read_be32(structure + scan);
                    if (scan_name_length == 14
                        && bytes_equal(scan_name, "#address-cells", 14)
                        && cells <= 3u) {
                        frame->child_address_cells = cells;
                    } else if (scan_name_length == 11
                        && bytes_equal(scan_name, "#size-cells", 11)
                        && cells <= 2u) {
                        frame->child_size_cells = cells;
                    }
                }
                scan = align_four(scan + scan_length);
            }
            ++depth;
        } else if (token == FDT_TOKEN_END_NODE) {
            if (depth == 0) {
                clear_tree(tree);
                return 0;
            }
            --depth;
            if (depth == 0) {
                root_closed = 1;
            }
        } else if (token == FDT_TOKEN_PROPERTY) {
            if (depth == 0 || !range_valid(cursor, 8u, structure_size)) {
                clear_tree(tree);
                return 0;
            }
            usize data_length = read_be32(structure + cursor);
            u32 name_offset = read_be32(structure + cursor + 4u);
            cursor += 8u;
            if (!range_valid(cursor, data_length, structure_size)) {
                clear_tree(tree);
                return 0;
            }
            const u8 *property_name = (const u8 *)0;
            usize property_name_length = 0;
            const char *owner = tree->nodes[stack[depth - 1u].node_index].name;
            if (depth == 2u && owner[0] == '_' && owner[1] == '_') {
                /* Overlay metadata (__symbols__ etc.) holds path strings. */
                cursor = align_four(cursor + data_length);
                continue;
            }
            if (!find_string(
                    strings, strings_size, name_offset,
                    &property_name, &property_name_length)
                || !parse_property(
                    &tree->nodes[stack[depth - 1u].node_index],
                    &stack[depth - 1u], stack, depth,
                    property_name, property_name_length, structure + cursor,
                    data_length)) {
                clear_tree(tree);
                return 0;
            }
            cursor = align_four(cursor + data_length);
            if (cursor > structure_size) {
                clear_tree(tree);
                return 0;
            }
        } else if (token == FDT_TOKEN_NOP) {
            continue;
        } else if (token == FDT_TOKEN_END) {
            if (depth != 0 || !root_closed
                || tree->node_count == 0 || cursor != structure_size) {
                clear_tree(tree);
                return 0;
            }
            end_seen = 1;
            break;
        } else {
            clear_tree(tree);
            return 0;
        }
    }
    if (!end_seen) {
        clear_tree(tree);
        return 0;
    }
    if (!resolve_interrupt_parents(tree)
        || !resolve_endpoint_references(tree)
        || !resolve_gpio_and_supply_references(tree)
        || !resolve_msi_references(tree)) {
        clear_tree(tree);
        return 0;
    }
    return 1;
}

int os_device_tree_find_uefi_blob(
    const efi_system_table *system_table,
    const void **blob,
    usize *blob_size
)
{
    static const efi_guid fdt_table_guid = {
        0xb1b621d5u, 0xf19cu, 0x41a5u,
        {0x83, 0x0b, 0xd9, 0x15, 0x2c, 0x69, 0xaa, 0xe0},
    };
    if (blob != (const void **)0) {
        *blob = (const void *)0;
    }
    if (blob_size != (usize *)0) {
        *blob_size = 0;
    }
    if (system_table == (const efi_system_table *)0
        || blob == (const void **)0 || blob_size == (usize *)0
        || system_table->number_of_table_entries == 0
        || system_table->number_of_table_entries > OS_FDT_MAX_UEFI_TABLES
        || system_table->configuration_table == (void *)0) {
        return 0;
    }
    const efi_configuration_table *tables =
        (const efi_configuration_table *)system_table->configuration_table;
    for (usize i = 0; i < system_table->number_of_table_entries; ++i) {
        const efi_guid *candidate = &tables[i].vendor_guid;
        int matches = candidate->data1 == fdt_table_guid.data1
            && candidate->data2 == fdt_table_guid.data2
            && candidate->data3 == fdt_table_guid.data3;
        for (usize byte = 0; byte < sizeof(candidate->data4); ++byte) {
            if (candidate->data4[byte] != fdt_table_guid.data4[byte]) {
                matches = 0;
            }
        }
        if (!matches || tables[i].vendor_table == (void *)0) {
            continue;
        }
        const u8 *fdt = (const u8 *)tables[i].vendor_table;
        if (read_be32(fdt) != FDT_MAGIC) {
            return 0;
        }
        usize size = read_be32(fdt + 4u);
        if (size < FDT_HEADER_SIZE || size > OS_DEVICE_TREE_MAX_BLOB_SIZE) {
            return 0;
        }
        *blob = fdt;
        *blob_size = size;
        return 1;
    }
    return 0;
}

int os_device_tree_discover_uefi(
    const efi_system_table *system_table,
    os_device_tree *tree
)
{
    const void *blob = (const void *)0;
    usize blob_size = 0;
    return os_device_tree_find_uefi_blob(
            system_table, &blob, &blob_size)
        && os_device_tree_parse(blob, blob_size, tree);
}
