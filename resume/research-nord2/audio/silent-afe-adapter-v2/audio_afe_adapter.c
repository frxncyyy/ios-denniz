#if defined(KERNEL)
#include <pexpert/arm64/board_config.h>
#endif
#if defined(NORD2)
#include "audio_afe_adapter.h"

/* naf_cycle owns a static plan buffer. Never release this reservation after
 * entry, including a clean teardown: a second object must not race/replay. */
static volatile unsigned naa_boot_attempted;

static const unsigned naa_watch_idx[] = {
	NAU_S_SPM_POWERON, NAU_S_PWR_STATUS, NAU_S_PWR_STATUS_2ND,
	NAU_S_ADSP_PWR_CON, NAU_S_AUDIO_PWR_CON, NAU_S_IFR1_CG,
	NAU_S_IFR3_CG, NAU_S_PROT2_STA1, NAU_S_CLK_CFG_7,
	NAU_S_CLK_CFG_8, NAU_S_CLK_CFG_12, NAU_S_CLK_CFG_13,
	NAU_S_AP_PLL_CON3, NAU_S_APLL2_TUNER, NAU_S_APLL2_CON0,
	NAU_S_APLL2_CON1, NAU_S_APLL2_CON2, NAU_S_APLL2_CON4,
	NAU_S_MODE_32, NAU_S_DIR_32, NAU_S_LT_PD32, NAU_S_LT_PU32
};
#define NAA_WATCH (sizeof(naa_watch_idx) / sizeof(naa_watch_idx[0]))

static int
naa_fail(struct naa_state *s, unsigned error)
{
	s->halt = 1;
	if (!s->error) { s->error = error; }
	return -1;
}

static int
naa_worker(struct naa_state *s)
{
	uintptr_t now = 0;
	if (s->halt || !s->hw.worker || s->hw.worker(s->cookie, &now) ||
	    !now || now != s->worker) {
		return naa_fail(s, NAA_E_WORKER);
	}
	return 0;
}

static const struct naf_op *
naa_op(struct naa_state *s)
{
	unsigned down = s->core.phase >= NAF_PH_STOP;
	unsigned n = down ? s->plan.n_down : s->plan.n_up;
	const struct naf_op *o = down ? s->plan.down : s->plan.up;
	if (s->core.op >= n || o[s->core.op].phase != s->core.phase) {
		naa_fail(s, NAA_E_PHASE);
		return 0;
	}
	unsigned key = 1u + s->core.op + (down ? NAF_MAX_OPS : 0);
	if (s->last_key != key) {
		if (s->rmw_pending || (s->op_last_kind == NAF_FILL &&
		    s->fill_words != NAF_SRAM_BYTES / 4u)) {
			naa_fail(s, NAA_E_PHASE);
			return 0;
		}
		s->last_key = key;
		s->op_last_kind = o[s->core.op].kind;
		s->op_reads = s->op_writes = s->fill_words = s->last_read_valid = 0;
	}
	return &o[s->core.op];
}

static void
naa_power_observe(struct naa_state *s, uint32_t pa, uint32_t v)
{
	if (pa == NAF_SPM_PWR_STATUS && (v & (1u << 21))) { s->status1 = 1; }
	if (pa == NAF_SPM_PWR_STATUS2 && (v & (1u << 21))) { s->status2 = 1; }
	if (pa == NAF_SPM_AUDIO_PWR && !(v & (1u << 12))) { s->sram_ack = 1; }
	if (pa == NAF_IFR_PROT2_STA1 && !(v & (1u << 4))) { s->protect_released = 1; }
	if (s->status1 && s->status2 && s->sram_ack && s->protect_released) {
		s->power_confirmed = 1;
	}
}

static int
naa_read(void *c, uint32_t pa, uint32_t *v)
{
	struct naa_state *s = c;
	if (!v || naa_worker(s)) { return -1; }
	/* Initial and final live checks are fixed 22-word sequences. */
	if (!s->core.phase || s->core.down_done == s->plan.n_down) {
		unsigned *i = !s->core.phase ? &s->prewatch : &s->postwatch;
		if (*i >= NAA_WATCH || pa != nau_snapshot_pa[naa_watch_idx[*i]]) {
			return naa_fail(s, NAA_E_PHASE);
		}
		if (s->hw.read32(s->cookie, pa, v)) { return naa_fail(s, NAA_E_BACKEND); }
		(*i)++;
		s->last_pa = pa; s->last_value = *v;
		return 0;
	}
	if (s->prewatch != NAA_WATCH) { return naa_fail(s, NAA_E_PHASE); }
	const struct naf_op *o = naa_op(s);
	if (!o || pa != o->pa ||
	    (o->kind != NAF_RMW && o->kind != NAF_POLL && o->kind != NAF_OBSERVE) ||
	    (o->kind == NAF_RMW && (s->op_reads || s->rmw_pending)) ||
	    ((o->kind == NAF_POLL || o->kind == NAF_OBSERVE) && s->op_reads >= o->arg) ||
	    (pa >= NAF_AFE && pa < NAF_AFE + 0x1000u && !s->power_confirmed)) {
		return naa_fail(s, NAA_E_PHASE);
	}
	if (s->hw.read32(s->cookie, pa, v)) { return naa_fail(s, NAA_E_BACKEND); }
	s->op_reads++;
	s->last_read_valid = 1;
	s->last_poll_match = o->kind == NAF_POLL && ((*v & o->mask) == o->value);
	if (o->kind == NAF_RMW) { s->rmw_raw = *v; s->rmw_pending = 1; }
	if (s->core.phase == NAF_PH_POWER) { naa_power_observe(s, pa, *v); }
	s->last_pa = pa; s->last_value = *v;
	return 0;
}

