#include "pi5_dwc3_core.h"

#include <stdio.h>
#include <string.h>

#define MODEL_WORDS (0xd000u / 4u)

typedef struct {
    u32 regs[MODEL_WORDS];
    u32 csftrst_polls_to_clear;
    u32 csftrst_remaining;
    int never_clear_reset;
    u32 fail_write_offset;
    int fail_write_armed;
    u32 writes;
    u32 delays;
    u32 delay_total_min_us;
} model_t;

static int fails;

static void check(int condition, const char *name)
{
    if (!condition) {
        printf("FAIL G12-DWC3-CORE %s\n", name);
        fails++;
    }
}

static int m_read(void *ctx, u32 region, u32 offset, u32 *value)
{
    model_t *m = ctx;
    if (region != 0u || offset >= 0xd000u || (offset & 3u)) {
        return 0;
    }
    if (offset == OS_PI5_DWC3_DCTL && m->csftrst_remaining
        && (m->regs[offset / 4u] & OS_PI5_DWC3_DCTL_CSFTRST)) {
        if (--m->csftrst_remaining == 0u && !m->never_clear_reset) {
            m->regs[offset / 4u] &= ~OS_PI5_DWC3_DCTL_CSFTRST;
        }
    }
    *value = m->regs[offset / 4u];
    return 1;
}

static int m_write(void *ctx, u32 region, u32 offset, u32 value)
{
    model_t *m = ctx;
    if (region != 0u || offset >= 0xd000u || (offset & 3u)) {
        return 0;
    }
    if (m->fail_write_armed && offset == m->fail_write_offset) {
        return 0;
    }
    m->writes++;
    m->regs[offset / 4u] = value;
    if (offset == OS_PI5_DWC3_DCTL && (value & OS_PI5_DWC3_DCTL_CSFTRST)) {
        m->csftrst_remaining = m->csftrst_polls_to_clear;
        if (m->csftrst_polls_to_clear == 0u && !m->never_clear_reset) {
            m->regs[offset / 4u] &= ~OS_PI5_DWC3_DCTL_CSFTRST;
        }
    }
    return 1;
}

static int m_delay(void *ctx, u32 min_us, u32 max_us)
{
    model_t *m = ctx;
    (void)max_us;
    m->delays++;
    m->delay_total_min_us += min_us;
    return 1;
}

static void model_init(
    model_t *m, u16 ip, u32 revision, u32 mode, u32 pwropt, u32 prtcap)
{
    memset(m, 0, sizeof(*m));
    m->regs[OS_RP1_DWC3_GSNPSID / 4u] = ((u32)ip << 16) | (revision & 0xffffu);
    m->regs[OS_PI5_DWC3_GHWPARAMS0 / 4u] = mode;
    m->regs[OS_PI5_DWC3_GHWPARAMS1 / 4u] = pwropt << 24;
    m->regs[OS_PI5_DWC3_GCTL / 4u] = (prtcap << 12) | (3u << 4) | (1u << 3);
    m->csftrst_polls_to_clear = 2u;
}

static void bind(
    model_t *m, os_pi5_rp1_dwc3_resources *r, os_pi5_rp1_dwc3_identity *id,
    u16 ip, u32 revision)
{
    memset(r, 0, sizeof(*r));
    r->mmio.context = m;
    r->mmio.region_sizes[0] = OS_RP1_DWC3_REGION_SIZE;
    r->mmio.read32 = m_read;
    r->mmio.write32 = m_write;
    r->region_size = OS_RP1_DWC3_REGION_SIZE;
    r->ready = 1u;
    memset(id, 0, sizeof(*id));
    id->identifier = ((u32)ip << 16) | (m->regs[OS_RP1_DWC3_GSNPSID / 4u] & 0xffffu);
    id->ip_id = ip;
    id->revision = revision;
    id->ready = 1u;
}

static u32 reg(model_t *m, u32 offset)
{
    return m->regs[offset / 4u];
}

