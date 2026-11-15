#ifndef EXPERIMENTALOS_KERNEL_DEVICE_TREE_H
#define EXPERIMENTALOS_KERNEL_DEVICE_TREE_H

#include "efi.h"

#ifndef OS_DEVICE_TREE_MAX_NODES
#define OS_DEVICE_TREE_MAX_NODES 64u
#endif
#define OS_DEVICE_TREE_NAME_MAX 64u
#define OS_DEVICE_TREE_COMPATIBLE_MAX 96u
#define OS_DEVICE_TREE_COMPATIBLE_LIST_MAX 256u
#define OS_DEVICE_TREE_NO_PARENT 0xffffu
#define OS_DEVICE_TREE_MAX_BLOB_SIZE (16u * 1024u * 1024u)
#define OS_DEVICE_TREE_MAX_REGIONS 16u
#define OS_DEVICE_TREE_MAX_RANGES 8u
#ifndef OS_DEVICE_TREE_MAX_INTERRUPT_CELLS
#define OS_DEVICE_TREE_MAX_INTERRUPT_CELLS 16u
#endif
#define OS_DEVICE_TREE_RP1_INTERRUPT_COUNT 61u
#define OS_DEVICE_TREE_MAX_REFERENCES 8u
#define OS_DEVICE_TREE_MAX_REFERENCE_CELLS 16u
#define OS_DEVICE_TREE_MAX_REFERENCE_ARGS 4u
#define OS_DEVICE_TREE_MAX_DMA_RANGES 8u
#define OS_DEVICE_TREE_MAX_DMA_CHANNELS 8u
#define OS_DEVICE_TREE_MAX_DATA_LANES 4u
#define OS_DEVICE_TREE_MAX_GPIO_BANKS 4u
#define OS_DEVICE_TREE_MAX_REGULATOR_CELLS 8u
#define OS_DEVICE_TREE_USB_DR_MODE_NONE 0u
#define OS_DEVICE_TREE_USB_DR_MODE_HOST 1u
#define OS_DEVICE_TREE_USB_DR_MODE_PERIPHERAL 2u
#define OS_DEVICE_TREE_USB_DR_MODE_OTG 3u
#define OS_DEVICE_TREE_USB_DR_MODE_UNSUPPORTED 4u

typedef struct {
    u16 provider_index;
    u8 argument_count;
    u32 arguments[OS_DEVICE_TREE_MAX_REFERENCE_ARGS];
} os_device_tree_reference;

typedef struct {
    u64 child_address;
    u64 parent_address;
    u64 size;
    u32 child_flags;
    u32 parent_flags;
} os_device_tree_dma_range;

typedef struct {
    u64 address;
    u64 size;
    u32 address_flags;
    u8 translated;
} os_device_tree_region;

typedef struct {
    u64 child_address;
    u64 parent_address;
    u64 size;
    u32 child_flags;
    u32 parent_flags;
} os_device_tree_range;

