.text
.global arm_g05_enter_user
arm_g05_enter_user:
    msr SP_EL0, x1
    msr ELR_EL1, x0
    mov x9, #0x3c0
    msr SPSR_EL1, x9
    mov x20, x2
    eret
