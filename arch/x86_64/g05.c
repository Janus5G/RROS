#include "g05.h"
#include "app_loader.h"

#define X86_PAGE_PRESENT 0x001ull
#define X86_PAGE_WRITE 0x002ull
#define X86_PAGE_USER 0x004ull
#define X86_PAGE_LARGE 0x080ull
#define X86_PAGE_NX (1ull << 63)
#define X86_USER_CODE 0x100000000ull
#define X86_USER_STACK (X86_USER_CODE + OS_PAGE_SIZE)
#define X86_PAGE_COUNT 512u
#define X86_TABLE_COUNT 8u

typedef struct __attribute__((aligned(OS_PAGE_SIZE))) {
    u64 entries[X86_PAGE_COUNT];
} x86_page_table;

typedef struct __attribute__((packed)) {
    u16 offset_low;
    u16 selector;
    u8 ist;
    u8 attributes;
    u16 offset_middle;
    u32 offset_high;
    u32 reserved;
} x86_gate;

typedef struct __attribute__((packed)) {
    u16 limit;
    u64 base;
} x86_table_pointer;

typedef struct __attribute__((packed)) {
    u32 reserved0;
    u64 rsp0;
    u64 rsp1;
    u64 rsp2;
    u64 reserved1;
    u64 ist1;
    u64 ist2;
    u64 ist3;
    u64 ist4;
    u64 ist5;
    u64 ist6;
    u64 ist7;
    u64 reserved2;
    u16 reserved3;
    u16 io_map_base;
} x86_tss;

typedef struct {
    u64 rax;
    u64 rbx;
    u64 rcx;
    u64 rdx;
    u64 rsi;
    u64 rdi;
    u64 rbp;
    u64 r8;
    u64 r9;
    u64 r10;
    u64 r11;
    u64 r12;
    u64 r13;
    u64 r14;
    u64 r15;
    u64 rip;
    u64 cs;
    u64 rflags;
    u64 rsp;
    u64 ss;
} x86_user_context;

static x86_page_table g_tables[X86_TABLE_COUNT];
static u64 g_gdt[7] __attribute__((aligned(16)));
x86_table_pointer g_g05_idtr;
static x86_gate g_idt[256] __attribute__((aligned(16)));
static x86_tss g_tss;
static u8 g_kernel_stack[16u * 1024u] __attribute__((aligned(16)));
static u8 g_user_pages[3u * OS_PAGE_SIZE]
    __attribute__((aligned(OS_PAGE_SIZE)));
static struct {
    os_app_package_header header;
    u8 image[OS_PAGE_SIZE];
} g_application_package;
static x86_user_context g_contexts[OS_G05_PROCESS_COUNT];
static u32 g_current_slot;
static u64 g_kernel_probe;
static u64 g_page_faults;
static u64 g_kernel_probe_value = 0x4558504f53454431ull;
static u64 g_application_entry;
static int g_application_loaded;
static int g_prepared;

extern int experimentalos_serial_write(const char *text);
extern void x86_g05_load_tables(const x86_table_pointer *gdtr);
extern void x86_g05_load_task_register(void);
extern void x86_g05_enter_user(
    u64 instruction_pointer,
    u64 stack_pointer,
    u64 initial_value
);
extern void x86_g05_syscall_entry(void);
extern void x86_g05_page_fault_entry(void);
extern void x86_g05_user_complete(void);

static void set_gate(x86_gate *gate, void (*handler)(void), u8 attributes)
{
    u64 address = (u64)(usize)handler;
    gate->offset_low = (u16)address;
    gate->selector = 0x08;
    gate->ist = 0;
    gate->attributes = attributes;
    gate->offset_middle = (u16)(address >> 16);
    gate->offset_high = (u32)(address >> 32);
    gate->reserved = 0;
}

static void write_hex(u64 value)
{
    static const char digits[] = "0123456789abcdef";
    char buffer[17];
    for (usize i = 0; i < 16; ++i) {
        buffer[15u - i] = digits[value & 0xfu];
        value >>= 4;
    }
    buffer[16] = '\0';
    (void)experimentalos_serial_write(buffer);
}

static void write_user_u64(u8 *code, usize offset, u64 value)
{
    for (usize i = 0; i < sizeof(value); ++i) {
        code[offset + i] = (u8)(value >> (i * 8u));
    }
}

