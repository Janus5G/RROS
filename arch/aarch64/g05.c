#include "g05.h"
#include "app_loader.h"

#define ARM_PAGE_COUNT 512u
#define ARM_TABLE_COUNT 8u
#define ARM_DESCRIPTOR_VALID 0x1ull
#define ARM_DESCRIPTOR_TABLE 0x2ull
#define ARM_DESCRIPTOR_AF (1ull << 10)
#define ARM_DESCRIPTOR_INNER_SHAREABLE (3ull << 8)
#define ARM_DESCRIPTOR_AP_USER_READ_WRITE (1ull << 6)
#define ARM_DESCRIPTOR_AP_READ_ONLY (3ull << 6)
#define ARM_DESCRIPTOR_PXN (1ull << 53)
#define ARM_DESCRIPTOR_UXN (1ull << 54)
#define ARM_DESCRIPTOR_ADDRESS_MASK 0x0000fffffffff000ull
#define ARM_DESCRIPTOR_BLOCK_ADDRESS_MASK 0x0000ffffffe00000ull
#define ARM_USER_CODE OS_G05_USER_CODE
#define ARM_USER_STACK OS_G05_USER_STACK
#define ARM_EC_SVC64 0x15u
#define ARM_EC_DATA_ABORT_LOWER 0x24u

typedef struct __attribute__((aligned(OS_PAGE_SIZE))) {
    u64 entries[ARM_PAGE_COUNT];
} arm_page_table;

typedef struct {
    u64 x[31];
    u64 esr;
    u64 elr;
    u64 spsr;
    u64 sp_el0;
    u64 far;
} arm_user_context;

static arm_page_table g_tables[ARM_TABLE_COUNT];
static u8 g_user_pages[3u * OS_PAGE_SIZE]
    __attribute__((aligned(OS_PAGE_SIZE)));
static struct {
    os_app_package_header header;
    u8 image[OS_PAGE_SIZE];
} g_application_package;
static arm_user_context g_contexts[OS_G05_PROCESS_COUNT];
static u32 g_current_slot;
static u32 g_page_faults;
static u64 g_unexpected_esr;
static u64 g_unexpected_far;
static u64 g_unexpected_elr;
static u64 g_kernel_probe;
static u64 g_saved_vbar;
static u64 g_kernel_probe_value = 0x4558504f53454431ull;
static u64 g_application_entry;
static int g_application_loaded;
static int g_prepared;

extern void arm_g05_activate_page_tables(u64 root);
extern void arm_g05_enter_user(u64 entry, u64 stack, u64 initial_value);
extern void arm_install_vectors(void);
extern u64 arm_current_vbar(void);
extern void arm_restore_vbar(u64 vbar);
extern void arm_g05_user_complete(void);
extern void experimentalos_aarch64_uart_write(const char *text);
extern void experimentalos_aarch64_idle(void);

static void write_hex(u64 value)
{
    static const char digits[] = "0123456789abcdef";
    char buffer[17];
    for (usize i = 0; i < 16; ++i) {
        buffer[15u - i] = digits[value & 0xfu];
        value >>= 4;
    }
    buffer[16] = '\0';
    experimentalos_aarch64_uart_write(buffer);
}

static void zero_memory(void *memory, usize size)
{
    volatile u8 *bytes = (volatile u8 *)memory;
    for (usize i = 0; i < size; ++i) {
        bytes[i] = 0;
    }
}

static void emit_instruction(usize *count, u32 instruction)
{
    ((u32 *)g_user_pages)[(*count)++] = instruction;
}

static void emit_mov64(usize *count, u32 reg, u64 value)
{
    emit_instruction(count,
        0xd2800000u | ((u32)(value & 0xffffu) << 5) | reg);
    emit_instruction(count,
        0xf2a00000u | ((u32)((value >> 16) & 0xffffu) << 5) | reg);
    emit_instruction(count,
        0xf2c00000u | ((u32)((value >> 32) & 0xffffu) << 5) | reg);
    emit_instruction(count,
        0xf2e00000u | ((u32)((value >> 48) & 0xffffu) << 5) | reg);
}

