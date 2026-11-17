#ifndef NORD2_AUDIO_AFE_ADAPTER_H
#define NORD2_AUDIO_AFE_ADAPTER_H
#include <stdint.h>
#include "audio_afe.h"

/* One private, per-boot object. The only physical accesses are delegated to
 * these callbacks after the exact reviewed plan and worker are checked. */
struct naa_backend {
	int (*worker)(void *, uintptr_t *);
	int (*read32)(void *, uint32_t, uint32_t *);
	int (*write32)(void *, uint32_t, uint32_t);
	int (*delay_us)(void *, unsigned);
};

enum {
	NAA_E_NONE, NAA_E_ARGS, NAA_E_COLD, NAA_E_PLAN, NAA_E_WORKER,
	NAA_E_PHASE, NAA_E_VALUE, NAA_E_BACKEND
};

struct naa_state {
	struct naf_plan plan;       /* retained; do not put on a kernel stack */
	struct nau_state snapshot;  /* immutable same-boot evidence copy */
	struct naf_amp_evidence amp[2];
	struct naf_state core;
	struct naa_backend hw;
	void *cookie;
	uintptr_t worker;
	unsigned prepared, entered, halt, error, prewatch, postwatch;
	unsigned last_key, op_reads, op_writes, fill_words, rmw_pending;
	unsigned status1, status2, sram_ack, protect_released, power_confirmed;
	unsigned op_last_kind, last_poll_match, last_read_valid;
	uint32_t rmw_raw, last_pa, last_value;
	int result;
};

/* Pure preparation: no read/write/map/delay; validates full cold snapshot,
 * measured powered-down amplifiers and source plan. */
int naa_prepare(struct naa_state *, const struct naa_backend *, void *,
    uintptr_t worker, const struct nau_state *, const struct naf_amp_evidence[2]);
/* Exactly one future attempt per boot across all adapter objects. The magic
 * is a software arm; it is not a device or human authorization. No current
 * source calls this function. */
int naa_run(struct naa_state *, uint32_t authorize);
/* Pure Linux-return check; a retained uncertain state refuses return. */
int naa_return_ok(const struct naa_state *);
#endif
