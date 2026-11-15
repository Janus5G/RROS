.text
.p2align 11
.global arm_exception_vectors
arm_exception_vectors:
.macro VECTOR handler
    b \handler
    .space 124
.endm
    VECTOR arm_sync_exception
    VECTOR arm_irq_exception
    VECTOR arm_unexpected_exception
    VECTOR arm_unexpected_exception
    VECTOR arm_sync_exception
    VECTOR arm_irq_exception
    VECTOR arm_unexpected_exception
    VECTOR arm_unexpected_exception
    VECTOR arm_lower_el_sync_exception
    VECTOR arm_irq_exception
    VECTOR arm_unexpected_exception
    VECTOR arm_unexpected_exception
    VECTOR arm_unexpected_exception
    VECTOR arm_unexpected_exception
    VECTOR arm_unexpected_exception
    VECTOR arm_unexpected_exception

.global arm_install_vectors
arm_install_vectors:
    mrs x0, CurrentEL
    adrp x1, arm_exception_vectors
    add x1, x1, :lo12:arm_exception_vectors
    cmp x0, #8
    b.eq 1f
    msr VBAR_EL1, x1
    isb
    ret
1:
    msr VBAR_EL2, x1
    isb
    ret

.global arm_current_vbar
arm_current_vbar:
    mrs x1, CurrentEL
    cmp x1, #8
    b.eq 1f
    mrs x0, VBAR_EL1
    ret
1:
    mrs x0, VBAR_EL2
    ret

.global arm_restore_vbar
arm_restore_vbar:
    mrs x1, CurrentEL
    cmp x1, #8
    b.eq 1f
    msr VBAR_EL1, x0
    isb
    ret
1:
    msr VBAR_EL2, x0
    isb
    ret

.global arm_current_el
arm_current_el:
    mrs x0, CurrentEL
    lsr x0, x0, #2
    and x0, x0, #3
    ret

.global arm_sctlr
arm_sctlr:
    mrs x0, CurrentEL
    cmp x0, #8
    b.eq 1f
    mrs x0, SCTLR_EL1
    ret
1:
    mrs x0, SCTLR_EL2
    ret

.global arm_sync_exception
arm_sync_exception:
    mrs x16, CurrentEL
    cmp x16, #8
    b.eq 1f
    mrs x16, ESR_EL1
    adrp x17, g_arm_exception_esr
    add x17, x17, :lo12:g_arm_exception_esr
    str x16, [x17]
    ubfx x16, x16, #26, #6
    cmp x16, #0x3c
    b.ne arm_unexpected_exception
    mrs x16, FAR_EL1
    adrp x17, g_arm_exception_far
    add x17, x17, :lo12:g_arm_exception_far
    str x16, [x17]
    mrs x16, ELR_EL1
    adrp x17, g_arm_exception_elr
    add x17, x17, :lo12:g_arm_exception_elr
    str x16, [x17]
    add x16, x16, #4
    msr ELR_EL1, x16
    b 2f
1:
    mrs x16, ESR_EL2
    adrp x17, g_arm_exception_esr
    add x17, x17, :lo12:g_arm_exception_esr
    str x16, [x17]
    ubfx x16, x16, #26, #6
    cmp x16, #0x3c
    b.ne arm_unexpected_exception
    mrs x16, FAR_EL2
    adrp x17, g_arm_exception_far
    add x17, x17, :lo12:g_arm_exception_far
    str x16, [x17]
    mrs x16, ELR_EL2
    adrp x17, g_arm_exception_elr
    add x17, x17, :lo12:g_arm_exception_elr
    str x16, [x17]
    add x16, x16, #4
    msr ELR_EL2, x16
2:
    adrp x16, g_arm_exception_count
    add x16, x16, :lo12:g_arm_exception_count
    ldr w17, [x16]
    add w17, w17, #1
    str w17, [x16]
    eret

