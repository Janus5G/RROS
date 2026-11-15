#include "storage.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition, label) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s at line %d\n", label, __LINE__); \
            return 1; \
        } \
    } while (0)

#define TEST_BLOCK_COUNT 64u
#define TEST_VOLUME_PATH "build/g06/disposable-volume.img"

typedef struct {
    FILE *file;
    int fail_after_writes;
    int writes;
} file_block_device;

static int file_read_block(
    void *context,
    u32 block,
    u8 data[OS_STORAGE_BLOCK_SIZE]
)
{
    file_block_device *device = (file_block_device *)context;
    if (device == 0 || device->file == 0 || block >= TEST_BLOCK_COUNT
        || fseek(device->file,
            (long)block * OS_STORAGE_BLOCK_SIZE, SEEK_SET) != 0
        || fread(data, 1, OS_STORAGE_BLOCK_SIZE, device->file)
            != OS_STORAGE_BLOCK_SIZE) {
        return 0;
    }
    return 1;
}

static int file_write_block(
    void *context,
    u32 block,
    const u8 data[OS_STORAGE_BLOCK_SIZE]
)
{
    file_block_device *device = (file_block_device *)context;
    if (device == 0 || device->file == 0 || block >= TEST_BLOCK_COUNT) {
        return 0;
    }
    if (device->fail_after_writes >= 0
        && device->writes >= device->fail_after_writes) {
        ++device->writes;
        return 0;
    }
    ++device->writes;
    if (fseek(device->file,
            (long)block * OS_STORAGE_BLOCK_SIZE, SEEK_SET) != 0
        || fwrite(data, 1, OS_STORAGE_BLOCK_SIZE, device->file)
            != OS_STORAGE_BLOCK_SIZE
        || fflush(device->file) != 0) {
        return 0;
    }
    return 1;
}

static int open_volume(file_block_device *context, const char *mode)
{
    context->file = fopen(TEST_VOLUME_PATH, mode);
    context->fail_after_writes = -1;
    context->writes = 0;
    return context->file != 0;
}

static int prepare_volume(file_block_device *context)
{
    static const u8 zero_block[OS_STORAGE_BLOCK_SIZE] = {0};
    if (!open_volume(context, "w+b")) {
        return 0;
    }
    for (u32 block = 0; block < TEST_BLOCK_COUNT; ++block) {
        if (fwrite(zero_block, 1, sizeof(zero_block), context->file)
            != sizeof(zero_block)) {
            fclose(context->file);
            context->file = 0;
            return 0;
        }
    }
    return fflush(context->file) == 0;
}

static int read_and_compare(
    os_storage_fs *filesystem,
    const char *path,
    const void *expected,
    usize expected_size
)
{
    u8 buffer[1024];
    usize bytes_read = 0;
    if (expected_size > sizeof(buffer)
        || !os_storage_read(
            filesystem, path, 0, buffer, sizeof(buffer), &bytes_read)) {
        fprintf(stderr, "FAIL read %s: storage read/checksum rejected\n", path);
        for (usize i = 0; i < OS_STORAGE_MAX_FILES; ++i) {
            const os_storage_entry *entry = &filesystem->metadata.entries[i];
            if (entry->used) {
                fprintf(stderr,
                    "  entry=%s size=%u blocks=%u start=%u checksum=%08x\n",
                    entry->path,
                    entry->size, entry->block_count, entry->start_block,
                    entry->data_checksum);
                u32 checksum = 2166136261u;
                u32 remaining = entry->size;
                u8 block[OS_STORAGE_BLOCK_SIZE];
                for (u32 j = 0; j < entry->block_count; ++j) {
                    if (!filesystem->device.read_block(
                            filesystem->device.context,
                            entry->start_block + j, block)) {
                        break;
                    }
                    u32 chunk = remaining < OS_STORAGE_BLOCK_SIZE
                        ? remaining : OS_STORAGE_BLOCK_SIZE;
                    for (u32 k = 0; k < chunk; ++k) {
                        checksum ^= block[k];
                        checksum *= 16777619u;
                    }
                    remaining -= chunk;
                }
                fprintf(stderr, "  observed checksum=%08x\n", checksum);
                break;
            }
        }
        return 0;
    }
    if (bytes_read != expected_size
        || memcmp(buffer, expected, expected_size) != 0) {
        int entry_index = -1;
        for (usize i = 0; i < OS_STORAGE_MAX_FILES; ++i) {
            if (filesystem->metadata.entries[i].used
                && strcmp(filesystem->metadata.entries[i].path, path) == 0) {
                entry_index = (int)i;
                break;
            }
        }
        fprintf(stderr,
            "FAIL read %s: size=%zu expected=%zu metadata=%u content mismatch\n",
            path, bytes_read, expected_size,
            entry_index < 0 ? 0u
                : filesystem->metadata.entries[entry_index].size);
        return 0;
    }
    return 1;
}

