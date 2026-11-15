#include "device_tree.h"
#include "pi5_driver.h"
#include "pi5_platform.h"

#define FDT_HEADER_SIZE 40u
#define FDT_MAGIC 0xd00dfeedu
#define FDT_BEGIN_NODE 1u
#define FDT_END_NODE 2u
#define FDT_PROPERTY 3u
#define FDT_END 9u
#define FDT_STRUCTURE_CAPACITY 4096u
#define FDT_STRINGS_CAPACITY 1024u
#define FDT_BLOB_CAPACITY 8192u

static u8 g_structure[FDT_STRUCTURE_CAPACITY];
static u8 g_strings[FDT_STRINGS_CAPACITY];
static u8 g_blob[FDT_BLOB_CAPACITY];
static os_device_tree g_tree;
static usize g_structure_size;
static usize g_strings_size;
static usize g_blob_size;
static u32 g_failure_id;
static u32 g_fixture_phy_handle = 24u;
static u32 g_fixture_phy_reset_pin = 32u;
static usize g_fixture_local_mac_length = 6u;
static os_device_tree g_owned_tree;
static os_pi5_rp1_pci g_rp1_owner;

typedef struct {
    u32 registers[OS_PI5_RP1_CFE_REGION_COUNT][0x4000u / sizeof(u32)];
    u64 addresses[OS_PI5_RP1_CFE_REGION_COUNT];
    u32 sizes[OS_PI5_RP1_CFE_REGION_COUNT];
    u32 map_calls;
    u32 unmap_calls;
    u32 fail_map_call;
    u8 mapped[OS_PI5_RP1_CFE_REGION_COUNT];
    u8 unmap_order[OS_PI5_RP1_CFE_REGION_COUNT];
    u8 invalid_unmap;
} cfe_mapper_model;

static void write_be32(u8 *destination, u32 value)
{
    destination[0] = (u8)(value >> 24);
    destination[1] = (u8)(value >> 16);
    destination[2] = (u8)(value >> 8);
    destination[3] = (u8)value;
}

static void write_be64(u8 *destination, u64 value)
{
    write_be32(destination, (u32)(value >> 32));
    write_be32(destination + 4u, (u32)value);
}

static int append_structure(const u8 *bytes, usize size)
{
    usize aligned = (size + 3u) & ~(usize)3u;
    if (aligned > FDT_STRUCTURE_CAPACITY - g_structure_size) {
        return 0;
    }
    for (usize i = 0; i < size; ++i) {
        g_structure[g_structure_size + i] = bytes[i];
    }
    for (usize i = size; i < aligned; ++i) {
        g_structure[g_structure_size + i] = 0;
    }
    g_structure_size += aligned;
    return 1;
}

static int append_token(u32 token)
{
    u8 bytes[4];
    write_be32(bytes, token);
    return append_structure(bytes, sizeof(bytes));
}

static int append_node_name(const char *name)
{
    u8 bytes[OS_DEVICE_TREE_NAME_MAX];
    usize length = 0;
    while (name[length] != '\0' && length + 1u < sizeof(bytes)) {
        bytes[length] = (u8)name[length];
        ++length;
    }
    if (name[length] != '\0') {
        return 0;
    }
    bytes[length++] = 0;
    return append_structure(bytes, length);
}

static int property_name_offset(const char *name, u32 *offset)
{
    usize length = 0;
    while (name[length] != '\0') {
        ++length;
    }
    for (usize i = 0; i < g_strings_size;) {
        usize end = i;
        while (end < g_strings_size && g_strings[end] != 0) {
            ++end;
        }
        if (end - i == length) {
            usize j = 0;
            while (j < length && g_strings[i + j] == (u8)name[j]) {
                ++j;
            }
            if (j == length) {
                *offset = (u32)i;
                return 1;
            }
        }
        i = end + 1u;
    }
    if (length + 1u > FDT_STRINGS_CAPACITY - g_strings_size) {
        return 0;
    }
    *offset = (u32)g_strings_size;
    for (usize i = 0; i < length; ++i) {
        g_strings[g_strings_size++] = (u8)name[i];
    }
    g_strings[g_strings_size++] = 0;
    return 1;
}

static int append_property(
    const char *name,
    const u8 *data,
    usize length
)
{
    u32 name_offset = 0;
    if (length > OS_DEVICE_TREE_MAX_REGIONS * 20u
        || !property_name_offset(name, &name_offset)) {
        return 0;
    }
    u8 header[12];
    write_be32(header, FDT_PROPERTY);
    write_be32(header + 4u, (u32)length);
    write_be32(header + 8u, name_offset);
    return append_structure(header, 4u)
        && append_structure(header + 4u, 8u)
        && append_structure(data, length);
}

static int append_string_property(const char *name, const char *value)
{
    u8 data[OS_DEVICE_TREE_COMPATIBLE_MAX];
    usize length = 0;
    while (value[length] != '\0'
        && length + 1u < sizeof(data)) {
        data[length] = (u8)value[length];
        ++length;
    }
    if (value[length] != '\0') {
        return 0;
    }
    data[length++] = 0;
    return append_property(name, data, length);
}

static int append_two_string_property(
    const char *name,
    const char *first,
    const char *second
)
{
    u8 data[OS_DEVICE_TREE_COMPATIBLE_LIST_MAX];
    usize length = 0;
    const char *values[2] = {first, second};
    for (usize string_index = 0; string_index < 2u; ++string_index) {
        usize i = 0;
        while (values[string_index][i] != '\0'
            && length + 1u < sizeof(data)) {
            data[length++] = (u8)values[string_index][i++];
        }
        if (values[string_index][i] != '\0') {
            return 0;
        }
        data[length++] = 0;
    }
    return append_property(name, data, length);
}

static int append_cell_property(const char *name, u32 value)
{
    u8 data[4];
    write_be32(data, value);
    return append_property(name, data, sizeof(data));
}

static int append_cells_property(const char *name, const u32 *cells, usize count)
{
    u8 data[OS_DEVICE_TREE_MAX_REGIONS * 20u];
    if (count == 0 || count > sizeof(data) / 4u) {
        return 0;
    }
    for (usize i = 0; i < count; ++i) {
        write_be32(data + i * 4u, cells[i]);
    }
    return append_property(name, data, count * 4u);
}

static int append_reg_property(u64 address, u64 size)
{
    u8 data[16];
    write_be64(data, address);
    write_be64(data + 8u, size);
    return append_property("reg", data, sizeof(data));
}

static int append_three_regs_property(
    u64 address0,
    u64 size0,
    u64 address1,
    u64 size1,
    u64 address2,
    u64 size2
)
{
    u8 data[48];
    write_be64(data, address0);
    write_be64(data + 8u, size0);
    write_be64(data + 16u, address1);
    write_be64(data + 24u, size1);
    write_be64(data + 32u, address2);
    write_be64(data + 40u, size2);
    return append_property("reg", data, sizeof(data));
}

static int append_two_regs_property(
    u64 address0,
    u64 size0,
    u64 address1,
    u64 size1
)
{
    u8 data[32];
    write_be64(data, address0);
    write_be64(data + 8u, size0);
    write_be64(data + 16u, address1);
    write_be64(data + 24u, size1);
    return append_property("reg", data, sizeof(data));
}

static int append_begin_node(const char *name)
{
    return append_token(FDT_BEGIN_NODE) && append_node_name(name);
}

static int append_end_node(void)
{
    return append_token(FDT_END_NODE);
}

static int finish_fixture_blob(void)
{
    usize structure_offset = FDT_HEADER_SIZE + 16u;
    usize strings_offset = structure_offset + g_structure_size;
    g_blob_size = strings_offset + g_strings_size;
    if (g_blob_size > sizeof(g_blob)) {
        return 0;
    }
    for (usize i = 0; i < g_blob_size; ++i) {
        g_blob[i] = 0;
    }
    write_be32(g_blob, FDT_MAGIC);
    write_be32(g_blob + 4u, (u32)g_blob_size);
    write_be32(g_blob + 8u, (u32)structure_offset);
    write_be32(g_blob + 12u, (u32)strings_offset);
    write_be32(g_blob + 16u, FDT_HEADER_SIZE);
    write_be32(g_blob + 20u, 17u);
    write_be32(g_blob + 24u, 16u);
    write_be32(g_blob + 32u, (u32)g_strings_size);
    write_be32(g_blob + 36u, (u32)g_structure_size);
    for (usize i = 0; i < g_structure_size; ++i) {
        g_blob[structure_offset + i] = g_structure[i];
    }
    for (usize i = 0; i < g_strings_size; ++i) {
        g_blob[strings_offset + i] = g_strings[i];
    }
    return 1;
}

static int build_fixture(
    u32 interrupt_parent,
    usize assigned_address_cell_count,
    u32 clock_phandle,
    usize dma_range_cell_count,
    u32 msi_parent,
    u32 scl_rising_time_ns,
    u32 scl_falling_time_ns,
    int duplicate_scl_rising_time
)
{
    g_structure_size = 0;
    g_strings_size = 0;
    g_blob_size = 0;
    if (!append_begin_node("")
        || !append_cell_property("#address-cells", 2)
        || !append_cell_property("#size-cells", 2)
        || !append_cell_property("interrupt-parent", interrupt_parent)
        || !append_begin_node("rp1@1f00000000")
        || !append_string_property("compatible", "simple-bus")
        || !append_string_property("status", "okay")
        || !append_reg_property(0x0000001f00000000ull, 0x00100000ull)
        || !append_cell_property("#address-cells", 2)
        || !append_cell_property("#size-cells", 2)
        || !append_cells_property(
            "ranges",
            (const u32[]){0, 0, 0x1f, 0, 0, 0x00200000},
            6)
        || !append_begin_node("uart@100000")
        || !append_string_property("compatible", "arm,pl011")
        || !append_string_property("status", "okay")
        || !append_property("skip-init", (const u8 *)0, 0u)
        || !append_reg_property(0x00100000ull, 0x1000ull)
        || !append_cells_property(
            "interrupts", (const u32[]){0u, 19u, 4u}, 3u)
        || !append_cells_property(
            "clocks", (const u32[]){clock_phandle, 5u, 4u}, 3u)
        || !append_cells_property(
            "resets", (const u32[]){3u, 1u}, 2u)
        || !append_end_node()
        || !append_begin_node("pinctrl@d0000")
        || !append_two_string_property(
            "compatible", "vendor,gpio-compatible",
            "raspberrypi,rp1-gpio")
        || !append_string_property("status", "okay")
        || !append_cell_property("phandle", 23u)
        || !append_cell_property("#gpio-cells", 2u)
        || !append_cells_property(
            "brcm,gpio-bank-widths", (const u32[]){32u, 22u}, 2u)
        || !append_three_regs_property(
            0x000d0000ull, 0xc000ull,
            0x000e0000ull, 0xc000ull,
            0x000f0000ull, 0xc000ull)
        || !append_end_node()
        || !append_end_node()
        || !append_begin_node("usb@200000")
        || !append_string_property("compatible", "example,usb")
        || !append_string_property("dr_mode", "host")
        || !append_string_property("status", "disabled")
        || !append_reg_property(0x0000001f00200000ull, 0x1000ull)
        || !append_cells_property("phys", (const u32[]){30u}, 1u)
        || !append_string_property("phy-names", "usb3-phy")
        || !append_cell_property("power-domains", 31u)
        || !append_string_property("usb-psy-name", "vbus")
        || !append_cell_property("vbus-supply", 32u)
        || !append_end_node()
        || !append_begin_node("pcie@1f10000000")
        || !append_string_property("compatible", "pci-host-ecam-generic")
        || !append_cell_property("msi-parent", msi_parent)
        || !append_cell_property("num-lanes", 4u)
        || !append_cell_property("#address-cells", 3u)
        || !append_cell_property("#size-cells", 2u)
        || !append_cells_property(
            "ranges",
            (const u32[]){
                0x02000000u, 0u, 0x40000000u,
                0x0000001fu, 0x10000000u,
                0u, 0x00100000u,
                0x43000000u, 4u, 0u,
                0x0000001fu, 0x20000000u,
                3u, 0u,
            },
            14u)
        || !append_cells_property(
            "dma-ranges",
            (const u32[]){
                0x02000000u, 0u, 0x40000000u,
                0x0000001fu, 0x10000000u,
                0u, 0x00100000u,
            },
            dma_range_cell_count)
        || !append_begin_node("endpoint@40001000")
        || !append_string_property("compatible", "example,pci-endpoint")
        || !append_cells_property(
            "reg",
            (const u32[]){
                0x00000800u, 0u, 0u, 0u, 0u,
            },
            5u)
        || !append_cells_property(
            "assigned-addresses",
            (const u32[]){
                0x02000810u, 0u, 0x40001000u, 0u, 0x1000u,
            },
            assigned_address_cell_count)
        || !append_end_node()
        || !append_end_node()
        || !append_begin_node("interrupt-controller@0")
        || !append_string_property("compatible", "arm,gic-400")
        || !append_cell_property("phandle", 1u)
        || !append_cell_property("#interrupt-cells", 3u)
        || !append_property(
            "interrupt-controller", (const u8 *)0, 0u)
        || !append_end_node()
        || !append_begin_node("clock-controller@2")
        || !append_string_property("compatible", "example,clock-controller")
        || !append_cell_property("phandle", 2u)
        || !append_cell_property("#clock-cells", 1u)
        || !append_end_node()
        || !append_begin_node("reset-controller@3")
        || !append_string_property("compatible", "example,reset-controller")
        || !append_cell_property("phandle", 3u)
        || !append_cell_property("#reset-cells", 1u)
        || !append_end_node()
        || !append_begin_node("fixed-clock")
        || !append_string_property("compatible", "fixed-clock")
        || !append_cell_property("phandle", 4u)
        || !append_cell_property("#clock-cells", 0u)
        || !append_cell_property("clock-frequency", 27000000u)
        || !append_end_node()
        || !append_begin_node("mip@1000130000")
        || !append_string_property("compatible", "brcm,bcm2712-mip")
        || !append_string_property("status", "okay")
        || !append_cell_property("phandle", 11u)
        || !append_property("msi-controller", (const u8 *)0, 0u)
        || !append_cells_property(
            "msi-ranges", (const u32[]){1u, 0u, 128u, 1u, 64u}, 5u)
        || !append_cell_property("brcm,msi-offset", 0u)
        || !append_two_regs_property(
            0x1000130000ull, 0xc0u, 0xfffffff000ull, 0x1000u)
        || !append_end_node()
        || !append_begin_node("i2c-timing-test")
        || !append_string_property("compatible", "snps,designware-i2c")
        || !append_cell_property(
            "i2c-scl-rising-time-ns", scl_rising_time_ns)
        || (duplicate_scl_rising_time
            && !append_cell_property(
                "i2c-scl-rising-time-ns", scl_rising_time_ns))
        || !append_cell_property(
            "i2c-scl-falling-time-ns", scl_falling_time_ns)
        || !append_end_node()
        || !append_begin_node("sdhci@7e300000")
        || !append_string_property("compatible", "brcm,bcm2712-sdhci")
        || !append_string_property("status", "okay")
        || !append_cell_property("vmmc-supply", 21u)
        || !append_cell_property("vqmmc-supply", 22u)
        || !append_cells_property(
            "cd-gpios", (const u32[]){20u, 5u, 1u}, 3u)
        || !append_end_node()
        || !append_begin_node("gpio@7d517c00")
        || !append_two_string_property(
            "compatible", "brcm,bcm7445-gpio", "brcm,brcmstb-gpio")
        || !append_cell_property("phandle", 20u)
        || !append_reg_property(0x7d517c00ull, 0x40u)
        || !append_cell_property("#gpio-cells", 2u)
        || !append_cells_property(
            "brcm,gpio-bank-widths", (const u32[]){17u, 6u}, 2u)
        || !append_end_node()
        || !append_begin_node("sd-vcc-reg")
        || !append_string_property("compatible", "regulator-fixed")
        || !append_cell_property("phandle", 21u)
        || !append_cell_property("regulator-min-microvolt", 3300000u)
        || !append_cell_property("regulator-max-microvolt", 3300000u)
        || !append_property("regulator-boot-on", (const u8 *)0, 0u)
        || !append_property("enable-active-high", (const u8 *)0, 0u)
        || !append_cells_property(
            "gpios", (const u32[]){20u, 4u, 0u}, 3u)
        || !append_end_node()
        || !append_begin_node("sd-io-reg")
        || !append_string_property("compatible", "regulator-gpio")
        || !append_cell_property("phandle", 22u)
        || !append_cell_property("regulator-min-microvolt", 1800000u)
        || !append_cell_property("regulator-max-microvolt", 3300000u)
        || !append_cell_property("regulator-settling-time-us", 5000u)
        || !append_property("regulator-boot-on", (const u8 *)0, 0u)
        || !append_property("regulator-always-on", (const u8 *)0, 0u)
        || !append_cells_property(
            "gpios", (const u32[]){20u, 3u, 0u}, 3u)
        || !append_cells_property(
            "states", (const u32[]){1800000u, 1u, 3300000u, 0u}, 4u)
        || !append_end_node()
        || !append_begin_node("ethernet@40100000")
        || !append_string_property("compatible", "raspberrypi,rp1-gem")
        || !append_string_property("status", "okay")
        || !append_reg_property(0x40100000ull, 0x4000ull)
        || !append_cells_property(
            "interrupts", (const u32[]){0u, 6u, 4u}, 3u)
        || !append_cells_property(
            "clocks",
            (const u32[]){2u, 12u, 2u, 12u, 2u, 16u, 2u, 29u},
            8u)
        || !append_property(
            "local-mac-address",
            (const u8[]){0x02u, 0u, 0u, 0u, 0u, 1u},
            g_fixture_local_mac_length)
        || !append_cell_property("phy-handle", g_fixture_phy_handle)
        || !append_cells_property(
            "phy-reset-gpios",
            (const u32[]){23u, g_fixture_phy_reset_pin, 1u}, 3u)
        || !append_cell_property("phy-reset-duration", 5u)
        || !append_cell_property("#address-cells", 1u)
        || !append_cell_property("#size-cells", 0u)
        || !append_begin_node("ethernet-phy@1")
        || !append_cell_property("phandle", 24u)
        || !append_end_node()
        || !append_end_node()
        || !append_begin_node("soc@107c000000")
        || !append_string_property("compatible", "simple-bus")
        || !append_cell_property("#address-cells", 1u)
        || !append_cell_property("#size-cells", 1u)
        || !append_cells_property(
            "ranges",
            (const u32[]){0u, 0x10u, 0u, 0x80000000u},
            4u)
        || !append_begin_node("hdmi@7c701400")
        || !append_string_property("compatible", "brcm,bcm2712-hdmi0")
        || !append_string_property("status", "okay")
        || !append_cells_property(
            "reg",
            (const u32[]){
                0x7c701400u, 0x300u,
                0x7c701000u, 0x200u,
                0x7c701d00u, 0x300u,
                0x7c702000u, 0x80u,
                0x7c703800u, 0x200u,
                0x7c704000u, 0x800u,
                0x7c700100u, 0x80u,
                0x7d510800u, 0x100u,
                0x7c720000u, 0x100u,
            },
            18u)
        || !append_cells_property(
            "resets", (const u32[]){3u, 1u}, 2u)
        || !append_end_node()
        || !append_end_node()
        || !append_end_node()
        || !append_token(FDT_END)) {
        return 0;
    }

    return finish_fixture_blob();
}

