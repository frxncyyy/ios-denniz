#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "audio_afe.h"

static struct naf_amp_evidence amps[2] = {{1,1,1,0,0},{1,1,1,0,0}};

static void cold(struct nau_state *s)
{
	memset(s, 0, sizeof(*s));
	s->snap_read = NAU_SNAP_COUNT;
	s->snap[NAU_S_SPM_POWERON] = (NAF_SPM_PROJECT_CODE << 16) | 1u;
	s->snap[NAU_S_AUDIO_PWR_CON] = 0x1112;
	s->snap[NAU_S_ADSP_PWR_CON] = 0x32;
	s->snap[NAU_S_IFR1_CG] = 1u << 25;
	s->snap[NAU_S_IFR3_CG] = 1u << 4;
	s->snap[NAU_S_PROT2_STA1] = 1u << 4;
	s->snap[NAU_S_CLK_CFG_7] = 1u << 31;
	s->snap[NAU_S_CLK_CFG_8] = 1u << 7;
	s->snap[NAU_S_CLK_CFG_12] = 1u << 7;
	s->snap[NAU_S_CLK_CFG_13] = 1u << 7;
	s->snap[NAU_S_APLL2_CON4] = 2;
	uint32_t code = 0;
	s->snap[NAU_S_APLL2_CON2] = naf_pll_pcw(NAF_APLL2_HZ, &code);
	s->snap[NAU_S_APLL2_CON1] = code << 24;
	s->snap[NAU_S_APLL2_TUNER] = s->snap[NAU_S_APLL2_CON2] + 1;
}

struct mock { unsigned reads, writes, delays, fail_read, fail_write; };
static int rd(void *p, uint32_t pa, uint32_t *v)
{
	struct mock *m = p;
	(void)pa;
	m->reads++;
	*v = 0;
	return m->fail_read && m->reads == m->fail_read ? -1 : 0;
}
static int wr(void *p, uint32_t pa, uint32_t v)
{
	struct mock *m = p;
	(void)v;
	/* No I2C8, PMIC, amplifier, or other out-of-plan writes. */
	assert(naf_write_allowed(pa));
	assert(pa < 0x11d04000 || pa >= 0x11d05000);
	m->writes++;
	return m->fail_write && m->writes == m->fail_write ? -1 : 0;
}
static int dl(void *p, unsigned us) { ((struct mock *)p)->delays++; assert(us <= 1000); return 0; }

/* A register-only model of the selected set/clear aliases, PG status/ack and
 * DL1 pointer. It does not pretend to model clocks or analog audio. */
struct reg { uint32_t pa, v; };
struct cycle_model {
	struct reg r[256];
	unsigned n, reads, writes, fail_write, cur_reads, sram_writes, stuck_cur;
};
static uint32_t *regp(struct cycle_model *m, uint32_t pa)
{
	for (unsigned i = 0; i < m->n; i++) if (m->r[i].pa == pa) return &m->r[i].v;
	assert(m->n < 256);
	m->r[m->n] = (struct reg){pa,0};
	return &m->r[m->n++].v;
}
static void model_init(struct cycle_model *m, const struct nau_state *s)
{
	memset(m,0,sizeof(*m));
	for (unsigned i = 0; i < NAU_SNAP_COUNT; i++)
		*regp(m,nau_snapshot_pa[i]) = s->snap[i];
}
static int model_read(void *p, uint32_t pa, uint32_t *v)
{
	struct cycle_model *m=p;
	m->reads++;
	if (pa==NAF_AFE_DL1_CUR && (*regp(m,NAF_AFE_DAC_CON0)&(1u<<4))) {
		*v=NAF_SRAM+(m->stuck_cur?0:(m->cur_reads++%4)*64);
	} else *v=*regp(m,pa);
	return 0;
}
static int model_write(void *p, uint32_t pa, uint32_t v)
{
	struct cycle_model *m=p;
	assert(naf_write_allowed(pa));
	m->writes++;
	if (m->fail_write && m->writes==m->fail_write) return -1;
	if (pa>=NAF_SRAM && pa<NAF_SRAM+NAF_SRAM_BYTES) {
		assert(!(pa&3u) && v==0);
		m->sram_writes++;
		return 0;
	}
	if (pa==NAF_IFR_CG1_CLR) *regp(m,NAF_IFR_CG1_STA)&=~v;
	else if (pa==NAF_IFR_CG1_SET) *regp(m,NAF_IFR_CG1_STA)|=v;
	else if (pa==NAF_IFR_CG3_CLR) *regp(m,NAF_IFR_CG3_STA)&=~v;
	else if (pa==NAF_IFR_CG3_SET) *regp(m,NAF_IFR_CG3_STA)|=v;
	else if (pa==NAF_IFR_PROT2_CLR) *regp(m,NAF_IFR_PROT2_STA1)&=~v;
	else if (pa==NAF_IFR_PROT2_SET) *regp(m,NAF_IFR_PROT2_STA1)|=v;
	else if (pa==NAF_TOP_UPDATE1) { /* write-only trigger */ }
	else if (pa==NAF_TOP_SET(NAF_TOP_CFG7)||pa==NAF_TOP_SET(NAF_TOP_CFG8)||
	         pa==NAF_TOP_SET(NAF_TOP_CFG12)||pa==NAF_TOP_SET(NAF_TOP_CFG13))
		*regp(m,pa-4)|=v;
	else if (pa==NAF_TOP_CLR(NAF_TOP_CFG7)||pa==NAF_TOP_CLR(NAF_TOP_CFG8)||
	         pa==NAF_TOP_CLR(NAF_TOP_CFG12)||pa==NAF_TOP_CLR(NAF_TOP_CFG13))
		*regp(m,pa-8)&=~v;
	else if (pa==NAF_SPM_AUDIO_PWR) {
		*regp(m,pa)=(v&~(1u<<12))|((v&(1u<<8))?1u<<12:0);
		uint32_t *a=regp(m,NAF_SPM_PWR_STATUS), *b=regp(m,NAF_SPM_PWR_STATUS2);
		if ((v&0xcu)==0xcu) { *a|=1u<<21; *b|=1u<<21; }
		else { *a&=~(1u<<21); *b&=~(1u<<21); }
	} else *regp(m,pa)=v;
	return 0;
}
static int model_delay(void *p, unsigned us) { (void)p; assert(us<=1000); return 0; }

