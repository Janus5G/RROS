.text
.globl x86_breakpoint_handler
x86_breakpoint_handler:
    pushq %rax
    movq 8(%rsp), %rax
    movq %rax, g_x86_exception_rip(%rip)
    incl g_x86_exception_count(%rip)
    popq %rax
    iretq