static int build_camera_fixture(void)
{
    g_structure_size = 0u;
    g_strings_size = 0u;
    g_blob_size = 0u;
    return append_begin_node("")
        && append_cell_property("#address-cells", 2u)
        && append_cell_property("#size-cells", 2u)
        && append_begin_node("rp1@1f00000000")
        && append_string_property("compatible", "simple-bus")
        && append_string_property("status", "okay")
        && append_reg_property(0x0000001f00000000ull, 0x00100000ull)
        && append_cell_property("#address-cells", 2u)
        && append_cell_property("#size-cells", 2u)
        && append_cells_property(
            "ranges",
            (const u32[]){0u, 0u, 0x1fu, 0u, 0u, 0x00200000u},
            6u)
        && append_begin_node("interrupt-controller@0")
        && append_string_property("compatible", "raspberrypi,rp1-intc")
        && append_cell_property("phandle", 26u)
        && append_cell_property("#interrupt-cells", 2u)
        && append_property(
            "interrupt-controller", (const u8 *)0, 0u)
        && append_end_node()
        && append_begin_node("clock-controller@100000")
        && append_string_property("compatible", "raspberrypi,rp1-clocks")
        && append_cell_property("phandle", 25u)
        && append_cell_property("#clock-cells", 1u)
        && append_end_node()
        && append_begin_node("cfe@100000")
        && append_string_property("compatible", "raspberrypi,rp1-cfe")
        && append_string_property("status", "okay")
        && append_cells_property(
            "reg",
            (const u32[]){
                0u, 0x00100000u, 0u, 0x1000u,
                0u, 0x00102000u, 0u, 0x1000u,
                0u, 0x00104000u, 0u, 0x1000u,
                0u, 0x00106000u, 0u, 0x1000u,
            },
            16u)
        && append_cell_property("interrupt-parent", 26u)
        && append_cells_property(
            "interrupts", (const u32[]){17u, 4u}, 2u)
        && append_cells_property(
            "clocks", (const u32[]){25u, 22u}, 2u)
        && append_begin_node("port@0")
        && append_begin_node("endpoint")
        && append_cell_property("phandle", 27u)
        && append_cell_property("remote-endpoint", 28u)
        && append_cells_property(
            "data-lanes", (const u32[]){1u, 2u}, 2u)
        && append_cell_property("clock-lanes", 0u)
        && append_end_node()
        && append_end_node()
        && append_end_node()
        && append_begin_node("audio_out@94000")
        && append_string_property("compatible", "raspberrypi,rp1-audio-out")
        && append_string_property("status", "okay")
        && append_reg_property(0x00094000ull, 0x4000u)
        && append_cells_property(
            "clocks", (const u32[]){25u, OS_PI5_RP1_CLOCK_ID_AUDIO_OUT}, 2u)
        && append_cells_property("dmas", (const u32[]){30u, 29u}, 2u)
        && append_end_node()
        && append_begin_node("dma@188000")
        && append_string_property("compatible", "snps,axi-dma-1.01a")
        && append_cell_property("phandle", 30u)
        && append_cell_property("#dma-cells", 1u)
        && append_cell_property("snps,dma-targets", 64u)
        && append_cell_property("dma-channels", 8u)
        && append_cell_property("snps,dma-masters", 1u)
        && append_cell_property("snps,data-width", 4u)
        && append_cells_property(
            "snps,block-size",
            (const u32[]){
                0x40000u, 0x40000u, 0x40000u, 0x40000u,
                0x40000u, 0x40000u, 0x40000u, 0x40000u,
            },
            OS_DEVICE_TREE_MAX_DMA_CHANNELS)
        && append_cells_property(
            "snps,axi-max-burst-len",
            (const u32[]){8u, 8u, 4u, 4u, 4u, 4u, 4u, 4u},
            OS_DEVICE_TREE_MAX_DMA_CHANNELS)
        && append_reg_property(0x00188000ull, 0x1000u)
        && append_cells_property(
            "clocks", (const u32[]){25u, 14u, 25u, 12u}, 4u)
        && append_cell_property("interrupt-parent", 26u)
        && append_cells_property(
            "interrupts", (const u32[]){40u, 4u}, 2u)
        && append_string_property("status", "okay")
        && append_end_node()
        && append_begin_node("i2s@a0000")
        && append_string_property("compatible", "snps,designware-i2s")
        && append_string_property("status", "okay")
        && append_reg_property(0x000a0000ull, 0x1000u)
        && append_cells_property(
            "clocks", (const u32[]){25u, OS_PI5_RP1_CLOCK_ID_I2S}, 2u)
        && append_cells_property(
            "dmas", (const u32[]){30u, 0x20u, 30u, 0x1fu}, 4u)
        && append_end_node()
        && append_begin_node("i2s@a4000")
        && append_string_property("compatible", "snps,designware-i2s")
        && append_string_property("status", "okay")
        && append_reg_property(0x000a4000ull, 0x1000u)
        && append_cells_property(
            "clocks", (const u32[]){25u, OS_PI5_RP1_CLOCK_ID_I2S}, 2u)
        && append_cells_property(
            "dmas", (const u32[]){30u, 0x22u, 30u, 0x21u}, 4u)
        && append_end_node()
        && append_end_node()
        && append_begin_node("camera-sensor@0")
        && append_begin_node("port")
        && append_begin_node("endpoint")
        && append_cell_property("phandle", 28u)
        && append_cell_property("remote-endpoint", 27u)
        && append_cells_property(
            "data-lanes", (const u32[]){1u, 2u}, 2u)
        && append_cell_property("clock-lanes", 0u)
        && append_end_node()
        && append_end_node()
        && append_end_node()
        && append_end_node()
        && append_token(FDT_END)
        && finish_fixture_blob();
}

static int build_iommu_fixture(
    int include_iommu_cells,
    u32 iommu_cells,
    const u32 *iommus,
    usize iommu_cell_count
)
{
    g_structure_size = 0u;
    g_strings_size = 0u;
    g_blob_size = 0u;
    if (!append_begin_node("")
        || !append_string_property("compatible", "test,root")
        || !append_begin_node("iommu@0")
        || !append_string_property(
            "compatible", "brcm,bcm2712-iommu")
        || !append_cell_property("phandle", 41u)
        || (include_iommu_cells
            && !append_cell_property("#iommu-cells", iommu_cells))
        || !append_end_node()
        || !append_begin_node("hvs@0")
        || !append_string_property(
            "compatible", "brcm,bcm2712-hvs")
        || !append_cells_property("iommus", iommus, iommu_cell_count)
        || !append_end_node()
        || !append_end_node()
        || !append_token(FDT_END)) {
        return 0;
    }
    return finish_fixture_blob();
}

static int test_iommu_reference_parsing(void)
{
    static const u32 no_args[] = {41u};
    static const u32 one_arg[] = {41u, 7u};
    static const u32 missing_provider[] = {42u};
    static const u32 missing_argument[] = {41u};

    if (!build_iommu_fixture(1, 0u, no_args, 1u)
        || !os_device_tree_parse(g_blob, g_blob_size, &g_tree)
        || g_tree.node_count != 3u
        || !g_tree.nodes[1].iommu_specifier_cells_present
        || g_tree.nodes[1].iommu_specifier_cells != 0u
        || g_tree.nodes[2].iommu_reference_count != 1u
        || g_tree.nodes[2].iommu_references[0].provider_index != 1u
        || g_tree.nodes[2].iommu_references[0].argument_count != 0u) {
        return 0;
    }
    if (!build_iommu_fixture(1, 1u, one_arg, 2u)
        || !os_device_tree_parse(g_blob, g_blob_size, &g_tree)
        || g_tree.nodes[2].iommu_reference_count != 1u
        || g_tree.nodes[2].iommu_references[0].argument_count != 1u
        || g_tree.nodes[2].iommu_references[0].arguments[0] != 7u) {
        return 0;
    }
    return build_iommu_fixture(1, 0u, missing_provider, 1u)
            && !os_device_tree_parse(g_blob, g_blob_size, &g_tree)
        && g_tree.node_count == 0u
        && build_iommu_fixture(0, 0u, no_args, 1u)
        && !os_device_tree_parse(g_blob, g_blob_size, &g_tree)
        && g_tree.node_count == 0u
        && build_iommu_fixture(1, 1u, missing_argument, 1u)
        && !os_device_tree_parse(g_blob, g_blob_size, &g_tree)
        && g_tree.node_count == 0u;
}

static int build_wlan_fixture(u32 startup_delay_us)
{
    g_structure_size = 0u;
    g_strings_size = 0u;
    g_blob_size = 0u;
    return append_begin_node("")
        && append_cell_property("#address-cells", 2u)
        && append_cell_property("#size-cells", 2u)
        && append_cell_property("interrupt-parent", 66u)
        && append_begin_node("clock-controller")
        && append_string_property("compatible", "fixed-clock")
        && append_cell_property("phandle", 65u)
        && append_cell_property("#clock-cells", 0u)
        && append_cell_property("clock-frequency", 200000000u)
        && append_end_node()
        && append_begin_node("interrupt-controller")
        && append_string_property("compatible", "arm,gic-400")
        && append_cell_property("phandle", 66u)
        && append_cell_property("#interrupt-cells", 3u)
        && append_property(
            "interrupt-controller", (const u8 *)0, 0u)
        && append_end_node()
        && append_begin_node("pcie@1000120000")
        && append_string_property("compatible", "brcm,bcm2712-pcie")
        && append_string_property("status", "okay")
        && append_cell_property("#address-cells", 2u)
        && append_cell_property("#size-cells", 2u)
        && append_begin_node("rp1")
        && append_string_property("compatible", "simple-bus")
        && append_string_property("status", "okay")
        && append_cell_property("#address-cells", 1u)
        && append_cell_property("#size-cells", 1u)
        && append_cells_property(
            "ranges",
            (const u32[]){0xc0400000u, 0u, 0u, 0x00410000u},
            4u)
        && append_begin_node("gpio")
        && append_string_property("compatible", "raspberrypi,rp1-gpio")
        && append_string_property("status", "okay")
        && append_cell_property("phandle", 51u)
        && append_cell_property("#gpio-cells", 2u)
        && append_property("gpio-controller", (const u8 *)0, 0u)
        && append_end_node()
        && append_end_node()
        && append_end_node()
        && append_begin_node("sdio2")
        && append_string_property("compatible", "brcm,bcm2712-sdhci")
        && append_string_property("status", "okay")
        && append_cells_property(
            "reg",
            (const u32[]){
                0u, 0x01100000u, 0u, 0x260u,
                0u, 0x01100400u, 0u, 0x200u,
            },
            8u)
        && append_cells_property("clocks", (const u32[]){65u}, 1u)
        && append_cells_property(
            "interrupts", (const u32[]){0u, 274u, 4u}, 3u)
        && append_cell_property("vmmc-supply", 67u)
        && append_cell_property("bus-width", 4u)
        && append_property("non-removable", (const u8 *)0, 0u)
        && append_cell_property("#address-cells", 1u)
        && append_cell_property("#size-cells", 0u)
        && append_begin_node("wifi@1")
        && append_string_property("compatible", "brcm,bcm4329-fmac")
        && append_string_property("status", "okay")
        && append_cell_property("reg", 1u)
        && append_end_node()
        && append_end_node()
        && append_begin_node("wl-on-reg")
        && append_string_property("compatible", "regulator-fixed")
        && append_string_property("status", "okay")
        && append_cell_property("phandle", 67u)
        && append_cell_property("regulator-min-microvolt", 3300000u)
        && append_cell_property("regulator-max-microvolt", 3300000u)
        && append_cells_property("gpio", (const u32[]){51u, 28u, 0u}, 3u)
        && append_cell_property("startup-delay-us", startup_delay_us)
        && append_property(
            "enable-active-high", (const u8 *)0, 0u)
        && append_end_node()
        && append_end_node()
        && append_token(FDT_END)
        && finish_fixture_blob();
}

typedef struct {
    volatile u32 registers[2][OS_BCM2712_SDHCI_HOST_REGION_SIZE
        / sizeof(u32)];
    u64 addresses[2];
    u32 sizes[2];
    u32 map_calls;
    u32 unmap_calls;
    u32 fail_map_call;
    u8 unmap_error;
} wlan_sdio2_mapper_model;

typedef struct {
    u16 clock_control;
    u32 read_calls;
    u32 fail_read_call;
    u32 stable_after_reads;
    u8 software_reset;
    u32 reset_read_calls;
    u32 fail_reset_read_call;
    u32 reset_clear_after_reads;
} wlan_sdio2_clock_model;

typedef struct {
    u32 argument;
    u32 response;
    u32 interrupt_status;
    u32 command_calls;
    u32 last_command;
    u32 fail_command_call;
    u32 fail_command_call2;
    u32 fifo[
        OS_BCM2712_SDIO_CMD53_MAX_BLOCK_COUNT];
    u32 fifo_words;
    u32 fifo_index;
    u32 fifo_expected_words;
    u16 block_size;
    u16 block_count;
    u16 transfer_mode;
    u32 ready_after_reads;
    u32 ready_read_calls;
    u32 ready_reads_total;
    u32 ready_status_mask;
    u32 ready_reassert_delay_reads;
    u8 io_enable;
    u8 ready_stuck;
    u8 data_error;
    u8 block_mode;
    u16 function1_block_size;
} wlan_sdio2_command_model;

static wlan_sdio2_command_model *g_wlan_sdio2_command_model;

typedef struct {
    u32 calls;
    u32 pin;
    u32 fail_call;
    u8 enabled;
    u8 fail_after_change;
} wlan_supply_output_model;

typedef struct {
    u32 calls;
    u32 fail_call;
    u8 bad_bound;
} wlan_prerequisite_model;

typedef struct {
    u32 calls;
    u32 minimum_us;
    u32 maximum_us;
    u8 fail;
} wlan_delay_model;

static void clear_wlan_bytes(void *memory, usize size)
{
    u8 *bytes = (u8 *)memory;
    for (usize i = 0; i < size; ++i) {
        bytes[i] = 0u;
    }
}

static int wlan_sdio2_map(
    void *context,
    u64 physical_address,
    u32 size,
    volatile u32 **mapped_base
)
{
    wlan_sdio2_mapper_model *model =
        (wlan_sdio2_mapper_model *)context;
    if (mapped_base != (volatile u32 **)0) {
        *mapped_base = (volatile u32 *)0;
    }
    if (model == (wlan_sdio2_mapper_model *)0
        || mapped_base == (volatile u32 **)0) {
        return 0;
    }
    u32 index = model->map_calls++;
    if (index >= 2u || model->fail_map_call == model->map_calls
        || physical_address != model->addresses[index]
        || size != model->sizes[index]) {
        return 0;
    }
    *mapped_base = model->registers[index];
    return 1;
}

static void wlan_sdio2_unmap(
    void *context,
    volatile u32 *mapped_base,
    u32 size
)
{
    wlan_sdio2_mapper_model *model =
        (wlan_sdio2_mapper_model *)context;
    if (model == (wlan_sdio2_mapper_model *)0) {
        return;
    }
    ++model->unmap_calls;
    u32 index = mapped_base == model->registers[0]
        ? 0u
        : (mapped_base == model->registers[1] ? 1u : 2u);
    if (index >= 2u || size != model->sizes[index]) {
        model->unmap_error = 1u;
    }
}

static int wlan_sdio2_clock_read16(
    void *context,
    u32 region,
    u32 offset,
    u16 *value
)
{
    wlan_sdio2_clock_model *model =
        (wlan_sdio2_clock_model *)context;
    if (model == (wlan_sdio2_clock_model *)0
        || region != 0u
        || offset != OS_BCM2712_SDHCI_CLOCK_CONTROL
        || value == (u16 *)0) {
        return 0;
    }
    ++model->read_calls;
    if (model->read_calls == model->fail_read_call) {
        return 0;
    }
    *value = model->clock_control;
    if (model->stable_after_reads != 0u
        && model->read_calls >= model->stable_after_reads) {
        *value |= OS_BCM2712_SDHCI_CLOCK_INTERNAL_STABLE;
    }
    return 1;
}

static int wlan_sdio2_reset_read8(
    void *context,
    u32 region,
    u32 offset,
    u8 *value
)
{
    wlan_sdio2_clock_model *model =
        (wlan_sdio2_clock_model *)context;
    if (model == (wlan_sdio2_clock_model *)0
        || region != 0u
        || offset != OS_BCM2712_SDHCI_SOFTWARE_RESET
        || value == (u8 *)0) {
        return 0;
    }
    ++model->reset_read_calls;
    if (model->reset_read_calls == model->fail_reset_read_call) {
        return 0;
    }
    *value = model->software_reset;
    if (model->reset_clear_after_reads != 0u
        && model->reset_read_calls >= model->reset_clear_after_reads) {
        *value = 0u;
    }
    return 1;
}

static int wlan_sdio2_command_read16(
    void *context,
    u32 region,
    u32 offset,
    u16 *value
)
{
    wlan_sdio2_command_model *model =
        g_wlan_sdio2_command_model;
    if (context == (void *)0
        || model == (wlan_sdio2_command_model *)0
        || region != 0u
        || value == (u16 *)0) {
        return 0;
    }
    if (offset == OS_BCM2712_SDHCI_CLOCK_CONTROL) {
        *value = OS_BCM2712_SDHCI_CLOCK_INTERNAL_ENABLE
            | OS_BCM2712_SDHCI_CLOCK_INTERNAL_STABLE
            | OS_BCM2712_SDHCI_CLOCK_CARD_ENABLE;
    } else if (offset == OS_BCM2712_SDHCI_BLOCK_SIZE) {
        *value = model->block_size;
    } else if (offset == OS_BCM2712_SDHCI_BLOCK_COUNT) {
        *value = model->block_count;
    } else if (offset == OS_BCM2712_SDHCI_TRANSFER_MODE) {
        *value = model->transfer_mode;
    } else {
        return 0;
    }
    return 1;
}

static int wlan_sdio2_command_read8(
    void *context,
    u32 region,
    u32 offset,
    u8 *value
)
{
    if (context == (void *)0
        || g_wlan_sdio2_command_model
            == (wlan_sdio2_command_model *)0
        || region != 0u
        || offset != OS_BCM2712_SDHCI_SOFTWARE_RESET
        || value == (u8 *)0) {
        return 0;
    }
    *value = 0u;
    return 1;
}

static int wlan_sdio2_command_write8(
    void *context,
    u32 region,
    u32 offset,
    u8 value
)
{
    (void)value;
    return context != (void *)0
        && g_wlan_sdio2_command_model
            != (wlan_sdio2_command_model *)0
        && region == 0u
        && offset == OS_BCM2712_SDHCI_SOFTWARE_RESET;
}

static int wlan_sdio2_command_read32(
    void *context,
    u32 region,
    u32 offset,
    u32 *value
)
{
    wlan_sdio2_command_model *model =
        g_wlan_sdio2_command_model;
    if (context == (void *)0
        || model == (wlan_sdio2_command_model *)0
        || region != 0u || value == (u32 *)0) {
        return 0;
    }
    if (offset == OS_BCM2712_SDHCI_PRESENT_STATE) {
        *value = 0u;
    } else if (offset == OS_BCM2712_SDHCI_INTERRUPT_STATUS) {
        if (model->ready_reassert_delay_reads != 0u) {
            --model->ready_reassert_delay_reads;
            if (model->ready_reassert_delay_reads == 0u) {
                u32 processed =
                    (model->transfer_mode
                            & OS_BCM2712_SDHCI_TRANSFER_READ) != 0u
                        ? model->fifo_index : model->fifo_words;
                if (processed < model->fifo_expected_words) {
                    model->interrupt_status |= model->ready_status_mask;
                }
            }
        }
        *value = model->interrupt_status;
    } else if (offset == OS_BCM2712_SDHCI_RESPONSE0) {
        *value = model->response;
    } else if (offset == OS_BCM2712_SDHCI_BUFFER
        && (model->transfer_mode
            & OS_BCM2712_SDHCI_TRANSFER_READ) != 0u
        && model->fifo_index < model->fifo_words) {
        *value = model->fifo[model->fifo_index++];
    } else {
        return 0;
    }
    return 1;
}

static int wlan_sdio2_command_write32(
    void *context,
    u32 region,
    u32 offset,
    u32 value
)
{
    wlan_sdio2_command_model *model =
        g_wlan_sdio2_command_model;
    if (context == (void *)0
        || model == (wlan_sdio2_command_model *)0
        || region != 0u) {
        return 0;
    }
    if (offset == OS_BCM2712_SDHCI_ARGUMENT) {
        model->argument = value;
    } else if (offset == OS_BCM2712_SDHCI_INTERRUPT_STATUS) {
        model->interrupt_status &= ~value;
        if ((value & model->ready_status_mask) != 0u) {
            u32 processed = (model->transfer_mode
                    & OS_BCM2712_SDHCI_TRANSFER_READ) != 0u
                ? model->fifo_index : model->fifo_words;
            if (processed == model->fifo_expected_words) {
                model->interrupt_status |=
                    OS_BCM2712_SDHCI_INT_DATA_END;
            } else if (model->block_mode) {
                model->ready_reassert_delay_reads = 2u;
            }
        }
    } else if (offset == OS_BCM2712_SDHCI_BUFFER
        && (model->transfer_mode
            & OS_BCM2712_SDHCI_TRANSFER_READ) == 0u
        && model->fifo_words < model->fifo_expected_words
        && model->fifo_words < sizeof(model->fifo)
            / sizeof(model->fifo[0])) {
        model->fifo[model->fifo_words++] = value;
    } else {
        return 0;
    }
    return 1;
}

