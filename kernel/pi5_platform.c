#include "pi5_platform.h"

static const os_pi5_capability g_pi5_capabilities[] = {
    {OS_PI5_CAP_FDT_DISCOVERY, OS_PI5_CAPABILITY_METADATA_ONLY,
        "UEFI flattened device-tree discovery"},
    {OS_PI5_CAP_REG_METADATA, OS_PI5_CAPABILITY_METADATA_ONLY,
        "device-tree compatible and register metadata"},
    {OS_PI5_CAP_RP1_METADATA, OS_PI5_CAPABILITY_METADATA_ONLY,
        "RP1 compatible and resource metadata"},
    {OS_PI5_CAP_RP1_PCIE, OS_PI5_CAPABILITY_NOT_IMPLEMENTED,
        "RP1 PCIe enumeration and control"},
    {OS_PI5_CAP_USB, OS_PI5_CAPABILITY_NOT_IMPLEMENTED,
        "USB host controllers"},
    {OS_PI5_CAP_MICROSD, OS_PI5_CAPABILITY_NOT_IMPLEMENTED,
        "microSD storage controller"},
    {OS_PI5_CAP_HDMI, OS_PI5_CAPABILITY_NOT_IMPLEMENTED,
        "HDMI display output"},
    {OS_PI5_CAP_ETHERNET, OS_PI5_CAPABILITY_NOT_IMPLEMENTED,
        "Ethernet controller"},
    {OS_PI5_CAP_WIFI, OS_PI5_CAPABILITY_NOT_IMPLEMENTED,
        "wireless networking"},
    {OS_PI5_CAP_GPIO, OS_PI5_CAPABILITY_SOFTWARE_IMPLEMENTED,
        "GPIO"},
    {OS_PI5_CAP_I2C, OS_PI5_CAPABILITY_SOFTWARE_IMPLEMENTED,
        "DesignWare I2C master controller"},
    {OS_PI5_CAP_SPI, OS_PI5_CAPABILITY_SOFTWARE_IMPLEMENTED,
        "DesignWare SSI SPI controller"},
    {OS_PI5_CAP_UART, OS_PI5_CAPABILITY_SOFTWARE_IMPLEMENTED,
        "ARM PrimeCell PL011 UART"},
    {OS_PI5_CAP_AUDIO, OS_PI5_CAPABILITY_NOT_IMPLEMENTED,
        "audio"},
    {OS_PI5_CAP_CAMERA, OS_PI5_CAPABILITY_NOT_IMPLEMENTED,
        "camera and MIPI CSI"},
};

const os_pi5_capability *os_pi5_capabilities(usize *count)
{
    if (count != (usize *)0) {
        *count = sizeof(g_pi5_capabilities) / sizeof(g_pi5_capabilities[0]);
    }
    return g_pi5_capabilities;
}
