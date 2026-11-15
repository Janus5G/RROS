#include "efi.h"
#include "aarch64_mmu.h"
#include "aarch64_mmu_uefi.h"
#include "g04.h"
#include "g05.h"
#include "desktop.h"
#ifdef EXPERIMENTALOS_G13_TEST
#include "ai_worker.h"
#endif
#ifdef EXPERIMENTALOS_G19_TEST
const char *os_gfx_guest_self_test(void);
const char *os_gfx_guest_report(void);
#endif
#include "boot_console.h"
#include "device_tree.h"
#include "graphics.h"
#include "memory.h"
#include "network.h"
#include "pi5_driver.h"
#include "storage.h"

static u8 g_memory_map[128 * 1024] __attribute__((aligned(16)));
#ifdef EXPERIMENTALOS_G12_TEST
static os_device_tree g_uefi_device_tree;
static os_pi5_gicv2_platform g12_gic_platform;
static os_page_pool g12_dma_cache_test_pool;
static os_pi5_aarch64_dma_cache g12_dma_cache_test;
static os_page_pool g12_dma_runtime_pool;
static os_pi5_aarch64_dma_cache g12_dma_runtime_cache;
static os_pi5_dma_page_allocator g12_dma_runtime_page_allocator;
static os_pi5_dma_allocator g12_dma_runtime_allocator;
static u8 g_aarch64_mmu_test_tables[8u * OS_AARCH64_MMU_PAGE_SIZE]
    __attribute__((aligned(OS_AARCH64_MMU_PAGE_SIZE)));
static os_aarch64_mmu g_aarch64_mmu_test;
#endif
static void *g_memory_arena;
volatile u32 g_arm_exception_count;
volatile u64 g_arm_exception_esr;
volatile u64 g_arm_exception_far;
volatile u64 g_arm_exception_elr;

extern u64 arm_current_el(void);
extern u64 arm_current_vbar(void);
extern u64 arm_sctlr(void);
extern void arm_install_vectors(void);
extern void arm_restore_vbar(u64 vbar);
extern int arm_g05_prepare(void);
extern void arm_g05_start(void);

static inline void uart_init(void)
{
    os_boot_console_select_qemu_virt();
}

static inline void uart_write(const char *text)
{
    os_boot_console_write(text);
}

#ifdef EXPERIMENTALOS_G12_TEST
static void uart_write_hex64(u64 value)
{
    static const char digits[] = "0123456789abcdef";
    char text[19];
    text[0] = '0';
    text[1] = 'x';
    for (usize i = 0; i < 16u; ++i) {
        u32 shift = (u32)((15u - i) * 4u);
        text[i + 2u] = digits[(value >> shift) & 0xfu];
    }
    text[18] = '\0';
    uart_write(text);
}
#endif

void experimentalos_aarch64_uart_write(const char *text)
{
    uart_write(text);
}

static int display_write(
    efi_system_table *system_table,
    const u16 *message
)
{
    if (system_table->console_out == (efi_text_output_protocol *)0
        || system_table->console_out->output_string
            == (efi_output_string)0) {
        return 0;
    }
    return system_table->console_out->output_string(
        system_table->console_out, message) == 0;
}

static __attribute__((noreturn)) void idle(void)
{
    __asm__ volatile("msr daifset, #0xf");
    for (;;) {
        __asm__ volatile("wfi");
    }
}

__attribute__((noreturn)) void experimentalos_aarch64_idle(void)
{
    idle();
}

static __attribute__((noreturn)) void fail(const char *message)
{
    uart_write("EXPERIMENTALOS: G02 FAIL: ");
    uart_write(message);
    uart_write("\r\n");
    idle();
}

#ifdef EXPERIMENTALOS_G12_TEST
static int aarch64_mmu_table_fixture(void)
{
    const u64 virtual_base = 0x0000f00000000000ull;
    const u64 physical_base = 0x0000007c00000000ull;
    u64 translated = 0;
    u64 descriptor = 0;
    if (!os_aarch64_mmu_init(
            &g_aarch64_mmu_test, g_aarch64_mmu_test_tables,
            0x10000000u, sizeof(g_aarch64_mmu_test_tables), 48u)
        || !os_aarch64_mmu_map(
            &g_aarch64_mmu_test, virtual_base, physical_base,
            2u * OS_AARCH64_MMU_PAGE_SIZE, OS_AARCH64_MMU_DEVICE)
        || !os_aarch64_mmu_translate(
            &g_aarch64_mmu_test, virtual_base + 0x1234u,
            &translated, &descriptor)
        || translated != physical_base + 0x1234u
        || ((descriptor >> 2) & 7u) != 0u
        || (descriptor & (1ull << 53)) == 0
        || (descriptor & (1ull << 54)) == 0
        || !os_aarch64_mmu_unmap(
            &g_aarch64_mmu_test, virtual_base,
            2u * OS_AARCH64_MMU_PAGE_SIZE)
        || os_aarch64_mmu_translate(
            &g_aarch64_mmu_test, virtual_base, &translated, 0)) {
        return 0;
    }
    return 1;
}

