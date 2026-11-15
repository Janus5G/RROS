#include "pi5_dwc3_core.h"

#define SNAPSHOT_REGS 6u

typedef struct {
    u32 offset;
    u32 value;
    u8 valid;
} reg_snapshot;

typedef struct {
    reg_snapshot regs[SNAPSHOT_REGS + 2u * OS_PI5_DWC3_MAX_PORTS_PER_TYPE];
    u32 count;
} snapshot_set;

static int rd(os_pi5_rp1_dwc3_resources *r, u32 offset, u32 *value)
{
    return r->mmio.read32(r->mmio.context, 0u, offset, value);
}

static int wr(os_pi5_rp1_dwc3_resources *r, u32 offset, u32 value)
{
    return r->mmio.write32(r->mmio.context, 0u, offset, value);
}

/* Records the pre-modification value once so a failed init can roll back. */
static int save(os_pi5_rp1_dwc3_resources *r, snapshot_set *set, u32 offset)
{
    for (u32 i = 0u; i < set->count; i++) {
        if (set->regs[i].offset == offset) {
            return 1;
        }
    }
    if (set->count >= sizeof(set->regs) / sizeof(set->regs[0])) {
        return 0;
    }
    reg_snapshot *slot = &set->regs[set->count];
    if (!rd(r, offset, &slot->value)) {
        return 0;
    }
    slot->offset = offset;
    slot->valid = 1u;
    set->count++;
    return 1;
}

static void rollback(os_pi5_rp1_dwc3_resources *r, const snapshot_set *set)
{
    for (u32 i = set->count; i > 0u; i--) {
        (void)wr(r, set->regs[i - 1u].offset, set->regs[i - 1u].value);
    }
}

static int rmw(
    os_pi5_rp1_dwc3_resources *r,
    snapshot_set *set,
    u32 offset,
    u32 clear,
    u32 setbits
)
{
    u32 value = 0u;
    if (!save(r, set, offset) || !rd(r, offset, &value)) {
        return 0;
    }
    value = (value & ~clear) | setbits;
    return wr(r, offset, value);
}

static int is_dwc3(const os_pi5_dwc3_core_state *s)
{
    return s->ip_id == OS_RP1_DWC3_ID_DWC3;
}

static int is_dwc31(const os_pi5_dwc3_core_state *s)
{
    return s->ip_id == OS_RP1_DWC3_ID_DWC31;
}

static int is_dwc32(const os_pi5_dwc3_core_state *s)
{
    return s->ip_id == OS_RP1_DWC3_ID_DWC32;
}

static int susphy_set(
    os_pi5_rp1_dwc3_resources *r,
    snapshot_set *set,
    const os_pi5_dwc3_core_state *s,
    int enable
)
{
    for (u32 i = 0u; i < s->usb3_ports; i++) {
        u32 off = OS_PI5_DWC3_GUSB3PIPECTL(i);
        if (!rmw(r, set, off,
                 enable ? 0u : OS_PI5_DWC3_GUSB3PIPECTL_SUSPHY,
                 enable ? OS_PI5_DWC3_GUSB3PIPECTL_SUSPHY : 0u)) {
            return 0;
        }
    }
    for (u32 i = 0u; i < s->usb2_ports; i++) {
        u32 off = OS_PI5_DWC3_GUSB2PHYCFG(i);
        if (!rmw(r, set, off,
                 enable ? 0u : OS_PI5_DWC3_GUSB2PHYCFG_SUSPHY,
                 enable ? OS_PI5_DWC3_GUSB2PHYCFG_SUSPHY : 0u)) {
            return 0;
        }
    }
    return 1;
}

