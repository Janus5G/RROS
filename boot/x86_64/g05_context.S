.text
.globl x86_g05_load_tables
x86_g05_load_tables:
    lgdt (%rcx)
    pushq $0x08
    leaq 1f(%rip), %rax
    pushq %rax
    lretq
1:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    lidt g_g05_idtr(%rip)
    retq

.globl x86_g05_load_task_register
x86_g05_load_task_register:
    movw $0x28, %ax
    ltr %ax
    retq

.globl x86_g05_enter_user
x86_g05_enter_user:
    movq %r8, %r12
    movw $0x1b, %ax
    movw %ax, %ds
    movw %ax, %es
    pushq $0x1b
    pushq %rdx
    pushq $0x202
    pushq $0x23
    pushq %rcx
    iretq

.globl x86_g05_syscall_entry
x86_g05_syscall_entry:
    cld
    subq $120, %rsp
    movq %rax, 0(%rsp)
    movq %rbx, 8(%rsp)
    movq %rcx, 16(%rsp)
    movq %rdx, 24(%rsp)
    movq %rsi, 32(%rsp)
    movq %rdi, 40(%rsp)
    movq %rbp, 48(%rsp)
    movq %r8, 56(%rsp)
    movq %r9, 64(%rsp)
    movq %r10, 72(%rsp)
    movq %r11, 80(%rsp)
    movq %r12, 88(%rsp)
    movq %r13, 96(%rsp)
    movq %r14, 104(%rsp)
    movq %r15, 112(%rsp)
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movq %rsp, %rcx
    subq $32, %rsp
    callq x86_g05_syscall_dispatch
    addq $32, %rsp
    testq %rax, %rax
    jz 2f
    movq %rax, %rsp
    movw $0x1b, %dx
    movw %dx, %ds
    movw %dx, %es
    movq 8(%rsp), %rbx
    movq 16(%rsp), %rcx
    movq 24(%rsp), %rdx
    movq 32(%rsp), %rsi
    movq 40(%rsp), %rdi
    movq 48(%rsp), %rbp
    movq 56(%rsp), %r8
    movq 64(%rsp), %r9
    movq 72(%rsp), %r10
    movq 80(%rsp), %r11
    movq 88(%rsp), %r12
    movq 96(%rsp), %r13
    movq 104(%rsp), %r14
    movq 112(%rsp), %r15
    movq 0(%rsp), %rax
    addq $120, %rsp
    iretq
2:
    addq $120, %rsp
    addq $40, %rsp
    subq $32, %rsp
    callq x86_g05_user_complete
    ud2

.globl x86_g05_page_fault_entry
x86_g05_page_fault_entry:
    cld
    subq $120, %rsp
    movq %rax, 0(%rsp)
    movq %rbx, 8(%rsp)
    movq %rcx, 16(%rsp)
    movq %rdx, 24(%rsp)
    movq %rsi, 32(%rsp)
    movq %rdi, 40(%rsp)
    movq %rbp, 48(%rsp)
    movq %r8, 56(%rsp)
    movq %r9, 64(%rsp)
    movq %r10, 72(%rsp)
    movq %r11, 80(%rsp)
    movq %r12, 88(%rsp)
    movq %r13, 96(%rsp)
    movq %r14, 104(%rsp)
    movq %r15, 112(%rsp)
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movq %cr2, %rcx
    movq 120(%rsp), %rdx
    subq $40, %rsp
    callq x86_g05_handle_user_page_fault
    addq $40, %rsp
    testl %eax, %eax
    jz 3f
    movq 120(%rsp), %rax
    andq $5, %rax
    cmpq $5, %rax
    jne 3f
    addq $3, 128(%rsp)
    movq 8(%rsp), %rbx
    movq 16(%rsp), %rcx
    movq 24(%rsp), %rdx
    movq 32(%rsp), %rsi
    movq 40(%rsp), %rdi
    movq 48(%rsp), %rbp
    movq 56(%rsp), %r8
    movq 64(%rsp), %r9
    movq 72(%rsp), %r10
    movq 80(%rsp), %r11
    movq 88(%rsp), %r12
    movq 96(%rsp), %r13
    movq 104(%rsp), %r14
    movq 112(%rsp), %r15
    movq 0(%rsp), %rax
    addq $120, %rsp
    addq $8, %rsp
    iretq
3:
    cli
4:
    hlt
    jmp 4b