static int aarch64_dma_cache_fixture(void *arena_page)
{
    u64 physical_page = arm_mmu_translate_current((u64)(usize)arena_page);
    if (physical_page == ~(u64)0
        || (physical_page & (OS_PAGE_SIZE - 1u)) != 0u
        || !os_page_pool_init_physical(
            &g12_dma_cache_test_pool, arena_page, physical_page, 1u, 0u)
        || !os_pi5_aarch64_dma_cache_init(
            &g12_dma_cache_test, &g12_dma_cache_test_pool, 0x12u)) {
        return 0;
    }

    u64 allocated_system_address = 0u;
    void *allocated_page = os_page_alloc_contiguous(
        &g12_dma_cache_test_pool, 0x12u, 1u, 1u,
        &allocated_system_address);
    if (allocated_page != arena_page
        || allocated_system_address != physical_page
        || os_pi5_aarch64_dma_sync_for_device(
            &g12_dma_cache_test, (u8 *)allocated_page + 1u,
            allocated_system_address + 1u, OS_PAGE_SIZE - 1u)
        || os_pi5_aarch64_dma_sync_for_device(
            &g12_dma_cache_test, allocated_page,
            allocated_system_address + OS_PAGE_SIZE, OS_PAGE_SIZE)) {
        return 0;
    }
    g12_dma_cache_test_pool.owners[0] = 0x13u;
    int wrong_owner_rejected = !os_pi5_aarch64_dma_sync_for_device(
        &g12_dma_cache_test, allocated_page,
        allocated_system_address, OS_PAGE_SIZE);
    g12_dma_cache_test_pool.owners[0] = 0x12u;
    if (!wrong_owner_rejected
        || !os_pi5_aarch64_dma_sync_for_device(
            &g12_dma_cache_test, allocated_page,
            allocated_system_address, OS_PAGE_SIZE)
        || !os_pi5_aarch64_dma_sync_for_cpu(
            &g12_dma_cache_test, allocated_page,
            allocated_system_address, OS_PAGE_SIZE)
        || !os_page_free_contiguous(
            &g12_dma_cache_test_pool, allocated_page, 1u, 0x12u)) {
        return 0;
    }

    os_pi5_dma_page_allocator page_allocator;
    os_pi5_dma_allocator allocator;
    if (!os_pi5_dma_page_allocator_init_aarch64(
            &allocator, &page_allocator, &g12_dma_cache_test_pool,
            0x12u, &g12_dma_cache_test)) {
        return 0;
    }
    os_pi5_dma_buffer buffer;
    os_pi5_dma_buffer_init(&buffer);
    if (!allocator.allocate(
            allocator.context, OS_PAGE_SIZE, OS_PAGE_SIZE,
            &buffer.virtual_address, &buffer.system_address)) {
        return 0;
    }
    buffer.size = OS_PAGE_SIZE;
    buffer.active = 1u;
    int cache_sync_passed =
        os_pi5_dma_sync_for_device(&allocator, &buffer)
        && os_pi5_dma_sync_for_cpu(&allocator, &buffer);
    int buffer_released = os_pi5_dma_free(&allocator, &buffer);
    return cache_sync_passed && buffer_released
        && !buffer.active
        && os_page_free_count(&g12_dma_cache_test_pool) == 1u;
}

