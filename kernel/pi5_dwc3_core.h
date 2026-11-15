#ifndef EXPERIMENTALOS_PI5_DWC3_CORE_H
#define EXPERIMENTALOS_PI5_DWC3_CORE_H

#include "pi5_driver.h"

/*
 * Synopsys DWC3 global-core host-mode initialization for the RP1 USB
 * controllers. Register offsets, bit meanings and sequencing are taken from
 * raspberrypi/linux drivers/usb/dwc3/core.{c,h} and host.c at commit
 * 31a34885388835dce2460744d7961f2b9e7dc416 (host dr_mode, no external PHY,
 * no vendor quirk properties, which is what the pinned RP1 node declares).
 *
 * This programs the DWC3 global block only. It does not enable USB clocks
 * (IDs 25-28 remain unsupported), does not start the xHCI controller, and
 * does not by itself make USB operational.
 */

#define OS_PI5_DWC3_GSBUSCFG0 0x0000c100u
#define OS_PI5_DWC3_GCTL 0x0000c110u
#define OS_PI5_DWC3_GUCTL1 0x0000c11cu
#define OS_PI5_DWC3_GHWPARAMS0 0x0000c140u
#define OS_PI5_DWC3_GHWPARAMS1 0x0000c144u
#define OS_PI5_DWC3_GHWPARAMS6 0x0000c158u
#define OS_PI5_DWC3_GUSB2PHYCFG(n) (0x0000c200u + 4u * (u32)(n))
#define OS_PI5_DWC3_GUSB3PIPECTL(n) (0x0000c2c0u + 4u * (u32)(n))
#define OS_PI5_DWC3_GUCTL2 0x0000c19cu
#define OS_PI5_DWC3_DCTL 0x0000c704u
#define OS_PI5_DWC3_DSTS 0x0000c70cu
#define OS_PI5_DWC3_GEVNTADRLO(n) (0x0000c400u + 0x10u * (u32)(n))
#define OS_PI5_DWC3_GEVNTADRHI(n) (0x0000c404u + 0x10u * (u32)(n))
#define OS_PI5_DWC3_GEVNTSIZ(n) (0x0000c408u + 0x10u * (u32)(n))
#define OS_PI5_DWC3_GEVNTCOUNT(n) (0x0000c40cu + 0x10u * (u32)(n))
#define OS_PI5_DWC3_GEVNTSIZ_INTMASK (1u << 31)
#define OS_PI5_DWC3_DSTS_DEVCTRLHLT (1u << 22)
#define OS_PI5_DWC3_EVENT_BUFFER_SIZE 4096u
#define OS_PI5_DWC3_EVENT_BUFFER_ALIGNMENT 16u

#define OS_PI5_DWC3_DCTL_CSFTRST (1u << 30)
#define OS_PI5_DWC3_DCTL_RUN_STOP (1u << 31)
#define OS_PI5_DWC3_GCTL_DSBLCLKGTNG (1u << 0)
#define OS_PI5_DWC3_GCTL_GBLHIBERNATIONEN (1u << 1)
#define OS_PI5_DWC3_GCTL_DISSCRAMBLE (1u << 3)
#define OS_PI5_DWC3_GCTL_SCALEDOWN_MASK (3u << 4)
#define OS_PI5_DWC3_GCTL_SOFITPSYNC (1u << 10)
#define OS_PI5_DWC3_GCTL_PRTCAP_SHIFT 12u
#define OS_PI5_DWC3_GCTL_PRTCAP_MASK (3u << 12)
#define OS_PI5_DWC3_GCTL_PRTCAP_HOST 1u
#define OS_PI5_DWC3_GCTL_U2RSTECN (1u << 16)
#define OS_PI5_DWC3_GUCTL1_DEV_DECOUPLE_L1L2_EVT (1u << 31)
#define OS_PI5_DWC3_GUCTL1_DEV_L1_EXIT_BY_HW (1u << 24)
#define OS_PI5_DWC3_GUCTL2_RST_ACTBITLATER (1u << 14)
#define OS_PI5_DWC3_GUCTL2_LC_TIMER (1u << 19)
#define OS_PI5_DWC3_GUSB2PHYCFG_SUSPHY (1u << 6)
#define OS_PI5_DWC3_GUSB3PIPECTL_SUSPHY (1u << 17)
#define OS_PI5_DWC3_GHWPARAMS0_MODE_GADGET 0u
#define OS_PI5_DWC3_GHWPARAMS0_MODE_HOST 1u
#define OS_PI5_DWC3_GHWPARAMS0_MODE_DRD 2u
#define OS_PI5_DWC3_GHWPARAMS1_PWROPT_NO 0u
#define OS_PI5_DWC3_GHWPARAMS1_PWROPT_CLK 1u
#define OS_PI5_DWC3_GHWPARAMS1_PWROPT_HIB 2u
#define OS_PI5_DWC3_GHWPARAMS6_EN_FPGA (1u << 7)

