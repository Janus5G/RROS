#include "efi.h"
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
#include "graphics.h"
#include "memory.h"
#include "network.h"
#include "storage.h"

static u8 g_memory_map[64 * 1024] __attribute__((aligned(16)));
static void *g_memory_arena;
volatile u32 g_x86_exception_count;
volatile u64 g_x86_exception_rip;

typedef struct __attribute__((packed)) {
    u16 offset_low;
    u16 selector;
    u8 ist;
    u8 attributes;
    u16 offset_middle;
    u32 offset_high;
    u32 reserved;
} x86_idt_entry;

typedef struct __attribute__((packed)) {
    u16 limit;
    u64 base;
} x86_idtr;

extern void x86_breakpoint_handler(void);
extern int x86_g05_prepare(void);
extern void x86_g05_start(void);

static int validate_breakpoint_handler(void)
{
    x86_idt_entry idt[4] __attribute__((aligned(16))) = {{0}};
    x86_idtr previous_idtr;
    x86_idtr test_idtr = {
        .limit = (u16)(sizeof(idt) - 1u),
        .base = (u64)(usize)idt,
    };
    u16 code_selector = 0;
    u64 handler_address = (u64)(usize)x86_breakpoint_handler;
    u64 previous_flags = 0;

    __asm__ volatile("pushfq; popq %0" : "=r"(previous_flags));
    __asm__ volatile("cli");
    __asm__ volatile("sidt %0" : "=m"(previous_idtr));
    __asm__ volatile("mov %%cs, %0" : "=r"(code_selector));
    idt[3].offset_low = (u16)handler_address;
    idt[3].selector = code_selector;
    idt[3].ist = 0;
    idt[3].attributes = 0x8e;
    idt[3].offset_middle = (u16)(handler_address >> 16);
    idt[3].offset_high = (u32)(handler_address >> 32);
    idt[3].reserved = 0;

    __asm__ volatile("lidt %0" : : "m"(test_idtr) : "memory");
    __asm__ volatile("int3" : : : "memory");
    __asm__ volatile("lidt %0" : : "m"(previous_idtr) : "memory");
    __asm__ volatile("pushq %0; popfq" : : "r"(previous_flags) : "memory");
    return g_x86_exception_count == 1 && g_x86_exception_rip != 0;
}

