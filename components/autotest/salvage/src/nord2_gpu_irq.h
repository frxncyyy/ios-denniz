/* GICv3 SPI plumbing and job-IRQ policy for the Nord 2 GPU, shared with host tests.
 *
 * Nothing here touches hardware by itself: every register access goes through
 * struct nord2_gic_ops, so the sequencing (never enable before route/priority/level
 * are programmed and verified, level-triggered, Group 1) is tested against a model.
 * SPDX-License-Identifier: MIT */
#ifndef NORD2_GPU_IRQ_H
#define NORD2_GPU_IRQ_H
#include <stdbool.h>
#include <stdint.h>

/* GICD registers (GICv3, affinity routing). */
#define NORD2_GICD_CTLR      0x0000u
#define NORD2_GICD_TYPER     0x0004u
#define NORD2_GICD_IGROUPR   0x0080u
#define NORD2_GICD_ISENABLER 0x0100u
#define NORD2_GICD_ICENABLER 0x0180u
#define NORD2_GICD_ISPENDR   0x0200u
#define NORD2_GICD_ICPENDR   0x0280u
#define NORD2_GICD_IPRIORITYR 0x0400u
#define NORD2_GICD_ICFGR     0x0c00u
#define NORD2_GICD_IROUTER   0x6000u
#define NORD2_GICD_CTLR_ARE_NS (1u << 4)

/* The ADT lists SPI numbers (362..366 for the Mali); the GIC INTID is SPI + 32. */
#define NORD2_GPU_JOB_SPI_DEFAULT 364u
#define NORD2_GPU_IRQ_PRIORITY    0xa0u   /* same as the kernel's Group 1 PPIs */
#define NORD2_GPU_IRQ_SPURIOUS_MAX 64u    /* consecutive empty interrupts before the SPI is shut off */
#define NORD2_GPU_JOB_IRQ_ALL     0xffffffffu

struct nord2_gic_ops {
        uint32_t (*rd32)(void *ctx, uint32_t off);
        void (*wr32)(void *ctx, uint32_t off, uint32_t v);
        uint64_t (*rd64)(void *ctx, uint32_t off);
        void (*wr64)(void *ctx, uint32_t off, uint64_t v);
        void *ctx;
};

static inline uint32_t nord2_gic_intid_for_spi(uint32_t spi) { return spi + 32u; }

/* MPIDR_EL1 -> GICD_IROUTER value: both use Aff3 in bits 39:32 and Aff2:Aff1:Aff0 in 23:0;
 * MPIDR's RES1/U/MT bits (31:24) must not reach IROUTER (bit 31 would select "any PE"). */
static inline uint64_t nord2_gic_irouter_for_mpidr(uint64_t mpidr)
{
        return mpidr & 0xff00ffffffull;
}

/* SPI INTIDs start at 32; GICD_TYPER.ITLinesNumber bounds the highest one. */
static inline bool nord2_gic_spi_valid(const struct nord2_gic_ops *g, uint32_t intid)
{
        if (!g || intid < 32u || intid > 1019u)
                return false;
        uint32_t lines = (g->rd32(g->ctx, NORD2_GICD_TYPER) & 0x1fu) + 1u;
        return intid < lines * 32u;
}

static inline void nord2_gic_bit(const struct nord2_gic_ops *g, uint32_t base, uint32_t intid)
{
        g->wr32(g->ctx, base + 4u * (intid / 32u), 1u << (intid % 32u));   /* write-1-to-set/clear */
}

static inline bool nord2_gic_spi_pending(const struct nord2_gic_ops *g, uint32_t intid)
{
        return (g->rd32(g->ctx, NORD2_GICD_ISPENDR + 4u * (intid / 32u)) >> (intid % 32u)) & 1u;
}

static inline void nord2_gic_spi_enable(const struct nord2_gic_ops *g, uint32_t intid, bool on)
{
        nord2_gic_bit(g, on ? NORD2_GICD_ISENABLER : NORD2_GICD_ICENABLER, intid);
}

