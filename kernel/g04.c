#include "g04.h"

#define EFI_EVENT_TIMER 0x80000000u
#define EFI_TIMER_CANCEL 0u
#define EFI_TIMER_RELATIVE 2u
#define EFI_TIMER_TEST_100NS 100000u
#define EFI_INPUT_TIMEOUT_100NS 30000000u

extern u64 arch_timer_now(void);

int os_g04_device_info_test(const efi_system_table *system_table)
{
    return system_table != (const efi_system_table *)0
        && system_table->firmware_vendor != (void *)0
        && system_table->console_in != (efi_simple_text_input_protocol *)0
        && system_table->console_in->read_key_stroke
            != (efi_read_key_stroke)0
        && system_table->console_in->wait_for_key != (void *)0;
}

int os_g04_write_marker(efi_system_table *system_table, const char *marker)
{
    if (system_table == (efi_system_table *)0
        || system_table->console_out == (efi_text_output_protocol *)0
        || system_table->console_out->output_string == (efi_output_string)0
        || marker == (const char *)0) {
        return 0;
    }

    u16 wide[64];
    usize length = 0;
    while (marker[length] != '\0' && length < 63) {
        wide[length] = (u8)marker[length];
        ++length;
    }
    if (marker[length] != '\0') {
        return 0;
    }
    wide[length] = 0;
    return system_table->console_out->output_string(
        system_table->console_out, wide) == 0;
}

int os_g04_timer_test(efi_boot_services *boot_services)
{
    if (boot_services == (efi_boot_services *)0
        || boot_services->create_event == (efi_create_event)0
        || boot_services->set_timer == (efi_set_timer)0
        || boot_services->wait_for_event == (efi_wait_for_event)0
        || boot_services->close_event == (efi_close_event)0) {
        return 0;
    }

    void *timer_event = (void *)0;
    if (boot_services->create_event(
            EFI_EVENT_TIMER, 0, (void *)0, (void *)0, &timer_event) != 0
        || timer_event == (void *)0) {
        return 0;
    }

    void *events[1] = {timer_event};
    usize event_index = ~(usize)0;
    u64 before = arch_timer_now();
    int passed = boot_services->set_timer(
            timer_event, EFI_TIMER_RELATIVE, EFI_TIMER_TEST_100NS) == 0
        && boot_services->wait_for_event(1, events, &event_index) == 0
        && event_index == 0
        && arch_timer_now() > before;
    (void)boot_services->set_timer(timer_event, EFI_TIMER_CANCEL, 0);
    if (boot_services->close_event(timer_event) != 0) {
        passed = 0;
    }
    return passed;
}

int os_g04_wait_for_key_test(
    efi_system_table *system_table,
    efi_boot_services *boot_services,
    u16 expected_character
)
{
    if (system_table == (efi_system_table *)0
        || system_table->console_in == (efi_simple_text_input_protocol *)0
        || system_table->console_in->wait_for_key == (void *)0
        || boot_services == (efi_boot_services *)0
        || boot_services->create_event == (efi_create_event)0
        || boot_services->set_timer == (efi_set_timer)0
        || boot_services->wait_for_event == (efi_wait_for_event)0
        || boot_services->close_event == (efi_close_event)0) {
        return 0;
    }

    efi_simple_text_input_protocol *input = system_table->console_in;
    void *timer_event = (void *)0;
    if (boot_services->create_event(
            EFI_EVENT_TIMER, 0, (void *)0, (void *)0, &timer_event) != 0
        || timer_event == (void *)0) {
        return 0;
    }

    void *events[2] = {input->wait_for_key, timer_event};
    usize event_index = ~(usize)0;
    int passed = boot_services->set_timer(
            timer_event, EFI_TIMER_RELATIVE, EFI_INPUT_TIMEOUT_100NS) == 0
        && boot_services->wait_for_event(2, events, &event_index) == 0
        && event_index == 0;

    if (passed) {
        efi_input_key key = {0, 0};
        passed = input->read_key_stroke(input, &key) == 0
            && key.unicode_char == expected_character;
    }
    (void)boot_services->set_timer(timer_event, EFI_TIMER_CANCEL, 0);
    if (boot_services->close_event(timer_event) != 0) {
        passed = 0;
    }
    return passed;
}