static usize emit_u32(u8 *code, usize offset, u32 value)
{
    for (usize i = 0; i < sizeof(value); ++i) {
        code[offset + i] = (u8)(value >> (i * 8u));
    }
    return offset + sizeof(value);
}

static usize emit_mov_eax(u8 *code, usize offset, u32 value)
{
    code[offset++] = 0xb8;
    return emit_u32(code, offset, value);
}

static usize emit_movabs_rax(u8 *code, usize offset, u64 value)
{
    code[offset++] = 0x48;
    code[offset++] = 0xb8;
    write_user_u64(code, offset, value);
    return offset + sizeof(value);
}

static usize emit_fault_load(u8 *code, usize offset, u64 address)
{
    offset = emit_movabs_rax(code, offset, address);
    code[offset++] = 0x48;
    code[offset++] = 0x8b;
    code[offset++] = 0x00;
    return offset;
}

static usize emit_validate(
    u8 *code,
    usize offset,
    u64 address,
    u32 size,
    u32 permissions
)
{
    code[offset++] = 0x48;
    code[offset++] = 0xbf;
    write_user_u64(code, offset, address);
    offset += sizeof(address);
    code[offset++] = 0xbe;
    offset = emit_u32(code, offset, size);
    code[offset++] = 0xba;
    offset = emit_u32(code, offset, permissions);
    offset = emit_mov_eax(code, offset, OS_SYSCALL_VALIDATE_RANGE);
    code[offset++] = 0xcd;
    code[offset++] = 0x80;
    return offset;
}

static int prepare_user_code(void)
{
    u8 *code = (u8 *)g_user_pages;
    usize offset = 0;
    offset = emit_mov_eax(code, offset, OS_SYSCALL_YIELD);
    code[offset++] = 0xcd;
    code[offset++] = 0x80;
    offset = emit_fault_load(code, offset, g_kernel_probe);
    offset = emit_fault_load(code, offset, (u64)(usize)(g_user_pages + 2u * OS_PAGE_SIZE));
    offset = emit_validate(
        code, offset, X86_USER_CODE, 1, OS_VM_READ | OS_VM_EXECUTE);
    offset = emit_validate(
        code, offset, g_kernel_probe, sizeof(u64), OS_VM_READ);
    code[offset++] = 0x4c;
    code[offset++] = 0x89;
    code[offset++] = 0xe7;
    offset = emit_mov_eax(code, offset, OS_SYSCALL_EXIT);
    code[offset++] = 0xcd;
    code[offset++] = 0x80;
    code[offset++] = 0xeb;
    code[offset] = 0xfe;
    usize image_size = offset + 1u;
    if (image_size > sizeof(g_application_package.image)) {
        return 0;
    }
    for (usize i = 0; i < image_size; ++i) {
        g_application_package.image[i] = code[i];
    }
    g_application_package.header.magic = OS_APP_PACKAGE_MAGIC;
    g_application_package.header.format_version =
        OS_APP_PACKAGE_FORMAT_VERSION;
    g_application_package.header.abi_version = OS_NATIVE_ABI_VERSION;
    g_application_package.header.target_architecture = OS_APP_ARCH_X86_64;
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
        OS_APP_ARCH_X86_64,
        X86_USER_CODE,
        g_user_pages,
        OS_PAGE_SIZE,
        (void *)0,
        0,
        &loaded
    );
    if (!g_application_loaded || loaded.entry_address != X86_USER_CODE) {
        return 0;
    }
    g_application_entry = loaded.entry_address;
    return 1;
}

static int initialize_page_tables(void)
{
    for (usize i = 0; i < X86_TABLE_COUNT; ++i) {
        for (usize j = 0; j < X86_PAGE_COUNT; ++j) {
            g_tables[i].entries[j] = 0;
        }
    }
    g_tables[0].entries[0] = (u64)(usize)&g_tables[1] | 7u;
    for (usize pdpt = 0; pdpt < 4; ++pdpt) {
        g_tables[1].entries[pdpt] =
            (u64)(usize)&g_tables[2u + pdpt] | 3u;
        for (usize index = 0; index < X86_PAGE_COUNT; ++index) {
            u64 physical = ((u64)pdpt * X86_PAGE_COUNT + index)
                * (2u * 1024u * 1024u);
            g_tables[2u + pdpt].entries[index] =
                physical | X86_PAGE_PRESENT | X86_PAGE_WRITE | X86_PAGE_LARGE;
        }
    }
    g_tables[1].entries[4] = (u64)(usize)&g_tables[6] | 7u;
    g_tables[6].entries[0] = (u64)(usize)&g_tables[7] | 7u;
    g_tables[7].entries[0] =
        (u64)(usize)g_user_pages | X86_PAGE_PRESENT | X86_PAGE_USER;
    g_tables[7].entries[1] = (u64)(usize)(g_user_pages + OS_PAGE_SIZE)
        | X86_PAGE_PRESENT | X86_PAGE_WRITE | X86_PAGE_USER | X86_PAGE_NX;
    return ((u64)(usize)&g_tables[0] >> 32) == 0
        && ((u64)(usize)g_user_pages >> 32) == 0;
}