static int wlan_sdio2_command_write16(
    void *context,
    u32 region,
    u32 offset,
    u16 value
)
{
    wlan_sdio2_command_model *model =
        g_wlan_sdio2_command_model;
    if (context == (void *)0
        || model == (wlan_sdio2_command_model *)0
        || region != 0u) {
        return 0;
    }
    if (offset == OS_BCM2712_SDHCI_BLOCK_SIZE) {
        model->block_size = value;
        return 1;
    }
    if (offset == OS_BCM2712_SDHCI_BLOCK_COUNT) {
        model->block_count = value;
        return 1;
    }
    if (offset == OS_BCM2712_SDHCI_TRANSFER_MODE) {
        model->transfer_mode = value;
        return 1;
    }
    if (offset != OS_BCM2712_SDHCI_COMMAND) {
        return 0;
    }
    ++model->command_calls;
    u32 command = value >> 8;
    model->last_command = command;
    if (model->command_calls == model->fail_command_call
        || model->command_calls == model->fail_command_call2) {
        return 0;
    }
    model->response = 0u;
    if (command == OS_BCM2712_SDIO_CMD_IO_SEND_OP_COND) {
        model->response =
            (1u << OS_BCM2712_SDIO_R4_FUNCTION_COUNT_SHIFT)
            | OS_BCM2712_SDHCI_OCR_VOLTAGE_MASK;
        if (model->argument != 0u) {
            model->response |= OS_BCM2712_SDIO_R4_READY;
        }
    } else if (command == OS_BCM2712_SDIO_CMD_IO_RW_DIRECT) {
        u32 function = (model->argument
            >> OS_BCM2712_SDIO_CMD52_FUNCTION_SHIFT) & 7u;
        u32 address = (model->argument
            >> OS_BCM2712_SDIO_CMD52_ADDRESS_SHIFT) & 0x1ffffu;
        int write = (model->argument
            & OS_BCM2712_SDIO_CMD52_WRITE) != 0u;
        int raw = (model->argument
            & OS_BCM2712_SDIO_CMD52_RAW) != 0u;
        if (function > 1u) {
            return 0;
        }
        if (function == 0u
            && address == OS_BCM2712_SDIO_CCCR_IO_ENABLE) {
            if (write) {
                model->io_enable = (u8)model->argument;
                model->ready_read_calls = 0u;
            }
            if (!write || raw) {
                model->response = model->io_enable;
            }
        } else if (function == 0u
            && address == OS_BCM2712_SDIO_CCCR_IO_READY
            && !write) {
            ++model->ready_read_calls;
            ++model->ready_reads_total;
            if (!model->ready_stuck
                && (model->io_enable
                    & OS_BCM2712_SDIO_CCCR_FUNCTION1) != 0u
                && model->ready_read_calls
                    >= model->ready_after_reads) {
                model->response =
                    OS_BCM2712_SDIO_CCCR_FUNCTION1;
            }
        } else if (function == 1u && !write
            && address
                == OS_BCM2712_SDIO_FBR_FUNCTION1_BLOCK_SIZE_LOW) {
            model->response =
                model->function1_block_size & 0xffu;
        } else if (function == 1u && !write
            && address
                == OS_BCM2712_SDIO_FBR_FUNCTION1_BLOCK_SIZE_HIGH) {
            model->response =
                model->function1_block_size >> 8;
        }
    } else if (command == OS_BCM2712_SDIO_CMD_IO_RW_EXTENDED) {
        u32 function = (model->argument
            >> OS_BCM2712_SDIO_CMD52_FUNCTION_SHIFT) & 7u;
        u32 count = model->argument
            & OS_BCM2712_SDIO_CMD53_COUNT_MASK;
        model->block_mode = (u8)(
            (model->argument & OS_BCM2712_SDIO_CMD53_BLOCK_MODE) != 0u);
        u32 command_count = count == 0u
            ? (model->block_mode
                ? OS_BCM2712_SDIO_CMD53_MAX_BLOCK_COUNT
                : OS_BCM2712_SDIO_CMD53_MAX_BYTE_COUNT)
            : count;
        if (function != 1u
            || (model->block_mode
                ? (command_count != model->block_count
                    || model->block_size
                        != model->function1_block_size
                    || ((model->transfer_mode
                            & OS_BCM2712_SDHCI_TRANSFER_MULTI_BLOCK) != 0u)
                        != (model->block_count > 1u))
                : (command_count != model->block_size
                    || model->block_count != 1u))) {
            return 0;
        }
        u32 total_bytes = model->block_size * model->block_count;
        model->fifo_expected_words = (total_bytes
            + sizeof(u32) - 1u) / sizeof(u32);
        model->fifo_index = 0u;
        model->ready_reassert_delay_reads = 0u;
        model->ready_status_mask =
            (model->transfer_mode
                    & OS_BCM2712_SDHCI_TRANSFER_READ) != 0u
                ? OS_BCM2712_SDHCI_INT_DATA_AVAILABLE
                : OS_BCM2712_SDHCI_INT_SPACE_AVAILABLE;
        if ((model->transfer_mode
                & OS_BCM2712_SDHCI_TRANSFER_READ) == 0u) {
            model->fifo_words = 0u;
        } else if (model->fifo_words
            != model->fifo_expected_words) {
            return 0;
        }
        if (model->data_error) {
            model->interrupt_status |= 1u << 15;
        }
        model->interrupt_status |= model->ready_status_mask;
    } else if (command == 0u) {
        model->io_enable = 0u;
        model->ready_read_calls = 0u;
    } else {
        return 0;
    }
    model->interrupt_status |= OS_BCM2712_SDHCI_INT_RESPONSE;
    return 1;
}

static void wlan_sdio2_command_model_bind(
    os_pi5_bcm2712_sdio2_host *host,
    wlan_sdio2_command_model *model
)
{
    g_wlan_sdio2_command_model = model;
    host->mapping.mmio.read16 = wlan_sdio2_command_read16;
    host->mapping.mmio.read8 = wlan_sdio2_command_read8;
    host->mapping.mmio.write8 = wlan_sdio2_command_write8;
    host->mapping.mmio.read32 = wlan_sdio2_command_read32;
    host->mapping.mmio.write32 = wlan_sdio2_command_write32;
    host->mapping.mmio.write16 = wlan_sdio2_command_write16;
    host->command_resources.mmio = host->mapping.mmio;
}

static int wlan_supply_set_output(
    void *context,
    u32 pin,
    int enabled
)
{
    wlan_supply_output_model *model =
        (wlan_supply_output_model *)context;
    if (model == (wlan_supply_output_model *)0
        || pin != 28u || (enabled != 0 && enabled != 1)) {
        return 0;
    }
    ++model->calls;
    model->pin = pin;
    if (model->fail_call == model->calls) {
        if (model->fail_after_change) {
            model->enabled = (u8)enabled;
        }
        return 0;
    }
    model->enabled = (u8)enabled;
    return 1;
}

static int wlan_prerequisite_check(
    void *context,
    u32 poll_limit
)
{
    wlan_prerequisite_model *model =
        (wlan_prerequisite_model *)context;
    if (model == (wlan_prerequisite_model *)0) {
        return 0;
    }
    ++model->calls;
    if (poll_limit == 0u
        || poll_limit > OS_BCM2712_SDHCI_MAX_POLL_LIMIT) {
        model->bad_bound = 1u;
        return 0;
    }
    return model->fail_call != model->calls;
}

static int wlan_startup_delay(
    void *context,
    u32 minimum_us,
    u32 maximum_us
)
{
    wlan_delay_model *model = (wlan_delay_model *)context;
    if (model == (wlan_delay_model *)0) {
        return 0;
    }
    ++model->calls;
    model->minimum_us = minimum_us;
    model->maximum_us = maximum_us;
    return !model->fail;
}

