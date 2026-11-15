#ifndef EXPERIMENTALOS_KERNEL_AARCH64_MMU_H
#define EXPERIMENTALOS_KERNEL_AARCH64_MMU_H

#include "efi.h"

#define OS_AARCH64_MMU_PAGE_SIZE 4096u
#define OS_AARCH64_MMU_BLOCK_2M (2u * 1024u * 1024u)
#define OS_AARCH64_MMU_BLOCK_1G (1024u * 1024u * 1024u)
#define OS_AARCH64_MMU_TABLE_ENTRIES 512u

#define OS_AARCH64_MMU_DEVICE 0x01u
#define OS_AARCH64_MMU_READ_ONLY 0x02u
#define OS_AARCH64_MMU_EXECUTABLE 0x04u
#define OS_AARCH64_MMU_NORMAL_WB 0x00u
#define OS_AARCH64_MMU_NORMAL_NC 0x08u
#define OS_AARCH64_MMU_NORMAL_WT 0x10u

typedef struct {
    u64 *table_pool;
    u64 table_pool_physical_base;
    usize table_pool_pages;
    usize table_pages_used;
    u8 physical_address_bits;
    u8 installed;
    u64 *rollback_slots[128];
    u64 rollback_previous[128];
} os_aarch64_mmu;

int os_aarch64_mmu_init(
    os_aarch64_mmu *mmu,
    void *table_pool,
    u64 table_pool_physical_base,
    usize table_pool_size,
    u8 physical_address_bits
);
int os_aarch64_mmu_map(
    os_aarch64_mmu *mmu,
    u64 virtual_base,
    u64 physical_base,
    u64 size,
    u32 attributes
);
int os_aarch64_mmu_unmap(
    os_aarch64_mmu *mmu,
    u64 virtual_base,
    u64 size
);
int os_aarch64_mmu_translate(
    const os_aarch64_mmu *mmu,
    u64 virtual_address,
    u64 *physical_address,
    u64 *descriptor
);
u64 os_aarch64_mmu_root_physical(const os_aarch64_mmu *mmu);

#endif