static void test_dwc31_drd_success(void)
{
    model_t m;
    os_pi5_rp1_dwc3_resources r;
    os_pi5_rp1_dwc3_identity id;
    os_pi5_dwc3_core_state st;
    model_init(&m, 0x3331u, 0x0001u, 2u, 1u, 2u);
    bind(&m, &r, &id, 0x3331u, OS_PI5_DWC3_REV_DWC31_190A);
    os_pi5_dwc3_core_state_init(&st);
    check(os_pi5_dwc3_core_host_init(&r, &id, 1u, 1u, m_delay, &m, &st),
          "dwc31-drd-init");
    check(st.host_configured && !st.susphy_enabled, "dwc31-drd-state");
    check(((reg(&m, OS_PI5_DWC3_GCTL) >> 12) & 3u) == 1u, "dwc31-prtcap-host");
    check((reg(&m, OS_PI5_DWC3_GCTL) & OS_PI5_DWC3_GCTL_SCALEDOWN_MASK) == 0u,
          "scaledown-cleared");
    check((reg(&m, OS_PI5_DWC3_GCTL) & OS_PI5_DWC3_GCTL_DISSCRAMBLE) == 0u,
          "disscramble-cleared");
    check((reg(&m, OS_PI5_DWC3_DCTL) & OS_PI5_DWC3_DCTL_CSFTRST) == 0u,
          "reset-completed");
    check((reg(&m, OS_PI5_DWC3_DCTL) & OS_PI5_DWC3_DCTL_RUN_STOP) == 0u,
          "run-stop-clear");
    /* DWC31 >= 1.90a polls at 20 ms: two polls pending -> at least one wait. */
    check(m.delays >= 1u && m.delay_total_min_us >= 20000u, "sync-reset-delay");
    check(reg(&m, OS_PI5_DWC3_GUCTL1)
              == (OS_PI5_DWC3_GUCTL1_DEV_L1_EXIT_BY_HW
                  | OS_PI5_DWC3_GUCTL1_DEV_DECOUPLE_L1L2_EVT),
          "guctl1-dwc31");
    /* DRD role change drops SUSPHY until the xHCI platform start hook. */
    check((reg(&m, OS_PI5_DWC3_GUSB3PIPECTL(0)) & OS_PI5_DWC3_GUSB3PIPECTL_SUSPHY) == 0u
              && (reg(&m, OS_PI5_DWC3_GUSB2PHYCFG(0)) & OS_PI5_DWC3_GUSB2PHYCFG_SUSPHY) == 0u,
          "drd-susphy-cleared");
    check(os_pi5_dwc3_core_host_plat_start(&r, &st), "plat-start");
    check((reg(&m, OS_PI5_DWC3_GUSB3PIPECTL(0)) & OS_PI5_DWC3_GUSB3PIPECTL_SUSPHY) != 0u
              && (reg(&m, OS_PI5_DWC3_GUSB2PHYCFG(0)) & OS_PI5_DWC3_GUSB2PHYCFG_SUSPHY) != 0u,
          "plat-start-susphy-set");
    check(!os_pi5_dwc3_core_host_plat_start(&r, &st), "plat-start-once");
    check(!os_pi5_dwc3_core_host_init(&r, &id, 1u, 1u, m_delay, &m, &st),
          "double-init-rejected");
    check(os_pi5_dwc3_core_host_release(&r, &st) && !st.host_configured,
          "release");
    check(!os_pi5_dwc3_core_host_release(&r, &st), "release-twice-rejected");
}