static int test_bcm2712_wlan_discovery(void)
{
    static os_device_tree tree;
    static wlan_sdio2_mapper_model mapper_model;
    static wlan_supply_output_model output_model;
    static wlan_prerequisite_model prerequisite_model;
    static wlan_delay_model delay_model;
    static wlan_sdio2_command_model command_model;
    static os_pi5_bcm2712_sdio2_host host;
    static os_pi5_bcm2712_wlan_supply supply;
    static u8 cmd53_tx_data[9];
    static u8 cmd53_rx_data[10];
    static u8 cmd53_block_read[16];
    static u8 cmd53_block_write[16];
    static u8 cmd53_max_count_read[
        4u * OS_BCM2712_SDIO_CMD53_MAX_BLOCK_COUNT];
    os_pi5_bcm2712_wlan_resources resources;
    os_pi5_mmio_mapper mapper = {
        .context = &mapper_model,
        .map_region = wlan_sdio2_map,
        .unmap_region = wlan_sdio2_unmap,
    };
    if (!build_wlan_fixture(0u)) {
        g_failure_id = 3602u;
        return 0;
    }
    if (!os_device_tree_parse(g_blob, g_blob_size, &tree)) {
        g_failure_id = 3607u;
        return 0;
    }
    if (!tree.nodes[8].regulator_startup_delay_present
        || tree.nodes[8].regulator_startup_delay_us != 0u
        || !build_wlan_fixture(OS_BCM2712_WLAN_STARTUP_DELAY_US)
        || !os_device_tree_parse(g_blob, g_blob_size, &tree)) {
        g_failure_id = 3603u;
        return 0;
    }
    if (tree.node_count != 9u
        || !tree.nodes[6].bus_width_present
        || tree.nodes[6].bus_width != 4u
        || !tree.nodes[6].non_removable
        || !tree.nodes[8].regulator_gpio_resolved
        || tree.nodes[8].regulator_gpio_pin != 28u
        || !tree.nodes[8].regulator_startup_delay_present
        || tree.nodes[8].regulator_startup_delay_us
            != OS_BCM2712_WLAN_STARTUP_DELAY_US) {
        g_failure_id = 3604u;
        return 0;
    }
    if (!os_pi5_bcm2712_wlan_resources_discover(
            &tree, 6u, 7u, &resources)) {
        g_failure_id = 3605u;
        return 0;
    }
    if (!resources.ready
        || resources.interrupt.interrupt_id
            != OS_BCM2712_SDIO2_GIC_INTERRUPT_ID
        || resources.clock_hz != OS_BCM2712_SDHCI_CLOCK_HZ
        || resources.supply_microvolt != 3300000u
        || resources.supply_startup_delay_us
            != OS_BCM2712_WLAN_STARTUP_DELAY_US
        || resources.supply_gpio_pin != 28u
        || resources.bus_width != 4u
        || resources.function_number != 1u) {
        g_failure_id = 3606u;
        return 0;
    }

    usize original_node_count = tree.node_count;
    u16 original_gpio_parent = tree.nodes[5].parent_index;
    tree.nodes[original_node_count].parent_index = 3u;
    tree.nodes[original_node_count].enabled = 0u;
    tree.nodes[5].parent_index = (u16)original_node_count;
    ++tree.node_count;
    if (os_pi5_bcm2712_wlan_resources_discover(
            &tree, 6u, 7u, &resources)
        || resources.ready) {
        g_failure_id = 3601u;
        return 0;
    }
    tree.nodes[5].parent_index = original_gpio_parent;
    tree.node_count = original_node_count;

    clear_wlan_bytes(&mapper_model, sizeof(mapper_model));
    clear_wlan_bytes(&output_model, sizeof(output_model));
    clear_wlan_bytes(&prerequisite_model, sizeof(prerequisite_model));
    clear_wlan_bytes(&delay_model, sizeof(delay_model));
    os_pi5_bcm2712_sdio2_host_init_state(&host);
    os_pi5_bcm2712_wlan_supply_init_state(&supply);
    mapper_model.addresses[0] = tree.nodes[6].regions[0].address;
    mapper_model.addresses[1] = tree.nodes[6].regions[1].address;
    mapper_model.sizes[0] = OS_BCM2712_SDHCI_HOST_REGION_SIZE;
    mapper_model.sizes[1] = OS_BCM2712_SDHCI_CONFIG_REGION_SIZE;
    if (os_pi5_bcm2712_sdio2_host_attach(
            &tree, 6u, 6u, &mapper, &host)
        || mapper_model.map_calls != 0u
        || !os_pi5_bcm2712_sdio2_host_attach(
            &tree, 6u, 7u, &mapper, &host)
        || !host.ready
        || host.resources.interrupt.interrupt_id
            != OS_BCM2712_SDIO2_GIC_INTERRUPT_ID
        || host.mapping.mapped_region_count != 2u
        || host.mapping.mmio.region_sizes[0]
            != OS_BCM2712_SDHCI_HOST_REGION_SIZE
        || host.mapping.mmio.region_sizes[1]
            != OS_BCM2712_SDHCI_CONFIG_REGION_SIZE
        || !host.command_resources.ready
        || host.command_resources.mmio.context
            != host.mapping.mmio.context
        || host.mapping.mmio.context != &host.mapping.direct
        || host.command_resources.mmio.read16
            != host.mapping.mmio.read16
        || host.command_resources.mmio.write16
            != host.mapping.mmio.write16
        || host.command_resources.mmio.read32
            != host.mapping.mmio.read32
        || host.command_resources.mmio.write32
            != host.mapping.mmio.write32
        || host.command_resources.mmio.read8
            != host.mapping.mmio.read8
        || host.command_resources.mmio.write8
            != host.mapping.mmio.write8
        || host.command_resources.clock_hz
            != host.resources.clock_hz
        || host.command_resources.interrupt.interrupt_id
            != host.resources.interrupt.interrupt_id
        || mapper_model.map_calls != 2u
        || mapper_model.unmap_calls != 0u) {
        g_failure_id = 3610u;
        return 0;
    }
    os_pi5_bcm2712_sdio_card_info card_info = {
        .ocr = 1u,
        .function_count = 1u,
        .memory_present = 1u,
        .card_ready = 1u,
    };
    if (os_pi5_bcm2712_sdio2_query_op_cond(
            &host, 1u, wlan_startup_delay, &delay_model, &card_info)
        || card_info.ocr != 0u || card_info.function_count != 0u
        || card_info.memory_present != 0u || card_info.card_ready != 0u
        || delay_model.calls != 0u) {
        g_failure_id = 3625u;
        return 0;
    }
    __atomic_store_n(
        &host.lifecycle_state, 1u, __ATOMIC_RELAXED);
    card_info.ocr = 1u;
    if (os_pi5_bcm2712_sdio2_query_op_cond(
            &host, 1u, wlan_startup_delay, &delay_model, &card_info)
        || card_info.ocr != 0u) {
        g_failure_id = 3626u;
        return 0;
    }
    __atomic_store_n(
        &host.lifecycle_state, 0u, __ATOMIC_RELAXED);
    wlan_sdio2_clock_model clock_model;
    clear_wlan_bytes(&clock_model, sizeof(clock_model));
    void *original_mmio_context = host.mapping.mmio.context;
    os_pi5_mmio_read16 original_read16 =
        host.mapping.mmio.read16;
    os_pi5_mmio_read8 original_read8 =
        host.mapping.mmio.read8;
    host.mapping.mmio.context = &clock_model;
    host.mapping.mmio.read16 = wlan_sdio2_clock_read16;
    if (os_pi5_bcm2712_sdio2_clock_ready(&host, 0u)
        || os_pi5_bcm2712_sdio2_clock_ready(
            &host, OS_BCM2712_SDHCI_MAX_POLL_LIMIT + 1u)) {
        g_failure_id = 3611u;
        return 0;
    }
    u32 original_clock_hz = host.resources.clock_hz;
    host.resources.clock_hz += 1u;
    if (os_pi5_bcm2712_sdio2_clock_ready(&host, 3u)
        || clock_model.read_calls != 0u) {
        g_failure_id = 3612u;
        return 0;
    }
    host.resources.clock_hz = original_clock_hz;
    clock_model.clock_control =
        OS_BCM2712_SDHCI_CLOCK_INTERNAL_ENABLE
        | OS_BCM2712_SDHCI_CLOCK_CARD_ENABLE;
    clock_model.read_calls = 0u;
    if (os_pi5_bcm2712_sdio2_clock_ready(&host, 3u)
        || clock_model.read_calls != 3u) {
        g_failure_id = 3613u;
        return 0;
    }
    clock_model.read_calls = 0u;
    clock_model.fail_read_call = 1u;
    if (os_pi5_bcm2712_sdio2_clock_ready(&host, 3u)
        || clock_model.read_calls != 1u) {
        g_failure_id = 3614u;
        return 0;
    }
    clock_model.fail_read_call = 0u;
    clock_model.stable_after_reads = 2u;
    clock_model.read_calls = 0u;
    if (!os_pi5_bcm2712_sdio2_clock_ready(&host, 3u)
        || clock_model.read_calls != 2u
        || __atomic_load_n(
            &host.lifecycle_state, __ATOMIC_ACQUIRE) != 0u) {
        g_failure_id = 3615u;
        return 0;
    }
    host.mapping.mmio.context = original_mmio_context;
    host.mapping.mmio.read16 = original_read16;
    host.mapping.mmio.read8 = wlan_sdio2_reset_read8;
    host.mapping.mmio.context = &clock_model;
    if (os_pi5_bcm2712_sdio2_reset_ready(&host, 0u)
        || os_pi5_bcm2712_sdio2_reset_ready(
            &host, OS_BCM2712_SDHCI_RESET_POLL_LIMIT + 1u)) {
        g_failure_id = 3616u;
        return 0;
    }
    clock_model.software_reset =
        OS_BCM2712_SDHCI_RESET_ALL
        | OS_BCM2712_SDHCI_RESET_COMMAND
        | OS_BCM2712_SDHCI_RESET_DATA;
    clock_model.reset_read_calls = 0u;
    if (os_pi5_bcm2712_sdio2_reset_ready(&host, 3u)
        || clock_model.reset_read_calls != 3u) {
        g_failure_id = 3617u;
        return 0;
    }
    clock_model.reset_read_calls = 0u;
    clock_model.fail_reset_read_call = 1u;
    if (os_pi5_bcm2712_sdio2_reset_ready(&host, 3u)
        || clock_model.reset_read_calls != 1u) {
        g_failure_id = 3618u;
        return 0;
    }
    clock_model.fail_reset_read_call = 0u;
    clock_model.reset_clear_after_reads = 2u;
    clock_model.reset_read_calls = 0u;
    if (!os_pi5_bcm2712_sdio2_reset_ready(&host, 3u)
        || clock_model.reset_read_calls != 2u
        || __atomic_load_n(
            &host.lifecycle_state, __ATOMIC_ACQUIRE) != 0u) {
        g_failure_id = 3619u;
        return 0;
    }
    host.mapping.mmio.context = original_mmio_context;
    host.mapping.mmio.read16 = original_read16;
    host.mapping.mmio.read8 = original_read8;
    __atomic_store_n(
        &host.lifecycle_state, 1u, __ATOMIC_RELAXED);
    if (os_pi5_bcm2712_sdio2_host_cleanup(&host)
        || !host.ready || host.mapping.mapped_region_count != 2u) {
        g_failure_id = 3620u;
        return 0;
    }
    __atomic_store_n(
        &host.lifecycle_state, 0u, __ATOMIC_RELAXED);
    host.command_resources.power_active = 1u;
    if (os_pi5_bcm2712_sdio2_host_cleanup(&host)
        || !host.ready || mapper_model.unmap_calls != 0u) {
        g_failure_id = 3627u;
        return 0;
    }
    host.command_resources.power_active = 0u;
    host.command_resources.clock_active = 1u;
    if (os_pi5_bcm2712_sdio2_host_cleanup(&host)
        || !host.ready || mapper_model.unmap_calls != 0u) {
        g_failure_id = 3628u;
        return 0;
    }
    host.command_resources.clock_active = 0u;

    os_pi5_bcm2712_wlan_supply_output output = {
        .tree = &tree,
        .gpio_node_index = host.resources.gpio_provider_index,
        .context = &output_model,
        .set_output = wlan_supply_set_output,
    };
    if (os_pi5_bcm2712_wlan_supply_attach(
            &g_tree, &host, &output, &supply)
        || host.supply_bound) {
        g_failure_id = 3621u;
        return 0;
    }
    output.tree = &g_tree;
    if (os_pi5_bcm2712_wlan_supply_attach(
            &tree, &host, &output, &supply)
        || host.supply_bound) {
        g_failure_id = 3622u;
        return 0;
    }
    output.tree = &tree;
    tree.nodes[6].interrupt_cells[1] = 273u;
    if (os_pi5_bcm2712_wlan_supply_attach(
            &tree, &host, &output, &supply)
        || host.supply_bound) {
        g_failure_id = 3623u;
        return 0;
    }
    tree.nodes[6].interrupt_cells[1] = 274u;
    if (os_pi5_bcm2712_wlan_supply_attach(
            &tree, &host, &(os_pi5_bcm2712_wlan_supply_output){
                .tree = &tree,
                .gpio_node_index =
                    (u16)(host.resources.gpio_provider_index + 1u),
                .context = &output_model,
                .set_output = wlan_supply_set_output,
            }, &supply)
        || host.supply_bound
        || !os_pi5_bcm2712_wlan_supply_attach(
            &tree, &host, &output, &supply)
        || !supply.ready || !host.supply_bound
        || os_pi5_bcm2712_sdio2_host_cleanup(&host)) {
        g_failure_id = 3624u;
        return 0;
    }
    host.command_resources.controller_reset = 1u;
    host.command_resources.power_active = 1u;
    host.command_resources.clock_active = 1u;
    tree.nodes[6].interrupt_cells[1] = 273u;
    if (os_pi5_bcm2712_wlan_resources_discover(
            &tree, 6u, 7u, &resources)) {
        g_failure_id = 3630u;
        return 0;
    }
    card_info.ocr = 1u;
    if (os_pi5_bcm2712_sdio2_query_op_cond(
            &host, 1u, wlan_startup_delay, &delay_model, &card_info)
        || card_info.ocr != 0u
        || card_info.function_count != 0u
        || card_info.memory_present != 0u || card_info.card_ready != 0u
        || __atomic_load_n(
            &host.lifecycle_state, __ATOMIC_ACQUIRE) != 0u) {
        g_failure_id = 3629u;
        return 0;
    }
    tree.nodes[6].interrupt_cells[1] = 274u;
    host.command_resources.controller_reset = 0u;
    host.command_resources.power_active = 0u;
    host.command_resources.clock_active = 0u;
    host.command_resources.mmio.context = (void *)0;
    card_info.ocr = 1u;
    if (os_pi5_bcm2712_sdio2_query_op_cond(
            &host, 1u, wlan_startup_delay, &delay_model, &card_info)
        || card_info.ocr != 0u) {
        g_failure_id = 3631u;
        return 0;
    }
    host.command_resources.mmio = host.mapping.mmio;
    os_pi5_bcm2712_wlan_prerequisite_checks checks = {
        .context = &prerequisite_model,
        .clock_verified = wlan_prerequisite_check,
        .reset_verified = wlan_prerequisite_check,
        .power_verified = wlan_prerequisite_check,
        .pinmux_verified = wlan_prerequisite_check,
        .dma_verified = wlan_prerequisite_check,
        .interrupt_verified = wlan_prerequisite_check,
    };
    u32 missing_prerequisites = 0u;
    prerequisite_model.calls = 0u;
    supply.output.tree = &g_tree;
    if (os_pi5_bcm2712_wlan_supply_prerequisites_check(
            &supply, &checks, 2u, &missing_prerequisites)
        || missing_prerequisites
            != (OS_BCM2712_WLAN_MISSING_CLOCK
                | OS_BCM2712_WLAN_MISSING_RESET
                | OS_BCM2712_WLAN_MISSING_POWER
                | OS_BCM2712_WLAN_MISSING_PINMUX
                | OS_BCM2712_WLAN_MISSING_DMA
                | OS_BCM2712_WLAN_MISSING_INTERRUPT)
        || prerequisite_model.calls != 0u
        || os_pi5_bcm2712_wlan_supply_enable(
            &supply, &checks, 2u, wlan_startup_delay, &delay_model,
            &missing_prerequisites)
        || output_model.calls != 0u || delay_model.calls != 0u) {
        g_failure_id = 3630u;
        return 0;
    }
    supply.output.tree = &tree;
    const os_device_tree *host_tree = host.tree;
    host.tree = &g_tree;
    prerequisite_model.calls = 0u;
    if (os_pi5_bcm2712_wlan_supply_prerequisites_check(
            &supply, &checks, 2u, &missing_prerequisites)
        || prerequisite_model.calls != 0u) {
            g_failure_id = 3626u;
            return 0;
    }
    host.tree = host_tree;
    if (os_pi5_bcm2712_wlan_supply_prerequisites_check(
            &supply, (const os_pi5_bcm2712_wlan_prerequisite_checks *)0,
            2u, &missing_prerequisites)
        || missing_prerequisites
            != (OS_BCM2712_WLAN_MISSING_CLOCK
                | OS_BCM2712_WLAN_MISSING_RESET
                | OS_BCM2712_WLAN_MISSING_POWER
                | OS_BCM2712_WLAN_MISSING_PINMUX
                | OS_BCM2712_WLAN_MISSING_DMA
                | OS_BCM2712_WLAN_MISSING_INTERRUPT)) {
        g_failure_id = 3627u;
        return 0;
    }

    checks.pinmux_verified =
        (os_pi5_bcm2712_wlan_prerequisite_check)0;
    prerequisite_model.calls = 0u;
    if (os_pi5_bcm2712_wlan_supply_prerequisites_check(
            &supply, &checks, 2u, &missing_prerequisites)
        || missing_prerequisites != OS_BCM2712_WLAN_MISSING_PINMUX
        || prerequisite_model.calls != 5u) {
        g_failure_id = 3628u;
        return 0;
    }
    checks.pinmux_verified = wlan_prerequisite_check;
    prerequisite_model.calls = 0u;
    u32 startup_delay =
        host.resources.supply_startup_delay_us;
    host.resources.supply_startup_delay_us =
        OS_BCM2712_WLAN_STARTUP_DELAY_US + 1u;
    if (os_pi5_bcm2712_wlan_supply_enable(
            &supply, &checks, 2u, wlan_startup_delay, &delay_model,
            &missing_prerequisites)
        || prerequisite_model.calls != 0u
        || output_model.calls != 0u || delay_model.calls != 0u) {
        g_failure_id = 3625u;
        return 0;
    }
    host.resources.supply_startup_delay_us = startup_delay;
    if (os_pi5_bcm2712_wlan_supply_enable(
            &supply, &checks, 0u, wlan_startup_delay, &delay_model,
            &missing_prerequisites)
        || missing_prerequisites
            != (OS_BCM2712_WLAN_MISSING_CLOCK
                | OS_BCM2712_WLAN_MISSING_RESET
                | OS_BCM2712_WLAN_MISSING_POWER
                | OS_BCM2712_WLAN_MISSING_PINMUX
                | OS_BCM2712_WLAN_MISSING_DMA
                | OS_BCM2712_WLAN_MISSING_INTERRUPT)
        || output_model.calls != 0u || delay_model.calls != 0u) {
        g_failure_id = 3629u;
        return 0;
    }
    prerequisite_model.fail_call = 3u;
    if (os_pi5_bcm2712_wlan_supply_enable(
            &supply, &checks, 2u, wlan_startup_delay, &delay_model,
            &missing_prerequisites)
        || missing_prerequisites != OS_BCM2712_WLAN_MISSING_POWER
        || output_model.calls != 0u
        || delay_model.calls != 0u) {
        g_failure_id = 3631u;
        return 0;
    }
    prerequisite_model.calls = 0u;
    prerequisite_model.fail_call = 0u;
    delay_model.fail = 1u;
    if (os_pi5_bcm2712_wlan_supply_enable(
            &supply, &checks, 2u, wlan_startup_delay, &delay_model,
            &missing_prerequisites)
        || output_model.calls != 2u || output_model.enabled
        || supply.active || supply.faulted
        || delay_model.minimum_us != OS_BCM2712_WLAN_STARTUP_DELAY_US
        || delay_model.maximum_us != OS_BCM2712_WLAN_STARTUP_DELAY_US) {
        return 0;
    }
    delay_model.fail = 0u;
    prerequisite_model.calls = 0u;
    if (!os_pi5_bcm2712_wlan_supply_enable(
            &supply, &checks, 2u, wlan_startup_delay, &delay_model,
            &missing_prerequisites)
        || missing_prerequisites != 0u
        || prerequisite_model.calls != 6u
        || prerequisite_model.bad_bound
        || !supply.active || !output_model.enabled
        || output_model.pin != 28u
        || delay_model.minimum_us != OS_BCM2712_WLAN_STARTUP_DELAY_US
        || delay_model.maximum_us != OS_BCM2712_WLAN_STARTUP_DELAY_US
        || os_pi5_bcm2712_wlan_supply_enable(
            &supply, &checks, 2u, wlan_startup_delay, &delay_model,
            &missing_prerequisites)) {
        return 0;
    }

    u32 output_calls_before_lifecycle_reject = output_model.calls;
    __atomic_store_n(
        &host.lifecycle_state, 1u, __ATOMIC_RELAXED);
    if (os_pi5_bcm2712_wlan_supply_disable(&supply)
        || output_model.calls != output_calls_before_lifecycle_reject
        || !supply.active || !host.supply_active) {
        g_failure_id = 3654u;
        return 0;
    }
    __atomic_store_n(
        &host.lifecycle_state, 0u, __ATOMIC_RELEASE);

    clear_wlan_bytes(&command_model, sizeof(command_model));
    command_model.ready_after_reads = 2u;
    command_model.block_size = 0x0410u;
    command_model.block_count = 0x0055u;
    command_model.transfer_mode = 0x1234u;
    host.command_resources.controller_reset = 1u;
    host.command_resources.power_active = 1u;
    host.command_resources.clock_active = 1u;
    wlan_sdio2_command_model_bind(&host, &command_model);
    delay_model.calls = 0u;
    prerequisite_model.calls = 0u;
    if (!host.supply_active
        || !os_pi5_bcm2712_sdio2_clock_ready(
            &host, OS_BCM2712_SDHCI_MAX_POLL_LIMIT)) {
        g_failure_id = 3643u;
        return 0;
    }
    if (!os_pi5_bcm2712_sdio2_reset_ready(
            &host, OS_BCM2712_SDHCI_RESET_POLL_LIMIT)) {
        g_failure_id = 3644u;
        return 0;
    }
    card_info = (os_pi5_bcm2712_sdio_card_info){0};
    int query_result = os_pi5_bcm2712_sdio2_query_op_cond(
        &host, 3u, wlan_startup_delay, &delay_model, &card_info);
    if (!query_result) {
        g_failure_id = command_model.command_calls != 1u
            ? 3650u + command_model.command_calls
            : (command_model.response
                    != ((1u
                            << OS_BCM2712_SDIO_R4_FUNCTION_COUNT_SHIFT)
                        | OS_BCM2712_SDHCI_OCR_VOLTAGE_MASK)
                ? 3646u
                : 3647u);
        return 0;
    }
    if (card_info.card_ready
        || card_info.function_count != 1u
        || command_model.command_calls != 1u) {
        g_failure_id = 3648u;
        return 0;
    }
    if (!os_pi5_bcm2712_sdio2_negotiate_op_cond(
            &host, OS_BCM2712_SDHCI_OCR_VOLTAGE_MASK, 3u,
            wlan_startup_delay, &delay_model, &card_info)) {
        g_failure_id = 3641u;
        return 0;
    }
    if (!card_info.card_ready
        || card_info.function_count != 1u
        || host.card_function_count != 1u
        || !host.card_state_active
        || command_model.command_calls != 3u
        || __atomic_load_n(
            &host.lifecycle_state, __ATOMIC_ACQUIRE) != 0u) {
        g_failure_id = 3642u;
        return 0;
    }

    u8 cccr_value = 0xffu;
    prerequisite_model.calls = 0u;
    if (!os_pi5_bcm2712_sdio2_cmd52(
            &host, &supply, &checks, 0u,
            OS_BCM2712_SDIO_CCCR_IO_ENABLE, 0, 0, 0u, 3u,
            wlan_startup_delay, &delay_model,
            &missing_prerequisites, &cccr_value)
        || cccr_value != 0u
        || missing_prerequisites != 0u
        || prerequisite_model.calls != 6u) {
        g_failure_id = 3633u;
        return 0;
    }

    u32 commands_before_rejected_write = command_model.command_calls;
    cccr_value = 0xffu;
    prerequisite_model.calls = 0u;
    if (os_pi5_bcm2712_sdio2_cmd52(
            &host, &supply, &checks, 0u,
            OS_BCM2712_SDIO_CCCR_IO_ENABLE, 1, 1,
            OS_BCM2712_SDIO_CCCR_FUNCTION1, 3u,
            wlan_startup_delay, &delay_model,
            &missing_prerequisites, &cccr_value)
        || cccr_value != 0u
        || prerequisite_model.calls != 0u
        || command_model.command_calls
            != commands_before_rejected_write) {
        g_failure_id = 3634u;
        return 0;
    }
    prerequisite_model.calls = 0u;
    cccr_value = 0xffu;
    if (os_pi5_bcm2712_sdio2_cmd52(
            &host, &supply, &checks, 2u, 0x20u,
            0, 0, 0u, 3u, wlan_startup_delay, &delay_model,
            &missing_prerequisites, &cccr_value)
        || cccr_value != 0u
        || prerequisite_model.calls != 0u
        || command_model.command_calls
            != commands_before_rejected_write) {
        g_failure_id = 3651u;
        return 0;
    }
    checks.dma_verified =
        (os_pi5_bcm2712_wlan_prerequisite_check)0;
    prerequisite_model.calls = 0u;
    if (os_pi5_bcm2712_sdio2_enable_function1(
            &host, &supply, &checks, 3u,
            wlan_startup_delay, &delay_model,
            &missing_prerequisites)
        || missing_prerequisites
            != OS_BCM2712_WLAN_MISSING_DMA
        || prerequisite_model.calls != 5u
        || command_model.command_calls
            != commands_before_rejected_write) {
        g_failure_id = 3653u;
        return 0;
    }
    checks.dma_verified = wlan_prerequisite_check;

    delay_model.calls = 0u;
    prerequisite_model.calls = 0u;
    if (!os_pi5_bcm2712_sdio2_enable_function1(
            &host, &supply, &checks, 3u,
            wlan_startup_delay, &delay_model,
            &missing_prerequisites)
        || missing_prerequisites != 0u
        || prerequisite_model.calls != 6u
        || !host.function1_enabled
        || host.card_state_unknown
        || command_model.io_enable
            != OS_BCM2712_SDIO_CCCR_FUNCTION1
        || command_model.ready_read_calls != 2u
        || delay_model.calls != 1u) {
        g_failure_id = 3635u;
        return 0;
    }

    cccr_value = 0xffu;
    if (!os_pi5_bcm2712_sdio2_cmd52(
            &host, &supply, &checks, 1u, 0x20u,
            0, 0, 0u, 3u, wlan_startup_delay, &delay_model,
            &missing_prerequisites, &cccr_value)
        || cccr_value != 0u) {
        g_failure_id = 3636u;
        return 0;
    }

    checks.dma_verified =
        (os_pi5_bcm2712_wlan_prerequisite_check)0;
    prerequisite_model.calls = 0u;
    u32 commands_before_missing_dma = command_model.command_calls;
    if (os_pi5_bcm2712_sdio2_cmd53_function1(
            &host, &supply, &checks, 0x100u, 1,
            (const u8 *)0, cmd53_rx_data, sizeof(cmd53_rx_data) - 1u,
            3u, wlan_startup_delay, &delay_model,
            &missing_prerequisites)
        || missing_prerequisites != OS_BCM2712_WLAN_MISSING_DMA
        || prerequisite_model.calls != 5u
        || command_model.command_calls != commands_before_missing_dma) {
        g_failure_id = 3655u;
        return 0;
    }
    checks.dma_verified = wlan_prerequisite_check;

    command_model.fifo_words = 3u;
    command_model.fifo_index = 0u;
    command_model.fifo[0] = 0x04030201u;
    command_model.fifo[1] = 0x08070605u;
    command_model.fifo[2] = 0x00000009u;
    for (usize i = 0u; i < sizeof(cmd53_rx_data); ++i) {
        cmd53_rx_data[i] = 0xeeu;
    }
    u32 commands_before_cmd53_read = command_model.command_calls;
    if (!os_pi5_bcm2712_sdio2_cmd53_function1(
            &host, &supply, &checks, 0x100u, 1,
            (const u8 *)0, cmd53_rx_data, sizeof(cmd53_rx_data) - 1u,
            3u, wlan_startup_delay, &delay_model,
            &missing_prerequisites)
        || command_model.command_calls
            != commands_before_cmd53_read + 1u
        || command_model.last_command
            != OS_BCM2712_SDIO_CMD_IO_RW_EXTENDED
        || command_model.argument
            != ((1u << OS_BCM2712_SDIO_CMD52_FUNCTION_SHIFT)
                | (0x100u << OS_BCM2712_SDIO_CMD52_ADDRESS_SHIFT)
                | OS_BCM2712_SDIO_CMD53_INCREMENT_ADDRESS | 9u)
        || command_model.fifo_index != 3u
        || cmd53_rx_data[0] != 1u || cmd53_rx_data[1] != 2u
        || cmd53_rx_data[2] != 3u || cmd53_rx_data[3] != 4u
        || cmd53_rx_data[4] != 5u || cmd53_rx_data[5] != 6u
        || cmd53_rx_data[6] != 7u || cmd53_rx_data[7] != 8u
        || cmd53_rx_data[8] != 9u || cmd53_rx_data[9] != 0xeeu
        || command_model.block_size != 0x0410u
        || command_model.block_count != 0x0055u
        || command_model.transfer_mode != 0x1234u) {
        g_failure_id = 3656u;
        return 0;
    }

    for (usize i = 0u; i < sizeof(cmd53_tx_data); ++i) {
        cmd53_tx_data[i] = (u8)(i * 17u + 3u);
    }
    command_model.fifo_words = 0u;
    command_model.fifo_index = 0u;
    u32 commands_before_cmd53_write = command_model.command_calls;
    if (!os_pi5_bcm2712_sdio2_cmd53_function1(
            &host, &supply, &checks, 0x200u, 0,
            cmd53_tx_data, (u8 *)0, sizeof(cmd53_tx_data),
            3u, wlan_startup_delay, &delay_model,
            &missing_prerequisites)
        || command_model.command_calls
            != commands_before_cmd53_write + 1u
        || command_model.last_command
            != OS_BCM2712_SDIO_CMD_IO_RW_EXTENDED
        || command_model.argument
            != (OS_BCM2712_SDIO_CMD52_WRITE
                | (1u << OS_BCM2712_SDIO_CMD52_FUNCTION_SHIFT)
                | (0x200u << OS_BCM2712_SDIO_CMD52_ADDRESS_SHIFT)
                | 9u)
        || command_model.fifo_words != 3u
        || command_model.fifo[0]
            != ((u32)cmd53_tx_data[0]
                | ((u32)cmd53_tx_data[1] << 8)
                | ((u32)cmd53_tx_data[2] << 16)
                | ((u32)cmd53_tx_data[3] << 24))
        || command_model.fifo[1]
            != ((u32)cmd53_tx_data[4]
                | ((u32)cmd53_tx_data[5] << 8)
                | ((u32)cmd53_tx_data[6] << 16)
                | ((u32)cmd53_tx_data[7] << 24))
        || command_model.fifo[2] != cmd53_tx_data[8]
        || command_model.block_size != 0x0410u
        || command_model.block_count != 0x0055u
        || command_model.transfer_mode != 0x1234u) {
        g_failure_id = 3657u;
        return 0;
    }

    command_model.function1_block_size = 8u;
    command_model.fifo_words = 4u;
    command_model.fifo_index = 0u;
    command_model.fifo[0] = 0x04030201u;
    command_model.fifo[1] = 0x08070605u;
    command_model.fifo[2] = 0x0c0b0a09u;
    command_model.fifo[3] = 0x100f0e0du;
    for (usize i = 0u; i < sizeof(cmd53_block_read); ++i) {
        cmd53_block_read[i] = 0xeeu;
    }
    u32 commands_before_block_read = command_model.command_calls;
    if (!os_pi5_bcm2712_sdio2_cmd53_function1_blocks(
            &host, &supply, &checks, 0x300u, 1, 8u, 2u,
            (const u8 *)0, cmd53_block_read,
            sizeof(cmd53_block_read), 3u,
            wlan_startup_delay, &delay_model,
            &missing_prerequisites)
        || command_model.command_calls
            != commands_before_block_read + 3u
        || command_model.last_command
            != OS_BCM2712_SDIO_CMD_IO_RW_EXTENDED
        || command_model.argument
            != ((1u << OS_BCM2712_SDIO_CMD52_FUNCTION_SHIFT)
                | (0x300u << OS_BCM2712_SDIO_CMD52_ADDRESS_SHIFT)
                | OS_BCM2712_SDIO_CMD53_BLOCK_MODE
                | OS_BCM2712_SDIO_CMD53_INCREMENT_ADDRESS | 2u)
        || command_model.fifo_index != 4u
        || cmd53_block_read[0] != 1u
        || cmd53_block_read[7] != 8u
        || cmd53_block_read[8] != 9u
        || cmd53_block_read[15] != 16u
        || command_model.block_size != 0x0410u
        || command_model.block_count != 0x0055u
        || command_model.transfer_mode != 0x1234u) {
        g_failure_id = 3662u;
        return 0;
    }

    for (usize i = 0u; i < sizeof(cmd53_block_write); ++i) {
        cmd53_block_write[i] = (u8)(i + 0x40u);
    }
    command_model.fifo_words = 0u;
    command_model.fifo_index = 0u;
    u32 commands_before_block_write = command_model.command_calls;
    if (!os_pi5_bcm2712_sdio2_cmd53_function1_blocks(
            &host, &supply, &checks, 0x400u, 0, 8u, 2u,
            cmd53_block_write, (u8 *)0,
            sizeof(cmd53_block_write), 3u,
            wlan_startup_delay, &delay_model,
            &missing_prerequisites)
        || command_model.command_calls
            != commands_before_block_write + 3u
        || command_model.argument
            != (OS_BCM2712_SDIO_CMD52_WRITE
                | (1u << OS_BCM2712_SDIO_CMD52_FUNCTION_SHIFT)
                | (0x400u << OS_BCM2712_SDIO_CMD52_ADDRESS_SHIFT)
                | OS_BCM2712_SDIO_CMD53_BLOCK_MODE | 2u)
        || command_model.fifo_words != 4u
        || command_model.fifo[0] != 0x43424140u
        || command_model.fifo[1] != 0x47464544u
        || command_model.fifo[2] != 0x4b4a4948u
        || command_model.fifo[3] != 0x4f4e4d4cu
        || command_model.block_size != 0x0410u
        || command_model.block_count != 0x0055u
        || command_model.transfer_mode != 0x1234u) {
        g_failure_id = 3663u;
        return 0;
    }

    command_model.function1_block_size = 4u;
    command_model.fifo_words =
        OS_BCM2712_SDIO_CMD53_MAX_BLOCK_COUNT;
    command_model.fifo_index = 0u;
    for (u32 i = 0u;
         i < OS_BCM2712_SDIO_CMD53_MAX_BLOCK_COUNT; ++i) {
        command_model.fifo[i] = i;
    }
    u32 commands_before_max_block_count = command_model.command_calls;
    if (!os_pi5_bcm2712_sdio2_cmd53_function1_blocks(
            &host, &supply, &checks, 0x500u, 0, 4u,
            OS_BCM2712_SDIO_CMD53_MAX_BLOCK_COUNT,
            (const u8 *)0, cmd53_max_count_read,
            sizeof(cmd53_max_count_read),
            OS_BCM2712_SDIO_CMD53_BLOCK_MAX_TOTAL_POLLS
                / OS_BCM2712_SDIO_CMD53_MAX_BLOCK_COUNT,
            wlan_startup_delay, &delay_model,
            &missing_prerequisites)
        || command_model.command_calls
            != commands_before_max_block_count + 3u
        || command_model.argument
            != ((1u << OS_BCM2712_SDIO_CMD52_FUNCTION_SHIFT)
                | (0x500u << OS_BCM2712_SDIO_CMD52_ADDRESS_SHIFT)
                | OS_BCM2712_SDIO_CMD53_BLOCK_MODE)
        || command_model.fifo_index
            != OS_BCM2712_SDIO_CMD53_MAX_BLOCK_COUNT
        || cmd53_max_count_read[0] != 0u
        || cmd53_max_count_read[4] != 1u
        || cmd53_max_count_read[
            sizeof(cmd53_max_count_read) - 1u] != 0u) {
        g_failure_id = 3664u;
        return 0;
    }

    prerequisite_model.calls = 0u;
    u32 commands_before_block_total_poll_limit =
        command_model.command_calls;
    if (os_pi5_bcm2712_sdio2_cmd53_function1_blocks(
            &host, &supply, &checks, 0x500u, 0,
            4u,
            OS_BCM2712_SDIO_CMD53_MAX_BLOCK_COUNT,
            (const u8 *)0, cmd53_max_count_read,
            sizeof(cmd53_max_count_read),
            OS_BCM2712_SDIO_CMD53_BLOCK_MAX_TOTAL_POLLS
                / OS_BCM2712_SDIO_CMD53_MAX_BLOCK_COUNT + 1u,
            wlan_startup_delay, &delay_model,
            &missing_prerequisites)
        || prerequisite_model.calls != 0u
        || command_model.command_calls
            != commands_before_block_total_poll_limit) {
        g_failure_id = 3667u;
        return 0;
    }

    u32 commands_before_block_size_mismatch =
        command_model.command_calls;
    for (usize i = 0u; i < sizeof(cmd53_block_read); ++i) {
        cmd53_block_read[i] = 0xeeu;
    }
    if (os_pi5_bcm2712_sdio2_cmd53_function1_blocks(
            &host, &supply, &checks, 0x300u, 1, 8u, 2u,
            (const u8 *)0, cmd53_block_read,
            sizeof(cmd53_block_read), 3u,
            wlan_startup_delay, &delay_model,
            &missing_prerequisites)
        || command_model.command_calls
            != commands_before_block_size_mismatch + 2u
        || cmd53_block_read[0] != 0u
        || cmd53_block_read[sizeof(cmd53_block_read) - 1u] != 0u
        || command_model.block_size != 0x0410u
        || command_model.block_count != 0x0055u
        || command_model.transfer_mode != 0x1234u) {
        g_failure_id = 3665u;
        return 0;
    }

    prerequisite_model.calls = 0u;
    u32 commands_before_block_poll_limit =
        command_model.command_calls;
    if (os_pi5_bcm2712_sdio2_cmd53_function1_blocks(
            &host, &supply, &checks, 0x300u, 1, 8u, 2u,
            (const u8 *)0, cmd53_block_read,
            sizeof(cmd53_block_read),
            OS_BCM2712_SDIO_CMD53_BLOCK_POLL_LIMIT + 1u,
            wlan_startup_delay, &delay_model,
            &missing_prerequisites)
        || prerequisite_model.calls != 0u
        || command_model.command_calls != commands_before_block_poll_limit) {
        g_failure_id = 3666u;
        return 0;
    }

    u32 commands_before_invalid_cmd53 = command_model.command_calls;
    prerequisite_model.calls = 0u;
    if (os_pi5_bcm2712_sdio2_cmd53_function1(
            &host, &supply, &checks,
            OS_BCM2712_SDIO_CMD52_MAX_ADDRESS - 1u, 1,
            (const u8 *)0, cmd53_rx_data, 4u, 3u,
            wlan_startup_delay, &delay_model,
            &missing_prerequisites)
        || prerequisite_model.calls != 0u
        || command_model.command_calls != commands_before_invalid_cmd53) {
        g_failure_id = 3658u;
        return 0;
    }

    if (!os_pi5_bcm2712_sdio2_disable_function1(
            &host, &supply, &checks, 3u,
            wlan_startup_delay, &delay_model,
            &missing_prerequisites)) {
        g_failure_id = 3649u;
        return 0;
    }
    if (host.function1_enabled) {
        g_failure_id = 3650u;
        return 0;
    }

    u32 commands_before_disabled_function = command_model.command_calls;
    cccr_value = 0xffu;
    if (os_pi5_bcm2712_sdio2_cmd52(
            &host, &supply, &checks, 1u, 0x20u,
            0, 0, 0u, 3u, wlan_startup_delay, &delay_model,
            &missing_prerequisites, &cccr_value)
        || cccr_value != 0u
        || command_model.command_calls
            != commands_before_disabled_function) {
        g_failure_id = 3637u;
        return 0;
    }

    command_model.ready_stuck = 1u;
    delay_model.calls = 0u;
    u32 ready_reads_before_timeout =
        command_model.ready_reads_total;
    if (os_pi5_bcm2712_sdio2_enable_function1(
            &host, &supply, &checks, 3u,
            wlan_startup_delay, &delay_model,
            &missing_prerequisites)
        || host.function1_enabled || host.card_state_unknown
        || command_model.io_enable != 0u
        || command_model.ready_reads_total
            - ready_reads_before_timeout != 3u
        || delay_model.calls != 2u) {
        g_failure_id = 3638u;
        return 0;
    }
    command_model.ready_stuck = 0u;

    command_model.fail_command_call =
        command_model.command_calls + 2u;
    if (os_pi5_bcm2712_sdio2_enable_function1(
            &host, &supply, &checks, 3u,
            wlan_startup_delay, &delay_model,
            &missing_prerequisites)
        || host.function1_enabled || host.card_state_unknown
        || command_model.io_enable != 0u) {
        g_failure_id = 3639u;
        return 0;
    }
    command_model.fail_command_call = 0u;

    command_model.fail_command_call =
        command_model.command_calls + 2u;
    command_model.fail_command_call2 =
        command_model.command_calls + 3u;
    if (os_pi5_bcm2712_sdio2_enable_function1(
            &host, &supply, &checks, 3u,
            wlan_startup_delay, &delay_model,
            &missing_prerequisites)
        || !host.card_state_unknown
        || !os_pi5_bcm2712_sdio2_reset_card(
            &host, &supply, 3u,
            wlan_startup_delay, &delay_model)
        || host.card_state_active || host.card_state_unknown
        || host.function1_enabled || command_model.io_enable != 0u) {
        g_failure_id = 3640u;
        return 0;
    }
    command_model.fail_command_call = 0u;
    command_model.fail_command_call2 = 0u;
    prerequisite_model.calls = 0u;
    u32 commands_after_reset = command_model.command_calls;
    if (os_pi5_bcm2712_sdio2_enable_function1(
            &host, &supply, &checks, 3u,
            wlan_startup_delay, &delay_model,
            &missing_prerequisites)
        || os_pi5_bcm2712_sdio2_disable_function1(
            &host, &supply, &checks, 3u,
            wlan_startup_delay, &delay_model,
            &missing_prerequisites)
        || prerequisite_model.calls != 0u
        || command_model.command_calls != commands_after_reset) {
        g_failure_id = 3652u;
        return 0;
    }
    card_info = (os_pi5_bcm2712_sdio_card_info){0};
    if (!os_pi5_bcm2712_sdio2_negotiate_op_cond(
            &host, OS_BCM2712_SDHCI_OCR_VOLTAGE_MASK, 3u,
            wlan_startup_delay, &delay_model, &card_info)
        || !os_pi5_bcm2712_sdio2_enable_function1(
            &host, &supply, &checks, 3u,
            wlan_startup_delay, &delay_model,
            &missing_prerequisites)) {
        g_failure_id = 3659u;
        return 0;
    }
    command_model.block_size = 0x0410u;
    command_model.block_count = 0x0055u;
    command_model.transfer_mode = 0x1234u;
    command_model.fifo_words = 1u;
    command_model.fifo_index = 0u;
    command_model.fifo[0] = 0x04030201u;
    command_model.data_error = 1u;
    for (usize i = 0u; i < 4u; ++i) {
        cmd53_rx_data[i] = 0xeeu;
    }
    if (os_pi5_bcm2712_sdio2_cmd53_function1(
            &host, &supply, &checks, 0x100u, 0,
            (const u8 *)0, cmd53_rx_data, 4u, 3u,
            wlan_startup_delay, &delay_model,
            &missing_prerequisites)
        || !host.card_state_unknown
        || cmd53_rx_data[0] != 0u || cmd53_rx_data[1] != 0u
        || cmd53_rx_data[2] != 0u || cmd53_rx_data[3] != 0u
        || command_model.block_size != 0x0410u
        || command_model.block_count != 0x0055u
        || command_model.transfer_mode != 0x1234u) {
        g_failure_id = 3660u;
        return 0;
    }
    command_model.data_error = 0u;
    if (!os_pi5_bcm2712_sdio2_reset_card(
            &host, &supply, 3u,
            wlan_startup_delay, &delay_model)
        || host.card_state_active || host.card_state_unknown
        || host.function1_enabled) {
        g_failure_id = 3661u;
        return 0;
    }
    host.command_resources.power_active = 0u;
    host.command_resources.clock_active = 0u;

    supply.output.tree = &g_tree;
    if (os_pi5_bcm2712_wlan_supply_disable(&supply)
        || !supply.active || !output_model.enabled
        || os_pi5_bcm2712_wlan_supply_cleanup(&supply)) {
        return 0;
    }
    supply.output.tree = &tree;
    if (!os_pi5_bcm2712_wlan_supply_disable(&supply)
        || supply.active || supply.faulted || output_model.enabled) {
        return 0;
    }
    supply.output.tree = &g_tree;
    if (os_pi5_bcm2712_wlan_supply_cleanup(&supply)) {
        return 0;
    }
    supply.output.tree = &tree;
    if (!os_pi5_bcm2712_wlan_supply_cleanup(&supply)
        || host.supply_bound) {
        return 0;
    }

    os_pi5_bcm2712_wlan_supply_init_state(&supply);
    output_model.fail_call = output_model.calls + 1u;
    output_model.fail_after_change = 1u;
    prerequisite_model.calls = 0u;
    if (!os_pi5_bcm2712_wlan_supply_attach(
            &tree, &host, &output, &supply)
        || os_pi5_bcm2712_wlan_supply_enable(
            &supply, &checks, 2u, wlan_startup_delay, &delay_model,
            &missing_prerequisites)
        || !supply.active || !supply.faulted || !output_model.enabled
        || os_pi5_bcm2712_wlan_supply_cleanup(&supply)
        || os_pi5_bcm2712_sdio2_host_cleanup(&host)) {
        return 0;
    }
    output_model.fail_call = output_model.calls + 1u;
    output_model.fail_after_change = 0u;
    if (os_pi5_bcm2712_wlan_supply_disable(&supply)
        || !supply.active || !supply.faulted
        || os_pi5_bcm2712_wlan_supply_cleanup(&supply)) {
        return 0;
    }
    output_model.fail_call = 0u;
    if (!os_pi5_bcm2712_wlan_supply_disable(&supply)
        || supply.active || supply.faulted || output_model.enabled
        || !os_pi5_bcm2712_wlan_supply_cleanup(&supply)
        || host.supply_bound
        || !os_pi5_bcm2712_sdio2_host_cleanup(&host)
        || host.ready || mapper_model.unmap_calls != 2u
        || mapper_model.unmap_error) {
        return 0;
    }

    os_pi5_bcm2712_sdio2_host_init_state(&host);
    os_pi5_bcm2712_wlan_supply_init_state(&supply);
    clear_wlan_bytes(&mapper_model, sizeof(mapper_model));
    mapper_model.addresses[0] = tree.nodes[6].regions[0].address;
    mapper_model.addresses[1] = tree.nodes[6].regions[1].address;
    mapper_model.sizes[0] = OS_BCM2712_SDHCI_HOST_REGION_SIZE;
    mapper_model.sizes[1] = OS_BCM2712_SDHCI_CONFIG_REGION_SIZE;
    mapper_model.fail_map_call = 2u;
    if (os_pi5_bcm2712_sdio2_host_attach(
            &tree, 6u, 7u, &mapper, &host)
        || host.ready || host.mapping.mapped_region_count != 0u
        || mapper_model.map_calls != 2u
        || mapper_model.unmap_calls != 1u
        || mapper_model.unmap_error) {
        return 0;
    }

    tree.nodes[6].bus_width = 1u;
    if (os_pi5_bcm2712_wlan_resources_discover(
            &tree, 6u, 7u, &resources)
        || resources.ready) {
        return 0;
    }
    tree.nodes[6].bus_width = 4u;
    tree.nodes[6].interrupt_cells[1] = 273u;
    if (os_pi5_bcm2712_wlan_resources_discover(
            &tree, 6u, 7u, &resources)
        || resources.ready) {
        return 0;
    }
    tree.nodes[6].interrupt_cells[1] = 274u;
    tree.nodes[7].parent_index = 3u;
    if (os_pi5_bcm2712_wlan_resources_discover(
            &tree, 6u, 7u, &resources)
        || resources.ready) {
        return 0;
    }
    tree.nodes[7].parent_index = 6u;
    tree.nodes[8].regulator_startup_delay_us = 5000u;
    if (os_pi5_bcm2712_wlan_resources_discover(
            &tree, 6u, 7u, &resources)
        || resources.ready) {
        return 0;
    }
    return 1;
}

