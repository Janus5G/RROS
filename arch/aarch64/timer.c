#include "efi.h"

u64 arch_timer_now(void)
{
    u64 counter = 0;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(counter));
    return counter;
}