static void emit_mov32(usize *count, u32 reg, u32 value)
{
    emit_instruction(count,
        0xd2800000u | ((value & 0xffffu) << 5) | reg);
}

static void emit_fault_load(usize *count, u64 address)
{
    emit_mov64(count, 10u, address);
    emit_instruction(count, 0xf9400140u);
}

static void emit_validate(
    usize *count,
    u64 address,
    u32 size,
    u32 permissions
)
{
    emit_mov64(count, 0u, address);
    emit_mov32(count, 1u, size);
    emit_mov32(count, 2u, permissions);
    emit_mov32(count, 8u, OS_SYSCALL_VALIDATE_RANGE);
    emit_instruction(count, 0xd4000001u);
}

static int prepare_user_code(void)
{
    usize count = 0;
    emit_mov32(&count, 8u, OS_SYSCALL_YIELD);
    emit_instruction(&count, 0xd4000001u);
    emit_fault_load(&count, g_kernel_probe);
    emit_fault_load(
        &count, (u64)(usize)(g_user_pages + 2u * OS_PAGE_SIZE));
    emit_validate(
        &count, ARM_USER_CODE, 1u, OS_VM_READ | OS_VM_EXECUTE);
    emit_validate(&count, g_kernel_probe, sizeof(u64), OS_VM_READ);
    emit_instruction(&count, 0xaa1403e0u);
    emit_mov32(&count, 8u, OS_SYSCALL_EXIT);
    emit_instruction(&count, 0xd4000001u);
    emit_instruction(&count, 0x14000000u);
    usize image_size = count * sizeof(u32);
    if (image_size == 0 || image_size > sizeof(g_application_package.image)) {
        return 0;
    }
    for (usize i = 0; i < image_size; ++i) {
        g_application_package.image[i] = g_user_pages[i];
    }
    g_application_package.header.magic = OS_APP_PACKAGE_MAGIC;
    g_application_package.header.format_version =
        OS_APP_PACKAGE_FORMAT_VERSION;
    g_application_package.header.abi_version = OS_NATIVE_ABI_VERSION;
    g_application_package.header.target_architecture = OS_APP_ARCH_AARCH64;
    g_application_package.header.header_size =
        sizeof(g_application_package.header);
    g_application_package.header.image_size = (u32)image_size;
    g_application_package.header.entry_offset = 0;
    g_application_package.header.data_size = 0;
    g_application_package.header.flags = 0;
    g_application_package.header.capabilities = OS_APP_CAP_SYSCALL;
    g_application_package.header.image_checksum = os_app_image_checksum(
        g_application_package.image, image_size);
    os_loaded_application loaded = {0};
    g_application_loaded = os_app_load_image(
        &g_application_package,
        sizeof(g_application_package.header) + image_size,
        OS_APP_ARCH_AARCH64,
        ARM_USER_CODE,
        g_user_pages,
        OS_PAGE_SIZE,
        (void *)0,
        0,
        &loaded
    );
    if (!g_application_loaded || loaded.entry_address != ARM_USER_CODE) {
        return 0;
    }
    g_application_entry = loaded.entry_address;
    return 1;
}

