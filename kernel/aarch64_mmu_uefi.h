#ifndef EXPERIMENTALOS_KERNEL_AARCH64_MMU_UEFI_H
#define EXPERIMENTALOS_KERNEL_AARCH64_MMU_UEFI_H

#include "aarch64_mmu_runtime.h"
#include "device_tree.h"

enum {
    OS_AARCH64_MMU_UEFI_PREPARE_OK = 0,
    OS_AARCH64_MMU_UEFI_PREPARE_INVALID_INPUT = 1,
    OS_AARCH64_MMU_UEFI_PREPARE_ARCH_STATE = 2,
    OS_AARCH64_MMU_UEFI_PREPARE_TABLE_POOL = 3,
    OS_AARCH64_MMU_UEFI_PREPARE_IMAGE = 4,
    OS_AARCH64_MMU_UEFI_PREPARE_STACK = 5,
    OS_AARCH64_MMU_UEFI_PREPARE_UART = 6,
    OS_AARCH64_MMU_UEFI_PREPARE_MMIO_WINDOW = 7,
    OS_AARCH64_MMU_UEFI_PREPARE_TABLE_POOL_NONIDENTITY = 8,
    OS_AARCH64_MMU_UEFI_PREPARE_TABLE_POOL_NONCONTIGUOUS = 9,
    OS_AARCH64_MMU_UEFI_PREPARE_TABLE_POOL_LAYOUT = 10,
    OS_AARCH64_MMU_UEFI_PREPARE_TABLE_POOL_RESOURCES = 11,
    OS_AARCH64_MMU_UEFI_PREPARE_DMA_POOL = 12
};

int os_aarch64_mmu_uefi_get_loaded_image(
    void *image_handle,
    efi_boot_services *boot_services,
    const efi_loaded_image_protocol **loaded_image
);
u32 os_aarch64_mmu_uefi_prepare(
    const efi_loaded_image_protocol *loaded_image,
    void *table_pool_virtual,
    u64 table_pool_size,
    void *dma_pool_virtual,
    u64 dma_pool_size,
    const os_device_tree *tree,
    const void *memory_map,
    usize memory_map_size,
    usize descriptor_size,
    u32 descriptor_version
);
int os_aarch64_mmu_uefi_install(void);
int os_aarch64_mmu_uefi_get_mapper(os_pi5_mmio_mapper *mapper);
int os_aarch64_mmu_uefi_mmio_self_test(void);
void os_aarch64_mmu_uefi_get_pool_translation(u64 *virtual_address, u64 *physical_address);

#endif