/* dwc3_core_soft_reset() for a core whose role has not been selected yet. */
static int soft_reset(
    os_pi5_rp1_dwc3_resources *r,
    snapshot_set *set,
    const os_pi5_dwc3_core_state *s,
    os_pi5_delay_us delay_us,
    void *delay_context
)
{
    if (!rmw(r, set, OS_PI5_DWC3_DCTL,
             OS_PI5_DWC3_DCTL_RUN_STOP, OS_PI5_DWC3_DCTL_CSFTRST)) {
        return 0;
    }
    int sync_reset = is_dwc32(s)
        || (is_dwc31(s) && s->revision >= OS_PI5_DWC3_REV_DWC31_190A);
    u32 polls = sync_reset ? OS_PI5_DWC3_SOFT_RESET_POLLS_SYNC
                           : OS_PI5_DWC3_SOFT_RESET_POLLS_LEGACY;
    int done = 0;
    for (u32 i = 0u; i < polls; i++) {
        u32 dctl = 0u;
        if (!rd(r, OS_PI5_DWC3_DCTL, &dctl)) {
            return 0;
        }
        if ((dctl & OS_PI5_DWC3_DCTL_CSFTRST) == 0u) {
            done = 1;
            break;
        }
        if (sync_reset
            ? !delay_us(delay_context, 20000u, 21000u)
            : !delay_us(delay_context, 1u, 2u)) {
            return 0;
        }
    }
    if (!done) {
        return 0;
    }
    if (is_dwc31(s) && s->revision <= OS_PI5_DWC3_REV_DWC31_180A
        && !delay_us(delay_context, 50000u, 51000u)) {
        return 0;
    }
    return 1;
}

static int global_control(
    os_pi5_rp1_dwc3_resources *r,
    snapshot_set *set,
    const os_pi5_dwc3_core_state *s,
    u32 hwparams1,
    u32 hwparams6
)
{
    u32 clear = OS_PI5_DWC3_GCTL_SCALEDOWN_MASK
        | OS_PI5_DWC3_GCTL_DISSCRAMBLE;
    u32 setbits = 0u;
    u32 power_opt = (hwparams1 >> 24) & 3u;

    if (power_opt == OS_PI5_DWC3_GHWPARAMS1_PWROPT_CLK) {
        if (is_dwc3(s) && s->revision >= OS_PI5_DWC3_REV_DWC3_210A
            && s->revision <= OS_PI5_DWC3_REV_DWC3_250A) {
            setbits |= OS_PI5_DWC3_GCTL_DSBLCLKGTNG
                | OS_PI5_DWC3_GCTL_SOFITPSYNC;
        } else {
            clear |= OS_PI5_DWC3_GCTL_DSBLCLKGTNG;
        }
    } else if (power_opt == OS_PI5_DWC3_GHWPARAMS1_PWROPT_HIB) {
        setbits |= OS_PI5_DWC3_GCTL_GBLHIBERNATIONEN;
    }
    /* STAR#4846132: DWC_usb31 2.00a host/DRD needs clock gating disabled. */
    if (power_opt != OS_PI5_DWC3_GHWPARAMS1_PWROPT_NO
        && s->hw_mode != OS_PI5_DWC3_GHWPARAMS0_MODE_GADGET
        && is_dwc31(s) && s->revision == OS_PI5_DWC3_REV_DWC31_200A) {
        clear &= ~OS_PI5_DWC3_GCTL_DSBLCLKGTNG;
        setbits |= OS_PI5_DWC3_GCTL_DSBLCLKGTNG;
    }
    (void)hwparams6; /* FPGA flag only matters with the disable-scramble quirk */
    if (is_dwc3(s) && s->revision < OS_PI5_DWC3_REV_DWC3_190A) {
        setbits |= OS_PI5_DWC3_GCTL_U2RSTECN;
    }
    return rmw(r, set, OS_PI5_DWC3_GCTL, clear, setbits);
}

