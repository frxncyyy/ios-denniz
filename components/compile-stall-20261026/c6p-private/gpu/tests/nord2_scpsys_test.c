#include "nord2_gpu_scpsys.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct fake {
        uint32_t spm[0x400 / 4];
        uint32_t infra[0x1000 / 4];
        unsigned delays;
        bool ack;
        bool fail_status;
        unsigned alias_reads;
        unsigned invalid_writes;
};

static uint32_t rs(void *c, uint32_t o) { return ((struct fake *)c)->spm[o / 4]; }
static void ws(void *c, uint32_t o, uint32_t v) { ((struct fake *)c)->spm[o / 4] = v; }
/* SET/CLR are write-one aliases, not readable state registers. Each
 * bank has a distinct status address in clk-mt6885-pg.c. */
static const uint32_t aliases[][3] = {
        {0x2a0, 0x2a4, 0x228}, {0x2a8, 0x2ac, 0x258},
        {0x714, 0x718, 0x724}, {0xbb8, 0xbbc, 0xbc4},
};
static uint32_t ri(void *c, uint32_t o) {
        struct fake *f = c;
        for (unsigned i = 0; i < 4; i++)
                if (o == aliases[i][0] || o == aliases[i][1]) {
                        f->alias_reads++;
                        return 0x80000000u; /* unrelated protection bit */
                }
        return f->infra[o / 4];
}
static void wi(void *c, uint32_t o, uint32_t v) {
        struct fake *f = c;
        if (v & 0x80000000u) f->invalid_writes++;
        for (unsigned i = 0; i < 4; i++) {
                if (o == aliases[i][0]) { f->infra[aliases[i][2]/4] |= v; return; }
                if (o == aliases[i][1]) { f->infra[aliases[i][2]/4] &= ~v; return; }
        }
        f->invalid_writes++;
}
static void ud(void *c, unsigned us) { (void)us; ((struct fake *)c)->delays++; }

/* Emulate the hardware status/ack bits from the control registers. */
static void refresh(struct fake *f)
{
        uint32_t sta = 0, sta2 = 0;
        for (unsigned d = 0; d < 7; d++) {
                uint32_t con = f->spm[(0x308 / 4) + d];
                if (con & (1u << 2)) sta |= 1u << (2 + d);
                if (con & (1u << 3)) sta2 |= 1u << (2 + d);
                if (f->ack && (con & (1u << 8)))
                        con |= 1u << 12;
                else
                        con &= ~(1u << 12);
                f->spm[(0x308 / 4) + d] = con;
        }
        if (f->fail_status)
                sta = sta2 = 0;
        f->spm[0x16c / 4] = sta;
        f->spm[0x170 / 4] = sta2;
}
static uint32_t rs2(void *c, uint32_t o) { refresh(c); return rs(c, o); }
static uint32_t ri2(void *c, uint32_t o) { refresh(c); return ri(c, o); }

int main(void)
{
        struct fake f;
        memset(&f, 0, sizeof(f));
        f.ack = true;
        const struct nord2_scpsys_ops ops = {rs2, ws, ri2, wi, ud, 0};
        struct nord2_scpsys s;
        if (!nord2_scpsys_init(&s, &ops, &f, 2000))
                return 1;
        if (!nord2_scpsys_mfg_all_on(&s)) {
                fprintf(stderr, "all_on failed\n");
                return 1;
        }
        bool on;
        for (unsigned d = 0; d < 7; d++) {
                if (!nord2_scpsys_mfg_status(&s, d, &on) || !on) {
                        fprintf(stderr, "domain %u not on\n", d);
                        return 1;
                }
        }
        if ((f.spm[0x16c / 4] & 0x1fc) != 0x1fc) {
                fprintf(stderr, "status mask wrong\n");
                return 1;
        }
        nord2_scpsys_mfg_all_off(&s);
        for (unsigned d = 0; d < 7; d++) {
                if (!nord2_scpsys_mfg_status(&s, d, &on) || on) {
                        fprintf(stderr, "domain %u still on\n", d);
                        return 1;
                }
        }
        if (f.alias_reads || f.invalid_writes) {
                fprintf(stderr, "SET/CLR aliases read or unrelated bits written\n");
                return 1;
        }
        /* Re-enable must release every protection bank. */
        if (!nord2_scpsys_mfg_all_on(&s) || f.infra[0x228/4] ||
            f.infra[0x258/4] || f.infra[0x724/4] || f.infra[0xbc4/4])
                return 1;
        struct fake bad;
        memset(&bad, 0, sizeof(bad));
        bad.ack = false;
        bad.fail_status = true;
        struct nord2_scpsys s2;
        if (!nord2_scpsys_init(&s2, &ops, &bad, 200))
                return 1;
        if (nord2_scpsys_mfg_on(&s2, 0) || bad.delays == 0 ||
            bad.delays > 100) {
                fprintf(stderr, "timeout not bounded\n");
                return 1;
        }
        printf("all SCPsys MTCMOS tests passed\n");
        return 0;
}
