#ifndef EXPERIMENTALOS_KERNEL_G04_H
#define EXPERIMENTALOS_KERNEL_G04_H

#include "efi.h"

int os_g04_device_info_test(const efi_system_table *system_table);
int os_g04_write_marker(efi_system_table *system_table, const char *marker);
int os_g04_timer_test(efi_boot_services *boot_services);
int os_g04_wait_for_key_test(
    efi_system_table *system_table,
    efi_boot_services *boot_services,
    u16 expected_character
);

#endif