typedef struct {
    char name[OS_DEVICE_TREE_NAME_MAX];
    char compatible[OS_DEVICE_TREE_COMPATIBLE_MAX];
    char compatible_list[OS_DEVICE_TREE_COMPATIBLE_LIST_MAX];
    u16 compatible_list_size;
    u32 phandle;
    u32 interrupt_parent_phandle;
    u32 msi_parent_phandle;
    u32 msi_range_cells[5];
    u32 msi_offset;
    u32 msi_range_base;
    u32 msi_range_flags;
    u32 msi_range_count;
    u8 clock_specifier_cells;
    u8 reset_specifier_cells;
    u8 dma_specifier_cells;
    u8 dma_specifier_cells_present;
    u8 iommu_specifier_cells;
    u8 iommu_specifier_cells_present;
    u8 dma_master_count_present;
    u8 dma_channel_count_present;
    u8 dma_target_count_present;
    u8 dma_data_width_present;
    u8 dma_block_size_count;
    u8 dma_axi_max_burst_len_count;
    u32 fixed_clock_frequency_hz;
    u32 dma_master_count;
    u32 dma_channel_count;
    u32 dma_target_count;
    u32 dma_data_width;
    u32 dma_block_sizes[OS_DEVICE_TREE_MAX_DMA_CHANNELS];
    u32 dma_axi_max_burst_lengths[OS_DEVICE_TREE_MAX_DMA_CHANNELS];
    u32 regulator_min_microvolt;
    u32 regulator_max_microvolt;
    u32 regulator_settling_time_us;
    u32 regulator_startup_delay_us;
    u32 regulator_gpio_cells[OS_DEVICE_TREE_MAX_REGULATOR_CELLS];
    u32 regulator_state_cells[OS_DEVICE_TREE_MAX_REGULATOR_CELLS];
    u32 gpio_bank_widths[OS_DEVICE_TREE_MAX_GPIO_BANKS];
    u32 card_detect_gpio_cells[OS_DEVICE_TREE_MAX_REGULATOR_CELLS];
    u32 phy_reset_gpio_cells[OS_DEVICE_TREE_MAX_REGULATOR_CELLS];
    u32 i2c_scl_rising_time_ns;
    u32 i2c_scl_falling_time_ns;
    u32 pcie_num_lanes;
    u32 phy_handle_phandle;
    u32 phy_reset_duration_ms;
    u32 remote_endpoint_phandle;
    u32 data_lanes[OS_DEVICE_TREE_MAX_DATA_LANES];
    u32 clock_lane;
    u32 vmmc_supply_phandle;
    u32 vqmmc_supply_phandle;
    u32 regulator_gpio_pin;
    u32 regulator_gpio_flags;
    u32 card_detect_gpio_pin;
    u32 card_detect_gpio_flags;
    u32 phy_reset_gpio_pin;
    u32 phy_reset_gpio_flags;
    u8 pcie_num_lanes_present;
    u8 i2c_scl_rising_time_present;
    u8 i2c_scl_falling_time_present;
    u8 clock_cell_count;
    u8 reset_cell_count;
    u8 dma_cell_count;
    u8 iommu_cell_count;
    u32 clock_cells[OS_DEVICE_TREE_MAX_REFERENCE_CELLS];
    u32 reset_cells[OS_DEVICE_TREE_MAX_REFERENCE_CELLS];
    u32 dma_cells[OS_DEVICE_TREE_MAX_REFERENCE_CELLS];
    u32 iommu_cells[OS_DEVICE_TREE_MAX_REFERENCE_CELLS];
    u8 clock_reference_count;
    u8 reset_reference_count;
    u8 dma_reference_count;
    u8 iommu_reference_count;
    os_device_tree_reference clock_references[OS_DEVICE_TREE_MAX_REFERENCES];
    os_device_tree_reference reset_references[OS_DEVICE_TREE_MAX_REFERENCES];
    os_device_tree_reference dma_references[OS_DEVICE_TREE_MAX_REFERENCES];
    os_device_tree_reference iommu_references[OS_DEVICE_TREE_MAX_REFERENCES];
    u16 parent_index;
    u16 interrupt_parent_index;
    u16 msi_parent_index;
    u16 msi_range_parent_index;
    u16 vmmc_supply_index;
    u16 vqmmc_supply_index;
    u16 regulator_gpio_provider_index;
    u16 card_detect_gpio_provider_index;
    u16 phy_node_index;
    u16 phy_reset_gpio_provider_index;
    u16 remote_endpoint_index;
    u8 enabled;
    u8 skip_init;
    u8 usb_dr_mode_present;
    u8 usb_dr_mode;
    u8 usb_phy_reference_present;
    u8 usb_power_domain_reference_present;
    u8 usb_power_supply_name_present;
    u8 usb_vbus_supply_present;
    u8 msi_parent_present;
    u8 msi_controller;
    u8 msi_ranges_present;
    u8 msi_offset_present;
    u8 gpio_specifier_cells;
    u8 gpio_specifier_cells_present;
    u8 gpio_bank_width_count;
    u8 vmmc_supply_present;
    u8 vqmmc_supply_present;
    u8 regulator_min_present;
    u8 regulator_max_present;
    u8 regulator_settling_present;
    u8 regulator_startup_delay_present;
    u8 regulator_gpio_cell_count;
    u8 regulator_state_cell_count;
    u8 regulator_boot_on;
    u8 regulator_always_on;
    u8 regulator_enable_active_high;
    u8 bus_width;
    u8 bus_width_present;
    u8 non_removable;
    u8 regulator_gpio_resolved;
    u8 gpio_hog_present;
    u8 card_detect_gpio_cell_count;
    u8 phy_reset_gpio_cell_count;
    u8 phy_handle_present;
    u8 phy_reset_duration_present;
    u8 local_mac_address_present;
    u8 local_mac_address[6];
    u8 card_detect_gpio_resolved;
    u8 phy_reset_gpio_resolved;
    u8 remote_endpoint_present;
    u8 data_lanes_present;
    u8 data_lane_count;
    u8 clock_lane_present;
    u8 region_count;
    os_device_tree_region regions[OS_DEVICE_TREE_MAX_REGIONS];
    u8 range_count;
    u8 ranges_present;
    u8 ranges_identity;
    os_device_tree_range ranges[OS_DEVICE_TREE_MAX_RANGES];
    u8 assigned_region_count;
    os_device_tree_region assigned_regions[OS_DEVICE_TREE_MAX_REGIONS];
    u8 dma_range_count;
    u8 dma_ranges_present;
    u8 dma_ranges_identity;
    os_device_tree_dma_range dma_ranges[OS_DEVICE_TREE_MAX_DMA_RANGES];
    u8 interrupt_cell_count;
    u8 interrupt_specifier_cells;
    u8 interrupt_parent_present;
    u8 interrupt_controller;
    u32 interrupt_cells[OS_DEVICE_TREE_MAX_INTERRUPT_CELLS];
} os_device_tree_node;

