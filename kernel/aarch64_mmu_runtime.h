#ifndef EXPERIMENTALOS_KERNEL_AARCH64_MMU_RUNTIME_H
#define EXPERIMENTALOS_KERNEL_AARCH64_MMU_RUNTIME_H

#include "aarch64_mmu.h"
#include "efi.h"
#include "pi5_driver.h"

#define OS_AARCH64_MMU_EFI_DESCRIPTOR_VERSION 1u
#define OS_AARCH64_MMU_EFI_DESCRIPTOR_MIN_SIZE 40u
#define OS_AARCH64_MMU_LIVE_RANGE_MAX 16u
#define OS_AARCH64_MMIO_MAX_MAPPINGS 32u
#define OS_AARCH64_MMIO_MAX_AUTHORIZED \
    (2u * OS_DEVICE_TREE_MAX_NODES * OS_DEVICE_TREE_MAX_REGIONS)

typedef struct {
    u64 current_el;
    u64 ttbr0;
    u64 tcr;
    u64 mair;
    u64 sctlr;
    u64 sp;
    u64 pc;
    u64 mmfr0;
    u64 vbar;
} os_aarch64_mmu_arch_state;

typedef struct {
    u64 virtual_base;
    u64 size;
} os_aarch64_mmu_live_range;

typedef struct {
    u64 virtual_base;
    u64 virtual_size;
    u64 physical_base;
    u64 mapped_size;
    u64 returned_address;
    u32 requested_size;
    u32 owner;
    u8 active;
} os_aarch64_mmio_mapping;

typedef struct {
    u64 physical_base;
    u64 size;
    u32 owner;
} os_aarch64_mmio_authorized_range;

typedef struct {
    os_aarch64_mmu *mmu;
    u64 virtual_base;
    u64 virtual_size;
    u32 active;
    u32 lock;
    usize authorized_count;
    os_aarch64_mmio_mapping mappings[OS_AARCH64_MMIO_MAX_MAPPINGS];
    os_aarch64_mmio_authorized_range authorized[
        OS_AARCH64_MMIO_MAX_AUTHORIZED];
} os_aarch64_mmio_space;

int os_aarch64_mmu_validate_efi_memory_map(
    const void *memory_map,
    usize memory_map_size,
    usize descriptor_size,
    u32 descriptor_version
);
int os_aarch64_mmu_find_efi_descriptor(
    const void *memory_map,
    usize memory_map_size,
    usize descriptor_size,
    u64 physical_address,
    u64 size,
    efi_memory_descriptor *descriptor
);
int os_aarch64_mmu_install_verified(
    os_aarch64_mmu *mmu,
    const os_aarch64_mmu_live_range *live_ranges,
    usize live_range_count
);
int os_aarch64_mmio_space_init(
    os_aarch64_mmio_space *space,
    os_aarch64_mmu *mmu,
    u64 virtual_base,
    u64 virtual_size
);
int os_aarch64_mmio_space_authorize(
    os_aarch64_mmio_space *space,
    u64 physical_base,
    u64 size,
    u32 owner
);
int os_aarch64_mmio_space_activate(os_aarch64_mmio_space *space);
int os_aarch64_mmio_space_get_mapper(
    os_aarch64_mmio_space *space,
    os_pi5_mmio_mapper *mapper
);

int arm_mmu_read_state(os_aarch64_mmu_arch_state *state);
u64 arm_mmu_translate_current(u64 virtual_address);
int arm_mmu_install(u64 ttbr0, u64 tcr, u64 mair, u64 current_el);
void arm_mmu_invalidate_current(void);

#endif
