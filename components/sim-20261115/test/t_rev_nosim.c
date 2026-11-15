/* sim-20261115 regression (coordinator 2026-11-15 "Modem Firmware empty"): the modem revision (+CGMR, DMS Get Revision / Get
 * Software Version answer) must survive every SIM path of 0710: absent card, card-gone forgetting, PIN states, fix on/off.
 * cc -std=c11 -Wall -Wextra -Werror -I<arm dir> t_rev_nosim.c -o t && ./t */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "nord2_bb_qmux.h"
#include "nord2_bb_qmi.h"
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); fails++; } } while (0)
static void q(struct nbm_state *s, unsigned id, int final, const char *t) { nbm_parse(s, id, final, t, (unsigned)strlen(t)); }
/* the fields bb_sim_forget() (Nord2Baseband.cpp, 0710) clears -- kept in sync by check_forget.py */
static void forget(struct nbm_state *s)
{
    memset(s->imsi, 0, sizeof s->imsi); memset(s->iccid_ef, 0, sizeof s->iccid_ef); s->have_iccid = 0;
    memset(s->gid, 0, sizeof s->gid); s->gid_len[0] = s->gid_len[1] = 0; s->gid_known[0] = s->gid_known[1] = 0;
    s->gid_size[0] = s->gid_size[1] = 0; s->pin_lock = -1; s->pin_seen = 0; s->pin1_left = s->puk1_left = 0;
}
int main(void)
{
    const char *rev = "MOLY.NR15.R3.TC16.PR6.SP.V8.P69, 2024/12/17 20:5";
    for (int fix = 0; fix <= 15; fix += 15) {
        struct nbm_state s; nbm_init(&s); s.fix = (uint8_t)fix;
        char line[96]; snprintf(line, sizeof line, "%s\nOK\n", rev);
        q(&s, NBM_Q_CGMR, 0, line);
        CHECK(!strncmp(s.rev, rev, 40));
        q(&s, NBM_Q_CFUN, 0, "+CFUN: 1\n");
        q(&s, NBM_Q_CPIN, 1, "+CME ERROR: SIM not inserted\n");
        CHECK(s.sim == NBM_SIM_ABSENT);
        forget(&s);                                        /* card gone */
        q(&s, NBM_Q_CPIN, 0, "+CPIN: SIM PIN2\n"); q(&s, NBM_Q_CPIN, 0, "+CPIN: SIM PIN\n");
        q(&s, NBM_Q_CLCK, 1, "+CME ERROR: SIM PIN required\n");
        q(&s, NBM_Q_CGMR, 1, "ERROR\n");                   /* a failed re-query must not wipe it */
        q(&s, NBM_Q_CGMR, 0, "+CGMR: OTHER\nOK\n");         /* nor replace it (read once) */
        CHECK(!strncmp(s.rev, rev, 40));
        CHECK(nbm_sim_shown(&s) != NBM_SIM_READY);
    }
    /* the +CGMR line with a prefix is accepted too */
    struct nbm_state s; nbm_init(&s); q(&s, NBM_Q_CGMR, 0, "+CGMR: MOLY.X\nOK\n"); CHECK(!strcmp(s.rev, "MOLY.X"));
    printf(fails ? "T_REV_NOSIM FAILED (%d)\n" : "T_REV_NOSIM_OK\n", fails);
    return fails != 0;
}
