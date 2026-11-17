#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "audio_afe_adapter.h"

/* Use the reviewed core test's register model, without changing that frozen
 * source or claiming it models electrical clocks/amplifiers. */
#define main reviewed_core_test_main
#include "../revision5-review/candidate/tools/test-audio-afe.c"
#undef main

struct adapter_model {
	struct cycle_model cm;
	uintptr_t worker;
	unsigned worker_calls, change_worker_at, fail_read_at, fail_write_at;
};
static int who(void *p, uintptr_t *v)
{
	struct adapter_model *m=p;
	m->worker_calls++;
	*v = m->change_worker_at && m->worker_calls >= m->change_worker_at ? 2 : m->worker;
	return 0;
}
static int read32(void *p, uint32_t pa, uint32_t *v)
{
	struct adapter_model *m=p;
	if (m->fail_read_at && m->cm.reads + 1 == m->fail_read_at) {
		m->cm.reads++; return -1;
	}
	return model_read(&m->cm,pa,v);
}
static int write32(void *p, uint32_t pa, uint32_t v)
{
	struct adapter_model *m=p;
	m->cm.fail_write=m->fail_write_at;
	return model_write(&m->cm,pa,v);
}
static int delay_us(void *p,unsigned us)
{
	return model_delay(&((struct adapter_model *)p)->cm,us);
}
static struct naa_state a;
int main(int argc,char **argv)
{
	assert(argc==2);
	struct nau_state s;
	cold(&s);
	struct adapter_model m={0};
	m.worker=1;
	model_init(&m.cm,&s);
	const struct naa_backend b={who,read32,write32,delay_us};
	assert(naa_prepare(&a,&b,&m,1,&s,amps)==0);
	assert(m.cm.reads==0&&m.cm.writes==0&&m.worker_calls==0);
	if (!strcmp(argv[1],"no-arm")) {
		assert(naa_run(&a,0)==-1);
		assert(m.cm.reads==0&&m.cm.writes==0);
	} else if (!strcmp(argv[1],"success")) {
		assert(naa_run(&a,NAF_AUTHORIZE)==0);
		assert(naa_return_ok(&a));
		assert(a.prewatch==22&&a.postwatch==22&&a.power_confirmed);
		assert(m.cm.sram_writes==NAF_SRAM_BYTES/4);
		assert(a.core.cur_changes>=2);
		unsigned old=m.cm.reads+m.cm.writes;
		assert(naa_run(&a,NAF_AUTHORIZE)==-1);
		assert(m.cm.reads+m.cm.writes==old);
	} else if (!strcmp(argv[1],"fault-first")) {
		m.fail_write_at=1;
		assert(naa_run(&a,NAF_AUTHORIZE)==-2);
		assert(a.halt&&a.core.uncertain&&a.core.active&&!naa_return_ok(&a));
	} else if (!strcmp(argv[1],"wrong-value")) {
		a.plan.up[0].value ^= 1u;
		assert(naa_run(&a,NAF_AUTHORIZE)==-2);
		assert(a.halt&&a.error==NAA_E_VALUE&&m.cm.writes==0);
	} else if (!strcmp(argv[1],"wrong-phase")) {
		a.plan.up[0].phase = NAF_PH_AFECLK;
		assert(naa_run(&a,NAF_AUTHORIZE)==-2);
		assert(a.halt&&a.error==NAA_E_PHASE&&m.cm.writes==0);
	} else if (!strcmp(argv[1],"wrong-fill")) {
		unsigned found=0;
		for (unsigned i=0;i<a.plan.n_up;i++) if (a.plan.up[i].kind==NAF_FILL) {
			a.plan.up[i].value=1; found++;
		}
		assert(found==1);
		assert(naa_run(&a,NAF_AUTHORIZE)==-2);
		assert(a.halt&&a.error==NAA_E_VALUE&&m.cm.sram_writes==0);
	} else if (!strcmp(argv[1],"fault-last")) {
		struct naf_io io={model_read,model_write,model_delay};
		struct naf_state run={0};
		assert(naf_cycle(&io,&m.cm,&s,amps,NAF_AUTHORIZE,&run)==0);
		unsigned last=m.cm.writes;
		model_init(&m.cm,&s);
		m.fail_write_at=last;
		assert(naa_run(&a,NAF_AUTHORIZE)==-2);
		assert(a.halt&&a.core.uncertain&&a.core.active&&
		    a.core.phase==NAF_PH_POWER_OFF&&!naa_return_ok(&a));
	} else if (!strcmp(argv[1],"worker-change")) {
		m.change_worker_at=3;
		assert(naa_run(&a,NAF_AUTHORIZE)==-1);
		assert(a.halt&&m.cm.writes==0&&!naa_return_ok(&a));
	} else if (!strcmp(argv[1],"worker-after-write")) {
		m.change_worker_at=25;
		assert(naa_run(&a,NAF_AUTHORIZE)==-2);
		assert(a.halt&&m.cm.writes==1&&a.core.uncertain&&!naa_return_ok(&a));
	} else if (!strcmp(argv[1],"live-read-fault")) {
		m.fail_read_at=1;
		assert(naa_run(&a,NAF_AUTHORIZE)==-1);
		assert(a.halt&&m.cm.writes==0&&!naa_return_ok(&a));
	} else if (!strcmp(argv[1],"observe-fail")) {
		m.cm.stuck_cur=1;
		assert(naa_run(&a,NAF_AUTHORIZE)==-3);
		assert(naa_return_ok(&a));
		assert(m.cm.sram_writes==NAF_SRAM_BYTES/4);
	} else { assert(0); }
	puts("adapter case PASS");
}
