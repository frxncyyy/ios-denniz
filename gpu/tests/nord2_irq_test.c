/* GICv3 SPI programming and job-IRQ policy against a GICD register model (no hardware). */
#include "nord2_gpu_irq.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct gicd {
        uint32_t ctlr, typer, igroupr[32], enabled[32], pending[32], icfgr[64];
        uint8_t prio[1024]; uint64_t irouter[1024];
        bool stuck_prio, stuck_route; unsigned writes; bool enabled_before_ready; uint32_t cfg_done_for;
};
static uint32_t rd32(void *c, uint32_t o) {
        struct gicd *g = c;
        if (o == NORD2_GICD_CTLR) return g->ctlr;
        if (o == NORD2_GICD_TYPER) return g->typer;
        if (o >= NORD2_GICD_IGROUPR && o < NORD2_GICD_IGROUPR + 128) return g->igroupr[(o - NORD2_GICD_IGROUPR) / 4];
        if (o >= NORD2_GICD_ISENABLER && o < NORD2_GICD_ISENABLER + 128) return g->enabled[(o - NORD2_GICD_ISENABLER) / 4];
        if (o >= NORD2_GICD_ISPENDR && o < NORD2_GICD_ISPENDR + 128) return g->pending[(o - NORD2_GICD_ISPENDR) / 4];
        if (o >= NORD2_GICD_IPRIORITYR && o < NORD2_GICD_IPRIORITYR + 1024) { uint32_t i = o - NORD2_GICD_IPRIORITYR; return g->prio[i] | g->prio[i+1] << 8 | g->prio[i+2] << 16 | (uint32_t)g->prio[i+3] << 24; }
        if (o >= NORD2_GICD_ICFGR && o < NORD2_GICD_ICFGR + 256) return g->icfgr[(o - NORD2_GICD_ICFGR) / 4];
        assert(!"unexpected read"); return 0;
}
static void wr32(void *c, uint32_t o, uint32_t v) {
        struct gicd *g = c; g->writes++;
        if (o >= NORD2_GICD_IGROUPR && o < NORD2_GICD_IGROUPR + 128) { g->igroupr[(o - NORD2_GICD_IGROUPR) / 4] = v; return; }
        if (o >= NORD2_GICD_ISENABLER && o < NORD2_GICD_ISENABLER + 128) { g->enabled[(o - NORD2_GICD_ISENABLER) / 4] |= v; return; }
        if (o >= NORD2_GICD_ICENABLER && o < NORD2_GICD_ICENABLER + 128) { g->enabled[(o - NORD2_GICD_ICENABLER) / 4] &= ~v; return; }
        if (o >= NORD2_GICD_ICPENDR && o < NORD2_GICD_ICPENDR + 128) { g->pending[(o - NORD2_GICD_ICPENDR) / 4] &= ~v; return; }
        if (o >= NORD2_GICD_IPRIORITYR && o < NORD2_GICD_IPRIORITYR + 1024) {
                uint32_t i = o - NORD2_GICD_IPRIORITYR;
                if (!g->stuck_prio) for (unsigned k = 0; k < 4; k++) g->prio[i + k] = v >> (8 * k);
                return;
        }
        if (o >= NORD2_GICD_ICFGR && o < NORD2_GICD_ICFGR + 256) { g->icfgr[(o - NORD2_GICD_ICFGR) / 4] = v; return; }
        assert(!"unexpected write");
}
static uint64_t rd64(void *c, uint32_t o) { struct gicd *g = c; assert(o >= NORD2_GICD_IROUTER && o < NORD2_GICD_IROUTER + 8 * 1024); return g->irouter[(o - NORD2_GICD_IROUTER) / 8]; }
static void wr64(void *c, uint32_t o, uint64_t v) {
        struct gicd *g = c; g->writes++; assert(o >= NORD2_GICD_IROUTER && o < NORD2_GICD_IROUTER + 8 * 1024);
        if (!g->stuck_route) g->irouter[(o - NORD2_GICD_IROUTER) / 8] = v;
}
static void model(struct gicd *g) {
        memset(g, 0, sizeof(*g)); g->ctlr = NORD2_GICD_CTLR_ARE_NS | 3; g->typer = 15; /* 16 x 32 = 512 INTIDs */
        memset(g->prio, 0x11, sizeof(g->prio));
}