int main(void)
{
    file_block_device backing = {0};
    CHECK(prepare_volume(&backing), "create disposable virtual volume");
    os_storage_device device = {
        .context = &backing,
        .block_count = TEST_BLOCK_COUNT,
        .read_block = file_read_block,
        .write_block = file_write_block,
    };
    os_storage_device undersized = device;
    undersized.block_count = OS_STORAGE_DATA_START;
    CHECK(!os_storage_format(&undersized), "reject undersized volume");

    os_storage_fs filesystem;
    CHECK(!os_storage_mount(&filesystem, &device),
          "reject unformatted volume");
    CHECK(os_storage_format(&device), "format disposable test volume");
    CHECK(os_storage_mount(&filesystem, &device), "mount formatted volume");
    CHECK(!os_storage_write(
        &filesystem, "relative/path", "x", 1), "reject relative path");
    CHECK(!os_storage_write(
        &filesystem, "/../escape", "x", 1), "reject parent traversal");
    CHECK(!os_storage_write(
        &filesystem, "/dir/./alias", "x", 1), "reject dot component");
    static const u8 oversized[
        (TEST_BLOCK_COUNT - OS_STORAGE_DATA_START)
            * OS_STORAGE_BLOCK_SIZE + 1u
    ] = {0};
    CHECK(!os_storage_write(
        &filesystem, "/data/oversized", oversized, sizeof(oversized)),
        "reject file larger than volume");

    static const char original[] = "preserved before interrupted write";
    static const char replacement[] = "new version committed atomically";
    CHECK(os_storage_write(
        &filesystem, "/data/settings", original, sizeof(original)),
        "write initial file");
    usize size = 0;
    CHECK(os_storage_write(&filesystem, "/data/empty", 0, 0),
          "create empty file");
    CHECK(os_storage_exists(&filesystem, "/data/empty", &size)
        && size == 0, "stat empty file");
    CHECK(os_storage_exists(&filesystem, "/data/settings", &size)
        && size == sizeof(original), "stat existing file");

    char partial[6] = {0};
    usize partial_size = 0;
    CHECK(os_storage_read(
        &filesystem, "/data/settings", 2, partial, 5, &partial_size)
        && partial_size == 5
        && memcmp(partial, original + 2, 5) == 0,
        "read bounded file range");
    CHECK(!os_storage_read(
        &filesystem, "/data/settings", sizeof(original) + 1u,
        partial, sizeof(partial), &partial_size),
        "reject file offset beyond end");
    CHECK(!os_storage_read(
        &filesystem, "/data/missing", 0, partial, sizeof(partial),
        &partial_size), "reject missing file");

    backing.fail_after_writes = 2;
    backing.writes = 0;
    CHECK(!os_storage_write(
        &filesystem, "/data/settings", replacement, sizeof(replacement)),
        "report interrupted metadata commit");
    backing.fail_after_writes = -1;
    CHECK(fclose(backing.file) == 0, "close interrupted volume");
    backing.file = 0;
    CHECK(open_volume(&backing, "r+b"), "reopen interrupted volume");
    CHECK(os_storage_mount(&filesystem, &device),
          "recover last valid metadata snapshot");
    CHECK(read_and_compare(
        &filesystem, "/data/settings", original, sizeof(original)),
        "retain prior file after interrupted write");

    CHECK(os_storage_write(
        &filesystem, "/data/settings", replacement, sizeof(replacement)),
        "commit file replacement");
    static const u8 multi_block[700] = {
        [0] = 0x11,
        [511] = 0x22,
        [512] = 0x33,
        [699] = 0x44,
    };
    CHECK(os_storage_write(
        &filesystem, "/data/multi", multi_block, sizeof(multi_block)),
        "write multi-block file");
    CHECK(fclose(backing.file) == 0, "close committed volume");
    backing.file = 0;
    CHECK(open_volume(&backing, "r+b"), "reopen committed volume");
    CHECK(os_storage_mount(&filesystem, &device),
          "mount persisted metadata after reopen");
    CHECK(read_and_compare(
        &filesystem, "/data/settings", replacement, sizeof(replacement)),
        "persist replacement across reopen");
    CHECK(read_and_compare(
        &filesystem, "/data/multi", multi_block, sizeof(multi_block)),
        "persist multi-block file across reopen");
    CHECK(os_storage_delete(&filesystem, "/data/settings"),
          "delete file transactionally");
    CHECK(fclose(backing.file) == 0, "close volume after delete");
    backing.file = 0;
    CHECK(open_volume(&backing, "r+b"), "reopen volume after delete");
    CHECK(os_storage_mount(&filesystem, &device)
        && !os_storage_exists(&filesystem, "/data/settings", 0),
        "persist deletion across reopen");
    CHECK(os_storage_exists(&filesystem, "/data/empty", 0)
        && os_storage_delete(&filesystem, "/data/empty"),
        "persist empty-file deletion");

    CHECK(os_storage_write(
        &filesystem, "/data/checksum", "checked", 7),
        "write checksummed file");
    int checksum_entry = -1;
    for (usize i = 0; i < OS_STORAGE_MAX_FILES; ++i) {
        if (filesystem.metadata.entries[i].used
            && strcmp(filesystem.metadata.entries[i].path,
                "/data/checksum") == 0) {
            checksum_entry = (int)i;
            break;
        }
    }
    CHECK(checksum_entry >= 0, "locate checksummed file");
    u8 block[OS_STORAGE_BLOCK_SIZE];
    u32 data_block = filesystem.metadata.entries[checksum_entry].start_block;
    CHECK(file_read_block(&backing, data_block, block),
          "read data block for corruption test");
    block[0] ^= 0x80u;
    CHECK(file_write_block(&backing, data_block, block),
          "modify disposable data block");
    u8 checksum_output[8];
    usize checksum_bytes = 0;
    CHECK(!os_storage_read(
        &filesystem, "/data/checksum", 0, checksum_output,
        sizeof(checksum_output), &checksum_bytes),
        "reject modified file data");

    CHECK(os_storage_format(&device), "restore clean disposable volume");
    CHECK(fclose(backing.file) == 0, "close final volume");
    puts("PASS G06-VOLUME-PERSISTENCE: reopen, atomic update and recovery");
    puts("PASS G06-STORAGE-HOST: bounds, file APIs, checksums and failure handling");
    return 0;
}
