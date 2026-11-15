#include "experimentalos.h"

int experimentalos_app_main(void)
{
    volatile u64 state = 0x4558504f53454431ull;
    if (os_app_validate_memory(
            (const void *)&state,
            sizeof(state),
            OS_APP_MEMORY_READ | OS_APP_MEMORY_WRITE) != 0) {
        return 1;
    }
    if (os_app_yield() < 0) {
        return 2;
    }
    return (int)os_app_exit(0);
}