static int cfe_model_map_region(
    void *context,
    u64 physical_address,
    u32 size,
    volatile u32 **mapped_base
)
{
    cfe_mapper_model *model = (cfe_mapper_model *)context;
    if (mapped_base != (volatile u32 **)0) {
        *mapped_base = (volatile u32 *)0;
    }
    if (model == (cfe_mapper_model *)0
        || mapped_base == (volatile u32 **)0) {
        return 0;
    }
    ++model->map_calls;
    if (model->map_calls > OS_PI5_RP1_CFE_REGION_COUNT
        || model->map_calls == model->fail_map_call
        || size > sizeof(model->registers[0])) {
        return 0;
    }
    usize index = model->map_calls - 1u;
    model->addresses[index] = physical_address;
    model->sizes[index] = size;
    model->mapped[index] = 1u;
    *mapped_base = &model->registers[index][0];
    return 1;
}

static void cfe_model_unmap_region(
    void *context,
    volatile u32 *mapped_base,
    u32 size
)
{
    cfe_mapper_model *model = (cfe_mapper_model *)context;
    if (model == (cfe_mapper_model *)0) {
        return;
    }
    usize order_index = model->unmap_calls;
    ++model->unmap_calls;
    for (usize i = 0u; i < OS_PI5_RP1_CFE_REGION_COUNT; ++i) {
        if (mapped_base == &model->registers[i][0]
            && model->mapped[i] && model->sizes[i] == size) {
            model->mapped[i] = 0u;
            if (order_index < OS_PI5_RP1_CFE_REGION_COUNT) {
                model->unmap_order[order_index] = (u8)i;
            }
            return;
        }
    }
    model->invalid_unmap = 1u;
}

static void cfe_mapper_model_init(cfe_mapper_model *model)
{
    volatile u8 *bytes = (volatile u8 *)model;
    for (usize i = 0u; i < sizeof(*model); ++i) {
        bytes[i] = 0u;
    }
}