static inline void outb(u16 port, u8 value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline u8 inb(u16 port)
{
    u8 value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void serial_init(void)
{
    outb(0x3f9, 0x00);
    outb(0x3fb, 0x80);
    outb(0x3f8, 0x01);
    outb(0x3f9, 0x00);
    outb(0x3fb, 0x03);
    outb(0x3fa, 0xc7);
    outb(0x3fc, 0x0b);
}

static int serial_putc(char value)
{
    for (usize attempt = 0; attempt < 100000; ++attempt) {
        if ((inb(0x3fd) & 0x20) != 0) {
            outb(0x3f8, (u8)value);
            return 1;
        }
    }
    return 0;
}

static int serial_write(const char *text)
{
    while (*text != '\0') {
        if (!serial_putc(*text++)) {
            return 0;
        }
    }
    return 1;
}

int experimentalos_serial_write(const char *text)
{
    return serial_write(text);
}

static __attribute__((noreturn)) void idle(void)
{
    __asm__ volatile("cli");
    for (;;) {
        __asm__ volatile("hlt");
    }
}

__attribute__((noreturn)) void experimentalos_x86_idle(void)
{
    idle();
}

static __attribute__((noreturn)) void fail(const char *message)
{
    serial_write("EXPERIMENTALOS: G01 FAIL: ");
    serial_write(message);
    serial_write("\r\n");
    idle();
}

efi_status efi_main(void *image_handle, efi_system_table *system_table)
{
    serial_init();
    serial_write("EXPERIMENTALOS: G01 UEFI ENTRY\r\n");

    if (system_table == (efi_system_table *)0
        || system_table->console_out == (efi_text_output_protocol *)0
        || system_table->console_out->output_string == (efi_output_string)0
        || system_table->boot_services == (efi_boot_services *)0) {
        fail("invalid system table");
    }

    static const u16 display_message[] = {
        'E', 'x', 'p', 'e', 'r', 'i', 'm', 'e', 'n', 't', 'a', 'l', 'O', 'S',
        ' ', 'G', '0', '1', ':', ' ', 'U', 'E', 'F', 'I', ' ', 'h', 'a', 'n',
        'd', 'o', 'f', 'f', ' ', 'p', 'a', 's', 's', 'e', 'd', '\r', '\n', 0
    };
    if (system_table->console_out->output_string(
            system_table->console_out, display_message) != 0) {
        fail("UEFI text output failed");
    }
    serial_write("EXPERIMENTALOS: G01 DISPLAY CONOUT_SUCCESS\r\n");

    if (!validate_breakpoint_handler()) {
        fail("x86 breakpoint diagnostics are invalid");
    }
    serial_write("EXPERIMENTALOS: G03 FAULT DIAGNOSTICS PASS\r\n");

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
    if (arena_start < (usize)g_memory_arena
        || os_memory_self_test((void *)arena_start, 64u * OS_PAGE_SIZE) != 0) {
        fail("kernel memory self-test failed");
    }
    serial_write("EXPERIMENTALOS: G03 MEMORY OWNERSHIP PASS\r\n");

#ifdef EXPERIMENTALOS_G04_TEST
    if (!os_g04_device_info_test(system_table)
        || !os_g04_write_marker(system_table, "G04 DEVICE INFO PASS")) {
        fail("UEFI console or device information validation failed");
    }
    if (!os_g04_timer_test(boot_services)
        || !os_g04_write_marker(system_table, "G04 TIMER PASS")) {
        fail("UEFI timer event validation failed");
    }
    if (!os_g04_write_marker(system_table, "G04 INPUT READY")
        || !os_g04_wait_for_key_test(system_table, boot_services, 'a')
        || !os_g04_write_marker(system_table, "G04 KEYBOARD INPUT PASS")) {
        fail("keyboard input validation failed");
    }
#endif
#ifdef EXPERIMENTALOS_G08_TEST
    serial_write("EXPERIMENTALOS: G08 INPUT READY\r\n");
    if (!os_g04_wait_for_key_test(
            system_table, boot_services, (u16)'g')) {
        fail("G08 injected graphical input was not received");
    }
    if (!os_graphics_prepare_guest(boot_services)) {
        fail("G08 GOP framebuffer preparation failed");
    }
#endif
#ifdef EXPERIMENTALOS_G10_TEST
    serial_write("EXPERIMENTALOS: G10 INPUT READY\r\n");
    if (!os_g04_wait_for_key_test(
            system_table, boot_services, (u16)'d')) {
        fail("G10 injected desktop activation key was not received");
    }
    if (!os_graphics_prepare_guest(boot_services)) {
        fail("G10 GOP framebuffer preparation failed");
    }
#endif
#ifdef EXPERIMENTALOS_G11_TEST
    serial_write("EXPERIMENTALOS: G11 INPUT READY\r\n");
    if (!os_g04_wait_for_key_test(
            system_table, boot_services, (u16)'f')) {
        fail("G11 injected Flow activation key was not received");
    }
    if (!os_graphics_prepare_guest(boot_services)) {
        fail("G11 GOP framebuffer preparation failed");
    }
#endif
#ifdef EXPERIMENTALOS_G13_TEST
    serial_write("EXPERIMENTALOS: G13 INPUT READY\r\n");
    if (!os_g04_wait_for_key_test(
            system_table, boot_services, (u16)'n')) {
        fail("G13 injected activation key was not received");
    }
#endif
#ifdef EXPERIMENTALOS_G19_TEST
    serial_write("EXPERIMENTALOS: G19 INPUT READY\r\n");
    if (!os_g04_wait_for_key_test(
            system_table, boot_services, (u16)'u')) {
        fail("G19 injected activation key was not received");
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
        fail("invalid or unavailable UEFI memory map");
    }

    usize descriptor_count = map_size / descriptor_size;
    status = boot_services->exit_boot_services(image_handle, map_key);
    if (status != 0) {
        fail("ExitBootServices failed");
    }

    serial_write("EXPERIMENTALOS: UEFI BOOT SERVICES EXITED\r\n");
    serial_write("EXPERIMENTALOS: MEMORY MAP VALIDATED\r\n");
    serial_write("EXPERIMENTALOS: G01 UEFI HANDOFF PASS\r\n");
    serial_write("EXPERIMENTALOS: G01 IDLE\r\n");
    (void)descriptor_count;
    (void)descriptor_version;
#ifdef EXPERIMENTALOS_G08_TEST
    if (!os_graphics_guest_self_test()) {
        fail("G08 framebuffer/window/compositor self-test failed");
    }
    serial_write("EXPERIMENTALOS: G08 FRAMEBUFFER WINDOW COMPOSITOR PASS\r\n");
    serial_write("EXPERIMENTALOS: G08 INPUT EVENTS PASS\r\n");
#endif
#ifdef EXPERIMENTALOS_G10_TEST
    os_framebuffer desktop_framebuffer;
    if (!os_graphics_guest_framebuffer(&desktop_framebuffer)
        || !os_classic_desktop_self_test(&desktop_framebuffer, (u16)'d')) {
        fail("G10 Classic desktop lifecycle self-test failed");
    }
    serial_write("EXPERIMENTALOS: G10 CLASSIC DESKTOP PASS\r\n");
    serial_write("EXPERIMENTALOS: G10 LAUNCH INPUT RESIZE CLOSE CLEANUP PASS\r\n");
#endif
#ifdef EXPERIMENTALOS_G11_TEST
    os_framebuffer flow_framebuffer;
    if (!os_graphics_guest_framebuffer(&flow_framebuffer)
        || !os_flow_desktop_self_test(&flow_framebuffer, (u16)'f')) {
        fail("G11 Flow desktop self-test failed");
    }
    serial_write("EXPERIMENTALOS: G11 FLOW DESKTOP PASS\r\n");
    serial_write("EXPERIMENTALOS: G11 DOCK SWITCH THEME SERVICES PASS\r\n");
#endif
#ifdef EXPERIMENTALOS_G13_TEST
    {
        const char *g13_failure = os_ai_core_self_test();
        if (g13_failure != (const char *)0) {
            serial_write("EXPERIMENTALOS: G13 FAILED: ");
            serial_write(g13_failure);
            serial_write("\r\n");
            fail("G13 process-isolated AI worker self-test failed");
        }
    }
    serial_write("EXPERIMENTALOS: G13 AI WORKER ISOLATION PASS\r\n");
    serial_write("EXPERIMENTALOS: G13 INFERENCE CANCEL CAPABILITY PASS\r\n");
#endif
#ifdef EXPERIMENTALOS_G19_TEST
    {
        const char *g19_failure = os_gfx_guest_self_test();
        if (g19_failure != (const char *)0) {
            serial_write("EXPERIMENTALOS: G19 FAILED: ");
            serial_write(g19_failure);
            serial_write("\r\n");
            fail("G19 graphics pipeline self-test failed");
        }
        serial_write("EXPERIMENTALOS: G19 MEASURE ");
        serial_write(os_gfx_guest_report());
        serial_write("\r\n");
    }
    serial_write("EXPERIMENTALOS: G19 GFX DAMAGE BUFFER SYNC PASS\r\n");
    serial_write("EXPERIMENTALOS: G19 GPU FALLBACK FAIL-CLOSED PASS\r\n");
#endif
#ifdef EXPERIMENTALOS_G06_TEST
    if (!os_storage_guest_self_test()) {
        fail("G06 storage self-test failed");
    }
    serial_write("EXPERIMENTALOS: G06 RAM FILESYSTEM PASS\r\n");
#endif
#ifdef EXPERIMENTALOS_G07_TEST
    if (!os_network_self_test()) {
        fail("G07 network self-test failed");
    }
    serial_write("EXPERIMENTALOS: G07 VM NETWORK DRIVER PASS\r\n");
    serial_write("EXPERIMENTALOS: G07 DHCP DNS TCP UDP PASS\r\n");
#endif
#ifdef EXPERIMENTALOS_G05_TEST
    if (!x86_g05_prepare()) {
        fail("G05 protected-process setup failed");
    }
    serial_write("EXPERIMENTALOS: G05 ENTERING USER MODE\r\n");
    x86_g05_start();
#endif
#ifdef EXPERIMENTALOS_G09_TEST
    if (!x86_g05_prepare()) {
        fail("G09 native application load failed");
    }
    serial_write("EXPERIMENTALOS: G09 ENTERING LOADED APPLICATION\r\n");
    x86_g05_start();
#endif
    idle();
}