static int aarch64_dma_runtime_fixture(
    void *arena,
    usize page_count
)
{
    if (arena == (void *)0 || page_count == 0u
        || page_count > OS_MAX_POOL_PAGES) {
        return 0;
    }

    u64 virtual_base = (u64)(usize)arena;
    u64 physical_base = arm_mmu_translate_current(virtual_base);
    u64 last_offset = (page_count - 1u) * OS_AARCH64_MMU_PAGE_SIZE;
    if (physical_base == ~(u64)0
        || last_offset > ~(u64)0 - physical_base
        || arm_mmu_translate_current(virtual_base + last_offset)
            != physical_base + last_offset
        || !os_page_pool_init_physical(
            &g12_dma_runtime_pool, arena, physical_base, page_count, 0u)
        || !os_pi5_aarch64_dma_cache_init(
            &g12_dma_runtime_cache, &g12_dma_runtime_pool, 0x12u)
        || !os_pi5_dma_page_allocator_init_aarch64(
            &g12_dma_runtime_allocator,
            &g12_dma_runtime_page_allocator, &g12_dma_runtime_pool,
            0x12u, &g12_dma_runtime_cache)) {
        return 0;
    }

    os_pi5_dma_buffer buffer;
    os_pi5_dma_buffer_init(&buffer);
    if (!g12_dma_runtime_allocator.allocate(
            g12_dma_runtime_allocator.context, OS_PAGE_SIZE, OS_PAGE_SIZE,
            &buffer.virtual_address, &buffer.system_address)) {
        return 0;
    }
    buffer.size = OS_PAGE_SIZE;
    buffer.active = 1u;
    int cache_sync_passed =
        os_pi5_dma_sync_for_device(&g12_dma_runtime_allocator, &buffer)
        && os_pi5_dma_sync_for_cpu(&g12_dma_runtime_allocator, &buffer);
    int buffer_released =
        os_pi5_dma_free(&g12_dma_runtime_allocator, &buffer);
    return cache_sync_passed && buffer_released && !buffer.active
        && os_page_free_count(&g12_dma_runtime_pool) == page_count;
}

static int aarch64_dma_pool_type_rejection_fixture(
    const efi_loaded_image_protocol *loaded_image,
    void *table_pool,
    u64 table_pool_size,
    void *dma_pool,
    u64 dma_pool_size,
    const os_device_tree *tree,
    void *memory_map,
    usize memory_map_size,
    usize descriptor_size,
    u32 descriptor_version
)
{
    if (loaded_image == (const efi_loaded_image_protocol *)0
        || table_pool == (void *)0 || dma_pool == (void *)0
        || tree == (const os_device_tree *)0
        || memory_map == (void *)0
        || descriptor_size < sizeof(efi_memory_descriptor)
        || memory_map_size == 0u
        || memory_map_size % descriptor_size != 0u) {
        return 0;
    }

    u64 dma_pool_physical =
        arm_mmu_translate_current((u64)(usize)dma_pool);
    if (dma_pool_physical == ~(u64)0) {
        return 0;
    }
    efi_memory_descriptor *pool_descriptor =
        (efi_memory_descriptor *)0;
    usize descriptor_count = memory_map_size / descriptor_size;
    for (usize i = 0; i < descriptor_count; ++i) {
        efi_memory_descriptor *descriptor = (efi_memory_descriptor *)(
            (u8 *)memory_map + i * descriptor_size);
        u64 descriptor_bytes =
            descriptor->number_of_pages * OS_AARCH64_MMU_PAGE_SIZE;
        if (dma_pool_physical >= descriptor->physical_start
            && dma_pool_physical - descriptor->physical_start
                < descriptor_bytes) {
            pool_descriptor = descriptor;
            break;
        }
    }
    if (pool_descriptor == (efi_memory_descriptor *)0
        || pool_descriptor->type != 2u) {
        return 0;
    }

    u32 original_type = pool_descriptor->type;
    pool_descriptor->type = 4u;
    u32 status = os_aarch64_mmu_uefi_prepare(
        loaded_image, table_pool, table_pool_size, dma_pool,
        dma_pool_size, tree, memory_map, memory_map_size,
        descriptor_size, descriptor_version);
    pool_descriptor->type = original_type;
    return status == OS_AARCH64_MMU_UEFI_PREPARE_DMA_POOL;
}
#endif