static int rp1_owner_fixture_init(
    const os_device_tree **tree,
    os_pi5_rp1_pci **rp1
)
{
    static os_pi5_bcm2712_pcie_host pcie_owner_host;
    if (tree == (const os_device_tree **)0
        || rp1 == (os_pi5_rp1_pci **)0
        || g_tree.node_count == 0u
        || g_tree.node_count >= OS_DEVICE_TREE_MAX_NODES) {
        return 0;
    }

    g_owned_tree = g_tree;
    usize rp1_index = g_owned_tree.node_count;
    for (usize i = 0u; i < g_owned_tree.node_count; ++i) {
        const os_device_tree_node *candidate = &g_owned_tree.nodes[i];
        if (candidate->name[0] == 'r'
            && candidate->name[1] == 'p'
            && candidate->name[2] == '1'
            && os_device_tree_node_has_compatible(
                candidate, "simple-bus")) {
            rp1_index = i;
            break;
        }
    }
    if (rp1_index == g_owned_tree.node_count) {
        return 0;
    }

    usize host_index = g_owned_tree.node_count;
    os_device_tree_node *host = &g_owned_tree.nodes[host_index];
    volatile u8 *host_bytes = (volatile u8 *)host;
    for (usize i = 0u; i < sizeof(*host); ++i) {
        host_bytes[i] = 0u;
    }
    static const char host_compatible[] = "brcm,bcm2712-pcie";
    static const char rp1_bus_compatible[] = "simple-bus";
    for (usize i = 0u; i < sizeof(host_compatible); ++i) {
        host->compatible[i] = host_compatible[i];
    }
    for (usize i = 0u; i < sizeof(rp1_bus_compatible); ++i) {
        g_owned_tree.nodes[rp1_index].compatible_list[i] =
            rp1_bus_compatible[i];
    }
    g_owned_tree.nodes[rp1_index].compatible_list_size =
        (u16)sizeof(rp1_bus_compatible);
    host->enabled = 1u;
    host->parent_index = 0u;
    g_owned_tree.nodes[rp1_index].parent_index = (u16)host_index;
    ++g_owned_tree.node_count;

    os_pi5_rp1_pci_init_state(&g_rp1_owner);
    g_rp1_owner.host_node = host;
    g_rp1_owner.function.vendor_id = OS_PI5_PCI_VENDOR_RPI;
    g_rp1_owner.function.device_id = OS_PI5_PCI_DEVICE_RP1_C0;
    g_rp1_owner.ready = 1u;
    volatile u8 *pcie_owner_host_bytes = (volatile u8 *)&pcie_owner_host;
    for (usize i = 0u; i < sizeof(pcie_owner_host); ++i) {
        pcie_owner_host_bytes[i] = 0u;
    }
    pcie_owner_host.ready = 1u;
    g_rp1_owner.host = &pcie_owner_host;
    for (usize i = 0u; i < OS_PI5_PCI_MAX_BARS; ++i) {
        os_pi5_pcie_bar_resource *resource = &g_rp1_owner.bars[i];
        os_pi5_pcie_bar_resource_init_state(resource);
        u64 physical_address = i == 0u
            ? 0x1f00000000ull
            : 0x1f60000000ull + (i - 1u) * 0x10000ull;
        u64 pci_address = i == 0u
            ? 0x40000000ull
            : 0x90000000ull + (i - 1u) * 0x10000ull;
        u64 size = i == 0u ? 0x50000000ull : 0x1000ull;
        resource->active = 1u;
        resource->bar_index = (u8)i;
        resource->function = g_rp1_owner.function;
        resource->allocation.pci_address = pci_address;
        resource->allocation.physical_address = physical_address;
        resource->allocation.size = size;
        resource->allocation.active = 1u;
        resource->mapping.bar.base = pci_address;
        resource->mapping.bar.size = size;
        resource->mapping.bar.index = (u8)i;
        resource->mapping.bar.implemented = 1u;
        resource->mapping.physical_address = physical_address;
        resource->mapping.mapped_size = (u32)size;
        resource->mapping.active = 1u;
    }
    *tree = &g_owned_tree;
    *rp1 = &g_rp1_owner;
    return 1;
}

static int test_rp1_cfe_mapping(void)
{
    static cfe_mapper_model model;
    os_pi5_rp1_cfe_mapping mapping;
    const os_device_tree *owned_tree;
    os_pi5_rp1_pci *rp1;
    os_pi5_mmio_mapper mapper = {
        .context = &model,
        .map_region = cfe_model_map_region,
        .unmap_region = cfe_model_unmap_region,
    };
    cfe_mapper_model_init(&model);
    os_pi5_rp1_cfe_mapping_init_state(&mapping);
    if (!rp1_owner_fixture_init(&owned_tree, &rp1)) {
        return 0;
    }
    if (!os_pi5_rp1_cfe_mapping_attach(
            rp1, owned_tree, 4u, &mapper, &mapping)
        || !mapping.ready
        || mapping.mapping.mapped_region_count
            != OS_PI5_RP1_CFE_REGION_COUNT
        || mapping.resources.region_count
            != OS_PI5_RP1_CFE_REGION_COUNT
        || model.map_calls != OS_PI5_RP1_CFE_REGION_COUNT
        || model.unmap_calls != 0u
        || model.invalid_unmap) {
        return 0;
    }
    for (usize i = 0u; i < OS_PI5_RP1_CFE_REGION_COUNT; ++i) {
        if (!model.mapped[i]
            || model.addresses[i] != mapping.resources.regions[i].address
            || model.sizes[i] != mapping.resources.regions[i].size
            || model.registers[i][0] != 0u) {
            return 0;
        }
    }
    {
        os_pi5_rp1_cfe_identity identity;
        const u32 fe = OS_PI5_RP1_CFE_REGION_FE;
        const u32 csi = OS_PI5_RP1_CFE_REGION_CSI2;
        model.registers[fe][OS_PI5_RP1_CFE_FE_VERSION / 4u] = 0x01200000u;
        model.registers[fe][OS_PI5_RP1_CFE_FE_STATUS / 4u] = 0u;
        model.registers[csi][OS_PI5_RP1_CFE_CSI2_STATUS / 4u] = 0x5u;
        if (!os_pi5_rp1_cfe_identity_read(&mapping, &identity)
            || identity.fe_version != 0x01200000u
            || identity.fe_major != 1u || identity.fe_minor != 2u
            || !identity.fe_idle || identity.csi2_status != 0x5u) {
            return 0;
        }
        model.registers[fe][OS_PI5_RP1_CFE_FE_STATUS / 4u] = 0x4u;
        if (!os_pi5_rp1_cfe_identity_read(&mapping, &identity)
            || identity.fe_idle) {
            return 0;
        }
        model.registers[fe][OS_PI5_RP1_CFE_FE_VERSION / 4u] = 0u;
        if (os_pi5_rp1_cfe_identity_read(&mapping, &identity)
            || identity.fe_version != 0u) {
            return 0;
        }
        model.registers[fe][OS_PI5_RP1_CFE_FE_VERSION / 4u] = 0xffffffffu;
        if (os_pi5_rp1_cfe_identity_read(&mapping, &identity)
            || os_pi5_rp1_cfe_identity_read(
                (const os_pi5_rp1_cfe_mapping *)0, &identity)
            || os_pi5_rp1_cfe_identity_read(
                &mapping, (os_pi5_rp1_cfe_identity *)0)) {
            return 0;
        }
        model.registers[fe][OS_PI5_RP1_CFE_FE_VERSION / 4u] = 0x01200000u;
        mapping.ready = 0u;
        if (os_pi5_rp1_cfe_identity_read(&mapping, &identity)) {
            return 0;
        }
        mapping.ready = 1u;
    }
    if (!os_pi5_rp1_cfe_mapping_cleanup(&mapping)
        || !os_pi5_rp1_cfe_mapping_cleanup(&mapping)
        || mapping.ready
        || mapping.mapping.mapped_region_count != 0u
        || model.unmap_calls != OS_PI5_RP1_CFE_REGION_COUNT
        || model.invalid_unmap) {
        return 0;
    }
    for (usize i = 0u; i < OS_PI5_RP1_CFE_REGION_COUNT; ++i) {
        if (model.mapped[i]
            || model.unmap_order[i]
                != OS_PI5_RP1_CFE_REGION_COUNT - i - 1u) {
            return 0;
        }
    }

    for (u32 fail_call = 1u;
        fail_call <= OS_PI5_RP1_CFE_REGION_COUNT;
        ++fail_call) {
        cfe_mapper_model_init(&model);
        model.fail_map_call = fail_call;
        os_pi5_rp1_cfe_mapping_init_state(&mapping);
        if (os_pi5_rp1_cfe_mapping_attach(
                rp1, owned_tree, 4u, &mapper, &mapping)
            || mapping.ready
            || mapping.mapping.mapped_region_count != 0u
            || mapping.resources.region_count != 0u
            || mapping.resources.clock_provider_index
                != OS_DEVICE_TREE_NO_PARENT
            || model.map_calls != fail_call
            || model.unmap_calls != fail_call - 1u
            || model.invalid_unmap) {
            return 0;
        }
        for (usize i = 0u; i < OS_PI5_RP1_CFE_REGION_COUNT; ++i) {
            if (model.mapped[i]) {
                return 0;
            }
        }
        for (usize i = 0u; i + 1u < fail_call; ++i) {
            if (model.unmap_order[i] != fail_call - i - 2u) {
                return 0;
            }
        }
    }
    cfe_mapper_model_init(&model);
    os_pi5_rp1_cfe_mapping_init_state(&mapping);
    g_owned_tree.nodes[4].regions[0].translated = 0u;
    int rejected_before_mapping = !os_pi5_rp1_cfe_mapping_attach(
        rp1, owned_tree, 4u, &mapper, &mapping) && model.map_calls == 0u;
    g_owned_tree.nodes[4].regions[0].translated = 1u;
    if (!rejected_before_mapping) {
        return 0;
    }

    cfe_mapper_model_init(&model);
    os_pi5_rp1_cfe_mapping_init_state(&mapping);
    rejected_before_mapping = !os_pi5_rp1_cfe_mapping_attach(
        rp1, &g_tree, 4u, &mapper, &mapping) && model.map_calls == 0u;
    if (!rejected_before_mapping) {
        return 0;
    }

    cfe_mapper_model_init(&model);
    os_pi5_rp1_cfe_mapping_init_state(&mapping);
    rp1->ready = 0u;
    rejected_before_mapping = !os_pi5_rp1_cfe_mapping_attach(
        rp1, owned_tree, 4u, &mapper, &mapping) && model.map_calls == 0u;
    rp1->ready = 1u;
    return rejected_before_mapping;
}

static int test_rp1_audio_out(void)
{
    static cfe_mapper_model model;
    os_pi5_rp1_audio_out_resources resources;
    os_pi5_rp1_audio_out_mapping mapping;
    const os_device_tree *owned_tree;
    os_pi5_rp1_pci *rp1;
    os_pi5_mmio_mapper mapper = {
        .context = &model,
        .map_region = cfe_model_map_region,
        .unmap_region = cfe_model_unmap_region,
    };
    os_device_tree_node *audio = &g_tree.nodes[7];
    if (!os_pi5_rp1_audio_out_resources_discover(
            &g_tree, 7u, &resources)
        || resources.region.address != 0x1f00094000ull
        || resources.region.size != OS_PI5_RP1_AUDIO_OUT_REGION_SIZE
        || !resources.region.translated
        || resources.clock_provider_index != 3u
        || resources.dma_provider_index != 8u
        || resources.clock_id != OS_PI5_RP1_CLOCK_ID_AUDIO_OUT
        || resources.dma_channel != OS_PI5_RP1_DMA_AUDIO_OUT) {
        g_failure_id = 3510u;
        return 0;
    }

    audio->dma_references[0].arguments[0] = 0x1cu;
    if (os_pi5_rp1_audio_out_resources_discover(
            &g_tree, 7u, &resources)
        || resources.dma_provider_index != OS_DEVICE_TREE_NO_PARENT) {
        g_failure_id = 3511u;
        return 0;
    }
    audio->dma_references[0].arguments[0] =
        OS_PI5_RP1_DMA_AUDIO_OUT;

    audio->clock_references[0].arguments[0] =
        OS_PI5_RP1_CLOCK_ID_I2S;
    if (os_pi5_rp1_audio_out_resources_discover(
            &g_tree, 7u, &resources)
        || resources.clock_provider_index != OS_DEVICE_TREE_NO_PARENT) {
        g_failure_id = 3512u;
        return 0;
    }
    audio->clock_references[0].arguments[0] =
        OS_PI5_RP1_CLOCK_ID_AUDIO_OUT;

    audio->regions[0].translated = 0u;
    if (os_pi5_rp1_audio_out_resources_discover(
            &g_tree, 7u, &resources)) {
        g_failure_id = 3513u;
        return 0;
    }
    audio->regions[0].translated = 1u;
    audio->enabled = 0u;
    if (os_pi5_rp1_audio_out_resources_discover(
            &g_tree, 7u, &resources)) {
        g_failure_id = 3514u;
        return 0;
    }
    audio->enabled = 1u;

    if (!rp1_owner_fixture_init(&owned_tree, &rp1)) {
        g_failure_id = 3515u;
        return 0;
    }
    cfe_mapper_model_init(&model);
    os_pi5_rp1_audio_out_mapping_init_state(&mapping);
    if (os_pi5_rp1_audio_out_mapping_attach(
            rp1, &g_tree, 7u, &mapper, &mapping)
        || model.map_calls != 0u) {
        g_failure_id = 3518u;
        return 0;
    }

    cfe_mapper_model_init(&model);
    os_pi5_rp1_audio_out_mapping_init_state(&mapping);
    rp1->ready = 0u;
    int rejected_before_mapping = !os_pi5_rp1_audio_out_mapping_attach(
        rp1, owned_tree, 7u, &mapper, &mapping) && model.map_calls == 0u;
    rp1->ready = 1u;
    if (!rejected_before_mapping) {
        g_failure_id = 3517u;
        return 0;
    }

    cfe_mapper_model_init(&model);
    model.fail_map_call = 1u;
    os_pi5_rp1_audio_out_mapping_init_state(&mapping);
    if (os_pi5_rp1_audio_out_mapping_attach(
            rp1, owned_tree, 7u, &mapper, &mapping)
        || mapping.ready
        || mapping.mapping.mapped_region_count != 0u
        || model.map_calls != 1u || model.unmap_calls != 0u) {
        g_failure_id = 3515u;
        return 0;
    }

    cfe_mapper_model_init(&model);
    os_pi5_rp1_audio_out_mapping_init_state(&mapping);
    if (!os_pi5_rp1_audio_out_mapping_attach(
            rp1, owned_tree, 7u, &mapper, &mapping)
        || !mapping.ready
        || mapping.mapping.mapped_region_count != 1u
        || model.map_calls != 1u || model.unmap_calls != 0u
        || !model.mapped[0]
        || model.addresses[0] != 0x1f00094000ull
        || model.sizes[0] != OS_PI5_RP1_AUDIO_OUT_REGION_SIZE
        || model.registers[0][0] != 0u
        || !os_pi5_rp1_audio_out_mapping_cleanup(&mapping)
        || !os_pi5_rp1_audio_out_mapping_cleanup(&mapping)
        || mapping.ready
        || mapping.mapping.mapped_region_count != 0u
        || model.unmap_calls != 1u
        || model.invalid_unmap || model.mapped[0]) {
        g_failure_id = 3516u;
        return 0;
    }
    return 1;
}

static int test_rp1_dma_controller(void)
{
    static cfe_mapper_model model;
    os_pi5_rp1_dma_resources resources;
    os_pi5_rp1_dma_mapping mapping;
    os_pi5_rp1_dma_channel_pool pool;
    const os_device_tree *owned_tree;
    os_pi5_rp1_pci *rp1;
    os_pi5_mmio_mapper mapper = {
        .context = &model,
        .map_region = cfe_model_map_region,
        .unmap_region = cfe_model_unmap_region,
    };
    os_device_tree_node *dma = &g_tree.nodes[8];

    if (!os_pi5_rp1_dma_resources_discover(
            &g_tree, 8u, &resources)
        || resources.region.address != 0x1f00188000ull
        || resources.region.size != 0x1000u
        || !resources.region.translated
        || resources.clock_provider_index != 3u
        || resources.dma_clock_id != OS_PI5_RP1_CLOCK_ID_DMA
        || resources.sys_clock_id != OS_PI5_RP1_CLOCK_ID_SYS
        || resources.interrupt_id != OS_PI5_RP1_DMA_INTERRUPT_ID
        || resources.interrupt_flags != 4u
        || resources.channel_count != OS_PI5_RP1_DMA_CHANNEL_COUNT
        || resources.target_count != OS_PI5_RP1_DMA_TARGET_COUNT
        || resources.dma_master_count != 1u
        || resources.data_width != 4u
        || resources.block_sizes[0] != 0x40000u
        || resources.block_sizes[7] != 0x40000u
        || resources.axi_max_burst_lengths[0] != 8u
        || resources.axi_max_burst_lengths[7] != 4u) {
        g_failure_id = 3530u;
        return 0;
    }

    dma->dma_channel_count = OS_PI5_RP1_DMA_CHANNEL_COUNT - 1u;
    if (os_pi5_rp1_dma_resources_discover(
            &g_tree, 8u, &resources)
        || resources.region.translated
        || resources.clock_provider_index != OS_DEVICE_TREE_NO_PARENT) {
        g_failure_id = 3531u;
        return 0;
    }
    dma->dma_channel_count = OS_PI5_RP1_DMA_CHANNEL_COUNT;
    dma->dma_target_count = OS_PI5_RP1_DMA_TARGET_COUNT - 1u;
    if (os_pi5_rp1_dma_resources_discover(
            &g_tree, 8u, &resources)) {
        g_failure_id = 3532u;
        return 0;
    }
    dma->dma_target_count = OS_PI5_RP1_DMA_TARGET_COUNT;
    dma->dma_data_width = 3u;
    if (os_pi5_rp1_dma_resources_discover(
            &g_tree, 8u, &resources)) {
        g_failure_id = 3533u;
        return 0;
    }
    dma->dma_data_width = 4u;
    dma->dma_block_sizes[5] = 0x3ffffu;
    if (os_pi5_rp1_dma_resources_discover(
            &g_tree, 8u, &resources)) {
        g_failure_id = 3534u;
        return 0;
    }
    dma->dma_block_sizes[5] = 0x40000u;
    dma->dma_axi_max_burst_lengths[0] = 4u;
    if (os_pi5_rp1_dma_resources_discover(
            &g_tree, 8u, &resources)) {
        g_failure_id = 3535u;
        return 0;
    }
    dma->dma_axi_max_burst_lengths[0] = 8u;
    dma->interrupt_cells[0] = OS_PI5_RP1_DMA_INTERRUPT_ID - 1u;
    if (os_pi5_rp1_dma_resources_discover(
            &g_tree, 8u, &resources)) {
        g_failure_id = 3536u;
        return 0;
    }
    dma->interrupt_cells[0] = OS_PI5_RP1_DMA_INTERRUPT_ID;
    dma->regions[0].translated = 0u;
    if (os_pi5_rp1_dma_resources_discover(
            &g_tree, 8u, &resources)) {
        g_failure_id = 3537u;
        return 0;
    }
    dma->regions[0].translated = 1u;

    if (!rp1_owner_fixture_init(&owned_tree, &rp1)) {
        g_failure_id = 3538u;
        return 0;
    }
    cfe_mapper_model_init(&model);
    os_pi5_rp1_dma_mapping_init_state(&mapping);
    if (os_pi5_rp1_dma_mapping_attach(
            rp1, &g_tree, 8u, &mapper, &mapping)
        || model.map_calls != 0u) {
        g_failure_id = 3544u;
        return 0;
    }

    cfe_mapper_model_init(&model);
    os_pi5_rp1_dma_mapping_init_state(&mapping);
    rp1->ready = 0u;
    int rejected_before_mapping = !os_pi5_rp1_dma_mapping_attach(
        rp1, owned_tree, 8u, &mapper, &mapping) && model.map_calls == 0u;
    rp1->ready = 1u;
    if (!rejected_before_mapping) {
        g_failure_id = 3543u;
        return 0;
    }

    cfe_mapper_model_init(&model);
    model.fail_map_call = 1u;
    os_pi5_rp1_dma_mapping_init_state(&mapping);
    if (os_pi5_rp1_dma_mapping_attach(
            rp1, owned_tree, 8u, &mapper, &mapping)
        || mapping.ready
        || mapping.mapping.mapped_region_count != 0u
        || model.map_calls != 1u || model.unmap_calls != 0u) {
        g_failure_id = 3538u;
        return 0;
    }

    cfe_mapper_model_init(&model);
    os_pi5_rp1_dma_mapping_init_state(&mapping);
    if (!os_pi5_rp1_dma_mapping_attach(
            rp1, owned_tree, 8u, &mapper, &mapping)
        || !mapping.ready
        || mapping.mapping.mapped_region_count != 1u
        || model.map_calls != 1u || model.unmap_calls != 0u
        || !model.mapped[0]
        || model.addresses[0] != 0x1f00188000ull
        || model.sizes[0] != 0x1000u
        || model.registers[0][0] != 0u) {
        g_failure_id = 3539u;
        return 0;
    }

    os_pi5_rp1_dma_channel_pool_init_state(&pool);
    if (!os_pi5_rp1_dma_channel_pool_init(&mapping, &pool)
        || !os_pi5_rp1_dma_channel_claim(
            &pool, 0u, OS_PI5_RP1_DMA_AUDIO_OUT, 1u)
        || pool.channels[0].state != OS_PI5_RP1_DMA_CHANNEL_CLAIMED
        || pool.channels[0].target != OS_PI5_RP1_DMA_AUDIO_OUT
        || pool.channels[0].owner != 1u
        || os_pi5_rp1_dma_channel_claim(
            &pool, 0u, OS_PI5_RP1_DMA_AUDIO_OUT, 2u)
        || os_pi5_rp1_dma_channel_claim(
            &pool, OS_PI5_RP1_DMA_CHANNEL_COUNT,
            OS_PI5_RP1_DMA_AUDIO_OUT, 2u)
        || os_pi5_rp1_dma_channel_claim(
            &pool, 1u, OS_PI5_RP1_DMA_TARGET_COUNT, 2u)
        || os_pi5_rp1_dma_channel_claim(
            &pool, 1u, OS_PI5_RP1_DMA_AUDIO_OUT, 0u)
        || os_pi5_rp1_dma_channel_release(&pool, 0u, 2u)
        || os_pi5_rp1_dma_channel_pool_cleanup(&pool)) {
        g_failure_id = 3540u;
        return 0;
    }
    pool.lock = 1u;
    int busy_lock_rejected = !os_pi5_rp1_dma_channel_claim(
        &pool, 1u, OS_PI5_RP1_DMA_I2S0_TX, 2u);
    pool.lock = 0u;
    if (!busy_lock_rejected
        || !os_pi5_rp1_dma_channel_release(&pool, 0u, 1u)
        || pool.channels[0].state != OS_PI5_RP1_DMA_CHANNEL_FREE
        || pool.channels[0].owner != 0u
        || !os_pi5_rp1_dma_channel_pool_cleanup(&pool)
        || pool.ready
        || pool.resources.clock_provider_index != OS_DEVICE_TREE_NO_PARENT
        || os_pi5_rp1_dma_channel_pool_cleanup(&pool)) {
        g_failure_id = 3541u;
        return 0;
    }
    if (!os_pi5_rp1_dma_mapping_cleanup(&mapping)
        || !os_pi5_rp1_dma_mapping_cleanup(&mapping)
        || mapping.ready
        || mapping.mapping.mapped_region_count != 0u
        || model.unmap_calls != 1u
        || model.invalid_unmap || model.mapped[0]) {
        g_failure_id = 3542u;
        return 0;
    }
    return 1;
}