typedef struct {
    usize node_count;
    os_device_tree_node nodes[OS_DEVICE_TREE_MAX_NODES];
} os_device_tree;

typedef struct {
    u32 interrupt_id;
    u32 flags;
    u8 is_peripheral;
} os_device_tree_gic_interrupt;

typedef struct {
    u16 gpio_node_index;
    u32 card_enable_pin;
    u32 io_voltage_select_pin;
    u32 card_detect_pin;
    u32 card_detect_flags;
} os_device_tree_bcm2712_sd_power;

typedef struct {
    u32 interrupt_id;
    u32 flags;
} os_device_tree_interrupt;

int os_device_tree_node_has_compatible(
    const os_device_tree_node *node,
    const char *compatible
);
int os_device_tree_get_gic_interrupt(
    const os_device_tree *tree,
    const os_device_tree_node *node,
    usize interrupt_index,
    os_device_tree_gic_interrupt *interrupt
);
int os_device_tree_get_rp1_interrupt(
    const os_device_tree *tree,
    const os_device_tree_node *node,
    usize interrupt_index,
    os_device_tree_interrupt *interrupt
);
int os_device_tree_get_clock_frequency(
    const os_device_tree *tree,
    const os_device_tree_node *node,
    usize clock_index,
    u32 *frequency_hz
);
int os_device_tree_get_bcm2712_sd_power(
    const os_device_tree *tree,
    const os_device_tree_node *sdhci_node,
    os_device_tree_bcm2712_sd_power *power
);
int os_device_tree_translate_dma_address(
    const os_device_tree_node *node,
    u64 dma_address,
    u32 address_flags,
    u64 size,
    u64 *system_address
);
int os_device_tree_translate_system_dma_address(
    const os_device_tree_node *node,
    u64 system_address,
    u32 address_flags,
    u64 size,
    u64 *dma_address
);
int os_device_tree_parse(
    const void *blob,
    usize blob_size,
    os_device_tree *tree
);
int os_device_tree_find_uefi_blob(
    const efi_system_table *system_table,
    const void **blob,
    usize *blob_size
);
int os_device_tree_discover_uefi(
    const efi_system_table *system_table,
    os_device_tree *tree
);
int os_device_tree_self_test(void);
u32 os_device_tree_self_test_failure(void);

#endif
