#include "../../kernel/ai_worker.h"

#include <stdio.h>

int main(void)
{
    const char *failure = os_ai_core_self_test();
    if (failure) {
        printf("FAIL %s\n", failure);
        return 1;
    }
    printf("PASS G13-WORKER: process-isolated worker, capabilities, cancellation, crash containment and restart\n");
    return 0;
}