static int test_rp1_i2s(void)
{
    static cfe_mapper_model model;
    os_pi5_rp1_i2s_resources resources;
    os_pi5_rp1_i2s_mapping mapping;
    const os_device_tree *owned_tree;
    os_pi5_rp1_pci *rp1;
    os_pi5_mmio_mapper mapper = {
        .context = &model,
        .map_region = cfe_model_map_region,
        .unmap_region = cfe_model_unmap_region,
    };
    os_device_tree_node *i2s0 = &g_tree.nodes[9];
    os_device_tree_node *dma = &g_tree.nodes[8];

    if (!os_pi5_rp1_i2s_resources_discover(
            &g_tree, 9u, &resources)
        || resources.region.address != OS_PI5_RP1_I2S0_ADDRESS
        || resources.region.size != OS_PI5_RP1_I2S_REGION_SIZE
        || !resources.region.translated
        || resources.clock_provider_index != 3u
        || resources.dma_provider_index != 8u
        || resources.clock_id != OS_PI5_RP1_CLOCK_ID_I2S
        || resources.tx_dma_channel != OS_PI5_RP1_DMA_I2S0_TX
        || resources.rx_dma_channel != OS_PI5_RP1_DMA_I2S0_RX
        || resources.instance != 0u
        || !os_pi5_rp1_i2s_resources_discover(
            &g_tree, 10u, &resources)
        || resources.region.address != OS_PI5_RP1_I2S1_ADDRESS
        || resources.tx_dma_channel != OS_PI5_RP1_DMA_I2S1_TX
        || resources.rx_dma_channel != OS_PI5_RP1_DMA_I2S1_RX
        || resources.instance != 1u) {
        g_failure_id = 3520u;
        return 0;
    }

    i2s0->dma_references[0].arguments[0] =
        OS_PI5_RP1_DMA_I2S0_RX;
    if (os_pi5_rp1_i2s_resources_discover(
            &g_tree, 9u, &resources)) {
        g_failure_id = 3521u;
        return 0;
    }
    i2s0->dma_references[0].arguments[0] =
        OS_PI5_RP1_DMA_I2S0_TX;
    dma->dma_target_count = OS_PI5_RP1_DMA_I2S1_TX;
    if (os_pi5_rp1_i2s_resources_discover(
            &g_tree, 10u, &resources)) {
        g_failure_id = 3522u;
        return 0;
    }
    dma->dma_target_count = OS_PI5_RP1_DMA_TARGET_COUNT;
    dma->dma_channel_count = 7u;
    if (os_pi5_rp1_i2s_resources_discover(
            &g_tree, 9u, &resources)) {
        g_failure_id = 3526u;
        return 0;
    }
    dma->dma_channel_count = 8u;
    dma->interrupt_cells[0] = OS_PI5_RP1_DMA_INTERRUPT_ID - 1u;
    if (os_pi5_rp1_i2s_resources_discover(
            &g_tree, 9u, &resources)) {
        g_failure_id = 3523u;
        return 0;
    }
    dma->interrupt_cells[0] = OS_PI5_RP1_DMA_INTERRUPT_ID;
    i2s0->interrupt_cell_count = 2u;
    if (os_pi5_rp1_i2s_resources_discover(
            &g_tree, 9u, &resources)) {
        g_failure_id = 3524u;
        return 0;
    }
    i2s0->interrupt_cell_count = 0u;
    i2s0->regions[0].address = 0x1f000a8000ull;
    if (os_pi5_rp1_i2s_resources_discover(
            &g_tree, 9u, &resources)) {
        g_failure_id = 3525u;
        return 0;
    }
    i2s0->regions[0].address = OS_PI5_RP1_I2S0_ADDRESS;

    if (!rp1_owner_fixture_init(&owned_tree, &rp1)) {
        g_failure_id = 3526u;
        return 0;
    }
    cfe_mapper_model_init(&model);
    os_pi5_rp1_i2s_mapping_init_state(&mapping);
    if (os_pi5_rp1_i2s_mapping_attach(
            rp1, &g_tree, 9u, &mapper, &mapping)
        || model.map_calls != 0u) {
        g_failure_id = 3529u;
        return 0;
    }

    cfe_mapper_model_init(&model);
    os_pi5_rp1_i2s_mapping_init_state(&mapping);
    rp1->ready = 0u;
    int rejected_before_mapping = !os_pi5_rp1_i2s_mapping_attach(
        rp1, owned_tree, 9u, &mapper, &mapping) && model.map_calls == 0u;
    rp1->ready = 1u;
    if (!rejected_before_mapping) {
        g_failure_id = 3528u;
        return 0;
    }

    cfe_mapper_model_init(&model);
    model.fail_map_call = 1u;
    os_pi5_rp1_i2s_mapping_init_state(&mapping);
    if (os_pi5_rp1_i2s_mapping_attach(
            rp1, owned_tree, 9u, &mapper, &mapping)
        || mapping.ready
        || mapping.mapping.mapped_region_count != 0u
        || model.map_calls != 1u || model.unmap_calls != 0u) {
        g_failure_id = 3526u;
        return 0;
    }

    cfe_mapper_model_init(&model);
    os_pi5_rp1_i2s_mapping_init_state(&mapping);
    if (!os_pi5_rp1_i2s_mapping_attach(
            rp1, owned_tree, 9u, &mapper, &mapping)
        || !mapping.ready
        || mapping.mapping.mapped_region_count != 1u
        || model.map_calls != 1u || model.unmap_calls != 0u
        || !model.mapped[0]
        || model.addresses[0] != OS_PI5_RP1_I2S0_ADDRESS
        || model.sizes[0] != OS_PI5_RP1_I2S_REGION_SIZE
        || model.registers[0][0] != 0u
        || !os_pi5_rp1_i2s_mapping_cleanup(&mapping)
        || !os_pi5_rp1_i2s_mapping_cleanup(&mapping)
        || mapping.ready
        || mapping.mapping.mapped_region_count != 0u
        || model.unmap_calls != 1u
        || model.invalid_unmap || model.mapped[0]) {
        g_failure_id = 3527u;
        return 0;
    }
    return 1;
}

static int test_uefi_configuration_table(void)
{
    efi_configuration_table configuration = {
        .vendor_guid = {
            0xb1b621d5u, 0xf19cu, 0x41a5u,
            {0x83, 0x0b, 0xd9, 0x15, 0x2c, 0x69, 0xaa, 0xe0},
        },
        .vendor_table = g_blob,
    };
    efi_system_table system_table = {0};
    system_table.configuration_table = &configuration;
    system_table.number_of_table_entries = 1;
    const void *blob = (const void *)0;
    usize blob_size = 0;
    if (!os_device_tree_find_uefi_blob(
            &system_table, &blob, &blob_size)
        || blob != g_blob || blob_size != g_blob_size
        || !os_device_tree_discover_uefi(&system_table, &g_tree)
        || g_tree.node_count != 21u) {
        return 0;
    }
    system_table.number_of_table_entries = 129u;
    return !os_device_tree_find_uefi_blob(
        &system_table, &blob, &blob_size)
        && blob == (const void *)0 && blob_size == 0;
}

static int test_rp1_interrupt_decode(void)
{
    static os_device_tree tree;
    tree.node_count = 3u;
    for (usize i = 0; i < tree.node_count; ++i) {
        volatile u8 *bytes = (volatile u8 *)&tree.nodes[i];
        for (usize byte = 0; byte < sizeof(tree.nodes[i]); ++byte) {
            bytes[byte] = 0u;
        }
    }

    tree.nodes[0].enabled = 1u;
    tree.nodes[0].parent_index = OS_DEVICE_TREE_NO_PARENT;
    tree.nodes[0].compatible[0] = 'b';
    const char host_compatible[] = "brcm,bcm2712-pcie";
    for (usize i = 0; i < sizeof(host_compatible); ++i) {
        tree.nodes[0].compatible[i] = host_compatible[i];
    }

    tree.nodes[1].enabled = 1u;
    tree.nodes[1].parent_index = 0u;
    tree.nodes[1].interrupt_parent_index = 1u;
    tree.nodes[1].interrupt_specifier_cells = 2u;
    tree.nodes[1].interrupt_controller = 1u;

    tree.nodes[2].enabled = 1u;
    tree.nodes[2].parent_index = 1u;
    tree.nodes[2].interrupt_parent_index = 1u;
    tree.nodes[2].interrupt_cell_count = 2u;
    tree.nodes[2].interrupt_cells[0] = 60u;
    tree.nodes[2].interrupt_cells[1] = 4u;

    os_device_tree_interrupt interrupt;
    if (!os_device_tree_get_rp1_interrupt(
            &tree, &tree.nodes[2], 0u, &interrupt)
        || interrupt.interrupt_id != 60u || interrupt.flags != 4u) {
        return 0;
    }
    tree.nodes[2].interrupt_cells[0] = 61u;
    if (os_device_tree_get_rp1_interrupt(
            &tree, &tree.nodes[2], 0u, &interrupt)) {
        return 0;
    }
    tree.nodes[2].interrupt_cells[0] = 0u;
    tree.nodes[2].interrupt_cells[1] = 0u;
    if (os_device_tree_get_rp1_interrupt(
            &tree, &tree.nodes[2], 0u, &interrupt)) {
        return 0;
    }
    tree.nodes[2].interrupt_cells[1] = 3u;
    if (!os_device_tree_get_rp1_interrupt(
            &tree, &tree.nodes[2], 0u, &interrupt)
        || interrupt.interrupt_id != 0u || interrupt.flags != 3u) {
        return 0;
    }
    tree.nodes[1].interrupt_controller = 0u;
    return !os_device_tree_get_rp1_interrupt(
        &tree, &tree.nodes[2], 0u, &interrupt);
}

static int attach_pcie_parent_for_test(
    os_device_tree *tree,
    usize child_index
)
{
    if (tree == (os_device_tree *)0
        || tree->node_count == 0u
        || tree->node_count >= OS_DEVICE_TREE_MAX_NODES
        || child_index >= tree->node_count
        || child_index == 0u) {
        return 0;
    }

    usize host_index = tree->node_count;
    os_device_tree_node *host = &tree->nodes[host_index];
    volatile u8 *bytes = (volatile u8 *)host;
    for (usize i = 0u; i < sizeof(*host); ++i) {
        bytes[i] = 0u;
    }
    static const char host_name[] = "pcie";
    static const char host_compatible[] = "brcm,bcm2712-pcie";
    for (usize i = 0u; i < sizeof(host_name); ++i) {
        host->name[i] = host_name[i];
    }
    for (usize i = 0u; i < sizeof(host_compatible); ++i) {
        host->compatible[i] = host_compatible[i];
    }
    host->enabled = 1u;
    host->parent_index = 0u;
    tree->nodes[child_index].parent_index = (u16)host_index;
    ++tree->node_count;
    return 1;
}

static int test_rp1_cfe_discovery(void)
{
    os_pi5_rp1_cfe_resources resources;
    if (!build_camera_fixture()
        || !os_device_tree_parse(g_blob, g_blob_size, &g_tree)
        || !attach_pcie_parent_for_test(&g_tree, 1u)
        || g_tree.node_count != 15u
        || g_tree.nodes[7].dma_reference_count != 1u
        || g_tree.nodes[7].dma_references[0].provider_index != 8u
        || g_tree.nodes[7].dma_references[0].arguments[0]
            != OS_PI5_RP1_DMA_AUDIO_OUT
        || g_tree.nodes[8].dma_specifier_cells != 1u
        || !g_tree.nodes[8].dma_target_count_present
        || g_tree.nodes[8].dma_target_count
            != OS_PI5_RP1_DMA_TARGET_COUNT
        || !g_tree.nodes[8].dma_channel_count_present
        || g_tree.nodes[8].dma_channel_count != 8u
        || !g_tree.nodes[8].dma_master_count_present
        || g_tree.nodes[8].dma_master_count != 1u
        || !g_tree.nodes[8].dma_data_width_present
        || g_tree.nodes[8].dma_data_width != 4u
        || g_tree.nodes[8].dma_block_size_count
            != OS_DEVICE_TREE_MAX_DMA_CHANNELS
        || g_tree.nodes[8].dma_block_sizes[0] != 0x40000u
        || g_tree.nodes[8].dma_block_sizes[7] != 0x40000u
        || g_tree.nodes[8].dma_axi_max_burst_len_count
            != OS_DEVICE_TREE_MAX_DMA_CHANNELS
        || g_tree.nodes[8].dma_axi_max_burst_lengths[0] != 8u
        || g_tree.nodes[8].dma_axi_max_burst_lengths[7] != 4u) {
        g_failure_id = 3501u;
        return 0;
    }
    if (!os_pi5_rp1_cfe_resources_discover(&g_tree, 4u, &resources)) {
        g_failure_id = 3502u;
        return 0;
    }
    if (resources.region_count != OS_PI5_RP1_CFE_REGION_COUNT
        || resources.endpoint_count != 1u
        || resources.clock_provider_index != 3u
        || resources.clock_id != OS_PI5_RP1_CLOCK_ID_MIPI0_CFG
        || resources.interrupt_id != 17u
        || resources.interrupt_flags != 4u
        || resources.regions[0].address != 0x1f00100000ull
        || resources.regions[1].address != 0x1f00102000ull
        || resources.regions[2].address != 0x1f00104000ull
        || resources.regions[3].address != 0x1f00106000ull
        || resources.endpoints[0].endpoint_node_index != 6u
        || resources.endpoints[0].remote_endpoint_node_index != 13u
        || resources.endpoints[0].data_lane_count != 2u
        || resources.endpoints[0].data_lanes[0] != 1u
        || resources.endpoints[0].data_lanes[1] != 2u
        || resources.endpoints[0].clock_lane != 0u
        || g_tree.nodes[6].remote_endpoint_index != 13u
        || g_tree.nodes[13].remote_endpoint_index != 6u) {
        g_failure_id = 3503u;
        return 0;
    }
    if (!test_rp1_cfe_mapping()) {
        g_failure_id = 3502u;
        return 0;
    }
    if (!test_rp1_audio_out()) {
        if (g_failure_id == 0u) {
            g_failure_id = 3503u;
        }
        return 0;
    }
    if (!test_rp1_i2s()) {
        if (g_failure_id == 0u) {
            g_failure_id = 3527u;
        }
        return 0;
    }
    if (!test_rp1_dma_controller()) {
        if (g_failure_id == 0u) {
            g_failure_id = 3537u;
        }
        return 0;
    }

    os_device_tree_node *cfe = &g_tree.nodes[4];
    os_device_tree_node *endpoint = &g_tree.nodes[6];
    os_device_tree_node *remote = &g_tree.nodes[13];

    cfe->regions[0].translated = 0u;
    if (os_pi5_rp1_cfe_resources_discover(&g_tree, 4u, &resources)
        || resources.region_count != 0u
        || resources.clock_provider_index != OS_DEVICE_TREE_NO_PARENT) {
        return 0;
    }
    cfe->regions[0].translated = 1u;

    cfe->regions[1].address = cfe->regions[0].address;
    if (os_pi5_rp1_cfe_resources_discover(&g_tree, 4u, &resources)) {
        return 0;
    }
    cfe->regions[1].address = 0x1f00102000ull;

    cfe->clock_references[0].arguments[0] =
        OS_PI5_RP1_CLOCK_ID_AUDIO_OUT;
    if (os_pi5_rp1_cfe_resources_discover(&g_tree, 4u, &resources)) {
        return 0;
    }
    cfe->clock_references[0].arguments[0] =
        OS_PI5_RP1_CLOCK_ID_MIPI0_CFG;

    cfe->interrupt_cell_count = 0u;
    if (os_pi5_rp1_cfe_resources_discover(&g_tree, 4u, &resources)) {
        return 0;
    }
    cfe->interrupt_cell_count = 2u;

    endpoint->remote_endpoint_index = 8u;
    if (os_pi5_rp1_cfe_resources_discover(&g_tree, 4u, &resources)) {
        return 0;
    }
    endpoint->remote_endpoint_index = 13u;
    endpoint->remote_endpoint_index = 6u;
    if (os_pi5_rp1_cfe_resources_discover(&g_tree, 4u, &resources)) {
        return 0;
    }
    endpoint->remote_endpoint_index = 13u;

    remote->data_lanes[1] = 3u;
    if (os_pi5_rp1_cfe_resources_discover(&g_tree, 4u, &resources)) {
        return 0;
    }
    remote->data_lanes[1] = 2u;

    endpoint->data_lanes[1] = endpoint->data_lanes[0];
    if (os_pi5_rp1_cfe_resources_discover(&g_tree, 4u, &resources)) {
        return 0;
    }
    endpoint->data_lanes[1] = 2u;

    cfe->enabled = 0u;
    if (os_pi5_rp1_cfe_resources_discover(&g_tree, 4u, &resources)) {
        return 0;
    }
    cfe->enabled = 1u;
    endpoint->enabled = 0u;
    if (os_pi5_rp1_cfe_resources_discover(&g_tree, 4u, &resources)) {
        return 0;
    }
    return 1;
}

