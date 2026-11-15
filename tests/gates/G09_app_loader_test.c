#include "app_loader.h"

#include <stdio.h>
#include <string.h>

#define TEST_IMAGE_SIZE 16u
#define TEST_DATA_SIZE 24u

typedef struct __attribute__((packed)) {
    os_app_package_header header;
    u8 image[TEST_IMAGE_SIZE];
} test_package;

static test_package g_package;
static u8 g_executable[TEST_IMAGE_SIZE];
static u8 g_data[TEST_DATA_SIZE];

static void reset_package(u32 architecture)
{
    memset(&g_package, 0, sizeof(g_package));
    for (usize i = 0; i < sizeof(g_package.image); ++i) {
        g_package.image[i] = (u8)(0x40u + i);
    }
    g_package.header.magic = OS_APP_PACKAGE_MAGIC;
    g_package.header.format_version = OS_APP_PACKAGE_FORMAT_VERSION;
    g_package.header.abi_version = OS_NATIVE_ABI_VERSION;
    g_package.header.target_architecture = architecture;
    g_package.header.header_size = sizeof(os_app_package_header);
    g_package.header.image_size = TEST_IMAGE_SIZE;
    g_package.header.entry_offset = 4;
    g_package.header.data_size = TEST_DATA_SIZE;
    g_package.header.capabilities = OS_APP_CAP_SYSCALL;
    g_package.header.image_checksum = os_app_image_checksum(
        g_package.image, sizeof(g_package.image));
    memset(g_executable, 0xa5, sizeof(g_executable));
    memset(g_data, 0xa5, sizeof(g_data));
}

static int load_package(u32 architecture, usize package_size)
{
    os_loaded_application loaded = {0};
    int passed = os_app_load_image(
        &g_package,
        package_size,
        architecture,
        0x100000000ull,
        g_executable,
        sizeof(g_executable),
        g_data,
        sizeof(g_data),
        &loaded
    );
    if (passed) {
        if (loaded.entry_address != 0x100000004ull
            || loaded.image_size != TEST_IMAGE_SIZE
            || loaded.data_size != TEST_DATA_SIZE
            || loaded.target_architecture != architecture
            || loaded.abi_version != OS_NATIVE_ABI_VERSION
            || loaded.capabilities != OS_APP_CAP_SYSCALL
            || memcmp(g_executable, g_package.image, TEST_IMAGE_SIZE) != 0) {
            return 0;
        }
        for (usize i = 0; i < TEST_DATA_SIZE; ++i) {
            if (g_data[i] != 0) {
                return 0;
            }
        }
    } else {
        for (usize i = 0; i < sizeof(g_executable); ++i) {
            if (g_executable[i] != 0xa5u) {
                return 0;
            }
        }
    }
    return passed;
}

static int rejects_invalid_package(void)
{
#define REQUIRE_REJECT(stage) \
    do { \
        if (load_package(OS_APP_ARCH_X86_64, sizeof(g_package))) { \
            fprintf(stderr, "G09 invalid package case %u was accepted\n", \
                (unsigned)(stage)); \
            return 0; \
        } \
    } while (0)
    reset_package(OS_APP_ARCH_X86_64);
    g_package.header.magic ^= 1u;
    REQUIRE_REJECT(1);
    reset_package(OS_APP_ARCH_X86_64);
    g_package.header.magic ^= 1u;
    REQUIRE_REJECT(2);
    reset_package(OS_APP_ARCH_X86_64);
    g_package.header.format_version++;
    REQUIRE_REJECT(3);
    reset_package(OS_APP_ARCH_X86_64);
    g_package.header.abi_version++;
    REQUIRE_REJECT(4);
    reset_package(OS_APP_ARCH_X86_64);
    g_package.header.header_size++;
    REQUIRE_REJECT(5);
    reset_package(OS_APP_ARCH_X86_64);
    g_package.header.image_size++;
    REQUIRE_REJECT(6);
    reset_package(OS_APP_ARCH_X86_64);
    g_package.header.entry_offset = TEST_IMAGE_SIZE;
    REQUIRE_REJECT(7);
    reset_package(OS_APP_ARCH_X86_64);
    g_package.header.flags = 1u;
    REQUIRE_REJECT(8);
    reset_package(OS_APP_ARCH_X86_64);
    g_package.header.capabilities = 0x100ull;
    REQUIRE_REJECT(9);
    reset_package(OS_APP_ARCH_X86_64);
    g_package.image[3] ^= 0x80u;
    REQUIRE_REJECT(10);
    reset_package(OS_APP_ARCH_X86_64);
    if (load_package(OS_APP_ARCH_AARCH64, sizeof(g_package))) {
        fputs("G09 architecture mismatch was accepted\n", stderr);
        return 0;
    }
    reset_package(OS_APP_ARCH_X86_64);
    if (load_package(OS_APP_ARCH_X86_64, sizeof(g_package) - 1u)) {
        fputs("G09 truncated package was accepted\n", stderr);
        return 0;
    }
    reset_package(OS_APP_ARCH_X86_64);
    if (load_package(OS_APP_ARCH_X86_64, sizeof(g_package) + 1u)) {
        fputs("G09 trailing package data was accepted\n", stderr);
        return 0;
    }
    reset_package(OS_APP_ARCH_X86_64);
    os_loaded_application loaded = {0};
    if (os_app_load_image(
            &g_package,
            sizeof(g_package),
            OS_APP_ARCH_X86_64,
            0x100000000ull,
            g_executable,
            sizeof(g_executable),
            g_executable + 8,
            sizeof(g_data),
            &loaded)) {
        fputs("G09 overlapping code/data buffers were accepted\n", stderr);
        return 0;
    }
    reset_package(OS_APP_ARCH_X86_64);
#undef REQUIRE_REJECT
    return 1;
}

int main(void)
{
    reset_package(OS_APP_ARCH_X86_64);
    if (!load_package(OS_APP_ARCH_X86_64, sizeof(g_package))) {
        fputs("FAIL G09-APP-LOADER: valid x86-64 image rejected\n", stderr);
        return 1;
    }
    reset_package(OS_APP_ARCH_AARCH64);
    if (!load_package(OS_APP_ARCH_AARCH64, sizeof(g_package))) {
        fputs("FAIL G09-APP-LOADER: valid AArch64 image rejected\n", stderr);
        return 1;
    }
    if (!rejects_invalid_package()) {
        fputs("FAIL G09-APP-LOADER: invalid image accepted or mutated memory\n",
            stderr);
        return 1;
    }
    puts("PASS G09-APP-LOADER: ABI, architecture, bounds, checksum and W^X buffers");
    return 0;
}
