# Raspberry Pi 5 compatibility matrix

Release target: `0.1.0-experimental.1`. All physical rows:
**HARDWARE_NOT_VERIFIED**. C1/D0 firmware claims below are upstream reports,
not RROS measurements.

| Component | Available software check | Physical test / limitation |
|---|---|---|
| Pi 5 Model B 4/8 GB | AArch64 cross-build, CPU/memory/FDT probe | Record revision, stepping, RAM, EEPROM and firmware; no accepted boot |
| EEPROM / Pi UEFI v0.3 | Archive and per-file SHA-256, pinned DTB parser | Archived firmware; C1 upstream baseline; D0/new EEPROM display risk |
| UART | FDT console selection and generic QEMU probe | Official connector pinout/voltage must be verified by tester; 115200 8N1 |
| MMU, GIC, timer | Guest/host fixtures; probe firmware state/counter | Probe does not initialize a complete native Pi interrupt service |
| PCIe / RP1 | FDT descriptions, register/resource fixtures | Probe presence in FDT is NOT link/BAR/MSI-X functionality |
| USB keyboard/mouse/storage | Controller and protocol fixtures | DWC3/xHCI/clock handoff incomplete; no operational Pi USB claim |
| Ethernet | GEM/DMA/PHY/protocol fixtures | Production startup/power feedback incomplete; upstream UEFI RP1 Ethernet limitation |
| HDMI/display | EDID/HVS/clock/software-renderer fixtures | Pinned disabled nodes, no native modesetting path; firmware display is not RROS display |
| microSD | GPT/FAT32 image integrity; SDHCI/PIO fixtures | No verified native physical filesystem or power-loss behavior |
| Camera | CFE resource/identity fixtures | No camera capture pipeline |
| Audio | Clock/I2S/DMA software fixtures | No production audio wiring; Pi 5 has no analog 3.5 mm jack |
| Wi-Fi / Bluetooth | SDIO and defensive lifecycle checks | No trusted bundled wireless firmware or operational wireless stack |
| GPIO / I2C / SPI | Register-model ownership/transfer/IRQ fixtures | No physical wiring or attached-device validation; do not drive actuators |
| GPU acceleration | Software rendering and fail-closed transport | No verified V3D backend; no Vulkan claim |
| AI/MCP/robotics | Host/guest contracts and bounded simulations | No physical robotics/PLC/robot actuation authorized |

Report individual results through the [issue form](.github/ISSUE_TEMPLATE/pi5-hardware.yml).
Historical QEMU reports are not included and are not hardware reports.
