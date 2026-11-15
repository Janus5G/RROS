#include "storage.h"

#define OS_STORAGE_MAGIC 0x45584653u
#define OS_STORAGE_VERSION 1u
#define OS_STORAGE_METADATA_BYTES \
    (OS_STORAGE_METADATA_BLOCKS * OS_STORAGE_BLOCK_SIZE)

typedef struct {
    u8 bytes[OS_STORAGE_METADATA_BYTES] __attribute__((aligned(8)));
} os_storage_metadata_buffer;

static void zero_bytes(void *memory, usize size)
{
    u8 *bytes = (u8 *)memory;
    for (usize i = 0; i < size; ++i) {
        bytes[i] = 0;
    }
}

static void copy_bytes(void *destination, const void *source, usize size)
{
    u8 *out = (u8 *)destination;
    const u8 *in = (const u8 *)source;
    for (usize i = 0; i < size; ++i) {
        out[i] = in[i];
    }
}

static u32 checksum_update(u32 checksum, const u8 *bytes, usize size)
{
    for (usize i = 0; i < size; ++i) {
        checksum ^= bytes[i];
        checksum *= 16777619u;
    }
    return checksum;
}

static u32 data_checksum(const void *data, usize size)
{
    return checksum_update(2166136261u, (const u8 *)data, size);
}

static int device_valid(const os_storage_device *device)
{
    return device != (const os_storage_device *)0
        && device->read_block != 0
        && device->write_block != 0
        && device->block_count > OS_STORAGE_DATA_START;
}

static int valid_path(const char *path)
{
    if (path == (const char *)0 || path[0] != '/') {
        return 0;
    }
    usize length = 0;
    usize component_start = 1;
    for (; length < OS_STORAGE_PATH_MAX; ++length) {
        char value = path[length];
        if (value == '\0') {
            usize component_length = length - component_start;
            if (length == 1 || path[length - 1u] == '/') {
                return 0;
            }
            return length < OS_STORAGE_PATH_MAX
                && !(component_length == 1u
                    && path[component_start] == '.')
                && !(component_length == 2u
                    && path[component_start] == '.'
                    && path[component_start + 1u] == '.');
        }
        if (value == '\\' || value < 0x20 || value == 0x7f) {
            return 0;
        }
        if (value == '/') {
            usize component_length = length - component_start;
            if (component_length == 0
                || (component_length == 1u
                    && path[component_start] == '.')
                || (component_length == 2u
                    && path[component_start] == '.'
                    && path[component_start + 1u] == '.')) {
                return 0;
            }
            component_start = length + 1u;
        }
    }
    return 0;
}

static int paths_equal(const char *left, const char *right)
{
    for (usize i = 0; i < OS_STORAGE_PATH_MAX; ++i) {
        if (left[i] != right[i]) {
            return 0;
        }
        if (left[i] == '\0') {
            return 1;
        }
    }
    return 0;
}

static int find_entry(const os_storage_metadata *metadata, const char *path)
{
    for (usize i = 0; i < OS_STORAGE_MAX_FILES; ++i) {
        if (metadata->entries[i].used
            && paths_equal(metadata->entries[i].path, path)) {
            return (int)i;
        }
    }
    return -1;
}

static u32 metadata_checksum(os_storage_metadata_buffer *buffer)
{
    u32 *stored_checksum = (u32 *)(
        buffer->bytes + OS_STORAGE_METADATA_BYTES - sizeof(u32));
    *stored_checksum = 0;
    return checksum_update(
        2166136261u, buffer->bytes, OS_STORAGE_METADATA_BYTES);
}

static __attribute__((noinline)) int write_metadata(
    const os_storage_device *device,
    u32 slot,
    const os_storage_metadata *metadata
)
{
    os_storage_metadata_buffer buffer;
    zero_bytes(&buffer, sizeof(buffer));
    copy_bytes(buffer.bytes, metadata, sizeof(*metadata));
    os_storage_metadata *image = (os_storage_metadata *)buffer.bytes;
    image->magic = OS_STORAGE_MAGIC;
    image->version = OS_STORAGE_VERSION;
    image->checksum = 0;
    u32 checksum = metadata_checksum(&buffer);
    copy_bytes(
        buffer.bytes + OS_STORAGE_METADATA_BYTES - sizeof(checksum),
        &checksum, sizeof(checksum));
    for (u32 i = 0; i < OS_STORAGE_METADATA_BLOCKS; ++i) {
        if (!device->write_block(
                device->context,
                slot * OS_STORAGE_METADATA_BLOCKS + i,
                &buffer.bytes[i * OS_STORAGE_BLOCK_SIZE])) {
            return 0;
        }
    }
    return 1;
}

