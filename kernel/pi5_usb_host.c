#include "pi5_usb_host.h"

static void zero_bytes(void *memory, usize size)
{
    volatile u8 *bytes = (volatile u8 *)memory;
    for (usize i = 0u; i < size; ++i) {
        bytes[i] = 0u;
    }
}

#define FORWARD(NAME, FIELD) \
static int NAME(void *context, u32 poll_limit) \
{ \
    const os_pi5_usb_host *host = (const os_pi5_usb_host *)context; \
    const os_pi5_rp1_dwc3_prerequisite_checks *p; \
    if (host == (const os_pi5_usb_host *)0) { \
        return 0; \
    } \
    p = host->config.platform_checks; \
    return p != (const os_pi5_rp1_dwc3_prerequisite_checks *)0 \
        && p->FIELD != (os_pi5_rp1_dwc3_prerequisite_check)0 \
        && p->FIELD(p->context, poll_limit); \
}

FORWARD(check_integrated_clock, integrated_usb_clock_verified)
FORWARD(check_reset, reset_verified)
FORWARD(check_clock, clock_verified)
FORWARD(check_phy, phy_verified)
FORWARD(check_power, power_verified)
FORWARD(check_interrupt_route, interrupt_route_verified)

/*
 * Event lifecycle is verified from real state: the DWC3 core must be in host
 * mode, DRD cores must have their event buffer programmed, and once dispatch
 * is bound it must be ready and error free. A platform-supplied check, when
 * present, must also pass.
 */
static int check_event_lifecycle(void *context, u32 poll_limit)
{
    const os_pi5_usb_host *host = (const os_pi5_usb_host *)context;
    if (host == (const os_pi5_usb_host *)0 || poll_limit == 0u
        || !host->core.host_configured) {
        return 0;
    }
    if (host->core.hw_mode == OS_PI5_DWC3_GHWPARAMS0_MODE_DRD
        && !host->core.event_buffer_programmed) {
        return 0;
    }
    if (host->dispatch_required
        && (!host->dispatch.ready || host->dispatch.dispatch_error
            || host->dispatch.dispatch_active)) {
        return 0;
    }
    const os_pi5_rp1_dwc3_prerequisite_checks *p =
        host->config.platform_checks;
    if (p->event_lifecycle_verified != (os_pi5_rp1_dwc3_prerequisite_check)0
        && !p->event_lifecycle_verified(p->context, poll_limit)) {
        return 0;
    }
    return 1;
}

void os_pi5_usb_host_init_state(os_pi5_usb_host *host)
{
    if (host == (os_pi5_usb_host *)0) {
        return;
    }
    zero_bytes(host, sizeof(*host));
    os_pi5_dwc3_core_state_init(&host->core);
    os_pi5_xhci_controller_state_init(&host->xhci);
    os_pi5_dma_buffer_init(&host->event_buffer);
}

static int config_valid(const os_pi5_usb_host_config *c)
{
    return c != (const os_pi5_usb_host_config *)0
        && c->node != (const os_device_tree_node *)0
        && c->allocator != (const os_pi5_dma_allocator *)0
        && c->resources != (os_pi5_rp1_dwc3_resources *)0
        && c->identity != (const os_pi5_rp1_dwc3_identity *)0
        && c->rings != (os_pi5_xhci_dma_rings *)0
        && c->controller_context != (os_pi5_xhci_controller_context *)0
        && c->platform_checks
            != (const os_pi5_rp1_dwc3_prerequisite_checks *)0
        && c->platform_checks->integrated_usb_clock_verified
            != (os_pi5_rp1_dwc3_prerequisite_check)0
        && c->platform_checks->interrupt_route_verified
            != (os_pi5_rp1_dwc3_prerequisite_check)0
        && c->delay_us != (os_pi5_delay_us)0
        && c->event_handler != (os_pi5_xhci_event_handler)0
        && c->event_budget != 0u
        && c->poll_limit != 0u;
}

/* Reverse-order rollback; leaves stage at the first step that failed. */
static int teardown(os_pi5_usb_host *host)
{
    os_pi5_rp1_dwc3_resources *resources = host->config.resources;
    if (host->stage >= OS_PI5_USB_HOST_PUBLISHED) {
        if (host->xhci.running != 0u
            && !os_pi5_xhci_controller_stop(
                &host->xhci, host->config.poll_limit)) {
            return 0;
        }
        if (!os_pi5_xhci_controller_unpublish(&host->xhci)) {
            return 0;
        }
        host->stage = OS_PI5_USB_HOST_DISPATCH;
    }
    if (host->stage >= OS_PI5_USB_HOST_DISPATCH) {
        host->dispatch.ready = 0u;
        zero_bytes(&host->dispatch, sizeof(host->dispatch));
        host->dispatch_required = 0u;
        host->stage = OS_PI5_USB_HOST_EVENT_BUFFER;
    }
    if (host->stage >= OS_PI5_USB_HOST_CORE) {
        if (!os_pi5_dwc3_core_event_buffer_cleanup(resources, &host->core)) {
            return 0;
        }
        if (host->event_buffer.active
            && !os_pi5_dma_free(host->config.allocator,
                                &host->event_buffer)) {
            return 0;
        }
        host->stage = OS_PI5_USB_HOST_CORE;
    }
    if (host->stage >= OS_PI5_USB_HOST_CORE) {
        if (!os_pi5_dwc3_core_host_release(resources, &host->core)) {
            return 0;
        }
    }
    os_pi5_usb_host_init_state(host);
    return 1;
}

