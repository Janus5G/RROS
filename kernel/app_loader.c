#include "app_loader.h"

static void copy_bytes(void *destination, const void *source, usize size)
{
    u8 *out = (u8 *)destination;
    const u8 *in = (const u8 *)source;
    for (usize i = 0; i < size; ++i) {
        out[i] = in[i];
    }
}

static void zero_bytes(void *destination, usize size)
{
    u8 *bytes = (u8 *)destination;
    for (usize i = 0; i < size; ++i) {
        bytes[i] = 0;
    }
}

u32 os_app_image_checksum(const void *image, usize image_size)
{
    if (image == (const void *)0 || image_size == 0) {
        return 0;
    }
    const u8 *bytes = (const u8 *)image;
    u32 checksum = 2166136261u;
    for (usize i = 0; i < image_size; ++i) {
        checksum = (checksum ^ bytes[i]) * 16777619u;
    }
    return checksum;
}

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
)
{
    if (package == (const void *)0
        || package_size < sizeof(os_app_package_header)
        || executable_memory == (void *)0
        || loaded == (os_loaded_application *)0
        || (load_address & 0xfffu) != 0) {
        return 0;
    }

    os_app_package_header header;
    copy_bytes(&header, package, sizeof(header));
    usize executable_start = (usize)executable_memory;
    usize data_start = (usize)data_memory;
    if (header.magic != OS_APP_PACKAGE_MAGIC
        || header.format_version != OS_APP_PACKAGE_FORMAT_VERSION
        || header.abi_version != OS_NATIVE_ABI_VERSION
        || header.target_architecture != target_architecture
        || header.header_size != sizeof(header)
        || header.image_size == 0
        || header.entry_offset >= header.image_size
        || header.flags != 0
        || (header.capabilities & ~OS_APP_KNOWN_CAPABILITIES) != 0
        || header.image_size > package_size - sizeof(header)
        || package_size != sizeof(header) + (usize)header.image_size
        || header.image_size > executable_capacity
        || header.data_size > data_capacity
        || (header.data_size != 0 && data_memory == (void *)0)
        || executable_start > (~(usize)0) - header.image_size
        || (header.data_size != 0
            && (data_start > (~(usize)0) - header.data_size
                || (executable_start < data_start + header.data_size
                    && data_start < executable_start + header.image_size)))
        || load_address > (~(u64)0) - header.entry_offset) {
        return 0;
    }

    const u8 *image = (const u8 *)package + sizeof(header);
    if (os_app_image_checksum(image, header.image_size)
        != header.image_checksum) {
        return 0;
    }

    copy_bytes(executable_memory, image, header.image_size);
    if (header.data_size != 0) {
        zero_bytes(data_memory, header.data_size);
    }
    loaded->entry_address = load_address + header.entry_offset;
    loaded->image_size = header.image_size;
    loaded->data_size = header.data_size;
    loaded->target_architecture = header.target_architecture;
    loaded->abi_version = header.abi_version;
    loaded->capabilities = header.capabilities;
    return 1;
}
