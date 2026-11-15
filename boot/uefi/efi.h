#ifndef EXPERIMENTALOS_BOOT_UEFI_EFI_H
#define EXPERIMENTALOS_BOOT_UEFI_EFI_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef signed int s32;
typedef unsigned long long u64;
typedef __SIZE_TYPE__ usize;
typedef u64 efi_status;

typedef struct efi_text_output_protocol efi_text_output_protocol;
typedef struct efi_simple_text_input_protocol efi_simple_text_input_protocol;
typedef struct efi_graphics_output_protocol efi_graphics_output_protocol;
typedef efi_status (*efi_output_string)(
    efi_text_output_protocol *self,
    const u16 *string
);

struct efi_text_output_protocol {
    void *reset;
    efi_output_string output_string;
};

typedef struct {
    u16 scan_code;
    u16 unicode_char;
} efi_input_key;

typedef efi_status (*efi_read_key_stroke)(
    efi_simple_text_input_protocol *self,
    efi_input_key *key
);

struct efi_simple_text_input_protocol {
    void *reset;
    efi_read_key_stroke read_key_stroke;
    void *wait_for_key;
};

typedef efi_status (*efi_get_memory_map)(
    usize *memory_map_size,
    void *memory_map,
    usize *map_key,
    usize *descriptor_size,
    u32 *descriptor_version
);

typedef efi_status (*efi_allocate_pages)(
    u32 allocation_type,
    u32 memory_type,
    usize pages,
    u64 *memory
);
typedef efi_status (*efi_free_pages)(u64 memory, usize pages);

typedef efi_status (*efi_allocate_pool)(
    u32 memory_type,
    usize size,
    void **buffer
);

typedef efi_status (*efi_create_event)(
    u32 event_type,
    u32 notify_tpl,
    void *notify_function,
    void *notify_context,
    void **event
);

typedef efi_status (*efi_set_timer)(
    void *event,
    u32 timer_type,
    u64 trigger_time
);

typedef efi_status (*efi_wait_for_event)(
    usize number_of_events,
    void **events,
    usize *index
);

typedef efi_status (*efi_close_event)(void *event);

typedef struct {
    u32 data1;
    u16 data2;
    u16 data3;
    u8 data4[8];
} efi_guid;

typedef struct {
    efi_guid vendor_guid;
    void *vendor_table;
} efi_configuration_table;

typedef struct {
    u32 revision;
    void *parent_handle;
    void *system_table;
    void *device_handle;
    void *file_path;
    void *reserved;
    u32 load_options_size;
    void *load_options;
    void *image_base;
    u64 image_size;
    u32 image_code_type;
    u32 image_data_type;
    void *unload;
} efi_loaded_image_protocol;

typedef struct {
    u32 type;
    u32 padding;
    u64 physical_start;
    u64 virtual_start;
    u64 number_of_pages;
    u64 attribute;
} efi_memory_descriptor;

typedef efi_status (*efi_locate_protocol)(
    const efi_guid *protocol,
    void *registration,
    void **interface
);
typedef efi_status (*efi_handle_protocol)(
    void *handle,
    const efi_guid *protocol,
    void **interface
);

typedef struct {
    u32 version;
    u32 horizontal_resolution;
    u32 vertical_resolution;
    u32 pixel_format;
    u32 pixel_information[4];
    u32 pixels_per_scan_line;
} efi_graphics_output_mode_information;

typedef struct {
    u32 max_mode;
    u32 mode;
    efi_graphics_output_mode_information *information;
    usize information_size;
    u64 framebuffer_base;
    usize framebuffer_size;
} efi_graphics_output_mode;

struct efi_graphics_output_protocol {
    void *query_mode;
    void *set_mode;
    void *blt;
    efi_graphics_output_mode *mode;
};

typedef efi_status (*efi_exit_boot_services)(void *image_handle, usize map_key);
typedef efi_status (*efi_stall)(usize microseconds);

typedef struct {
    u8 header[24];
    void *firmware_vendor;
    u32 firmware_revision;
    u32 padding;
    void *console_in_handle;
    efi_simple_text_input_protocol *console_in;
    void *console_out_handle;
    efi_text_output_protocol *console_out;
    void *standard_error_handle;
    void *standard_error;
    void *runtime_services;
    struct efi_boot_services *boot_services;
    usize number_of_table_entries;
    void *configuration_table;
} efi_system_table;

typedef struct efi_boot_services {
    u8 header[24];
    void *before_allocate_pages[2];
    efi_allocate_pages allocate_pages;
    efi_free_pages free_pages;
    efi_get_memory_map get_memory_map;
    efi_allocate_pool allocate_pool;
    void *before_create_event;
    efi_create_event create_event;
    efi_set_timer set_timer;
    efi_wait_for_event wait_for_event;
    void *before_close_event;
    efi_close_event close_event;
    void *before_handle_protocol[4];
    efi_handle_protocol handle_protocol;
    void *after_handle_protocol[9];
    efi_exit_boot_services exit_boot_services;
    void *before_stall[1];
    efi_stall stall;
    void *before_locate_protocol[8];
    efi_locate_protocol locate_protocol;
} efi_boot_services;

#endif
