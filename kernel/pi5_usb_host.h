#ifndef EXPERIMENTALOS_PI5_USB_HOST_H
#define EXPERIMENTALOS_PI5_USB_HOST_H

#include "pi5_dwc3_core.h"

/*
 * Production startup composition for an RP1 DWC3 host controller:
 *   platform clock preflight -> DWC3 global host init -> DWC3 event buffer
 *   (DRD cores only) -> xHCI event dispatch state -> xHCI publish/start ->
 *   dwc3_xhci_plat_start() equivalent.
 * Every stage is rolled back in reverse order on failure and by shutdown.
 *
 * The platform checks are mandatory-fail-closed: integrated_usb_clock_verified
 * must be supplied and must pass before any DWC3 register is written. On the
 * pinned Raspberry Pi 5 device tree the USBH clock IDs (25-28) have no
 * registered provider, so no production platform check can pass yet and this
 * composition cannot enable a physical controller. That is a documented
 * external blocker, not something this module simulates.
 */

typedef enum {
    OS_PI5_USB_HOST_IDLE = 0,
    OS_PI5_USB_HOST_CORE = 1,
    OS_PI5_USB_HOST_EVENT_BUFFER = 2,
    OS_PI5_USB_HOST_DISPATCH = 3,
    OS_PI5_USB_HOST_PUBLISHED = 4,
    OS_PI5_USB_HOST_RUNNING = 5
} os_pi5_usb_host_stage;

typedef struct {
    const os_device_tree_node *node;
    const os_pi5_dma_allocator *allocator;
    u32 address_flags;
    os_pi5_rp1_dwc3_resources *resources;
    const os_pi5_rp1_dwc3_identity *identity;
    u8 usb2_ports;
    u8 usb3_ports;
    os_pi5_xhci_dma_rings *rings;
    os_pi5_xhci_controller_context *controller_context;
    const os_pi5_rp1_dwc3_prerequisite_checks *platform_checks;
    os_pi5_delay_us delay_us;
    void *delay_context;
    os_pi5_xhci_event_handler event_handler;
    void *event_context;
    u32 event_budget;
    u32 poll_limit;
} os_pi5_usb_host_config;

typedef struct os_pi5_usb_host os_pi5_usb_host;

struct os_pi5_usb_host {
    os_pi5_dwc3_core_state core;
    os_pi5_xhci_controller_state xhci;
    os_pi5_xhci_msix_dispatch_state dispatch;
    os_pi5_dma_buffer event_buffer;
    os_pi5_usb_host_config config;
    os_pi5_rp1_dwc3_prerequisite_checks checks;
    u8 stage;
    u8 dispatch_required;
};

void os_pi5_usb_host_init_state(os_pi5_usb_host *host);

/*
 * Runs the full startup. Returns 1 only when every stage succeeded and the
 * controller is running. On failure all completed stages are rolled back; if
 * a rollback step itself fails the host stays at the failed stage so the
 * caller can retry os_pi5_usb_host_shutdown() and must not free the supplied
 * rings or context. host and *config must outlive the running controller.
 */
int os_pi5_usb_host_bringup(
    os_pi5_usb_host *host,
    const os_pi5_usb_host_config *config
);

/* Reverse-order teardown. Idempotent at IDLE; retryable after a failure. */
int os_pi5_usb_host_shutdown(os_pi5_usb_host *host);

/* Interrupt entry; context must be the os_pi5_usb_host pointer. */
void os_pi5_usb_host_interrupt_handler(void *context, u32 interrupt_vector);

#endif