static __attribute__((noinline)) int read_metadata(
    const os_storage_device *device,
    u32 slot,
    os_storage_metadata *metadata
)
{
    os_storage_metadata_buffer buffer;
    for (u32 i = 0; i < OS_STORAGE_METADATA_BLOCKS; ++i) {
        if (!device->read_block(
                device->context,
                slot * OS_STORAGE_METADATA_BLOCKS + i,
                &buffer.bytes[i * OS_STORAGE_BLOCK_SIZE])) {
            return -1;
        }
    }
    os_storage_metadata *image = (os_storage_metadata *)buffer.bytes;
    u32 expected_checksum = 0;
    copy_bytes(
        &expected_checksum,
        buffer.bytes + OS_STORAGE_METADATA_BYTES - sizeof(expected_checksum),
        sizeof(expected_checksum));
    if (image->magic != OS_STORAGE_MAGIC
        || image->version != OS_STORAGE_VERSION) {
        return 0;
    }
    if (metadata_checksum(&buffer) != expected_checksum) {
        return 0;
    }
    if (image->entry_count > OS_STORAGE_MAX_FILES) {
        return 0;
    }
    u32 observed_entry_count = 0;
    for (usize i = 0; i < OS_STORAGE_MAX_FILES; ++i) {
        const os_storage_entry *entry = &image->entries[i];
        if (!entry->used) {
            continue;
        }
        ++observed_entry_count;
        if (!valid_path(entry->path)
            || (u64)entry->size
                > (u64)entry->block_count * OS_STORAGE_BLOCK_SIZE
            || (entry->block_count == 0 && entry->size != 0)
            || (entry->block_count != 0
                && (entry->start_block < OS_STORAGE_DATA_START
                    || entry->start_block >= device->block_count
                    || entry->block_count
                        > device->block_count - entry->start_block))) {
            return 0;
        }
        for (usize j = 0; j < i; ++j) {
            const os_storage_entry *other = &image->entries[j];
            if (other->used
                && (paths_equal(entry->path, other->path)
                    || (entry->block_count != 0
                        && other->block_count != 0
                        && entry->start_block
                            < other->start_block + other->block_count
                        && other->start_block
                            < entry->start_block + entry->block_count))) {
                return 0;
            }
        }
    }
    if (observed_entry_count != image->entry_count) {
        return 0;
    }
    copy_bytes(metadata, image, sizeof(*metadata));
    return 1;
}

int os_storage_format(const os_storage_device *device)
{
    if (!device_valid(device)) {
        return 0;
    }
    os_storage_metadata metadata;
    zero_bytes(&metadata, sizeof(metadata));
    metadata.magic = OS_STORAGE_MAGIC;
    metadata.version = OS_STORAGE_VERSION;
    metadata.generation = 1;
    if (!write_metadata(device, 0, &metadata)) {
        return 0;
    }
    metadata.generation = 2;
    return write_metadata(device, 1, &metadata);
}

int os_storage_mount(os_storage_fs *filesystem, const os_storage_device *device)
{
    if (filesystem == (os_storage_fs *)0 || !device_valid(device)) {
        return 0;
    }
    os_storage_metadata copies[2];
    int valid[2];
    for (u32 slot = 0; slot < 2; ++slot) {
        valid[slot] = read_metadata(device, slot, &copies[slot]);
        if (valid[slot] < 0) {
            return 0;
        }
    }
    if (!valid[0] && !valid[1]) {
        return 0;
    }
    u32 selected = !valid[0] ? 1u
        : (!valid[1] ? 0u
            : (copies[0].generation > copies[1].generation ? 0u : 1u));
    zero_bytes(filesystem, sizeof(*filesystem));
    copy_bytes(&filesystem->device, device, sizeof(*device));
    copy_bytes(
        &filesystem->metadata, &copies[selected],
        sizeof(filesystem->metadata));
    filesystem->active_slot = selected;
    filesystem->mounted = 1;
    return 1;
}

static int sector_used(
    const os_storage_metadata *metadata,
    u32 block,
    u32 ignore_entry
)
{
    for (u32 i = 0; i < OS_STORAGE_MAX_FILES; ++i) {
        const os_storage_entry *entry = &metadata->entries[i];
        if (i != ignore_entry && entry->used && entry->block_count != 0
            && block >= entry->start_block
            && block - entry->start_block < entry->block_count) {
            return 1;
        }
    }
    return 0;
}