static void test_host_only_keeps_susphy(void)
{
    model_t m;
    os_pi5_rp1_dwc3_resources r;
    os_pi5_rp1_dwc3_identity id;
    os_pi5_dwc3_core_state st;
    model_init(&m, 0x3332u, 0x0001u, 1u, 0u, 1u);
    bind(&m, &r, &id, 0x3332u, 0x3230302au);
    os_pi5_dwc3_core_state_init(&st);
    check(os_pi5_dwc3_core_host_init(&r, &id, 2u, 2u, m_delay, &m, &st),
          "host-only-init");
    check((reg(&m, OS_PI5_DWC3_GUSB3PIPECTL(1)) & OS_PI5_DWC3_GUSB3PIPECTL_SUSPHY) != 0u
              && (reg(&m, OS_PI5_DWC3_GUSB2PHYCFG(1)) & OS_PI5_DWC3_GUSB2PHYCFG_SUSPHY) != 0u,
          "host-only-susphy-kept");
}

static void test_legacy_dwc3(void)
{
    model_t m;
    os_pi5_rp1_dwc3_resources r;
    os_pi5_rp1_dwc3_identity id;
    os_pi5_dwc3_core_state st;
    /* DWC3 1.80a: no SUSPHY, U2RSTECN, no GUCTL1/GUCTL2, 1 us reset polling. */
    model_init(&m, 0x5533u, 0x180au, 1u, 0u, 1u);
    bind(&m, &r, &id, 0x5533u, 0x5533180au);
    os_pi5_dwc3_core_state_init(&st);
    check(os_pi5_dwc3_core_host_init(&r, &id, 1u, 1u, m_delay, &m, &st),
          "legacy-init");
    check((reg(&m, OS_PI5_DWC3_GCTL) & OS_PI5_DWC3_GCTL_U2RSTECN) != 0u,
          "legacy-u2rstecn");
    check(reg(&m, OS_PI5_DWC3_GUCTL1) == 0u && reg(&m, OS_PI5_DWC3_GUCTL2) == 0u,
          "legacy-no-guctl");
    check(reg(&m, OS_PI5_DWC3_GUSB2PHYCFG(0)) == 0u
              && reg(&m, OS_PI5_DWC3_GUSB3PIPECTL(0)) == 0u,
          "legacy-no-susphy");
    check(m.delay_total_min_us < 20000u, "legacy-short-delay");

    /* DWC3 3.10a sets GUCTL2.RST_ACTBITLATER; 3.20a clears LC_TIMER. */
    model_init(&m, 0x5533u, 0x310au, 1u, 0u, 1u);
    bind(&m, &r, &id, 0x5533u, 0x5533310au);
    os_pi5_dwc3_core_state_init(&st);
    check(os_pi5_dwc3_core_host_init(&r, &id, 1u, 1u, m_delay, &m, &st),
          "dwc3-310a-init");
    check((reg(&m, OS_PI5_DWC3_GUCTL2) & OS_PI5_DWC3_GUCTL2_RST_ACTBITLATER) != 0u,
          "dwc3-310a-actbitlater");
    model_init(&m, 0x5533u, 0x320au, 1u, 0u, 1u);
    m.regs[OS_PI5_DWC3_GUCTL2 / 4u] = OS_PI5_DWC3_GUCTL2_LC_TIMER;
    bind(&m, &r, &id, 0x5533u, 0x5533320au);
    os_pi5_dwc3_core_state_init(&st);
    check(os_pi5_dwc3_core_host_init(&r, &id, 1u, 1u, m_delay, &m, &st),
          "dwc3-320a-init");
    check((reg(&m, OS_PI5_DWC3_GUCTL2) & OS_PI5_DWC3_GUCTL2_LC_TIMER) == 0u
              && (reg(&m, OS_PI5_DWC3_GUCTL2) & OS_PI5_DWC3_GUCTL2_RST_ACTBITLATER) != 0u,
          "dwc3-320a-lc-timer");
}