int main(void)
{
	struct nau_state s;
	cold(&s);
	assert(naf_gate(&s, amps) == 0);
	struct naf_plan p;
	assert(naf_build(&s, amps, &p) == 0);
	assert(p.n_up > 40 && p.n_up <= NAF_MAX_OPS);
	assert(p.n_down > 30 && p.n_down <= NAF_MAX_OPS);
	unsigned fill = 0, observe = 0;
	for (unsigned i = 0; i < p.n_up; i++) {
		assert(p.up[i].kind != NAF_W || naf_write_allowed(p.up[i].pa));
		fill += p.up[i].kind == NAF_FILL;
		observe += p.up[i].kind == NAF_OBSERVE;
	}
	assert(fill == 1 && observe == 1);
	assert(p.up[0].pa == NAF_TOP_CLR(NAF_TOP_CFG8));
	assert(p.down[p.n_down-1].pa == NAF_TOP_SET(NAF_TOP_CFG8));
	/* Each prerequisite independently fails closed without a write. */
	s.snap[NAU_S_SPM_POWERON] = 0;
	assert(naf_gate(&s, amps) & NAF_G_SPM_CFG);
	s.snap[NAU_S_SPM_POWERON] = (NAF_SPM_PROJECT_CODE << 16) | 1u;
	s.snap[NAU_S_ADSP_PWR_CON] = 0;
	assert(naf_gate(&s, amps) & NAF_G_ADSP_ON);
	s.snap[NAU_S_ADSP_PWR_CON] = 0x32;
	s.snap[NAU_S_PWR_STATUS] = 1u << 22;
	assert(naf_gate(&s, amps) & NAF_G_ADSP_ON);
	s.snap[NAU_S_PWR_STATUS] = 0;
	amps[0].pwdn = 0;
	assert(naf_gate(&s, amps) & NAF_G_AMPS);
	amps[0].pwdn = 1;
	struct mock m = {0};
	struct naf_io io = {rd,wr,dl};
	struct naf_state run = {0};
	assert(naf_cycle(&io, &m, &s, amps, 0, &run) == -1);
	assert(m.reads == 0 && m.writes == 0);
	assert(naf_return_ok(&run));
	/* A live-read callback failure is pre-mutation; one-shot remains latched. */
	m = (struct mock){.fail_read = 1};
	run = (struct naf_state){0};
	assert(naf_cycle(&io, &m, &s, amps, NAF_AUTHORIZE, &run) == -1);
	assert(m.writes == 0 && naf_return_ok(&run));
	unsigned old = m.reads;
	assert(naf_cycle(&io, &m, &s, amps, NAF_AUTHORIZE, &run) == -1 && m.reads == old);
	/* First write may have happened even when the callback reports failure. */
	m = (struct mock){.fail_write = 1};
	run = (struct naf_state){0};
	/* Mock live reads are zero, so this attempt is refused by drift. */
	assert(naf_cycle(&io, &m, &s, amps, NAF_AUTHORIZE, &run) == -1);
	assert(m.writes == 0);
	/* The source-ordered cycle returns the measured cold boundary in this
	 * register model, and an uncertain write latches no-retry/manual-reset. */
	struct cycle_model cm;
	model_init(&cm,&s);
	struct naf_io mio={model_read,model_write,model_delay};
	run=(struct naf_state){0};
	assert(naf_cycle(&mio,&cm,&s,amps,NAF_AUTHORIZE,&run)==0);
	assert(run.error==0&&!run.active&&!run.uncertain&&run.cur_changes>=2);
	assert(run.up_done==p.n_up&&run.down_done==p.n_down);
	assert(cm.sram_writes==NAF_SRAM_BYTES/4);
	assert(naf_return_ok(&run));
	unsigned total_writes=cm.writes;
	model_init(&cm,&s);cm.stuck_cur=1;
	run=(struct naf_state){0};
	assert(naf_cycle(&mio,&cm,&s,amps,NAF_AUTHORIZE,&run)==-3);
	assert(run.failure==NAF_FAIL_OBSERVE&&!run.active&&!run.uncertain);
	assert(run.down_done==p.n_down&&naf_return_ok(&run));
	model_init(&cm,&s);cm.fail_write=1;
	run=(struct naf_state){0};
	assert(naf_cycle(&mio,&cm,&s,amps,NAF_AUTHORIZE,&run)==-2);
	assert(run.active&&run.uncertain&&run.writes==1&&!naf_return_ok(&run));
	old=cm.reads+cm.writes;
	assert(naf_cycle(&mio,&cm,&s,amps,NAF_AUTHORIZE,&run)==-1);
	assert(cm.reads+cm.writes==old);
	model_init(&cm,&s);cm.fail_write=total_writes;
	run=(struct naf_state){0};
	assert(naf_cycle(&mio,&cm,&s,amps,NAF_AUTHORIZE,&run)==-2);
	assert(run.active&&run.uncertain&&run.phase==NAF_PH_POWER_OFF);
	printf("plan up=%u down=%u gate/fault checks passed\n", p.n_up, p.n_down);
}