efi_status efi_main(void *image_handle, efi_system_table *system_table)
{
    static const u16 fault_diagnostics_message[] = {
        'G', '0', '2', ' ', 'E', 'X', 'C', 'E', 'P', 'T', 'I', 'O', 'N', ' ',
        'V', 'E', 'C', 'T', 'O', 'R', ' ', 'P', 'A', 'S', 'S', '\r', '\n',
        'G', '0', '3', ' ', 'F', 'A', 'U', 'L', 'T', ' ',
        'D', 'I', 'A', 'G', 'N', 'O', 'S', 'T', 'I', 'C', 'S', ' ',
        'P', 'A', 'S', 'S', '\r', '\n', 0
    };
    static const u16 memory_ownership_message[] = {
        'G', '0', '3', ' ', 'M', 'E', 'M', 'O', 'R', 'Y', ' ',
        'O', 'W', 'N', 'E', 'R', 'S', 'H', 'I', 'P', ' ',
        'P', 'A', 'S', 'S', '\r', '\n', 0
    };
    if (system_table == (efi_system_table *)0
        || system_table->boot_services == (efi_boot_services *)0) {
        fail("invalid UEFI system table");
    }
    uart_init();

    static const u16 started[] = {
        'E', 'x', 'p', 'e', 'r', 'i', 'm', 'e', 'n', 't', 'a', 'l', 'O', 'S',
        ' ', 'G', '0', '2', ':', ' ', 'A', 'A', 'r', 'c', 'h', '6', '4',
        ' ', 's', 't', 'a', 'r', 't', '\r', '\n', 0
    };
    if (!display_write(system_table, started)) {
        fail("UEFI text output failed");
    }
    uart_write("EXPERIMENTALOS: G02 AARCH64 UEFI ENTRY\r\n");

    u64 current_el = arm_current_el();
    if (current_el < 1 || current_el > 2) {
        fail("unsupported exception level");
    }
    uart_write("EXPERIMENTALOS: G02 EXCEPTION LEVEL VALIDATED\r\n");

    if ((arm_sctlr() & 1u) == 0) {
        fail("firmware MMU is not enabled");
    }
    uart_write("EXPERIMENTALOS: G02 MMU STATE VALIDATED\r\n");

    u64 previous_daif = 0;
    __asm__ volatile(
        "mrs %0, daif\n\t"
        "msr daifset, #0xf"
        : "=r"(previous_daif)
        :
        : "memory"
    );
    u64 previous_vbar = arm_current_vbar();
    arm_install_vectors();
    __asm__ volatile("brk #0" : : : "x16", "x17", "memory");
    arm_restore_vbar(previous_vbar);
    __asm__ volatile("msr daif, %0" : : "r"(previous_daif) : "memory");
    if (g_arm_exception_count == 0) {
        fail("synchronous exception vector was not entered");
    }
    if (g_arm_exception_count != 1) {
        fail("synchronous exception vector ran more than once");
    }
    if (((g_arm_exception_esr >> 26) & 0x3fu) != 0x3cu
        || g_arm_exception_elr == 0) {
        fail("synchronous exception diagnostics are invalid");
    }
    if (!display_write(system_table, fault_diagnostics_message)) {
        fail("UEFI fault-diagnostics output failed");
    }

    efi_boot_services *boot_services = system_table->boot_services;
    if (boot_services->allocate_pool == (efi_allocate_pool)0
        || boot_services->get_memory_map == (efi_get_memory_map)0
        || boot_services->exit_boot_services == (efi_exit_boot_services)0) {
        fail("missing UEFI memory-map services");
    }

    if (boot_services->allocate_pool(
            2, 65u * OS_PAGE_SIZE, &g_memory_arena) != 0
        || g_memory_arena == (void *)0) {
        fail("unable to reserve the memory test arena");
    }
    usize arena_start = ((usize)g_memory_arena + OS_PAGE_SIZE - 1u)
        & ~(usize)(OS_PAGE_SIZE - 1u);
    int memory_test_status = arena_start < (usize)g_memory_arena
        ? 1 : os_memory_self_test(
            (void *)arena_start, 64u * OS_PAGE_SIZE);
    if (memory_test_status != 0) {
        u16 memory_failure_message[] = {
            'G', '0', '3', ' ', 'M', 'E', 'M', 'O', 'R', 'Y', ' ',
            'T', 'E', 'S', 'T', ' ', 'F', 'A', 'I', 'L', ':', ' ',
            '0', 0
        };
        memory_failure_message[22] = (u16)('0' + memory_test_status);
        (void)display_write(system_table, memory_failure_message);
        fail("kernel memory self-test failed");
    }
    if (!display_write(system_table, memory_ownership_message)) {
        fail("UEFI memory-ownership output failed");
    }

#ifdef EXPERIMENTALOS_G04_TEST
    efi_boot_services *g04_boot_services = system_table->boot_services;
    if (!os_g04_device_info_test(system_table)
        || !os_g04_write_marker(system_table, "G04 DEVICE INFO PASS")) {
        fail("UEFI console or device information validation failed");
    }
    if (!os_g04_timer_test(g04_boot_services)
        || !os_g04_write_marker(system_table, "G04 TIMER PASS")) {
        fail("UEFI timer event validation failed");
    }
    if (!os_g04_write_marker(system_table, "G04 INPUT READY")
        || !os_g04_wait_for_key_test(system_table, g04_boot_services, 'a')
        || !os_g04_write_marker(system_table, "G04 KEYBOARD INPUT PASS")) {
        fail("keyboard input validation failed");
    }
#endif
#ifdef EXPERIMENTALOS_G08_TEST
    uart_write("EXPERIMENTALOS: G08 INPUT READY\r\n");
    if (!os_g04_wait_for_key_test(
            system_table, boot_services, (u16)'g')) {
        fail("G08 injected graphical input was not received");
    }
    if (!os_graphics_prepare_guest(boot_services)) {
        fail("G08 GOP framebuffer preparation failed");
    }
#endif
#ifdef EXPERIMENTALOS_G10_TEST
    uart_write("EXPERIMENTALOS: G10 INPUT READY\r\n");
    if (!os_g04_wait_for_key_test(
            system_table, boot_services, (u16)'d')) {
        fail("G10 injected desktop activation key was not received");
    }
    if (!os_graphics_prepare_guest(boot_services)) {
        fail("G10 GOP framebuffer preparation failed");
    }
#endif
#ifdef EXPERIMENTALOS_G11_TEST
    uart_write("EXPERIMENTALOS: G11 INPUT READY\r\n");
    if (!os_g04_wait_for_key_test(
            system_table, boot_services, (u16)'f')) {
        fail("G11 injected Flow activation key was not received");
    }
    if (!os_graphics_prepare_guest(boot_services)) {
        fail("G11 GOP framebuffer preparation failed");
    }
#endif
#ifdef EXPERIMENTALOS_G13_TEST
    uart_write("EXPERIMENTALOS: G13 INPUT READY\r\n");
    if (!os_g04_wait_for_key_test(
            system_table, boot_services, (u16)'n')) {
        fail("G13 injected activation key was not received");
    }
#endif
#ifdef EXPERIMENTALOS_G19_TEST
    uart_write("EXPERIMENTALOS: G19 INPUT READY\r\n");
    if (!os_g04_wait_for_key_test(
            system_table, boot_services, (u16)'u')) {
        fail("G19 injected activation key was not received");
    }
#endif
#ifdef EXPERIMENTALOS_G12_TEST
    uart_write("EXPERIMENTALOS: G12 INPUT READY\r\n");
    if (!os_g04_wait_for_key_test(
            system_table, boot_services, (u16)'p')) {
        fail("G12 injected device-discovery trigger was not received");
    }
    const efi_loaded_image_protocol *g12_loaded_image =
        (const efi_loaded_image_protocol *)0;
    if (!os_aarch64_mmu_uefi_get_loaded_image(
            image_handle, boot_services, &g12_loaded_image)) {
        fail("G12 loaded-image protocol is unavailable");
    }
    int g12_fdt_discovered = os_device_tree_discover_uefi(
        system_table, &g_uefi_device_tree);
    if (boot_services->allocate_pages == (efi_allocate_pages)0
        || boot_services->free_pages == (efi_free_pages)0) {
        fail("G12 UEFI page allocation services are unavailable");
    }
    u64 g12_table_pool_address = 0u;
    const usize g12_table_pool_pages = 128u;
    if (boot_services->allocate_pages(
            0u, 2u, g12_table_pool_pages,
            &g12_table_pool_address) != 0
        || g12_table_pool_address == 0u) {
        fail("G12 page-table pool allocation failed");
    }
#endif

    usize map_size = sizeof(g_memory_map);
    usize map_key = 0;
    usize descriptor_size = 0;
    u32 descriptor_version = 0;
    efi_status status = boot_services->get_memory_map(
        &map_size,
        g_memory_map,
        &map_key,
        &descriptor_size,
        &descriptor_version
    );
    if (status != 0 || descriptor_size < 40 || map_size == 0
        || map_size > sizeof(g_memory_map) || map_size % descriptor_size != 0) {
#ifdef EXPERIMENTALOS_G12_TEST
        if (boot_services->free_pages(
                g12_table_pool_address, g12_table_pool_pages) != 0) {
            fail("G12 invalid memory map and page-pool rollback both failed");
        }
#endif
        fail("invalid or unavailable UEFI memory map");
    }
#ifdef EXPERIMENTALOS_G12_TEST
    if (!os_aarch64_mmu_validate_efi_memory_map(
            g_memory_map, map_size, descriptor_size, descriptor_version)) {
        if (boot_services->free_pages(
                g12_table_pool_address, g12_table_pool_pages) != 0) {
            fail("G12 malformed memory map and page-pool rollback both failed");
        }
        fail("G12 UEFI memory descriptor validation failed");
    }
    if (!aarch64_dma_pool_type_rejection_fixture(
            g12_loaded_image,
            (void *)(usize)g12_table_pool_address,
            g12_table_pool_pages * OS_AARCH64_MMU_PAGE_SIZE,
            (void *)arena_start, 64u * OS_AARCH64_MMU_PAGE_SIZE,
            &g_uefi_device_tree, g_memory_map, map_size,
            descriptor_size, descriptor_version)) {
        fail("G12 DMA page-pool mapping did not reject non-LoaderData memory");
    }
    uart_write(
        "EXPERIMENTALOS: G12 AARCH64 DMA POOL TYPE REJECTION PASS\r\n");
    u32 mmu_prepare_status = os_aarch64_mmu_uefi_prepare(
            g12_loaded_image,
            (void *)(usize)g12_table_pool_address,
            g12_table_pool_pages * OS_AARCH64_MMU_PAGE_SIZE,
            (void *)arena_start,
            64u * OS_AARCH64_MMU_PAGE_SIZE,
            &g_uefi_device_tree,
            g_memory_map, map_size, descriptor_size,
            descriptor_version);
    if (mmu_prepare_status != OS_AARCH64_MMU_UEFI_PREPARE_OK) {
        if (boot_services->free_pages(
                g12_table_pool_address, g12_table_pool_pages) != 0) {
            fail("G12 failed to release an uninstalled page-table pool");
        }
        if (mmu_prepare_status == OS_AARCH64_MMU_UEFI_PREPARE_ARCH_STATE) {
            fail("G12 active EL or physical address width is unsupported");
        }
        if (mmu_prepare_status == OS_AARCH64_MMU_UEFI_PREPARE_TABLE_POOL) {
            fail("G12 page-table pool is not currently translated");
        }
        if (mmu_prepare_status
            == OS_AARCH64_MMU_UEFI_PREPARE_TABLE_POOL_NONIDENTITY) {
            u64 pool_virtual = 0;
            u64 pool_physical = 0;
            os_aarch64_mmu_uefi_get_pool_translation(
                &pool_virtual, &pool_physical);
            uart_write("G12 table pool VA=");
            uart_write_hex64(pool_virtual);
            uart_write(" translated PA=");
            uart_write_hex64(pool_physical);
            uart_write("\r\n");
            fail("G12 UEFI page allocation has no validated accessible VA");
        }
        if (mmu_prepare_status
            == OS_AARCH64_MMU_UEFI_PREPARE_TABLE_POOL_NONCONTIGUOUS) {
            fail("G12 page-table pool pages are not physically contiguous");
        }
        if (mmu_prepare_status
                == OS_AARCH64_MMU_UEFI_PREPARE_TABLE_POOL_LAYOUT
            || mmu_prepare_status
                == OS_AARCH64_MMU_UEFI_PREPARE_TABLE_POOL_RESOURCES) {
            fail("G12 page-table pool memory descriptor or layout is invalid");
        }
        if (mmu_prepare_status == OS_AARCH64_MMU_UEFI_PREPARE_IMAGE) {
            fail("G12 loaded-image sections or translations are invalid");
        }
        if (mmu_prepare_status == OS_AARCH64_MMU_UEFI_PREPARE_STACK) {
            fail("G12 live stack mapping is not safely reproducible");
        }
        if (mmu_prepare_status == OS_AARCH64_MMU_UEFI_PREPARE_UART) {
            fail("G12 active UART resource is not verified");
        }
        if (mmu_prepare_status
            == OS_AARCH64_MMU_UEFI_PREPARE_MMIO_WINDOW) {
            fail("G12 MMIO virtual window is invalid");
        }
        if (mmu_prepare_status
            == OS_AARCH64_MMU_UEFI_PREPARE_DMA_POOL) {
            fail("G12 DMA page-pool mapping is not safely reproducible");
        }
        fail("G12 AArch64 MMU handoff input validation failed");
    }
    if (!aarch64_dma_cache_fixture((void *)arena_start)) {
        fail("G12 AArch64 DMA cache-maintenance fixture failed");
    }
    uart_write("EXPERIMENTALOS: G12 AARCH64 DMA CACHE MAINTENANCE PASS\r\n");
    uart_write(
        "EXPERIMENTALOS: G12 AARCH64 DMA PRE-EXIT PAGE ALLOCATOR PASS\r\n");
#endif

    if (boot_services->exit_boot_services(image_handle, map_key) != 0) {
#ifdef EXPERIMENTALOS_G12_TEST
        if (boot_services->free_pages(
                g12_table_pool_address, g12_table_pool_pages) != 0) {
            fail("G12 ExitBootServices and page-pool rollback both failed");
        }
#endif
        fail("ExitBootServices failed");
    }

#ifdef EXPERIMENTALOS_G12_TEST
    u64 firmware_vbar = arm_current_vbar();
    arm_install_vectors();
    if (!os_aarch64_mmu_uefi_install()) {
        arm_restore_vbar(firmware_vbar);
        fail("G12 validated AArch64 MMU installation failed");
    }
    if (!os_aarch64_mmu_uefi_mmio_self_test()) {
        fail("G12 active non-identity MMIO mapping lifecycle failed");
    }
    if (!aarch64_dma_runtime_fixture(
            (void *)arena_start, 64u)) {
        fail("G12 post-handoff DMA page allocator/cache fixture failed");
    }
    uart_write(
        "EXPERIMENTALOS: G12 AARCH64 DMA RUNTIME ALLOCATOR PASS\r\n");
    int g12_gic_available = 0;
    for (usize i = 0; i < g_uefi_device_tree.node_count; ++i) {
        const os_device_tree_node *node = &g_uefi_device_tree.nodes[i];
        if (node->enabled
            && os_device_tree_node_has_compatible(
                node, "arm,gic-400")) {
            g12_gic_available = 1;
            break;
        }
    }
    if (g12_fdt_discovered && g12_gic_available) {
        os_pi5_mmio_mapper g12_mmio_mapper;
        os_pi5_gicv2_platform_init_state(&g12_gic_platform);
        __asm__ volatile("msr daifset, #2" : : : "memory");
        if (!os_aarch64_mmu_uefi_get_mapper(&g12_mmio_mapper)
            || !os_pi5_gicv2_platform_init_current_cpu(
                &g12_gic_platform, &g_uefi_device_tree,
                &g12_mmio_mapper)) {
            fail("G12 FDT-authorized GIC mapping or CPU routing failed");
        }
        if (!os_pi5_gicv2_platform_start(&g12_gic_platform)) {
            if (!os_pi5_gicv2_platform_cleanup(&g12_gic_platform)) {
                fail("G12 GIC startup failed and platform rollback failed");
            }
            fail("G12 GIC distributor or CPU-interface startup failed");
        }
        uart_write("EXPERIMENTALOS: G12 FDT GIC MMIO START PASS\r\n");
    } else {
        uart_write("EXPERIMENTALOS: G12 GIC START NOT VERIFIED\r\n");
    }
    uart_write("EXPERIMENTALOS: G12 AARCH64 MMU INSTALL PASS\r\n");
    uart_write("EXPERIMENTALOS: G12 ACTIVE MMIO MAP PASS\r\n");
#endif
    uart_init();
    uart_write("EXPERIMENTALOS: G02 MEMORY MAP VALIDATED\r\n");
    uart_write("EXPERIMENTALOS: G02 UEFI HANDOFF PASS\r\n");
    (void)descriptor_version;
#ifdef EXPERIMENTALOS_G08_TEST
    if (!os_graphics_guest_self_test()) {
        fail("G08 framebuffer/window/compositor self-test failed");
    }
    uart_write("EXPERIMENTALOS: G08 FRAMEBUFFER WINDOW COMPOSITOR PASS\r\n");
    uart_write("EXPERIMENTALOS: G08 INPUT EVENTS PASS\r\n");
#endif
#ifdef EXPERIMENTALOS_G10_TEST
    os_framebuffer desktop_framebuffer;
    if (!os_graphics_guest_framebuffer(&desktop_framebuffer)
        || !os_classic_desktop_self_test(&desktop_framebuffer, (u16)'d')) {
        fail("G10 Classic desktop lifecycle self-test failed");
    }
    uart_write("EXPERIMENTALOS: G10 CLASSIC DESKTOP PASS\r\n");
    uart_write("EXPERIMENTALOS: G10 LAUNCH INPUT RESIZE CLOSE CLEANUP PASS\r\n");
#endif
#ifdef EXPERIMENTALOS_G11_TEST
    os_framebuffer flow_framebuffer;
    if (!os_graphics_guest_framebuffer(&flow_framebuffer)
        || !os_flow_desktop_self_test(&flow_framebuffer, (u16)'f')) {
        fail("G11 Flow desktop self-test failed");
    }
    uart_write("EXPERIMENTALOS: G11 FLOW DESKTOP PASS\r\n");
    uart_write("EXPERIMENTALOS: G11 DOCK SWITCH THEME SERVICES PASS\r\n");
#endif
#ifdef EXPERIMENTALOS_G13_TEST
    {
        const char *g13_failure = os_ai_core_self_test();
        if (g13_failure != (const char *)0) {
            uart_write("EXPERIMENTALOS: G13 FAILED: ");
            uart_write(g13_failure);
            uart_write("\r\n");
            fail("G13 process-isolated AI worker self-test failed");
        }
    }
    uart_write("EXPERIMENTALOS: G13 AI WORKER ISOLATION PASS\r\n");
    uart_write("EXPERIMENTALOS: G13 INFERENCE CANCEL CAPABILITY PASS\r\n");
#endif
#ifdef EXPERIMENTALOS_G19_TEST
    {
        const char *g19_failure = os_gfx_guest_self_test();
        if (g19_failure != (const char *)0) {
            uart_write("EXPERIMENTALOS: G19 FAILED: ");
            uart_write(g19_failure);
            uart_write("\r\n");
            fail("G19 graphics pipeline self-test failed");
        }
        uart_write("EXPERIMENTALOS: G19 MEASURE ");
        uart_write(os_gfx_guest_report());
        uart_write("\r\n");
    }
    uart_write("EXPERIMENTALOS: G19 GFX DAMAGE BUFFER SYNC PASS\r\n");
    uart_write("EXPERIMENTALOS: G19 GPU FALLBACK FAIL-CLOSED PASS\r\n");
#endif
#ifdef EXPERIMENTALOS_G12_TEST
    if (!aarch64_mmu_table_fixture()) {
        fail("G12 AArch64 MMU table-construction fixture failed");
    }
    uart_write("EXPERIMENTALOS: G12 AARCH64 MMU TABLE FIXTURE PASS\r\n");
    if (!os_device_tree_self_test()) {
        fail("G12 device-tree parser and capability fixtures failed");
    }
    uart_write("EXPERIMENTALOS: G12 RESOURCE GIC DMA BCM2712 PCIE RP1 MSIX IRQ GPIO BINDING PL011 IRQ I2C SPI FIXTURES PASS\r\n");
    if (g12_fdt_discovered) {
        uart_write("EXPERIMENTALOS: G12 UEFI DEVICE TREE DISCOVERED\r\n");
    } else {
        uart_write("EXPERIMENTALOS: G12 UEFI DEVICE TREE UNAVAILABLE\r\n");
    }
    uart_write("EXPERIMENTALOS: G12 FDT BOUNDS DISCOVERY PASS\r\n");
    uart_write("EXPERIMENTALOS: G12 HARDWARE DRIVERS NOT VERIFIED\r\n");
#endif
#ifdef EXPERIMENTALOS_G06_TEST
    if (!os_storage_guest_self_test()) {
        fail("G06 storage self-test failed");
    }
    uart_write("EXPERIMENTALOS: G06 RAM FILESYSTEM PASS\r\n");
#endif
#ifdef EXPERIMENTALOS_G07_TEST
    if (!os_network_self_test()) {
        fail("G07 network self-test failed");
    }
    uart_write("EXPERIMENTALOS: G07 VM NETWORK DRIVER PASS\r\n");
    uart_write("EXPERIMENTALOS: G07 DHCP DNS TCP UDP PASS\r\n");
#endif
#ifdef EXPERIMENTALOS_G05_TEST
    if (!arm_g05_prepare()) {
        fail("G05 protected-process setup failed");
    }
    uart_write("EXPERIMENTALOS: G05 ENTERING USER MODE\r\n");
    arm_g05_start();
#endif
#ifdef EXPERIMENTALOS_G09_TEST
    if (!arm_g05_prepare()) {
        fail("G09 native application load failed");
    }
    uart_write("EXPERIMENTALOS: G09 ENTERING LOADED APPLICATION\r\n");
    arm_g05_start();
#endif
    uart_write("EXPERIMENTALOS: G02 IDLE\r\n");
    idle();
}