/* Program an SPI as level-triggered Group 1, routed to one PE, still DISABLED.
 * Returns false (SPI left disabled) if the INTID is out of range, affinity routing
 * is off, or the priority/route readback disagrees.  Group writes are not verified:
 * from the non-secure side they may read back as zero even when delivery works. */
static inline bool nord2_gic_spi_configure(const struct nord2_gic_ops *g, uint32_t intid,
                                           uint64_t irouter, uint8_t priority)
{
        if (!nord2_gic_spi_valid(g, intid) ||
            !(g->rd32(g->ctx, NORD2_GICD_CTLR) & NORD2_GICD_CTLR_ARE_NS))
                return false;
        nord2_gic_bit(g, NORD2_GICD_ICENABLER, intid);
        uint32_t off = NORD2_GICD_IGROUPR + 4u * (intid / 32u);
        g->wr32(g->ctx, off, g->rd32(g->ctx, off) | (1u << (intid % 32u)));
        uint32_t poff = NORD2_GICD_IPRIORITYR + (intid & ~3u), sh = 8u * (intid & 3u);
        g->wr32(g->ctx, poff, (g->rd32(g->ctx, poff) & ~(0xffu << sh)) | ((uint32_t)priority << sh));
        uint32_t coff = NORD2_GICD_ICFGR + 4u * (intid / 16u), cbit = 2u * (intid % 16u) + 1u;
        g->wr32(g->ctx, coff, g->rd32(g->ctx, coff) & ~(1u << cbit));          /* level */
        g->wr64(g->ctx, NORD2_GICD_IROUTER + 8u * intid, irouter);
        nord2_gic_bit(g, NORD2_GICD_ICPENDR, intid);
        return ((g->rd32(g->ctx, poff) >> sh) & 0xffu) == priority &&
               g->rd64(g->ctx, NORD2_GICD_IROUTER + 8u * intid) == irouter &&
               !(g->rd32(g->ctx, coff) & (1u << cbit));
}

/* Interrupt top half decision: a job IRQ with no status bits is spurious. */
struct nord2_irq_guard {
        uint32_t total, spurious_run, spurious_total;
        bool tripped;
};

/* Returns true while the SPI should stay enabled; false once NORD2_GPU_IRQ_SPURIOUS_MAX
 * consecutive empty interrupts show the line is not (only) the Mali job interrupt. */
static inline bool nord2_irq_guard_note(struct nord2_irq_guard *s, uint32_t job_status)
{
        s->total++;
        if (job_status) {
                s->spurious_run = 0;
                return !s->tripped;
        }
        s->spurious_total++;
        if (++s->spurious_run >= NORD2_GPU_IRQ_SPURIOUS_MAX)
                s->tripped = true;
        return !s->tripped;
}

/* Bring-up state of the optional interrupt path.  Polling is always the fallback. */
enum nord2_irq_state {
        NORD2_IRQ_OFF = 0,     /* boot-arg absent or polling forced */
        NORD2_IRQ_LEARN,       /* SPI configured but disabled; JOB mask armed; waiting to see the SPI pending */
        NORD2_IRQ_ACTIVE,      /* SPI enabled; waiters sleep until the top half wakes them */
        NORD2_IRQ_FAILED,      /* mapping unconfirmed or guard tripped; back to polling */
};
#define NORD2_GPU_IRQ_LEARN_JOBS 8u   /* completed jobs without a pending SPI before giving up */

/* Sleep slice while waiting for the interrupt: still wakes to poll, so a lost or
 * mis-mapped interrupt costs latency (one slice), never correctness. */
static inline uint32_t nord2_gpu_irq_slice_us(uint64_t elapsed_us)
{
        return elapsed_us < 2000u ? 250u : 1000u;
}
#endif
