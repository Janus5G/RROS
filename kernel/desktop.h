#ifndef EXPERIMENTALOS_KERNEL_DESKTOP_H
#define EXPERIMENTALOS_KERNEL_DESKTOP_H

#include "graphics.h"

int os_classic_desktop_self_test(
    const os_framebuffer *framebuffer,
    u16 activation_key
);
int os_flow_desktop_self_test(
    const os_framebuffer *framebuffer,
    u16 activation_key
);

#endif