#define OS_PI5_DWC3_REV_DWC3_190A 0x5533190au
#define OS_PI5_DWC3_REV_DWC3_194A 0x5533194au
#define OS_PI5_DWC3_REV_DWC3_210A 0x5533210au
#define OS_PI5_DWC3_REV_DWC3_250A 0x5533250au
#define OS_PI5_DWC3_REV_DWC3_310A 0x5533310au
#define OS_PI5_DWC3_REV_DWC3_320A 0x5533320au
#define OS_PI5_DWC3_REV_DWC31_180A 0x3138302au
#define OS_PI5_DWC3_REV_DWC31_190A 0x3139302au
#define OS_PI5_DWC3_REV_DWC31_200A 0x3230302au

#define OS_PI5_DWC3_MAX_PORTS_PER_TYPE 16u
#define OS_PI5_DWC3_SOFT_RESET_POLLS_LEGACY 1000u
#define OS_PI5_DWC3_SOFT_RESET_POLLS_SYNC 10u

typedef struct {
    u16 ip_id;
    u32 revision;
    u8 hw_mode;
    u8 usb2_ports;
    u8 usb3_ports;
    u8 host_configured;
    u8 susphy_enabled;
    u8 event_buffer_programmed;
    u64 event_buffer_dma;
    u32 event_buffer_length;
} os_pi5_dwc3_core_state;

void os_pi5_dwc3_core_state_init(os_pi5_dwc3_core_state *state);

/*
 * Performs the Linux dwc3_core_init() global sequence for a host-mode core
 * without external PHYs, then selects the host port capability. delay_us is
 * required (soft reset waits). On any failure all registers modified so far
 * are restored (best effort) and state stays unconfigured. A successful
 * return means registers read back as programmed; it does not mean a USB
 * device can be enumerated.
 */
int os_pi5_dwc3_core_host_init(
    os_pi5_rp1_dwc3_resources *resources,
    const os_pi5_rp1_dwc3_identity *identity,
    u8 usb2_ports,
    u8 usb3_ports,
    os_pi5_delay_us delay_us,
    void *delay_context,
    os_pi5_dwc3_core_state *state
);

/*
 * Equivalent of dwc3_xhci_plat_start(): enables PHY suspend after the xHCI
 * controller has been started. Call only after os_pi5_xhci_controller_start.
 */
int os_pi5_dwc3_core_host_plat_start(
    os_pi5_rp1_dwc3_resources *resources,
    os_pi5_dwc3_core_state *state
);

/*
 * Equivalent of dwc3_event_buffers_setup() for event buffer 0. Linux
 * allocates a DWC3 event buffer only when GHWPARAMS0 reports a DRD core; for
 * a host-only core this is a successful no-op (xHCI uses its own event ring).
 * For DRD cores the caller supplies a coherent DMA buffer of exactly
 * OS_PI5_DWC3_EVENT_BUFFER_SIZE bytes that stays allocated until
 * os_pi5_dwc3_core_event_buffer_cleanup() succeeds.
 */
int os_pi5_dwc3_core_event_buffer_setup(
    os_pi5_rp1_dwc3_resources *resources,
    os_pi5_dwc3_core_state *state,
    u64 dma_address,
    u32 length
);

/*
 * Equivalent of dwc3_event_buffers_cleanup(). Returns 0 and leaves the buffer
 * programmed when the controller is not halted (DSTS.DEVCTRLHLT clear), in
 * which case the caller must not free the DMA buffer.
 */
int os_pi5_dwc3_core_event_buffer_cleanup(
    os_pi5_rp1_dwc3_resources *resources,
    os_pi5_dwc3_core_state *state
);

/* Fails while an event buffer is programmed; performs no register writes. */
int os_pi5_dwc3_core_host_release(
    os_pi5_rp1_dwc3_resources *resources,
    os_pi5_dwc3_core_state *state
);

#endif