static void test_power_options(void)
{
    model_t m;
    os_pi5_rp1_dwc3_resources r;
    os_pi5_rp1_dwc3_identity id;
    os_pi5_dwc3_core_state st;
    model_init(&m, 0x3331u, 0x0001u, 1u, 1u, 1u);
    m.regs[OS_PI5_DWC3_GCTL / 4u] |= OS_PI5_DWC3_GCTL_DSBLCLKGTNG;
    bind(&m, &r, &id, 0x3331u, OS_PI5_DWC3_REV_DWC31_190A);
    os_pi5_dwc3_core_state_init(&st);
    check(os_pi5_dwc3_core_host_init(&r, &id, 1u, 1u, m_delay, &m, &st),
          "clkgating-init");
    check((reg(&m, OS_PI5_DWC3_GCTL) & OS_PI5_DWC3_GCTL_DSBLCLKGTNG) == 0u,
          "clkgating-enabled-by-default");

    model_init(&m, 0x3331u, 0x0001u, 1u, 1u, 1u);
    bind(&m, &r, &id, 0x3331u, OS_PI5_DWC3_REV_DWC31_200A);
    os_pi5_dwc3_core_state_init(&st);
    check(os_pi5_dwc3_core_host_init(&r, &id, 1u, 1u, m_delay, &m, &st),
          "dwc31-200a-init");
    check((reg(&m, OS_PI5_DWC3_GCTL) & OS_PI5_DWC3_GCTL_DSBLCLKGTNG) != 0u,
          "dwc31-200a-clkgating-disabled");

    model_init(&m, 0x3331u, 0x0001u, 1u, 2u, 1u);
    bind(&m, &r, &id, 0x3331u, OS_PI5_DWC3_REV_DWC31_190A);
    os_pi5_dwc3_core_state_init(&st);
    check(os_pi5_dwc3_core_host_init(&r, &id, 1u, 1u, m_delay, &m, &st),
          "hibernation-init");
    check((reg(&m, OS_PI5_DWC3_GCTL) & OS_PI5_DWC3_GCTL_GBLHIBERNATIONEN) != 0u,
          "hibernation-enabled");

    model_init(&m, 0x5533u, 0x230au, 1u, 1u, 1u);
    bind(&m, &r, &id, 0x5533u, 0x5533230au);
    os_pi5_dwc3_core_state_init(&st);
    check(os_pi5_dwc3_core_host_init(&r, &id, 1u, 1u, m_delay, &m, &st),
          "dwc3-230a-init");
    check((reg(&m, OS_PI5_DWC3_GCTL)
           & (OS_PI5_DWC3_GCTL_DSBLCLKGTNG | OS_PI5_DWC3_GCTL_SOFITPSYNC))
              == (OS_PI5_DWC3_GCTL_DSBLCLKGTNG | OS_PI5_DWC3_GCTL_SOFITPSYNC),
          "dwc3-230a-compliance-workaround");
}

static void snapshot_compare(const model_t *a, const model_t *b, const char *name)
{
    int same = 1;
    for (u32 i = 0u; i < MODEL_WORDS; i++) {
        if (i == OS_PI5_DWC3_DCTL / 4u) {
            continue; /* soft reset is not reversible */
        }
        if (a->regs[i] != b->regs[i]) {
            same = 0;
        }
    }
    check(same, name);
}