.global arm_lower_el_sync_exception
arm_lower_el_sync_exception:
    sub sp, sp, #288
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30, [sp, #240]
    mrs x16, ESR_EL1
    str x16, [sp, #248]
    mrs x16, ELR_EL1
    str x16, [sp, #256]
    mrs x16, SPSR_EL1
    str x16, [sp, #264]
    mrs x16, SP_EL0
    str x16, [sp, #272]
    mrs x16, FAR_EL1
    str x16, [sp, #280]
    mov x0, sp
    bl arm_g05_exception_dispatch
    cbz x0, 1f
    mov x17, x0
    ldr x16, [x17, #256]
    msr ELR_EL1, x16
    ldr x16, [x17, #264]
    msr SPSR_EL1, x16
    ldr x16, [x17, #272]
    msr SP_EL0, x16
    ldp x0, x1, [x17, #0]
    ldp x2, x3, [x17, #16]
    ldp x4, x5, [x17, #32]
    ldp x6, x7, [x17, #48]
    ldp x8, x9, [x17, #64]
    ldp x10, x11, [x17, #80]
    ldp x12, x13, [x17, #96]
    ldp x14, x15, [x17, #112]
    ldp x18, x19, [x17, #144]
    ldp x20, x21, [x17, #160]
    ldp x22, x23, [x17, #176]
    ldp x24, x25, [x17, #192]
    ldp x26, x27, [x17, #208]
    ldp x28, x29, [x17, #224]
    ldr x30, [x17, #240]
    ldr x16, [x17, #128]
    ldr x17, [x17, #136]
    add sp, sp, #288
    eret
1:
    add sp, sp, #288
    b arm_g05_user_complete

.global arm_irq_exception
arm_irq_exception:
    msr SPSel, #1
    isb
    sub sp, sp, #288
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30, [sp, #240]
    bl arm_gic_irq_dispatch
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]
    ldp x8, x9, [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    ldp x18, x19, [sp, #144]
    ldp x20, x21, [sp, #160]
    ldp x22, x23, [sp, #176]
    ldp x24, x25, [sp, #192]
    ldp x26, x27, [sp, #208]
    ldp x28, x29, [sp, #224]
    ldr x30, [sp, #240]
    add sp, sp, #288
    eret

.global arm_unexpected_exception
arm_unexpected_exception:
    b arm_unexpected_exception

.global arm_mmu_read_state
arm_mmu_read_state:
    mrs x1, CurrentEL
    lsr x2, x1, #2
    str x2, [x0, #0]
    cmp x1, #4
    b.eq 1f
    cmp x1, #8
    b.eq 2f
    mov w0, #0
    ret
1:
    mrs x2, TTBR0_EL1
    str x2, [x0, #8]
    mrs x2, TCR_EL1
    str x2, [x0, #16]
    mrs x2, MAIR_EL1
    str x2, [x0, #24]
    mrs x2, SCTLR_EL1
    str x2, [x0, #32]
    mrs x2, ID_AA64MMFR0_EL1
    str x2, [x0, #56]
    mrs x2, VBAR_EL1
    str x2, [x0, #64]
    b 3f
2:
    mrs x2, TTBR0_EL2
    str x2, [x0, #8]
    mrs x2, TCR_EL2
    str x2, [x0, #16]
    mrs x2, MAIR_EL2
    str x2, [x0, #24]
    mrs x2, SCTLR_EL2
    str x2, [x0, #32]
    mrs x2, ID_AA64MMFR0_EL1
    str x2, [x0, #56]
    mrs x2, VBAR_EL2
    str x2, [x0, #64]
3:
    mov x2, sp
    str x2, [x0, #40]
    adr x2, 4f
    str x2, [x0, #48]
    mov w0, #1
    ret
4:

.global arm_mmu_translate_current
arm_mmu_translate_current:
    mov x1, x0
    mrs x2, CurrentEL
    cmp x2, #4
    b.eq 1f
    cmp x2, #8
    b.eq 2f
    b 3f
1:
    at s1e1r, x1
    isb
    mrs x0, PAR_EL1
    b 4f
2:
    at s1e2r, x1
    isb
    mrs x0, PAR_EL1
4:
    tbnz x0, #0, 3f
    ubfx x0, x0, #12, #36
    lsl x0, x0, #12
    ubfx x1, x1, #0, #12
    orr x0, x0, x1
    ret
3:
    mov x0, #-1
    ret

.global arm_mmu_install
arm_mmu_install:
    cmp x3, #1
    b.eq 1f
    cmp x3, #2
    b.eq 2f
    mov w0, #0
    ret
1:
    dsb ishst
    tlbi vmalle1
    dsb ish
    msr MAIR_EL1, x2
    msr TCR_EL1, x1
    isb
    msr TTBR0_EL1, x0
    isb
    tlbi vmalle1
    dsb ish
    isb
    mov w0, #1
    ret
2:
    dsb ishst
    tlbi alle2
    dsb ish
    msr MAIR_EL2, x2
    msr TCR_EL2, x1
    isb
    msr TTBR0_EL2, x0
    isb
    tlbi alle2
    dsb ish
    isb
    mov w0, #1
    ret

.global arm_mmu_invalidate_current
arm_mmu_invalidate_current:
    mrs x0, CurrentEL
    cmp x0, #4
    b.eq 1f
    cmp x0, #8
    b.eq 2f
    ret
1:
    dsb ishst
    tlbi vmalle1
    dsb ish
    isb
    ret
2:
    dsb ishst
    tlbi alle2
    dsb ish
    isb
    ret
