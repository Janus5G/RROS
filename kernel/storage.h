#ifndef EXPERIMENTALOS_KERNEL_STORAGE_H
#define EXPERIMENTALOS_KERNEL_STORAGE_H

#include "efi.h"

#define OS_STORAGE_BLOCK_SIZE 512u
#define OS_STORAGE_MAX_FILES 16u
#define OS_STORAGE_PATH_MAX 48u
#define OS_STORAGE_METADATA_BLOCKS 4u
#define OS_STORAGE_DATA_START (2u * OS_STORAGE_METADATA_BLOCKS)

typedef int (*os_storage_read_block)(
    void *context,
    u32 block,
    u8 data[OS_STORAGE_BLOCK_SIZE]
);
typedef int (*os_storage_write_block)(
    void *context,
    u32 block,
    const u8 data[OS_STORAGE_BLOCK_SIZE]
);

typedef struct {
    void *context;
    u32 block_count;
    os_storage_read_block read_block;
    os_storage_write_block write_block;
} os_storage_device;

typedef struct __attribute__((packed)) {
    u8 used;
    char path[OS_STORAGE_PATH_MAX];
    u32 start_block;
    u32 block_count;
    u32 size;
    u32 data_checksum;
} os_storage_entry;

typedef struct __attribute__((packed)) {
    u32 magic;
    u32 version;
    u32 generation;
    u32 entry_count;
    u32 checksum;
    os_storage_entry entries[OS_STORAGE_MAX_FILES];
} os_storage_metadata;

typedef struct {
    os_storage_device device;
    os_storage_metadata metadata;
    u32 active_slot;
    int mounted;
} os_storage_fs;

int os_storage_format(const os_storage_device *device);
int os_storage_mount(os_storage_fs *filesystem, const os_storage_device *device);
int os_storage_write(
    os_storage_fs *filesystem,
    const char *path,
    const void *data,
    usize size
);
int os_storage_read(
    os_storage_fs *filesystem,
    const char *path,
    usize offset,
    void *data,
    usize length,
    usize *bytes_read
);
int os_storage_delete(os_storage_fs *filesystem, const char *path);
int os_storage_exists(
    const os_storage_fs *filesystem,
    const char *path,
    usize *size
);
int os_storage_guest_self_test(void);

#endif