int os_device_tree_self_test(void)
{
    g_failure_id = 0;
    if (!test_iommu_reference_parsing()) {
        g_failure_id = 3670u;
        return 0;
    }
    if (!build_fixture(1u, 5u, 2u, 7u, 11u, 65u, 100u, 0)) {
        g_failure_id = 1;
        return 0;
    }
    if (!os_device_tree_parse(g_blob, g_blob_size, &g_tree)) {
        g_failure_id = 2;
        return 0;
    }
    if (g_tree.node_count != 21u) {
        g_failure_id = 12;
        return 0;
    }
    const os_device_tree_node *hdmi = &g_tree.nodes[20];
    static const u64 hdmi_addresses[9] = {
        0x107c701400u, 0x107c701000u, 0x107c701d00u,
        0x107c702000u, 0x107c703800u, 0x107c704000u,
        0x107c700100u, 0x107d510800u, 0x107c720000u,
    };
    static const u64 hdmi_sizes[9] = {
        0x300u, 0x200u, 0x300u, 0x80u, 0x200u,
        0x800u, 0x80u, 0x100u, 0x100u,
    };
    if (!hdmi->enabled || hdmi->region_count != 9u
        || !os_device_tree_node_has_compatible(
            hdmi, "brcm,bcm2712-hdmi0")
        || hdmi->reset_reference_count != 1u
        || hdmi->reset_references[0].argument_count != 1u
        || hdmi->reset_references[0].arguments[0] != 1u) {
        g_failure_id = 34u;
        return 0;
    }
    for (usize i = 0u; i < 9u; ++i) {
        if (hdmi->regions[i].address != hdmi_addresses[i]
            || hdmi->regions[i].size != hdmi_sizes[i]
            || !hdmi->regions[i].translated) {
            g_failure_id = 35u;
            return 0;
        }
    }
    if (!g_tree.nodes[4].usb_phy_reference_present
        || !g_tree.nodes[4].usb_power_domain_reference_present
        || !g_tree.nodes[4].usb_power_supply_name_present
        || !g_tree.nodes[4].usb_vbus_supply_present) {
        g_failure_id = 33u;
        return 0;
    }
    os_device_tree_bcm2712_sd_power sd_power;
    if (g_tree.nodes[13].vmmc_supply_index != 15u
        || g_tree.nodes[13].vqmmc_supply_index != 16u
        || g_tree.nodes[14].gpio_bank_width_count != 2u
        || g_tree.nodes[14].gpio_bank_widths[0] != 17u
        || g_tree.nodes[14].gpio_bank_widths[1] != 6u
        || !os_device_tree_get_bcm2712_sd_power(
            &g_tree, &g_tree.nodes[13], &sd_power)
        || sd_power.gpio_node_index != 14u
        || sd_power.card_enable_pin != 4u
        || sd_power.io_voltage_select_pin != 3u
        || sd_power.card_detect_pin != 5u
        || sd_power.card_detect_flags != 1u) {
        g_failure_id = 32u;
        return 0;
    }
    if (!os_device_tree_node_has_compatible(
            &g_tree.nodes[12], "snps,designware-i2c")
        || !g_tree.nodes[12].i2c_scl_rising_time_present
        || g_tree.nodes[12].i2c_scl_rising_time_ns != 65u
        || !g_tree.nodes[12].i2c_scl_falling_time_present
        || g_tree.nodes[12].i2c_scl_falling_time_ns != 100u) {
        g_failure_id = 31u;
        return 0;
    }
    if (!g_tree.nodes[7].interrupt_controller
        || !test_rp1_interrupt_decode()) {
        g_failure_id = 29;
        return 0;
    }
    if (!os_device_tree_node_has_compatible(
            &g_tree.nodes[17], "raspberrypi,rp1-gem")
        || !g_tree.nodes[17].enabled
        || !g_tree.nodes[17].local_mac_address_present
        || g_tree.nodes[17].local_mac_address[0] != 0x02u
        || g_tree.nodes[17].local_mac_address[5] != 1u
        || g_tree.nodes[17].phy_node_index != 18u
        || !g_tree.nodes[17].phy_reset_gpio_resolved
        || g_tree.nodes[17].phy_reset_gpio_provider_index != 3u
        || g_tree.nodes[17].phy_reset_gpio_pin != 32u
        || g_tree.nodes[17].phy_reset_gpio_flags != 1u
        || !g_tree.nodes[17].phy_reset_duration_present
        || g_tree.nodes[17].phy_reset_duration_ms != 5u
        || g_tree.nodes[17].clock_reference_count != 4u
        || g_tree.nodes[17].clock_references[0].arguments[0] != 12u
        || g_tree.nodes[17].clock_references[2].arguments[0] != 16u
        || g_tree.nodes[17].clock_references[3].arguments[0] != 29u) {
        g_failure_id = 34u;
        return 0;
    }
    if (g_tree.nodes[5].msi_parent_index != 11u
        || !g_tree.nodes[11].msi_controller
        || !g_tree.nodes[11].msi_ranges_present
        || g_tree.nodes[11].msi_range_parent_index != 7u
        || g_tree.nodes[11].msi_range_base != 128u
        || g_tree.nodes[11].msi_range_flags != 1u
        || g_tree.nodes[11].msi_range_count != 64u
        || g_tree.nodes[11].msi_offset != 0u
        || !g_tree.nodes[11].msi_offset_present
        || g_tree.nodes[11].region_count != 2u
        || g_tree.nodes[11].regions[0].address != 0x1000130000ull
        || g_tree.nodes[11].regions[0].size != 0xc0u
        || g_tree.nodes[11].regions[0].translated == 0
        || g_tree.nodes[11].regions[1].address != 0xfffffff000ull
        || g_tree.nodes[11].regions[1].size != 0x1000u) {
        g_failure_id = 28;
        return 0;
    }
    u64 dma_address = 0;
    if (!os_device_tree_translate_dma_address(
            &g_tree.nodes[5], 0x40001000u, 0x02000000u,
            0x1000u, &dma_address)
        || dma_address != 0x0000001f10001000ull
        || os_device_tree_translate_dma_address(
            &g_tree.nodes[5], 0x400ff000u, 0x02000000u,
            0x2000u, &dma_address)
        || os_device_tree_translate_dma_address(
            &g_tree.nodes[5], 0x40001000u, 0x01000000u,
            0x1000u, &dma_address)) {
        g_failure_id = 20;
        return 0;
    }
    if (g_tree.nodes[0].parent_index != OS_DEVICE_TREE_NO_PARENT
        || g_tree.nodes[1].parent_index != 0
        || g_tree.nodes[1].enabled == 0
        || g_tree.nodes[1].region_count != 1u
        || g_tree.nodes[1].regions[0].address != 0x0000001f00000000ull
        || g_tree.nodes[1].regions[0].size != 0x00100000ull
        || g_tree.nodes[1].regions[0].translated == 0
        || g_tree.nodes[1].compatible[0] == '\0') {
        g_failure_id = 3;
        return 0;
    }
    if (g_tree.nodes[2].parent_index != 1
        || g_tree.nodes[2].region_count != 1u
        || g_tree.nodes[2].regions[0].address != 0x0000001f00100000ull
        || g_tree.nodes[2].regions[0].size != 0x1000ull
        || g_tree.nodes[2].regions[0].translated == 0) {
        g_failure_id = 4;
        return 0;
    }
    if (g_tree.nodes[2].interrupt_parent_index != 7u) {
        g_failure_id = 22;
        return 0;
    }
    if (!g_tree.nodes[2].skip_init) {
        g_failure_id = 30;
        return 0;
    }
    if (g_tree.nodes[2].clock_reference_count != 2u) {
        g_failure_id = 23;
        return 0;
    }
    if (g_tree.nodes[2].clock_references[0].provider_index != 8u
        || g_tree.nodes[2].clock_references[0].argument_count != 1u
        || g_tree.nodes[2].clock_references[0].arguments[0] != 5u) {
        g_failure_id = 24;
        return 0;
    }
    if (g_tree.nodes[2].reset_reference_count != 1u
        || g_tree.nodes[2].reset_references[0].provider_index != 9u
        || g_tree.nodes[2].reset_references[0].arguments[0] != 1u) {
        g_failure_id = 25;
        return 0;
    }
    os_device_tree_gic_interrupt gic_interrupt;
    if (g_tree.nodes[2].interrupt_cell_count != 3u
        || g_tree.nodes[2].interrupt_cells[0] != 0u
        || g_tree.nodes[2].interrupt_cells[1] != 19u
        || g_tree.nodes[2].interrupt_cells[2] != 4u
        || !os_device_tree_get_gic_interrupt(
            &g_tree, &g_tree.nodes[2], 0u, &gic_interrupt)
        || gic_interrupt.interrupt_id != 51u
        || !gic_interrupt.is_peripheral || gic_interrupt.flags != 4u) {
        g_failure_id = 26;
        return 0;
    }
    if (g_tree.nodes[3].region_count != 3u
        || g_tree.nodes[3].regions[0].address != 0x0000001f000d0000ull
        || g_tree.nodes[3].regions[1].address != 0x0000001f000e0000ull
        || g_tree.nodes[3].regions[2].address != 0x0000001f000f0000ull
        || g_tree.nodes[4].enabled != 0
        || !g_tree.nodes[4].usb_dr_mode_present
        || g_tree.nodes[4].usb_dr_mode
            != OS_DEVICE_TREE_USB_DR_MODE_HOST
        || g_tree.nodes[6].region_count != 1u
        || g_tree.nodes[6].regions[0].address != 0u
        || g_tree.nodes[6].regions[0].size != 0u
        || g_tree.nodes[6].regions[0].address_flags != 0x00000800u
        || g_tree.nodes[6].regions[0].translated != 0
        || g_tree.nodes[6].assigned_region_count != 1u
        || g_tree.nodes[6].assigned_regions[0].address
            != 0x0000001f10001000ull
        || g_tree.nodes[6].assigned_regions[0].size != 0x1000u
        || g_tree.nodes[6].assigned_regions[0].translated == 0
        || g_tree.nodes[6].assigned_regions[0].address_flags != 0
        || g_tree.nodes[5].ranges_present == 0
        || g_tree.nodes[5].range_count != 2u
        || g_tree.nodes[5].ranges[0].child_flags != 0x02000000u
        || g_tree.nodes[5].ranges[0].child_address != 0x40000000u
        || g_tree.nodes[5].ranges[0].parent_address
            != 0x0000001f10000000ull
        || g_tree.nodes[5].ranges[0].size != 0x100000u
        || g_tree.nodes[5].ranges[1].child_flags != 0x43000000u
        || g_tree.nodes[5].ranges[1].child_address != 0x400000000ull
        || g_tree.nodes[5].ranges[1].parent_address
            != 0x0000001f20000000ull
        || g_tree.nodes[5].ranges[1].size != 0x300000000ull
        || g_tree.nodes[5].dma_ranges_present == 0
        || g_tree.nodes[5].dma_range_count != 1u
        || g_tree.nodes[5].dma_ranges[0].child_flags != 0x02000000u
        || g_tree.nodes[5].dma_ranges[0].child_address != 0x40000000u
        || g_tree.nodes[5].dma_ranges[0].parent_address
            != 0x0000001f10000000ull
        || g_tree.nodes[5].dma_ranges[0].size != 0x100000u
        || g_tree.nodes[5].pcie_num_lanes_present == 0u
        || g_tree.nodes[5].pcie_num_lanes != 4u) {
        g_failure_id = 5;
        return 0;
    }
    u32 clock_frequency = 0;
    if (g_tree.nodes[2].clock_reference_count != 2u
        || !os_device_tree_get_clock_frequency(
            &g_tree, &g_tree.nodes[2], 1u, &clock_frequency)
        || clock_frequency != 27000000u
        || os_device_tree_get_clock_frequency(
            &g_tree, &g_tree.nodes[2], 0u, &clock_frequency)) {
        g_failure_id = 21;
        return 0;
    }
    if (!os_device_tree_node_has_compatible(
            &g_tree.nodes[3], "raspberrypi,rp1-gpio")
        || !os_device_tree_node_has_compatible(
            &g_tree.nodes[3], "vendor,gpio-compatible")
        || os_device_tree_node_has_compatible(
            &g_tree.nodes[3], "vendor,not-listed")) {
        g_failure_id = 17;
        return 0;
    }
    if (!test_uefi_configuration_table()) {
        g_failure_id = 6;
        return 0;
    }

    g_fixture_phy_handle = 99u;
    int unresolved_phy_rejected = build_fixture(
            1u, 5u, 2u, 7u, 11u, 65u, 100u, 0)
        && !os_device_tree_parse(g_blob, g_blob_size, &g_tree);
    g_fixture_phy_handle = 24u;
    g_fixture_phy_reset_pin = 54u;
    int invalid_phy_gpio_rejected = build_fixture(
            1u, 5u, 2u, 7u, 11u, 65u, 100u, 0)
        && !os_device_tree_parse(g_blob, g_blob_size, &g_tree);
    g_fixture_phy_reset_pin = 32u;
    g_fixture_local_mac_length = 5u;
    int invalid_mac_length_rejected = build_fixture(
            1u, 5u, 2u, 7u, 11u, 65u, 100u, 0)
        && !os_device_tree_parse(g_blob, g_blob_size, &g_tree);
    g_fixture_local_mac_length = 6u;
    if (!unresolved_phy_rejected || !invalid_phy_gpio_rejected
        || !invalid_mac_length_rejected) {
        g_failure_id = 35u;
        return 0;
    }

    if (!build_fixture(99u, 5u, 2u, 7u, 11u, 65u, 100u, 0)
        || os_device_tree_parse(g_blob, g_blob_size, &g_tree)
        || g_tree.node_count != 0
        || !build_fixture(1u, 5u, 99u, 7u, 11u, 65u, 100u, 0)
        || os_device_tree_parse(g_blob, g_blob_size, &g_tree)
        || g_tree.node_count != 0
        || !build_fixture(1u, 4u, 2u, 7u, 11u, 65u, 100u, 0)
        || os_device_tree_parse(g_blob, g_blob_size, &g_tree)
        || g_tree.node_count != 0
        || !build_fixture(1u, 5u, 2u, 6u, 11u, 65u, 100u, 0)
        || os_device_tree_parse(g_blob, g_blob_size, &g_tree)
        || g_tree.node_count != 0
        || !build_fixture(1u, 5u, 2u, 7u, 99u, 65u, 100u, 0)
        || os_device_tree_parse(g_blob, g_blob_size, &g_tree)
        || g_tree.node_count != 0
        || !build_fixture(1u, 5u, 2u, 7u, 11u, 0u, 100u, 0)
        || os_device_tree_parse(g_blob, g_blob_size, &g_tree)
        || g_tree.node_count != 0
        || !build_fixture(1u, 5u, 2u, 7u, 11u, 65u, 100u, 1)
        || os_device_tree_parse(g_blob, g_blob_size, &g_tree)
        || g_tree.node_count != 0
        || !build_fixture(1u, 5u, 2u, 7u, 11u, 65u, 100u, 0)
        || !os_device_tree_parse(g_blob, g_blob_size, &g_tree)) {
        g_failure_id = 18;
        return 0;
    }

    u32 original_magic = g_blob[0];
    g_blob[0] = 0;
    int rejected = !os_device_tree_parse(g_blob, g_blob_size, &g_tree)
        && g_tree.node_count == 0;
    g_blob[0] = (u8)original_magic;
    if (!rejected
        || os_device_tree_parse(g_blob, g_blob_size - 1u, &g_tree)
        || g_tree.node_count != 0) {
        g_failure_id = 7;
        return 0;
    }
    u32 original_strings_offset = (u32)g_blob[12] << 24
        | (u32)g_blob[13] << 16 | (u32)g_blob[14] << 8 | g_blob[15];
    write_be32(g_blob + 12u, (u32)g_blob_size + 1u);
    rejected = !os_device_tree_parse(g_blob, g_blob_size, &g_tree)
        && g_tree.node_count == 0;
    write_be32(g_blob + 12u, original_strings_offset);
    if (!rejected) {
        g_failure_id = 8;
        return 0;
    }

    usize capability_count = 0;
    const os_pi5_capability *capabilities =
        os_pi5_capabilities(&capability_count);
    if (capabilities == (const os_pi5_capability *)0
        || capability_count != 15u) {
        g_failure_id = 9;
        return 0;
    }
    for (usize i = 0; i < capability_count; ++i) {
        if ((u32)capabilities[i].id != (u32)i + 1u
            || capabilities[i].name == (const char *)0
            || capabilities[i].name[0] == '\0') {
            g_failure_id = 10;
            return 0;
        }
        os_pi5_capability_state expected = i < 3u
            ? OS_PI5_CAPABILITY_METADATA_ONLY
            : ((i == 9u || i == 10u || i == 11u || i == 12u)
                ? OS_PI5_CAPABILITY_SOFTWARE_IMPLEMENTED
                : OS_PI5_CAPABILITY_NOT_IMPLEMENTED);
        if (capabilities[i].state != expected) {
            g_failure_id = 11;
            return 0;
        }
    }
    if (!os_rp1_gpio_self_test()) {
        g_failure_id = 13;
        return 0;
    }
    if (!os_pl011_uart_self_test()) {
        g_failure_id = 100u + os_pl011_uart_self_test_failure();
        return 0;
    }
    if (!os_dw_spi_self_test()) {
        g_failure_id = 15;
        return 0;
    }
    if (!os_dw_i2c_self_test()) {
        g_failure_id = 16;
        return 0;
    }
    if (!os_pi5_dma_self_test()) {
        g_failure_id = 26;
        return 0;
    }
    if (!os_pi5_usb_host_self_test()) {
        g_failure_id = 38000u + os_pi5_usb_host_self_test_failure();
        return 0;
    }
    if (!os_pi5_rp1_audio_out_self_test()) {
        g_failure_id = 3520u;
        return 0;
    }
    if (!os_pi5_bcm2712_sdhci_resources_self_test()) {
        g_failure_id = 3000u;
        return 0;
    }
    if (!os_pi5_bcm2712_sd_pinctrl_self_test()) {
        g_failure_id =
            3001u + os_pi5_bcm2712_sd_pinctrl_self_test_failure();
        return 0;
    }
    if (!os_pi5_gicv2_self_test()) {
        g_failure_id = 27;
        return 0;
    }
    if (!os_pi5_bcm2712_mip_self_test()) {
        g_failure_id = 2900u + os_pi5_bcm2712_mip_self_test_failure();
        return 0;
    }
    if (!os_pi5_bcm2712_hdmi_resources_self_test()) {
        g_failure_id = 3100u;
        return 0;
    }
    if (!os_pi5_bcm2711_aon_l2_self_test()) {
        g_failure_id = 3101u
            + os_pi5_bcm2711_aon_l2_self_test_failure();
        return 0;
    }
    if (!os_pi5_bcm2712_firmware_mailbox_self_test()) {
        g_failure_id = 3150u;
        return 0;
    }
    if (!os_pi5_hdmi_edid_self_test()) {
        g_failure_id = 3200u;
        return 0;
    }
    if (!os_pi5_bcm2712_ddc_self_test()) {
        g_failure_id = 3300u
            + os_pi5_bcm2712_ddc_self_test_failure();
        return 0;
    }
    if (!os_pi5_hdmi_cta_self_test()) {
        g_failure_id = 3400u + os_pi5_hdmi_cta_self_test_failure();
        return 0;
    }
    if (!os_pi5_pcie_self_test()) {
        u32 pcie_failure = os_pi5_pcie_self_test_failure();
        g_failure_id = pcie_failure == 0u ? 19u : 1900u + pcie_failure;
        return 0;
    }
    if (!test_rp1_cfe_discovery()) {
        if (g_failure_id == 0u) {
            g_failure_id = 3500u;
        }
        return 0;
    }
    if (!test_bcm2712_wlan_discovery()) {
        if (g_failure_id == 0u) {
            g_failure_id = 3600u;
        }
        return 0;
    }
    return 1;
}

u32 os_device_tree_self_test_failure(void)
{
    return g_failure_id;
}