static int guctl_config(
    os_pi5_rp1_dwc3_resources *r,
    snapshot_set *set,
    const os_pi5_dwc3_core_state *s
)
{
    if (is_dwc3(s) && s->revision >= OS_PI5_DWC3_REV_DWC3_310A
        && !rmw(r, set, OS_PI5_DWC3_GUCTL2, 0u,
                OS_PI5_DWC3_GUCTL2_RST_ACTBITLATER)) {
        return 0;
    }
    if (is_dwc3(s) && s->revision == OS_PI5_DWC3_REV_DWC3_320A
        && !rmw(r, set, OS_PI5_DWC3_GUCTL2,
                OS_PI5_DWC3_GUCTL2_LC_TIMER, 0u)) {
        return 0;
    }
    /* Pre-2.50a DWC3 cores skip GUCTL1; every other IP takes this path. */
    if (!(is_dwc3(s) && s->revision < OS_PI5_DWC3_REV_DWC3_250A)) {
        u32 bits = OS_PI5_DWC3_GUCTL1_DEV_L1_EXIT_BY_HW
            | OS_PI5_DWC3_GUCTL1_DEV_DECOUPLE_L1L2_EVT;
        if (is_dwc3(s) && s->revision < 0x5533290au) {
            bits &= ~OS_PI5_DWC3_GUCTL1_DEV_L1_EXIT_BY_HW;
        }
        if (is_dwc3(s) && s->revision < 0x5533300au) {
            bits &= ~OS_PI5_DWC3_GUCTL1_DEV_DECOUPLE_L1L2_EVT;
        }
        if (!rmw(r, set, OS_PI5_DWC3_GUCTL1, 0u, bits)) {
            return 0;
        }
    }
    return 1;
}

void os_pi5_dwc3_core_state_init(os_pi5_dwc3_core_state *state)
{
    if (state == (os_pi5_dwc3_core_state *)0) {
        return;
    }
    state->ip_id = 0u;
    state->revision = 0u;
    state->hw_mode = 0u;
    state->usb2_ports = 0u;
    state->usb3_ports = 0u;
    state->host_configured = 0u;
    state->susphy_enabled = 0u;
    state->event_buffer_programmed = 0u;
    state->event_buffer_dma = 0u;
    state->event_buffer_length = 0u;
}

static int resources_usable(const os_pi5_rp1_dwc3_resources *r)
{
    return r != (const os_pi5_rp1_dwc3_resources *)0
        && r->ready
        && r->region_size == OS_RP1_DWC3_REGION_SIZE
        && r->mmio.region_sizes[0] >= r->region_size
        && r->mmio.read32 != (os_pi5_mmio_read32)0
        && r->mmio.write32 != (os_pi5_mmio_write32)0;
}