static int initialize_page_tables(void)
{
    zero_memory(g_tables, sizeof(g_tables));
    g_tables[0].entries[0] =
        (u64)(usize)&g_tables[1] | ARM_DESCRIPTOR_VALID | ARM_DESCRIPTOR_TABLE;

    for (usize index = 0; index < 5; ++index) {
        g_tables[1].entries[index] =
            (u64)(usize)&g_tables[2u + index]
            | ARM_DESCRIPTOR_VALID | ARM_DESCRIPTOR_TABLE;
    }

    for (usize table = 0; table < 4; ++table) {
        for (usize index = 0; index < ARM_PAGE_COUNT; ++index) {
            u64 physical = ((u64)table * ARM_PAGE_COUNT + index)
                * (2u * 1024u * 1024u);
            u64 attributes = ARM_DESCRIPTOR_VALID
                | ARM_DESCRIPTOR_AF
                | ARM_DESCRIPTOR_INNER_SHAREABLE
                | ARM_DESCRIPTOR_UXN;
            if (physical >= 0x08000000ull && physical < 0x0a000000ull) {
                attributes |= 1ull << 2;
            }
            g_tables[2u + table].entries[index] =
                (physical & ARM_DESCRIPTOR_BLOCK_ADDRESS_MASK) | attributes;
        }
    }

    g_tables[6].entries[0] =
        (u64)(usize)&g_tables[7] | ARM_DESCRIPTOR_VALID | ARM_DESCRIPTOR_TABLE;
    g_tables[7].entries[0] =
        ((u64)(usize)g_user_pages & ARM_DESCRIPTOR_ADDRESS_MASK)
        | ARM_DESCRIPTOR_VALID | ARM_DESCRIPTOR_TABLE
        | ARM_DESCRIPTOR_AF | ARM_DESCRIPTOR_INNER_SHAREABLE
        | ARM_DESCRIPTOR_AP_READ_ONLY | ARM_DESCRIPTOR_PXN;
    g_tables[7].entries[1] =
        ((u64)(usize)(g_user_pages + OS_PAGE_SIZE)
            & ARM_DESCRIPTOR_ADDRESS_MASK)
        | ARM_DESCRIPTOR_VALID | ARM_DESCRIPTOR_TABLE
        | ARM_DESCRIPTOR_AF | ARM_DESCRIPTOR_INNER_SHAREABLE
        | ARM_DESCRIPTOR_AP_USER_READ_WRITE
        | ARM_DESCRIPTOR_PXN | ARM_DESCRIPTOR_UXN;

    for (usize i = 0; i < ARM_TABLE_COUNT; ++i) {
        if (((u64)(usize)&g_tables[i] >> 32) != 0) {
            return 0;
        }
    }
    return ((u64)(usize)g_user_pages >> 32) == 0;
}

static void clean_table_memory(void)
{
    for (usize offset = 0; offset < sizeof(g_tables); offset += 64u) {
        __asm__ volatile("dc cvac, %0" : : "r"((u64)(usize)g_tables + offset)
                         : "memory");
    }
    for (usize offset = 0; offset < OS_PAGE_SIZE; offset += 64u) {
        __asm__ volatile("dc cvau, %0"
            : : "r"((u64)(usize)g_user_pages + offset) : "memory");
    }
    __asm__ volatile("dsb sy\n\t"
                     "ic iallu\n\t"
                     "dsb sy\n\t"
                     "isb"
                     : : : "memory");
}

static void configure_contexts(void)
{
    for (usize slot = 0; slot < OS_G05_PROCESS_COUNT; ++slot) {
        zero_memory(&g_contexts[slot], sizeof(g_contexts[slot]));
        g_contexts[slot].x[20] = 10u + slot;
        g_contexts[slot].elr = g_application_entry;
        g_contexts[slot].spsr = 0x3c0u;
        g_contexts[slot].sp_el0 = ARM_USER_STACK + OS_PAGE_SIZE;
    }
}

int arm_g05_prepare(void)
{
    if (g_prepared) {
        return 0;
    }
    zero_memory(g_user_pages, sizeof(g_user_pages));
    g_kernel_probe = (u64)(usize)&g_kernel_probe_value;
    if (!initialize_page_tables()
        || !os_g05_initialize(
            (u64)(usize)g_user_pages,
            (u64)(usize)(g_user_pages + OS_PAGE_SIZE),
            (u64)(usize)(g_user_pages + 2u * OS_PAGE_SIZE),
            g_kernel_probe)
        || !prepare_user_code()) {
        return 0;
    }
    configure_contexts();
    clean_table_memory();
    g_prepared = 1;
    return 1;
}