static void test_failures_roll_back(void)
{
    model_t m;
    model_t before;
    os_pi5_rp1_dwc3_resources r;
    os_pi5_rp1_dwc3_identity id;
    os_pi5_dwc3_core_state st;

    /* Reset never completes. */
    model_init(&m, 0x3331u, 0x0001u, 2u, 1u, 2u);
    m.never_clear_reset = 1;
    m.csftrst_polls_to_clear = 1u;
    bind(&m, &r, &id, 0x3331u, OS_PI5_DWC3_REV_DWC31_190A);
    before = m;
    os_pi5_dwc3_core_state_init(&st);
    check(!os_pi5_dwc3_core_host_init(&r, &id, 1u, 1u, m_delay, &m, &st),
          "reset-timeout-fails");
    check(!st.host_configured, "reset-timeout-state");
    m.regs[OS_PI5_DWC3_DCTL / 4u] = before.regs[OS_PI5_DWC3_DCTL / 4u];
    snapshot_compare(&before, &m, "reset-timeout-rollback");

    /* GCTL write fails after SUSPHY and reset were already touched. */
    model_init(&m, 0x3331u, 0x0001u, 2u, 1u, 2u);
    bind(&m, &r, &id, 0x3331u, OS_PI5_DWC3_REV_DWC31_190A);
    before = m;
    m.fail_write_offset = OS_PI5_DWC3_GCTL;
    m.fail_write_armed = 1;
    os_pi5_dwc3_core_state_init(&st);
    check(!os_pi5_dwc3_core_host_init(&r, &id, 1u, 1u, m_delay, &m, &st),
          "gctl-write-fails");
    m.fail_write_armed = 0;
    snapshot_compare(&before, &m, "gctl-write-rollback");

    /* Late GUSB2PHYCFG failure while dropping SUSPHY for the role change. */
    model_init(&m, 0x3331u, 0x0001u, 2u, 0u, 2u);
    bind(&m, &r, &id, 0x3331u, OS_PI5_DWC3_REV_DWC31_190A);
    m.regs[OS_PI5_DWC3_GUSB2PHYCFG(0) / 4u] = 0x1234u;
    before = m;
    m.fail_write_offset = OS_PI5_DWC3_GUSB2PHYCFG(0);
    m.fail_write_armed = 1;
    os_pi5_dwc3_core_state_init(&st);
    check(!os_pi5_dwc3_core_host_init(&r, &id, 1u, 1u, m_delay, &m, &st),
          "phycfg-write-fails");
    m.fail_write_armed = 0;
    snapshot_compare(&before, &m, "phycfg-write-rollback");

    /* PRTCAP does not take (read-back mismatch) -> rolled back, not reported. */
    model_init(&m, 0x3331u, 0x0001u, 1u, 0u, 2u);
    bind(&m, &r, &id, 0x3331u, OS_PI5_DWC3_REV_DWC31_190A);
    r.mmio.write32 = m_write; /* model accepts writes... */
    m.regs[OS_PI5_DWC3_GHWPARAMS0 / 4u] = 1u;
    os_pi5_dwc3_core_state_init(&st);
    /* simulate hardware refusing the capability by failing the write */
    m.fail_write_offset = OS_PI5_DWC3_GCTL;
    m.fail_write_armed = 1;
    check(!os_pi5_dwc3_core_host_init(&r, &id, 1u, 1u, m_delay, &m, &st),
          "prtcap-refused");
}

static void test_rejections(void)
{
    model_t m;
    os_pi5_rp1_dwc3_resources r;
    os_pi5_rp1_dwc3_identity id;
    os_pi5_dwc3_core_state st;

    model_init(&m, 0x3331u, 0x0001u, 0u, 0u, 2u);
    bind(&m, &r, &id, 0x3331u, OS_PI5_DWC3_REV_DWC31_190A);
    os_pi5_dwc3_core_state_init(&st);
    check(!os_pi5_dwc3_core_host_init(&r, &id, 1u, 1u, m_delay, &m, &st)
              && m.writes == 0u,
          "gadget-only-rejected-without-writes");

    model_init(&m, 0x3331u, 0x0001u, 2u, 0u, 2u);
    bind(&m, &r, &id, 0x3331u, OS_PI5_DWC3_REV_DWC31_190A);
    check(!os_pi5_dwc3_core_host_init(&r, &id, 0u, 1u, m_delay, &m, &st)
              && !os_pi5_dwc3_core_host_init(&r, &id, 17u, 1u, m_delay, &m, &st)
              && !os_pi5_dwc3_core_host_init(&r, &id, 1u, 17u, m_delay, &m, &st)
              && !os_pi5_dwc3_core_host_init(&r, &id, 1u, 1u, 0, &m, &st)
              && !os_pi5_dwc3_core_host_init(&r, &id, 1u, 1u, m_delay, &m, 0)
              && !os_pi5_dwc3_core_host_init(0, &id, 1u, 1u, m_delay, &m, &st)
              && !os_pi5_dwc3_core_host_init(&r, 0, 1u, 1u, m_delay, &m, &st)
              && m.writes == 0u,
          "invalid-arguments-rejected-without-writes");

    id.identifier = 0x5533u << 16;
    check(!os_pi5_dwc3_core_host_init(&r, &id, 1u, 1u, m_delay, &m, &st)
              && m.writes == 0u,
          "identity-mismatch-rejected");
    id.identifier = 0x3331u << 16;
    id.ready = 0u;
    check(!os_pi5_dwc3_core_host_init(&r, &id, 1u, 1u, m_delay, &m, &st),
          "unready-identity-rejected");
    id.ready = 1u;
    r.ready = 0u;
    check(!os_pi5_dwc3_core_host_init(&r, &id, 1u, 1u, m_delay, &m, &st)
              && !os_pi5_dwc3_core_host_plat_start(&r, &st),
          "unready-resources-rejected");
    check(!os_pi5_dwc3_core_host_plat_start(&r, 0)
              && !os_pi5_dwc3_core_host_release(&r, 0),
          "null-state-rejected");
}

