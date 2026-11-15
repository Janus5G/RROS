#include "pi5_gem_host.h"

void os_pi5_gem_host_init_state(os_pi5_gem_host *host)
{
    if (host == (os_pi5_gem_host *)0) {
        return;
    }
    os_pi5_rp1_gem_capabilities_init_state(&host->capabilities);
    os_pi5_rp1_gem_dma_rings_init_state(&host->rings);
    os_pi5_rp1_gem_irq_init_state(&host->irq);
    os_pi5_rp1_gem_dma_publication_init_state(&host->publication);
    os_pi5_rp1_gem_controller_init_state(&host->controller);
    host->network.allocator = (const os_pi5_dma_allocator *)0;
    host->network.rings = (os_pi5_rp1_gem_dma_rings *)0;
    host->network.controller = (os_pi5_rp1_gem_controller *)0;
    host->network.tx_kicks = 0u;
    host->network.rx_frames = 0u;
    host->network.errors = 0u;
    host->network.ready = 0u;
    host->config.node = (const os_device_tree_node *)0;
    host->config.allocator = (const os_pi5_dma_allocator *)0;
    host->config.address_flags = 0u;
    host->config.rp1 = (os_pi5_rp1_pci *)0;
    host->config.mip = (os_pi5_bcm2712_mip *)0;
    host->config.resources = (os_pi5_rp1_gem_resources *)0;
    host->config.checks = (const os_pi5_rp1_gem_prerequisite_checks *)0;
    host->config.irq_handler = (os_pi5_interrupt_handler)0;
    host->config.irq_context = (void *)0;
    host->config.poll_limit = 0u;
    host->checks.context = (void *)0;
    host->checks.clock_context = (void *)0;
    host->checks.dma_sync_context = (void *)0;
    host->checks.reset_context = (void *)0;
    host->checks.phy_context = (void *)0;
    host->checks.power_context = (void *)0;
    host->checks.clock_verified = (os_pi5_rp1_gem_prerequisite_check)0;
    host->checks.reset_verified = (os_pi5_rp1_gem_prerequisite_check)0;
    host->checks.phy_verified = (os_pi5_rp1_gem_prerequisite_check)0;
    host->checks.power_verified = (os_pi5_rp1_gem_prerequisite_check)0;
    host->checks.dma_sync_verified = (os_pi5_rp1_gem_prerequisite_check)0;
    host->checks.interrupt_route_verified =
        (os_pi5_rp1_gem_prerequisite_check)0;
    host->missing_prerequisites = 0u;
    host->stage = OS_PI5_GEM_HOST_IDLE;
}

/*
 * Inspects the real sub-state rather than only the recorded stage, because a
 * failed controller start can leave a locked controller that must be stopped
 * before the publication, IRQ and rings can be released.
 */
static int gem_host_teardown(os_pi5_gem_host *host)
{
    u32 missing = 0u;
    host->network.ready = 0u;

    if (host->controller.ready) {
        if (host->controller.active) {
            /* Halt the DMA engines before dropping unsent frames. */
            os_pi5_mmio *mmio = &host->config.resources->mapping.mmio;
            u32 control = 0u;
            if (!mmio->read32(mmio->context, 0u, OS_PI5_RP1_GEM_NCR, &control)
                || !mmio->write32(
                    mmio->context, 0u, OS_PI5_RP1_GEM_NCR,
                    control & ~(OS_PI5_RP1_GEM_NCR_RE
                        | OS_PI5_RP1_GEM_NCR_TE))) {
                host->missing_prerequisites |=
                    OS_PI5_RP1_GEM_MISSING_RESOURCES;
                return 0;
            }
        }
        host->rings.tx_pending_count = 0u;
        if (!os_pi5_rp1_gem_controller_stop(
                &host->controller, &host->capabilities, &host->checks,
                host->config.poll_limit, &missing)) {
            host->missing_prerequisites |= missing;
            return 0;
        }
    }
    if (host->publication.ready) {
        if (!os_pi5_rp1_gem_dma_publication_restore(
                &host->publication, &host->capabilities, &host->checks,
                host->config.poll_limit, &missing)) {
            host->missing_prerequisites |= missing;
            return 0;
        }
    }
    if (host->irq.ready) {
        if (host->irq.enabled && !os_pi5_rp1_gem_irq_disable(&host->irq)) {
            return 0;
        }
        if (host->irq.bound && !os_pi5_rp1_gem_irq_unbind(&host->irq)) {
            return 0;
        }
    }
    if (host->stage >= OS_PI5_GEM_HOST_RINGS && host->rings.ready) {
        /* Transmitter is stopped and unpublished; unsent frames are dropped. */
        host->rings.tx_pending_count = 0u;
        if (!os_pi5_rp1_gem_dma_rings_release(
                host->config.allocator, &host->rings)) {
            return 0;
        }
    }

    u32 retained = host->missing_prerequisites;
    os_pi5_gem_host_init_state(host);
    host->missing_prerequisites = retained;
    return 1;
}