static int context_slot(u32 thread_id)
{
    for (usize i = 0; i < OS_G05_PROCESS_COUNT; ++i) {
        if (os_g05_thread_for_slot(i) == thread_id) {
            return (int)i;
        }
    }
    return -1;
}

static void install_user_stack(usize slot)
{
    u8 *physical = g_user_pages + (slot + 1u) * OS_PAGE_SIZE;
    g_tables[7].entries[1] =
        ((u64)(usize)physical & ARM_DESCRIPTOR_ADDRESS_MASK)
        | ARM_DESCRIPTOR_VALID | ARM_DESCRIPTOR_TABLE
        | ARM_DESCRIPTOR_AF | ARM_DESCRIPTOR_INNER_SHAREABLE
        | ARM_DESCRIPTOR_AP_USER_READ_WRITE
        | ARM_DESCRIPTOR_PXN | ARM_DESCRIPTOR_UXN;
    __asm__ volatile("dc cvac, %0\n\t"
                     "dsb ishst\n\t"
                     "tlbi vmalle1is\n\t"
                     "dsb ish\n\t"
                     "isb"
                     : : "r"((u64)(usize)&g_tables[7].entries[1])
                     : "memory");
}

arm_user_context *arm_g05_exception_dispatch(arm_user_context *frame)
{
    if (frame == (arm_user_context *)0 || !g_prepared) {
        return (arm_user_context *)0;
    }
    u32 exception_class = (u32)((frame->esr >> 26) & 0x3fu);
    if (exception_class == ARM_EC_DATA_ABORT_LOWER) {
        g_unexpected_esr = frame->esr;
        g_unexpected_far = frame->far;
        g_unexpected_elr = frame->elr;
        if (!os_g05_record_user_fault(frame->far)) {
            return (arm_user_context *)0;
        }
        ++g_page_faults;
        volatile u64 *saved = (volatile u64 *)&g_contexts[g_current_slot];
        const volatile u64 *incoming = (const volatile u64 *)frame;
        for (usize i = 0; i < sizeof(*frame) / sizeof(u64); ++i) {
            saved[i] = incoming[i];
        }
        g_contexts[g_current_slot].elr += 4u;
        return &g_contexts[g_current_slot];
    }
    if (exception_class != ARM_EC_SVC64) {
        g_unexpected_esr = frame->esr;
        g_unexpected_far = frame->far;
        g_unexpected_elr = frame->elr;
        return (arm_user_context *)0;
    }

    volatile u64 *saved = (volatile u64 *)&g_contexts[g_current_slot];
    const volatile u64 *incoming = (const volatile u64 *)frame;
    for (usize i = 0; i < sizeof(*frame) / sizeof(u64); ++i) {
        saved[i] = incoming[i];
    }
    u32 syscall_number = (u32)frame->x[8];
    u32 next_thread = os_g05_schedule_syscall(
        os_g05_thread_for_slot(g_current_slot),
        syscall_number,
        frame->x[0],
        frame->x[1],
        frame->x[2]
    );
    if (syscall_number == OS_SYSCALL_VALIDATE_RANGE) {
        g_contexts[g_current_slot].x[0] =
            frame->x[0] == g_kernel_probe
            ? (u64)OS_SYSCALL_EFAULT : 0;
    }
    if (next_thread == 0) {
        return (arm_user_context *)0;
    }
    int next_slot = context_slot(next_thread);
    if (next_slot < 0) {
        return (arm_user_context *)0;
    }
    g_current_slot = (u32)next_slot;
    install_user_stack(g_current_slot);
    return &g_contexts[g_current_slot];
}