static void test_event_buffer(void)
{
    model_t m;
    os_pi5_rp1_dwc3_resources r;
    os_pi5_rp1_dwc3_identity id;
    os_pi5_dwc3_core_state st;
    const u64 dma = 0x1f10001000ull;

    /* Host-only core: no DWC3 event buffer exists. */
    model_init(&m, 0x3331u, 0x0001u, 1u, 0u, 1u);
    bind(&m, &r, &id, 0x3331u, OS_PI5_DWC3_REV_DWC31_190A);
    os_pi5_dwc3_core_state_init(&st);
    check(os_pi5_dwc3_core_host_init(&r, &id, 1u, 1u, m_delay, &m, &st),
          "eb-host-init");
    m.writes = 0u;
    check(!os_pi5_dwc3_core_event_buffer_setup(&r, &st, dma, 4096u)
              && !st.event_buffer_programmed && m.writes == 0u,
          "eb-host-only-rejects-buffer");
    check(os_pi5_dwc3_core_event_buffer_setup(&r, &st, 0u, 0u)
              && !st.event_buffer_programmed && m.writes == 0u,
          "eb-host-only-noop");
    check(os_pi5_dwc3_core_event_buffer_cleanup(&r, &st) && m.writes == 0u,
          "eb-host-only-cleanup-noop");

    /* DRD core. */
    model_init(&m, 0x3331u, 0x0001u, 2u, 1u, 2u);
    bind(&m, &r, &id, 0x3331u, OS_PI5_DWC3_REV_DWC31_190A);
    m.regs[OS_PI5_DWC3_DSTS / 4u] = OS_PI5_DWC3_DSTS_DEVCTRLHLT;
    os_pi5_dwc3_core_state_init(&st);
    check(os_pi5_dwc3_core_host_init(&r, &id, 1u, 1u, m_delay, &m, &st),
          "eb-drd-init");
    check(!os_pi5_dwc3_core_event_buffer_setup(&r, &st, dma, 2048u)
              && !os_pi5_dwc3_core_event_buffer_setup(&r, &st, dma + 8u, 4096u)
              && !os_pi5_dwc3_core_event_buffer_setup(&r, &st, 0u, 4096u)
              && !st.event_buffer_programmed,
          "eb-drd-bad-args");
    check(reg(&m, OS_PI5_DWC3_GEVNTADRLO(0)) == 0u
              && reg(&m, OS_PI5_DWC3_GEVNTSIZ(0)) == 0u,
          "eb-bad-args-no-writes");

    /* Failure at each of the event-buffer registers rolls back. */
    {
        const u32 offsets[] = {
            OS_PI5_DWC3_GEVNTADRLO(0), OS_PI5_DWC3_GEVNTADRHI(0),
            OS_PI5_DWC3_GEVNTSIZ(0), OS_PI5_DWC3_GEVNTCOUNT(0),
        };
        for (u32 i = 0u; i < 4u; ++i) {
            model_t before = m;
            m.fail_write_offset = offsets[i];
            m.fail_write_armed = 1;
            check(!os_pi5_dwc3_core_event_buffer_setup(&r, &st, dma, 4096u)
                      && !st.event_buffer_programmed,
                  "eb-write-failure-rejected");
            m.fail_write_armed = 0;
            check(reg(&m, OS_PI5_DWC3_GEVNTADRLO(0))
                          == reg(&before, OS_PI5_DWC3_GEVNTADRLO(0))
                      && reg(&m, OS_PI5_DWC3_GEVNTADRHI(0))
                          == reg(&before, OS_PI5_DWC3_GEVNTADRHI(0))
                      && reg(&m, OS_PI5_DWC3_GEVNTSIZ(0))
                          == reg(&before, OS_PI5_DWC3_GEVNTSIZ(0)),
                  "eb-write-failure-restored");
        }
    }

    check(os_pi5_dwc3_core_event_buffer_setup(&r, &st, dma, 4096u)
              && st.event_buffer_programmed,
          "eb-drd-setup");
    check(reg(&m, OS_PI5_DWC3_GEVNTADRLO(0)) == (u32)dma
              && reg(&m, OS_PI5_DWC3_GEVNTADRHI(0)) == (u32)(dma >> 32)
              && (reg(&m, OS_PI5_DWC3_GEVNTSIZ(0)) & 0xffffu) == 4096u
              && (reg(&m, OS_PI5_DWC3_GEVNTSIZ(0)) & OS_PI5_DWC3_GEVNTSIZ_INTMASK)
                  == 0u,
          "eb-registers-programmed");
    check(!os_pi5_dwc3_core_event_buffer_setup(&r, &st, dma, 4096u),
          "eb-double-setup-rejected");
    check(!os_pi5_dwc3_core_host_release(&r, &st),
          "eb-release-blocked-while-programmed");

    /* Cleanup is refused while the device controller is not halted. */
    m.regs[OS_PI5_DWC3_DSTS / 4u] = 0u;
    check(!os_pi5_dwc3_core_event_buffer_cleanup(&r, &st)
              && st.event_buffer_programmed
              && reg(&m, OS_PI5_DWC3_GEVNTADRLO(0)) == (u32)dma,
          "eb-cleanup-requires-halt");
    m.regs[OS_PI5_DWC3_DSTS / 4u] = OS_PI5_DWC3_DSTS_DEVCTRLHLT;
    check(os_pi5_dwc3_core_event_buffer_cleanup(&r, &st)
              && !st.event_buffer_programmed
              && reg(&m, OS_PI5_DWC3_GEVNTADRLO(0)) == 0u
              && reg(&m, OS_PI5_DWC3_GEVNTADRHI(0)) == 0u
              && reg(&m, OS_PI5_DWC3_GEVNTSIZ(0))
                  == OS_PI5_DWC3_GEVNTSIZ_INTMASK
              && reg(&m, OS_PI5_DWC3_GEVNTCOUNT(0)) == 0u,
          "eb-cleanup");
    check(os_pi5_dwc3_core_event_buffer_cleanup(&r, &st),
          "eb-cleanup-idempotent");
    check(os_pi5_dwc3_core_host_release(&r, &st), "eb-release-after-cleanup");
}

int main(void)
{
    test_dwc31_drd_success();
    test_host_only_keeps_susphy();
    test_legacy_dwc3();
    test_power_options();
    test_failures_roll_back();
    test_rejections();
    test_event_buffer();
    if (fails != 0) {
        return 1;
    }
    puts("PASS G12-DWC3-CORE-HOST-INIT: DWC3 global host-mode init, SUSPHY lifecycle, version quirks, rollback and rejections");
    return 0;
}
