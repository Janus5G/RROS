#ifndef EXPERIMENTALOS_KERNEL_APP_LOADER_H
#define EXPERIMENTALOS_KERNEL_APP_LOADER_H

#include "abi.h"

#define OS_APP_PACKAGE_MAGIC 0x41505845u
#define OS_APP_PACKAGE_FORMAT_VERSION 1u
#define OS_APP_ARCH_X86_64 1u
#define OS_APP_ARCH_AARCH64 2u
#define OS_APP_CAP_SYSCALL 0x1ull
#define OS_APP_CAP_THREAD 0x2ull
#define OS_APP_CAP_FILE_READ 0x4ull
#define OS_APP_CAP_NETWORK 0x8ull
#define OS_APP_KNOWN_CAPABILITIES 0xfull

typedef struct __attribute__((packed)) {
    u32 magic;
    u16 format_version;
    u16 abi_version;
    u32 target_architecture;
    u32 header_size;
    u32 image_size;
    u32 entry_offset;
    u32 data_size;
    u32 flags;
    u64 capabilities;
    u32 image_checksum;
} os_app_package_header;

typedef struct {
    u64 entry_address;
    u32 image_size;
    u32 data_size;
    u32 target_architecture;
    u16 abi_version;
    u64 capabilities;
} os_loaded_application;

u32 os_app_image_checksum(const void *image, usize image_size);
int os_app_load_image(
    const void *package,
    usize package_size,
    u32 target_architecture,
    u64 load_address,
    void *executable_memory,
    usize executable_capacity,
    void *data_memory,
    usize data_capacity,
    os_loaded_application *loaded
);

#endif