static int bringup_stages(os_pi5_usb_host *host)
{
    const os_pi5_usb_host_config *c = &host->config;

    if (!os_pi5_dwc3_core_host_init(
            c->resources, c->identity, c->usb2_ports, c->usb3_ports,
            c->delay_us, c->delay_context, &host->core)) {
        return 0;
    }
    host->stage = OS_PI5_USB_HOST_CORE;

    if (host->core.hw_mode == OS_PI5_DWC3_GHWPARAMS0_MODE_DRD) {
        if (!os_pi5_dma_alloc(
                c->node, c->allocator, c->address_flags,
                OS_PI5_DWC3_EVENT_BUFFER_SIZE,
                OS_PI5_DWC3_EVENT_BUFFER_ALIGNMENT, &host->event_buffer)) {
            return 0;
        }
        zero_bytes(host->event_buffer.virtual_address,
                   host->event_buffer.size);
        if (!os_pi5_dma_sync_for_device(c->allocator, &host->event_buffer)
            || !os_pi5_dwc3_core_event_buffer_setup(
                c->resources, &host->core, host->event_buffer.dma_address,
                OS_PI5_DWC3_EVENT_BUFFER_SIZE)) {
            return 0;
        }
    } else if (!os_pi5_dwc3_core_event_buffer_setup(
                   c->resources, &host->core, 0u, 0u)) {
        return 0;
    }
    host->stage = OS_PI5_USB_HOST_EVENT_BUFFER;

    if (!os_pi5_xhci_msix_dispatch_state_init(
            &host->dispatch, c->allocator, c->resources, c->rings,
            &host->checks, c->poll_limit, c->event_handler,
            c->event_context, c->event_budget)) {
        return 0;
    }
    host->dispatch_required = 1u;
    host->stage = OS_PI5_USB_HOST_DISPATCH;

    if (!os_pi5_xhci_controller_publish(
            &host->xhci, c->resources, c->rings, c->controller_context,
            &host->checks, c->poll_limit)) {
        return 0;
    }
    host->stage = OS_PI5_USB_HOST_PUBLISHED;

    if (!os_pi5_xhci_controller_start(
            &host->xhci, &host->checks, c->poll_limit)) {
        return 0;
    }
    host->stage = OS_PI5_USB_HOST_RUNNING;

    return os_pi5_dwc3_core_host_plat_start(c->resources, &host->core);
}

int os_pi5_usb_host_bringup(
    os_pi5_usb_host *host,
    const os_pi5_usb_host_config *config
)
{
    if (host == (os_pi5_usb_host *)0 || host->stage != OS_PI5_USB_HOST_IDLE
        || !config_valid(config)) {
        return 0;
    }
    host->config = *config;
    const os_pi5_rp1_dwc3_prerequisite_checks *p = config->platform_checks;

    /* No DWC3 register is touched until the platform proves the USB clock. */
    if (!p->integrated_usb_clock_verified(p->context, config->poll_limit)) {
        os_pi5_usb_host_init_state(host);
        return 0;
    }

    host->checks.context = host;
    host->checks.integrated_usb_clock_verified = check_integrated_clock;
    host->checks.interrupt_route_verified = check_interrupt_route;
    host->checks.event_lifecycle_verified = check_event_lifecycle;
    if (p->reset_verified != (os_pi5_rp1_dwc3_prerequisite_check)0) {
        host->checks.reset_verified = check_reset;
    }
    if (p->clock_verified != (os_pi5_rp1_dwc3_prerequisite_check)0) {
        host->checks.clock_verified = check_clock;
    }
    if (p->phy_verified != (os_pi5_rp1_dwc3_prerequisite_check)0) {
        host->checks.phy_verified = check_phy;
    }
    if (p->power_verified != (os_pi5_rp1_dwc3_prerequisite_check)0) {
        host->checks.power_verified = check_power;
    }

    if (bringup_stages(host)) {
        return 1;
    }
    (void)teardown(host);
    return 0;
}

int os_pi5_usb_host_shutdown(os_pi5_usb_host *host)
{
    if (host == (os_pi5_usb_host *)0) {
        return 0;
    }
    if (host->stage == OS_PI5_USB_HOST_IDLE) {
        return 1;
    }
    return teardown(host);
}

void os_pi5_usb_host_interrupt_handler(void *context, u32 interrupt_vector)
{
    os_pi5_usb_host *host = (os_pi5_usb_host *)context;
    if (host == (os_pi5_usb_host *)0
        || host->stage < OS_PI5_USB_HOST_DISPATCH) {
        return;
    }
    os_pi5_xhci_msix_interrupt_handler(&host->dispatch, interrupt_vector);
}
