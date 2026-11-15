#include "efi.h"

u64 arch_timer_now(void)
{
    u32 low = 0;
    u32 high = 0;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return ((u64)high << 32) | low;
}