static int
naa_write(void *c, uint32_t pa, uint32_t v)
{
	struct naa_state *s = c;
	if (naa_worker(s) || s->prewatch != NAA_WATCH || s->postwatch) {
		return naa_fail(s, NAA_E_PHASE);
	}
	const struct naf_op *o = naa_op(s);
	if (!o || !naf_write_allowed(pa) ||
	    (pa >= NAF_AFE && pa < NAF_SRAM + NAF_SRAM_BYTES && !s->power_confirmed)) {
		return naa_fail(s, NAA_E_PHASE);
	}
	if (o->kind == NAF_FILL) {
		if (o->pa != NAF_SRAM || o->arg != NAF_SRAM_BYTES || o->value ||
		    s->fill_words >= NAF_SRAM_BYTES / 4u ||
		    pa != NAF_SRAM + 4u * s->fill_words || v) {
			return naa_fail(s, NAA_E_VALUE);
		}
		s->fill_words++;
	} else if (o->kind == NAF_RMW) {
		if (!s->rmw_pending || pa != o->pa ||
		    s->op_writes ||
		    v != ((s->rmw_raw & ~o->mask) | (o->value & o->mask))) {
			return naa_fail(s, NAA_E_VALUE);
		}
		s->rmw_pending = 0;
	} else if (o->kind != NAF_W || s->op_writes || pa != o->pa || v != o->value) {
		return naa_fail(s, NAA_E_VALUE);
	}
	s->op_writes++;
	/* The core latches active before calling this. A backend failure leaves
	 * that uncertain lifetime in place; the adapter never retries. */
	if (s->hw.write32(s->cookie, pa, v)) { return naa_fail(s, NAA_E_BACKEND); }
	s->last_pa = pa; s->last_value = v;
	return 0;
}

static int
naa_delay(void *c, unsigned us)
{
	struct naa_state *s = c;
	if (naa_worker(s) || s->prewatch != NAA_WATCH || s->postwatch) {
		return naa_fail(s, NAA_E_PHASE);
	}
	const struct naf_op *o = naa_op(s);
	if (!o || !((o->kind == NAF_DELAY && us == o->arg) ||
	    (o->kind == NAF_POLL && s->last_read_valid && !s->last_poll_match && us == o->step_us) ||
	    (o->kind == NAF_OBSERVE && s->last_read_valid && us == o->step_us))) {
		return naa_fail(s, NAA_E_VALUE);
	}
	s->last_read_valid = 0;
	if (s->hw.delay_us(s->cookie, us)) { return naa_fail(s, NAA_E_BACKEND); }
	return 0;
}

int
naa_prepare(struct naa_state *s, const struct naa_backend *hw, void *cookie,
    uintptr_t worker, const struct nau_state *snapshot,
    const struct naf_amp_evidence amp[2])
{
	if (!s || !hw || !hw->worker || !hw->read32 || !hw->write32 ||
	    !hw->delay_us || !worker || !snapshot || !amp || s->entered || s->prepared) {
		return -1;
	}
	if (naf_gate(snapshot, amp)) { s->error = NAA_E_COLD; return -1; }
	s->snapshot = *snapshot;
	s->amp[0] = amp[0]; s->amp[1] = amp[1];
	if (naf_build(&s->snapshot, s->amp, &s->plan)) {
		s->error = NAA_E_PLAN; return -1;
	}
	s->hw = *hw; s->cookie = cookie; s->worker = worker;
	s->prepared = 1;
	return 0;
}

int
naa_run(struct naa_state *s, uint32_t authorize)
{
	if (!s || !s->prepared || s->entered || s->halt || authorize != NAF_AUTHORIZE) {
		return -1;
	}
	if (naa_worker(s)) { return -1; }
	if (__sync_lock_test_and_set(&naa_boot_attempted, 1u)) {
		return naa_fail(s, NAA_E_WORKER);
	}
	s->entered = 1;
	static const struct naf_io io = {naa_read, naa_write, naa_delay};
	s->result = naf_cycle(&io, s, &s->snapshot, s->amp, authorize, &s->core);
	if ((s->result && s->result != -3) || s->prewatch != NAA_WATCH ||
	    s->postwatch != NAA_WATCH ||
	    s->rmw_pending || s->halt || !naf_return_ok(&s->core)) {
		s->halt = 1;
		if (!s->error) { s->error = NAA_E_BACKEND; }
	}
	return s->result;
}

int
naa_return_ok(const struct naa_state *s)
{
	return s && s->entered && !s->halt && !s->error &&
	    (s->result == 0 || s->result == -3) &&
	    s->prewatch == NAA_WATCH && s->postwatch == NAA_WATCH &&
	    naf_return_ok(&s->core);
}
#endif