int os_pi5_dwc3_core_host_init(
    os_pi5_rp1_dwc3_resources *resources,
    const os_pi5_rp1_dwc3_identity *identity,
    u8 usb2_ports,
    u8 usb3_ports,
    os_pi5_delay_us delay_us,
    void *delay_context,
    os_pi5_dwc3_core_state *state
)
{
    if (state == (os_pi5_dwc3_core_state *)0) {
        return 0;
    }
    os_pi5_dwc3_core_state fresh;
    os_pi5_dwc3_core_state_init(&fresh);
    if (state->host_configured
        || !resources_usable(resources)
        || identity == (const os_pi5_rp1_dwc3_identity *)0
        || !identity->ready
        || delay_us == (os_pi5_delay_us)0
        || usb2_ports == 0u
        || usb2_ports > OS_PI5_DWC3_MAX_PORTS_PER_TYPE
        || usb3_ports > OS_PI5_DWC3_MAX_PORTS_PER_TYPE) {
        return 0;
    }

    u32 gsnpsid = 0u;
    u32 hwparams0 = 0u;
    u32 hwparams1 = 0u;
    u32 hwparams6 = 0u;
    if (!rd(resources, OS_RP1_DWC3_GSNPSID, &gsnpsid)
        || (u16)(gsnpsid >> 16) != (u16)(identity->identifier >> 16)
        || !rd(resources, OS_PI5_DWC3_GHWPARAMS0, &hwparams0)
        || !rd(resources, OS_PI5_DWC3_GHWPARAMS1, &hwparams1)
        || !rd(resources, OS_PI5_DWC3_GHWPARAMS6, &hwparams6)) {
        return 0;
    }
    fresh.ip_id = identity->ip_id;
    fresh.revision = identity->revision;
    fresh.hw_mode = (u8)(hwparams0 & 3u);
    fresh.usb2_ports = usb2_ports;
    fresh.usb3_ports = usb3_ports;
    if (fresh.hw_mode != OS_PI5_DWC3_GHWPARAMS0_MODE_HOST
        && fresh.hw_mode != OS_PI5_DWC3_GHWPARAMS0_MODE_DRD) {
        return 0;
    }

    snapshot_set set;
    set.count = 0u;
    int ok = 1;

    /* PHY interfaces are internal; keep SUSPHY clear through PHY bring-up and
     * enable it afterwards for cores newer than DWC3 1.94a. */
    if (!(is_dwc3(&fresh) && fresh.revision <= OS_PI5_DWC3_REV_DWC3_194A)) {
        ok = susphy_set(resources, &set, &fresh, 1);
    }
    ok = ok && soft_reset(resources, &set, &fresh, delay_us, delay_context);
    ok = ok && global_control(resources, &set, &fresh, hwparams1, hwparams6);
    ok = ok && guctl_config(resources, &set, &fresh);

    /* dwc3_set_prtcap(HOST): DRD cores drop SUSPHY while the role changes. */
    if (ok) {
        u32 gctl = 0u;
        ok = rd(resources, OS_PI5_DWC3_GCTL, &gctl);
        if (ok && fresh.hw_mode == OS_PI5_DWC3_GHWPARAMS0_MODE_DRD
            && ((gctl & OS_PI5_DWC3_GCTL_PRTCAP_MASK)
                >> OS_PI5_DWC3_GCTL_PRTCAP_SHIFT)
                != OS_PI5_DWC3_GCTL_PRTCAP_HOST) {
            ok = susphy_set(resources, &set, &fresh, 0);
        }
        ok = ok && rmw(resources, &set, OS_PI5_DWC3_GCTL,
                       OS_PI5_DWC3_GCTL_PRTCAP_MASK,
                       OS_PI5_DWC3_GCTL_PRTCAP_HOST
                           << OS_PI5_DWC3_GCTL_PRTCAP_SHIFT);
        if (ok) {
            ok = rd(resources, OS_PI5_DWC3_GCTL, &gctl)
                && ((gctl & OS_PI5_DWC3_GCTL_PRTCAP_MASK)
                    >> OS_PI5_DWC3_GCTL_PRTCAP_SHIFT)
                    == OS_PI5_DWC3_GCTL_PRTCAP_HOST;
        }
    }
    if (!ok) {
        rollback(resources, &set);
        return 0;
    }
    fresh.host_configured = 1u;
    *state = fresh;
    return 1;
}

int os_pi5_dwc3_core_host_plat_start(
    os_pi5_rp1_dwc3_resources *resources,
    os_pi5_dwc3_core_state *state
)
{
    if (state == (os_pi5_dwc3_core_state *)0
        || !state->host_configured || state->susphy_enabled
        || !resources_usable(resources)) {
        return 0;
    }
    snapshot_set set;
    set.count = 0u;
    if (!susphy_set(resources, &set, state, 1)) {
        rollback(resources, &set);
        return 0;
    }
    state->susphy_enabled = 1u;
    return 1;
}

int os_pi5_dwc3_core_host_release(
    os_pi5_rp1_dwc3_resources *resources,
    os_pi5_dwc3_core_state *state
)
{
    if (state == (os_pi5_dwc3_core_state *)0
        || !state->host_configured
        || state->event_buffer_programmed
        || !resources_usable(resources)) {
        return 0;
    }
    os_pi5_dwc3_core_state_init(state);
    return 1;
}