int main(void) {
        struct gicd g; struct nord2_gic_ops ops = { rd32, wr32, rd64, wr64, &g };
        const uint32_t spi = NORD2_GPU_JOB_SPI_DEFAULT, intid = nord2_gic_intid_for_spi(spi);
        assert(spi == 364 && intid == 396);
        /* router value drops the MPIDR RES1/U/MT bits and keeps Aff3 */
        assert(nord2_gic_irouter_for_mpidr(0x80000000ull) == 0);
        assert(nord2_gic_irouter_for_mpidr(0x81000300ull | (0x2ull << 32)) == (0x300ull | (0x2ull << 32)));
        /* range checks */
        model(&g);
        assert(!nord2_gic_spi_valid(&ops, 31) && nord2_gic_spi_valid(&ops, 32) && nord2_gic_spi_valid(&ops, 511) && !nord2_gic_spi_valid(&ops, 512));
        assert(!nord2_gic_spi_valid(&ops, 1020) && !nord2_gic_spi_valid(NULL, 40));
        g.typer = 12; assert(intid < 13 * 32 ? nord2_gic_spi_valid(&ops, intid) : !nord2_gic_spi_valid(&ops, intid)); /* 416 lines: 396 ok */
        g.typer = 11; assert(!nord2_gic_spi_valid(&ops, intid));                                                        /* 384: refused */
        /* affinity routing off: nothing written */
        model(&g); g.ctlr = 0; unsigned w = g.writes;
        assert(!nord2_gic_spi_configure(&ops, intid, 0x100, 0xa0) && g.writes == w);
        /* configure: neighbours untouched, level, group1, routed, still disabled, pending cleared */
        model(&g); g.icfgr[intid / 16] = 0xffffffffu; g.igroupr[intid / 32] = 0x1; g.pending[intid / 32] = 1u << (intid % 32);
        assert(nord2_gic_spi_configure(&ops, intid, 0x100, NORD2_GPU_IRQ_PRIORITY));
        assert(g.prio[intid] == 0xa0 && g.prio[intid - 1] == 0x11 && g.prio[intid + 1] == 0x11);
        assert(!(g.icfgr[intid / 16] & (1u << (2 * (intid % 16) + 1))) && (g.icfgr[intid / 16] | (1u << (2 * (intid % 16) + 1))) == 0xffffffffu);
        assert((g.igroupr[intid / 32] & 1) && (g.igroupr[intid / 32] & (1u << (intid % 32))));
        assert(g.irouter[intid] == 0x100 && !(g.enabled[intid / 32] & (1u << (intid % 32))) && !(g.pending[intid / 32] & (1u << (intid % 32))));
        assert(!nord2_gic_spi_pending(&ops, intid));
        g.pending[intid / 32] |= 1u << (intid % 32); assert(nord2_gic_spi_pending(&ops, intid));
        nord2_gic_spi_enable(&ops, intid, true);  assert(g.enabled[intid / 32] & (1u << (intid % 32)));
        nord2_gic_spi_enable(&ops, intid, false); assert(!(g.enabled[intid / 32] & (1u << (intid % 32))));
        /* an SPI that was enabled is disabled again before reprogramming */
        model(&g); g.enabled[intid / 32] = 1u << (intid % 32);
        assert(nord2_gic_spi_configure(&ops, intid, 0x200, 0x80) && !(g.enabled[intid / 32] & (1u << (intid % 32))));
        /* readback disagreement: refused, never left enabled */
        model(&g); g.stuck_prio = true; assert(!nord2_gic_spi_configure(&ops, intid, 0x100, 0xa0) && !g.enabled[intid / 32]);
        model(&g); g.stuck_route = true; assert(!nord2_gic_spi_configure(&ops, intid, 0x100, 0xa0) && !g.enabled[intid / 32]);
        /* spurious guard */
        struct nord2_irq_guard s = {0};
        for (unsigned i = 0; i < NORD2_GPU_IRQ_SPURIOUS_MAX - 1; i++) assert(nord2_irq_guard_note(&s, 0));
        assert(nord2_irq_guard_note(&s, 4) && s.spurious_run == 0 && !s.tripped);   /* a real interrupt resets the run */
        for (unsigned i = 0; i < NORD2_GPU_IRQ_SPURIOUS_MAX - 1; i++) assert(nord2_irq_guard_note(&s, 0));
        assert(!nord2_irq_guard_note(&s, 0) && s.tripped);
        assert(!nord2_irq_guard_note(&s, 4));                                        /* stays tripped */
        assert(s.total == 2 * (NORD2_GPU_IRQ_SPURIOUS_MAX - 1) + 3 && s.spurious_total == 2 * NORD2_GPU_IRQ_SPURIOUS_MAX - 1);
        assert(nord2_gpu_irq_slice_us(0) == 250 && nord2_gpu_irq_slice_us(1999) == 250 && nord2_gpu_irq_slice_us(2000) == 1000);
        assert(NORD2_IRQ_OFF == 0 && NORD2_GPU_IRQ_LEARN_JOBS == 8u);
        puts("nord2_irq_test: GICD programming order/readback, SPI range, guard and slices pass (register model)");
        return 0;
}