static int allocate_extent(
    const os_storage_fs *filesystem,
    u32 block_count,
    u32 *start_block
)
{
    if (block_count == 0) {
        *start_block = 0;
        return 1;
    }
    if (block_count > filesystem->device.block_count - OS_STORAGE_DATA_START) {
        return 0;
    }
    u32 limit = filesystem->device.block_count - block_count;
    for (u32 candidate = OS_STORAGE_DATA_START; candidate <= limit; ++candidate) {
        u32 offset = 0;
        while (offset < block_count
            && !sector_used(&filesystem->metadata, candidate + offset,
                OS_STORAGE_MAX_FILES)) {
            ++offset;
        }
        if (offset == block_count) {
            *start_block = candidate;
            return 1;
        }
        candidate += offset;
    }
    return 0;
}

static __attribute__((noinline)) int commit_metadata(
    os_storage_fs *filesystem,
    const os_storage_metadata *metadata
)
{
    os_storage_metadata next;
    copy_bytes(&next, metadata, sizeof(next));
    next.generation = filesystem->metadata.generation + 1u;
    u32 next_slot = filesystem->active_slot ^ 1u;
    if (!write_metadata(&filesystem->device, next_slot, &next)) {
        return 0;
    }
    copy_bytes(&filesystem->metadata, &next, sizeof(next));
    filesystem->active_slot = next_slot;
    return 1;
}

int os_storage_write(
    os_storage_fs *filesystem,
    const char *path,
    const void *data,
    usize size
)
{
    if (filesystem == (os_storage_fs *)0 || !filesystem->mounted
        || !valid_path(path) || (size != 0 && data == (const void *)0)
        || size > 0xffffffffu
        || size > (usize)(filesystem->device.block_count
            - OS_STORAGE_DATA_START) * OS_STORAGE_BLOCK_SIZE) {
        return 0;
    }
    os_storage_metadata next;
    copy_bytes(&next, &filesystem->metadata, sizeof(next));
    int entry_index = find_entry(&next, path);
    int is_new_entry = entry_index < 0;
    if (is_new_entry) {
        for (usize i = 0; i < OS_STORAGE_MAX_FILES; ++i) {
            if (!next.entries[i].used) {
                entry_index = (int)i;
                break;
            }
        }
    }
    if (entry_index < 0) {
        return 0;
    }
    if (is_new_entry) {
        ++next.entry_count;
    }
    u32 block_count = (u32)((size + OS_STORAGE_BLOCK_SIZE - 1u)
        / OS_STORAGE_BLOCK_SIZE);
    u32 start_block;
    if (!allocate_extent(filesystem, block_count, &start_block)) {
        return 0;
    }
    u8 block_data[OS_STORAGE_BLOCK_SIZE];
    const u8 *input = (const u8 *)data;
    usize copied = 0;
    for (u32 i = 0; i < block_count; ++i) {
        zero_bytes(block_data, sizeof(block_data));
        usize remaining = size - copied;
        usize chunk = remaining < sizeof(block_data)
            ? remaining : sizeof(block_data);
        copy_bytes(block_data, input + copied, chunk);
        if (!filesystem->device.write_block(
                filesystem->device.context, start_block + i, block_data)) {
            return 0;
        }
        copied += chunk;
    }

    os_storage_entry *entry = &next.entries[entry_index];
    zero_bytes(entry, sizeof(*entry));
    entry->used = 1;
    usize path_length = 0;
    while (path[path_length] != '\0') {
        ++path_length;
    }
    copy_bytes(entry->path, path, path_length + 1u);
    entry->start_block = start_block;
    entry->block_count = block_count;
    entry->size = (u32)size;
    entry->data_checksum = data_checksum(data, size);
    return commit_metadata(filesystem, &next);
}

