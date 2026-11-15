#ifndef EXPERIMENTALOS_PI5_GEM_HOST_H
#define EXPERIMENTALOS_PI5_GEM_HOST_H

#include "pi5_driver.h"

/*
 * Production startup composition for the RP1 Cadence GEM Ethernet controller:
 *   capability read -> DMA ring allocation -> MSI-X/MIP/GIC IRQ bind ->
 *   DMA ring publication -> controller start (prerequisite-gated) ->
 *   os_network_device binding.
 * Every stage is rolled back in reverse order on failure and by shutdown.
 *
 * The prerequisite checks (clock lease, reset/PHY, power, DMA sync, interrupt
 * route) are supplied by the caller and are mandatory: the controller start
 * refuses to run unless every one of them passes. The pinned Raspberry Pi 5
 * platform still lacks an external power-feedback source for the GEM PHY, so
 * no production power check exists; this module does not fabricate one.
 */

typedef enum {
    OS_PI5_GEM_HOST_IDLE = 0,
    OS_PI5_GEM_HOST_RINGS = 1,
    OS_PI5_GEM_HOST_IRQ_BOUND = 2,
    OS_PI5_GEM_HOST_PUBLISHED = 3,
    OS_PI5_GEM_HOST_RUNNING = 4
} os_pi5_gem_host_stage;

typedef struct {
    const os_device_tree_node *node;
    const os_pi5_dma_allocator *allocator;
    u32 address_flags;
    os_pi5_rp1_pci *rp1;
    os_pi5_bcm2712_mip *mip;
    os_pi5_rp1_gem_resources *resources;
    const os_pi5_rp1_gem_prerequisite_checks *checks;
    os_pi5_interrupt_handler irq_handler;
    void *irq_context;
    u32 poll_limit;
} os_pi5_gem_host_config;

typedef struct {
    os_pi5_rp1_gem_capabilities capabilities;
    os_pi5_rp1_gem_dma_rings rings;
    os_pi5_rp1_gem_irq irq;
    os_pi5_rp1_gem_dma_publication publication;
    os_pi5_rp1_gem_controller controller;
    os_pi5_rp1_gem_network_device network;
    os_pi5_gem_host_config config;
    os_pi5_rp1_gem_prerequisite_checks checks;
    u32 missing_prerequisites;
    u8 stage;
} os_pi5_gem_host;

void os_pi5_gem_host_init_state(os_pi5_gem_host *host);

/*
 * Returns 1 only when the controller is running and *network_device is bound.
 * On failure completed stages are rolled back; if a rollback step itself
 * fails the host keeps its stage so os_pi5_gem_host_shutdown() can be
 * retried, and the caller must keep host and *config alive. On failure
 * host->missing_prerequisites reports the failing prerequisite mask.
 */
int os_pi5_gem_host_bringup(
    os_pi5_gem_host *host,
    const os_pi5_gem_host_config *config,
    os_network_device *network_device
);

/* Reverse-order teardown. Idempotent at IDLE; retryable after a failure. */
int os_pi5_gem_host_shutdown(os_pi5_gem_host *host);

#endif