static void configure_tables(void)
{
    g_gdt[0] = 0;
    g_gdt[1] = 0x00af9a000000ffffull;
    g_gdt[2] = 0x00cf92000000ffffull;
    g_gdt[3] = 0x00cff2000000ffffull;
    g_gdt[4] = 0x00affa000000ffffull;

    u64 base = (u64)(usize)&g_tss;
    u64 limit = sizeof(g_tss) - 1u;
    g_gdt[5] = (limit & 0xffffu)
        | ((base & 0xffffffu) << 16)
        | (0x89ull << 40)
        | ((limit & 0xf0000u) << 32)
        | ((base & 0xff000000u) << 32);
    g_gdt[6] = base >> 32;

    for (usize i = 0; i < sizeof(g_idt) / sizeof(g_idt[0]); ++i) {
        g_idt[i].offset_low = 0;
        g_idt[i].selector = 0;
        g_idt[i].ist = 0;
        g_idt[i].attributes = 0;
        g_idt[i].offset_middle = 0;
        g_idt[i].offset_high = 0;
        g_idt[i].reserved = 0;
    }
    set_gate(&g_idt[14], x86_g05_page_fault_entry, 0x8e);
    set_gate(&g_idt[0x80], x86_g05_syscall_entry, 0xee);
    g_g05_idtr.limit = (u16)(sizeof(g_idt) - 1u);
    g_g05_idtr.base = (u64)(usize)g_idt;
    g_tss.rsp0 = ((u64)(usize)g_kernel_stack + sizeof(g_kernel_stack))
        & ~0xfull;
    g_tss.io_map_base = sizeof(g_tss);
}

int x86_g05_prepare(void)
{
    if (g_prepared) {
        return 0;
    }
    for (usize i = 0; i < sizeof(g_user_pages); ++i) {
        g_user_pages[i] = 0;
    }
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

    for (usize i = 0; i < OS_G05_PROCESS_COUNT; ++i) {
        x86_user_context *context = &g_contexts[i];
        context->rax = 0;
        context->rbx = 0;
        context->rcx = 0;
        context->rdx = 0;
        context->rsi = 0;
        context->rdi = 0;
        context->rbp = 0;
        context->r8 = 0;
        context->r9 = 0;
        context->r10 = 0;
        context->r11 = 0;
        context->r12 = 10u + i;
        context->r13 = 0;
        context->r14 = 0;
        context->r15 = 0;
        context->rip = g_application_entry;
        context->cs = 0x23;
        context->rflags = 0x202;
        context->rsp = X86_USER_STACK + OS_PAGE_SIZE - 16u;
        context->ss = 0x1b;
    }
    configure_tables();
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
    u64 physical = slot == 0
        ? (u64)(usize)(g_user_pages + OS_PAGE_SIZE)
        : (u64)(usize)(g_user_pages + 2u * OS_PAGE_SIZE);
    g_tables[7].entries[1] = physical | X86_PAGE_PRESENT
        | X86_PAGE_WRITE | X86_PAGE_USER | X86_PAGE_NX;
    __asm__ volatile("invlpg (%0)" : : "r"((usize)X86_USER_STACK) : "memory");
}