int os_pi5_gem_host_bringup(
    os_pi5_gem_host *host,
    const os_pi5_gem_host_config *config,
    os_network_device *network_device
)
{
    if (host == (os_pi5_gem_host *)0
        || config == (const os_pi5_gem_host_config *)0
        || network_device == (os_network_device *)0
        || host->stage != OS_PI5_GEM_HOST_IDLE
        || config->node == (const os_device_tree_node *)0
        || config->allocator == (const os_pi5_dma_allocator *)0
        || config->rp1 == (os_pi5_rp1_pci *)0
        || config->mip == (os_pi5_bcm2712_mip *)0
        || config->resources == (os_pi5_rp1_gem_resources *)0
        || config->checks == (const os_pi5_rp1_gem_prerequisite_checks *)0
        || config->irq_handler == (os_pi5_interrupt_handler)0
        || config->poll_limit == 0u) {
        return 0;
    }

    os_pi5_gem_host_init_state(host);
    host->config = *config;
    host->checks = *config->checks;
    network_device->context = (void *)0;
    network_device->send_frame = (os_network_send_frame)0;
    network_device->receive_frame = (os_network_receive_frame)0;

    if (!os_pi5_rp1_gem_capabilities_read(
            config->resources, &host->capabilities)) {
        os_pi5_gem_host_init_state(host);
        return 0;
    }
    if (!os_pi5_rp1_gem_dma_rings_allocate_with_capabilities(
            config->node, config->allocator, config->address_flags,
            &host->capabilities, &host->rings)) {
        os_pi5_gem_host_init_state(host);
        return 0;
    }
    host->stage = OS_PI5_GEM_HOST_RINGS;

    if (!os_pi5_rp1_gem_irq_bind(
            config->resources, config->rp1, config->mip,
            &host->capabilities, config->irq_handler,
            config->irq_context, &host->irq)) {
        goto fail;
    }
    host->stage = OS_PI5_GEM_HOST_IRQ_BOUND;

    if (!os_pi5_rp1_gem_dma_publication_program(
            config->resources, &host->capabilities, &host->rings,
            &host->irq, &host->checks, config->poll_limit,
            &host->missing_prerequisites, &host->publication)) {
        goto fail;
    }
    host->stage = OS_PI5_GEM_HOST_PUBLISHED;

    if (!os_pi5_rp1_gem_controller_start(
            &host->publication, &host->capabilities, &host->checks,
            config->poll_limit, &host->missing_prerequisites,
            &host->controller)) {
        goto fail;
    }
    if (!os_pi5_rp1_gem_network_device_init(
            &host->network, config->allocator, &host->rings,
            &host->controller, network_device)) {
        goto fail;
    }
    host->stage = OS_PI5_GEM_HOST_RUNNING;
    return 1;

fail:
    network_device->context = (void *)0;
    network_device->send_frame = (os_network_send_frame)0;
    network_device->receive_frame = (os_network_receive_frame)0;
    (void)gem_host_teardown(host);
    return 0;
}

int os_pi5_gem_host_shutdown(os_pi5_gem_host *host)
{
    if (host == (os_pi5_gem_host *)0) {
        return 0;
    }
    if (host->stage == OS_PI5_GEM_HOST_IDLE) {
        return 1;
    }
    return gem_host_teardown(host);
}