int os_pi5_dwc3_core_event_buffer_setup(
    os_pi5_rp1_dwc3_resources *resources,
    os_pi5_dwc3_core_state *state,
    u64 dma_address,
    u32 length
)
{
    if (state == (os_pi5_dwc3_core_state *)0
        || !state->host_configured
        || state->event_buffer_programmed
        || !resources_usable(resources)) {
        return 0;
    }
    if (state->hw_mode == OS_PI5_DWC3_GHWPARAMS0_MODE_HOST) {
        return dma_address == 0u && length == 0u;
    }
    if (length != OS_PI5_DWC3_EVENT_BUFFER_SIZE || dma_address == 0u
        || (dma_address & (OS_PI5_DWC3_EVENT_BUFFER_ALIGNMENT - 1u)) != 0u
        || dma_address > ~(u64)0 - length) {
        return 0;
    }

    snapshot_set set;
    set.count = 0u;
    u32 count = 0u;
    int ok = save(resources, &set, OS_PI5_DWC3_GEVNTADRLO(0))
        && save(resources, &set, OS_PI5_DWC3_GEVNTADRHI(0))
        && save(resources, &set, OS_PI5_DWC3_GEVNTSIZ(0))
        && wr(resources, OS_PI5_DWC3_GEVNTADRLO(0), (u32)dma_address)
        && wr(resources, OS_PI5_DWC3_GEVNTADRHI(0),
              (u32)(dma_address >> 32))
        && wr(resources, OS_PI5_DWC3_GEVNTSIZ(0), length & 0xffffu)
        && rd(resources, OS_PI5_DWC3_GEVNTCOUNT(0), &count)
        && wr(resources, OS_PI5_DWC3_GEVNTCOUNT(0), count);
    if (ok) {
        u32 lo = 0u;
        u32 hi = 0u;
        u32 siz = 0u;
        ok = rd(resources, OS_PI5_DWC3_GEVNTADRLO(0), &lo)
            && rd(resources, OS_PI5_DWC3_GEVNTADRHI(0), &hi)
            && rd(resources, OS_PI5_DWC3_GEVNTSIZ(0), &siz)
            && lo == (u32)dma_address
            && hi == (u32)(dma_address >> 32)
            && (siz & 0xffffu) == length
            && (siz & OS_PI5_DWC3_GEVNTSIZ_INTMASK) == 0u;
    }
    if (!ok) {
        rollback(resources, &set);
        return 0;
    }
    state->event_buffer_programmed = 1u;
    state->event_buffer_dma = dma_address;
    state->event_buffer_length = length;
    return 1;
}

int os_pi5_dwc3_core_event_buffer_cleanup(
    os_pi5_rp1_dwc3_resources *resources,
    os_pi5_dwc3_core_state *state
)
{
    if (state == (os_pi5_dwc3_core_state *)0
        || !state->host_configured
        || !resources_usable(resources)) {
        return 0;
    }
    if (!state->event_buffer_programmed) {
        return 1;
    }
    u32 dsts = 0u;
    u32 count = 0u;
    if (!rd(resources, OS_PI5_DWC3_DSTS, &dsts)
        || (dsts & OS_PI5_DWC3_DSTS_DEVCTRLHLT) == 0u
        || !wr(resources, OS_PI5_DWC3_GEVNTADRLO(0), 0u)
        || !wr(resources, OS_PI5_DWC3_GEVNTADRHI(0), 0u)
        || !wr(resources, OS_PI5_DWC3_GEVNTSIZ(0),
               OS_PI5_DWC3_GEVNTSIZ_INTMASK)
        || !rd(resources, OS_PI5_DWC3_GEVNTCOUNT(0), &count)
        || !wr(resources, OS_PI5_DWC3_GEVNTCOUNT(0), count)) {
        return 0;
    }
    state->event_buffer_programmed = 0u;
    state->event_buffer_dma = 0u;
    state->event_buffer_length = 0u;
    return 1;
}
