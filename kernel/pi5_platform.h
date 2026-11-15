#ifndef EXPERIMENTALOS_KERNEL_PI5_PLATFORM_H
#define EXPERIMENTALOS_KERNEL_PI5_PLATFORM_H

#include "efi.h"

typedef enum {
    OS_PI5_CAP_FDT_DISCOVERY = 1,
    OS_PI5_CAP_REG_METADATA,
    OS_PI5_CAP_RP1_METADATA,
    OS_PI5_CAP_RP1_PCIE,
    OS_PI5_CAP_USB,
    OS_PI5_CAP_MICROSD,
    OS_PI5_CAP_HDMI,
    OS_PI5_CAP_ETHERNET,
    OS_PI5_CAP_WIFI,
    OS_PI5_CAP_GPIO,
    OS_PI5_CAP_I2C,
    OS_PI5_CAP_SPI,
    OS_PI5_CAP_UART,
    OS_PI5_CAP_AUDIO,
    OS_PI5_CAP_CAMERA
} os_pi5_capability_id;

typedef enum {
    OS_PI5_CAPABILITY_NOT_IMPLEMENTED = 0,
    OS_PI5_CAPABILITY_METADATA_ONLY,
    OS_PI5_CAPABILITY_SOFTWARE_IMPLEMENTED
} os_pi5_capability_state;

typedef struct {
    os_pi5_capability_id id;
    os_pi5_capability_state state;
    const char *name;
} os_pi5_capability;

const os_pi5_capability *os_pi5_capabilities(usize *count);

#endif