int os_storage_read(
    os_storage_fs *filesystem,
    const char *path,
    usize offset,
    void *data,
    usize length,
    usize *bytes_read
)
{
    if (filesystem == (os_storage_fs *)0 || !filesystem->mounted
        || !valid_path(path) || bytes_read == (usize *)0
        || (length != 0 && data == (void *)0)
        || length > (usize)-1 - offset) {
        return 0;
    }
    int entry_index = find_entry(&filesystem->metadata, path);
    if (entry_index < 0) {
        return 0;
    }
    const os_storage_entry *entry =
        &filesystem->metadata.entries[entry_index];
    if (offset > entry->size) {
        return 0;
    }
    usize available = entry->size - offset;
    usize requested = length < available ? length : available;
    usize request_end = offset + requested;
    usize output_offset = 0;
    u32 checksum = 2166136261u;
    u8 block_data[OS_STORAGE_BLOCK_SIZE];
    for (u32 i = 0; i < entry->block_count; ++i) {
        if (!filesystem->device.read_block(
                filesystem->device.context,
                entry->start_block + i, block_data)) {
            return 0;
        }
        usize block_start = (usize)i * OS_STORAGE_BLOCK_SIZE;
        usize remaining = entry->size - block_start;
        usize content_size = remaining < OS_STORAGE_BLOCK_SIZE
            ? remaining : OS_STORAGE_BLOCK_SIZE;
        checksum = checksum_update(checksum, block_data, content_size);
        usize copy_start = offset > block_start ? offset : block_start;
        usize block_end = block_start + content_size;
        usize copy_end = request_end < block_end ? request_end : block_end;
        if (copy_start < copy_end) {
            usize chunk = copy_end - copy_start;
            copy_bytes(
                (u8 *)data + output_offset,
                block_data + (copy_start - block_start),
                chunk);
            output_offset += chunk;
        }
    }
    if (checksum != entry->data_checksum) {
        return 0;
    }
    *bytes_read = output_offset;
    return 1;
}

int os_storage_delete(os_storage_fs *filesystem, const char *path)
{
    if (filesystem == (os_storage_fs *)0 || !filesystem->mounted
        || !valid_path(path)) {
        return 0;
    }
    os_storage_metadata next;
    copy_bytes(&next, &filesystem->metadata, sizeof(next));
    int entry_index = find_entry(&next, path);
    if (entry_index < 0) {
        return 0;
    }
    zero_bytes(&next.entries[entry_index], sizeof(next.entries[entry_index]));
    --next.entry_count;
    return commit_metadata(filesystem, &next);
}

int os_storage_exists(
    const os_storage_fs *filesystem,
    const char *path,
    usize *size
)
{
    if (filesystem == (const os_storage_fs *)0 || !filesystem->mounted
        || !valid_path(path)) {
        return 0;
    }
    int entry_index = find_entry(&filesystem->metadata, path);
    if (entry_index < 0) {
        return 0;
    }
    if (size != (usize *)0) {
        *size = filesystem->metadata.entries[entry_index].size;
    }
    return 1;
}

static u8 g_guest_volume[64u * OS_STORAGE_BLOCK_SIZE]
    __attribute__((aligned(OS_STORAGE_BLOCK_SIZE)));

static int memory_read_block(
    void *context,
    u32 block,
    u8 data[OS_STORAGE_BLOCK_SIZE]
)
{
    os_storage_device *device = (os_storage_device *)context;
    if (device == (os_storage_device *)0 || block >= device->block_count) {
        return 0;
    }
    copy_bytes(
        data,
        g_guest_volume + (usize)block * OS_STORAGE_BLOCK_SIZE,
        OS_STORAGE_BLOCK_SIZE);
    return 1;
}

static int memory_write_block(
    void *context,
    u32 block,
    const u8 data[OS_STORAGE_BLOCK_SIZE]
)
{
    os_storage_device *device = (os_storage_device *)context;
    if (device == (os_storage_device *)0 || block >= device->block_count) {
        return 0;
    }
    copy_bytes(
        g_guest_volume + (usize)block * OS_STORAGE_BLOCK_SIZE,
        data,
        OS_STORAGE_BLOCK_SIZE);
    return 1;
}

int os_storage_guest_self_test(void)
{
    os_storage_device device = {
        .context = 0,
        .block_count = 64,
        .read_block = memory_read_block,
        .write_block = memory_write_block,
    };
    device.context = &device;
    zero_bytes(g_guest_volume, sizeof(g_guest_volume));
    if (!os_storage_format(&device)) {
        return 0;
    }
    os_storage_fs filesystem;
    if (!os_storage_mount(&filesystem, &device)) {
        return 0;
    }
    static const char content[] = "ExperimentalOS G06 RAM filesystem";
    if (!os_storage_write(
            &filesystem, "/system/readme", content, sizeof(content))) {
        return 0;
    }
    char output[sizeof(content)];
    usize bytes_read = 0;
    if (!os_storage_read(
            &filesystem, "/system/readme", 0, output,
            sizeof(output), &bytes_read)
        || bytes_read != sizeof(content)) {
        return 0;
    }
    for (usize i = 0; i < sizeof(content); ++i) {
        if (output[i] != content[i]) {
            return 0;
        }
    }
    os_storage_fs recovered;
    if (!os_storage_mount(&recovered, &device)
        || !os_storage_exists(&recovered, "/system/readme", 0)
        || !os_storage_delete(&recovered, "/system/readme")
        || os_storage_exists(&recovered, "/system/readme", 0)) {
        return 0;
    }
    return 1;
}