void arm_g05_activate_page_tables(u64 root)
{
    u64 tcr;
    u64 mair;
    __asm__ volatile("mrs %0, tcr_el1" : "=r"(tcr));
    tcr &= ~((u64)0x3fu | (3ull << 8) | (3ull << 10)
        | (3ull << 12) | (3ull << 14) | (7ull << 32));
    tcr |= 16u | (1ull << 8) | (1ull << 10)
        | (3ull << 12) | (2ull << 32);
    __asm__ volatile("msr tcr_el1, %0" : : "r"(tcr) : "memory");

    __asm__ volatile("mrs %0, mair_el1" : "=r"(mair));
    mair = (mair & ~0xffffull) | 0x00ffull;
    __asm__ volatile("msr mair_el1, %0" : : "r"(mair) : "memory");
    __asm__ volatile("msr ttbr0_el1, %0\n\t"
                     "dsb sy\n\t"
                     "tlbi vmalle1\n\t"
                     "dsb sy\n\t"
                     "isb"
                     : : "r"(root) : "memory");
}

void arm_g05_start(void)
{
    if (!g_prepared) {
        arm_g05_user_complete();
    }
    u32 first_thread = os_g05_initial_thread();
    int first_slot = context_slot(first_thread);
    if (first_slot < 0) {
        arm_g05_user_complete();
    }
    g_current_slot = (u32)first_slot;
    install_user_stack(g_current_slot);
    g_saved_vbar = arm_current_vbar();
    arm_install_vectors();
    arm_g05_activate_page_tables((u64)(usize)&g_tables[0]);
    arm_g05_enter_user(
        ARM_USER_CODE,
        ARM_USER_STACK + OS_PAGE_SIZE,
        g_contexts[g_current_slot].x[20]);
}

void arm_g05_user_complete(void)
{
    if (g_page_faults == 4u && os_g05_finish()) {
        experimentalos_aarch64_uart_write(
            "EXPERIMENTALOS: G05 USER ISOLATION PASS\r\n");
        experimentalos_aarch64_uart_write(
            "EXPERIMENTALOS: G05 SYSCALL SCHEDULER CLEANUP PASS\r\n");
        if (g_application_loaded) {
            experimentalos_aarch64_uart_write(
                "EXPERIMENTALOS: G09 VERSIONED APP EXECUTION PASS\r\n");
            experimentalos_aarch64_uart_write(
                "EXPERIMENTALOS: G09 NATIVE ABI V1 PASS\r\n");
        }
    } else {
        experimentalos_aarch64_uart_write(
            "EXPERIMENTALOS: G05 KERNEL CHECK FAIL\r\n");
        if (g_page_faults != 4u) {
            (void)os_g05_finish();
        }
        experimentalos_aarch64_uart_write("G05 finish error=");
        write_hex(os_g05_finish_error());
        experimentalos_aarch64_uart_write(" faults=");
        write_hex(g_page_faults);
        experimentalos_aarch64_uart_write(" expected faults=");
        write_hex(os_g05_fault_count());
        experimentalos_aarch64_uart_write(" invalid=");
        write_hex(os_g05_invalid_range_count());
        experimentalos_aarch64_uart_write(" syscalls=");
        write_hex(os_g05_syscall_count());
        experimentalos_aarch64_uart_write(" ESR=");
        write_hex(g_unexpected_esr);
        experimentalos_aarch64_uart_write(" FAR=");
        write_hex(g_unexpected_far);
        experimentalos_aarch64_uart_write(" ELR=");
        write_hex(g_unexpected_elr);
        experimentalos_aarch64_uart_write(" PTE=");
        write_hex(g_tables[7].entries[0]);
        experimentalos_aarch64_uart_write("\r\n");
    }
    experimentalos_aarch64_uart_write("EXPERIMENTALOS: G02 IDLE\r\n");
    arm_restore_vbar(g_saved_vbar);
    experimentalos_aarch64_idle();
}

u64 arm_g05_kernel_probe_address(void)
{
    return g_kernel_probe;
}