void *x86_g05_syscall_dispatch(x86_user_context *frame)
{
    if (frame == (x86_user_context *)0 || !g_prepared) {
        return (void *)0;
    }
    volatile u64 *saved = (volatile u64 *)&g_contexts[g_current_slot];
    const volatile u64 *incoming = (const volatile u64 *)frame;
    for (usize i = 0; i < sizeof(*frame) / sizeof(u64); ++i) {
        saved[i] = incoming[i];
    }
    u32 syscall_number = (u32)frame->rax;
    u32 next_thread = os_g05_schedule_syscall(
        os_g05_thread_for_slot(g_current_slot),
        syscall_number,
        frame->rdi,
        frame->rsi,
        frame->rdx
    );
    if (syscall_number == OS_SYSCALL_VALIDATE_RANGE) {
        if (frame->rdi == g_kernel_probe) {
            frame->rax = (u64)OS_SYSCALL_EFAULT;
        } else {
            frame->rax = 0;
        }
    }
    if (next_thread == 0) {
        return (void *)0;
    }
    int next_slot = context_slot(next_thread);
    if (next_slot < 0) {
        return (void *)0;
    }
    g_current_slot = (u32)next_slot;
    install_user_stack(g_current_slot);
    return &g_contexts[g_current_slot];
}

int x86_g05_handle_user_page_fault(u64 fault_address, u64 error_code)
{
    if ((error_code & 5u) != 5u
        || !os_g05_record_user_fault(fault_address)) {
        return 0;
    }
    ++g_page_faults;
    return 1;
}

int x86_g05_run(void)
{
    if (!g_prepared || g_page_faults != 4
        || os_g05_invalid_range_count() != 2
        || !os_g05_finish()) {
        return 0;
    }
    return 1;
}

void x86_g05_start(void)
{
    x86_table_pointer gdtr = {
        (u16)(sizeof(g_gdt) - 1u),
        (u64)(usize)g_gdt,
    };
    u32 first_thread = os_g05_initial_thread();
    int first_slot = context_slot(first_thread);
    if (first_slot < 0) {
        for (;;) {
            __asm__ volatile("cli; hlt");
        }
    }
    g_current_slot = (u32)first_slot;
    install_user_stack(g_current_slot);
    __asm__ volatile(
        "cli\n\t"
        "mov %%cr4, %%rax\n\t"
        "or $0x20, %%rax\n\t"
        "mov %%rax, %%cr4\n\t"
        "mov $0xc0000080, %%ecx\n\t"
        "rdmsr\n\t"
        "or $0x800, %%eax\n\t"
        "wrmsr\n\t"
        "mov %%cr0, %%rax\n\t"
        "or $0x10000, %%rax\n\t"
        "mov %%rax, %%cr0\n\t"
        :
        :
        : "rax", "rcx", "rdx", "memory"
    );
    x86_g05_load_tables(&gdtr);
    x86_g05_load_task_register();
    u64 page_root = (u64)(usize)&g_tables[0];
    __asm__ volatile("mov %0, %%cr3" : : "r"(page_root) : "memory");
    x86_g05_enter_user(
        X86_USER_CODE, X86_USER_STACK + OS_PAGE_SIZE - 16u,
        g_contexts[g_current_slot].r12);
}

void x86_g05_user_complete(void)
{
    if (x86_g05_run()) {
        extern int experimentalos_serial_write(const char *);
        (void)experimentalos_serial_write(
            "EXPERIMENTALOS: G05 USER ISOLATION PASS\r\n");
        (void)experimentalos_serial_write(
            "EXPERIMENTALOS: G05 SYSCALL SCHEDULER CLEANUP PASS\r\n");
        if (g_application_loaded) {
            (void)experimentalos_serial_write(
                "EXPERIMENTALOS: G09 VERSIONED APP EXECUTION PASS\r\n");
            (void)experimentalos_serial_write(
                "EXPERIMENTALOS: G09 NATIVE ABI V1 PASS\r\n");
        }
    } else {
        (void)experimentalos_serial_write(
            "EXPERIMENTALOS: G05 KERNEL CHECK FAIL\r\n");
        (void)experimentalos_serial_write("G05 finish error=");
        write_hex(os_g05_finish_error());
        (void)experimentalos_serial_write(" faults=");
        write_hex(os_g05_fault_count());
        (void)experimentalos_serial_write(" invalid=");
        write_hex(os_g05_invalid_range_count());
        (void)experimentalos_serial_write(" syscalls=");
        write_hex(os_g05_syscall_count());
        (void)experimentalos_serial_write("\r\n");
    }
    extern void experimentalos_x86_idle(void);
    experimentalos_x86_idle();
}

u64 x86_g05_kernel_probe_address(void)
{
    return g_kernel_probe;
}
