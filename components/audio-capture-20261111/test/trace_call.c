/* Write trace of the call path (MT6359 tables, call AFE up, calibration, mute, downlink, restore): compiled once against
 * the q10in audio_call.c (base/) and once against 0320 (src/); run.sh requires the two traces to be identical. */
#include "host_kernel.h"
#include <assert.h>
#include <stdlib.h>
#include SRC
static uint64_t fake_ns;
uint64_t mach_absolute_time(void) { fake_ns += 1000000; return fake_ns; }
void absolutetime_to_nanoseconds(uint64_t a, uint64_t *ns) { *ns = a; }
void clock_interval_to_deadline(uint32_t a, uint32_t b, uint64_t *d) { *d = (uint64_t)a * b; }
int assert_wait_deadline(event_t e, int i, uint64_t d) { (void)e; (void)i; (void)d; return 0; }
int thread_block(int c) { (void)c; return 0; }
void delay(int us) { (void)us; }
static uint32_t fake_pwrap_page[1024];
vm_offset_t ml_io_map(vm_offset_t pa, vm_size_t sz) { assert(pa == N2PMIF_PWRAP_BASE && sz == 0x1000); return (vm_offset_t)fake_pwrap_page; }
void (*nord2_call_ccci_rx_hook)(const uint8_t *, uint32_t);
unsigned (*nord2_call_ccci_take_hook)(uint8_t *, unsigned);
void (*nord2_call_state_hook)(unsigned, unsigned);

static uint16_t pmic[0x4000];
static unsigned pm_writes;
bool n2pmif_init(struct n2pmif *p, enum n2pmif_kind k, const struct n2pmif_ops *o, void *c)
{ memset(p, 0, sizeof *p); p->kind = k; p->ops = *o; p->ctx = c; return true; }
enum n2pmif_error n2pmif_pwrap_read(struct n2pmif *p, uint16_t a, uint16_t *v) { (void)p; *v = pmic[a]; return N2PMIF_OK; }
enum n2pmif_error n2pmif_pwrap_write(struct n2pmif *p, uint16_t a, uint16_t v)
{
	(void)p; pm_writes++; printf("pm %04x=%04x\n", a, v);
	if (a == P_GPIO_MODE2_SET || a == P_GPIO_MODE3_SET || a == P_GPIO_MODE4_SET) pmic[a - 2] |= v;
	else if (a == P_GPIO_MODE2_CLR || a == P_GPIO_MODE3_CLR || a == P_GPIO_MODE4_CLR) pmic[a - 4] &= (uint16_t)~v;
	else pmic[a] = v;
	if (a == P_LDO_VAUD18_CON0) pmic[P_LDO_VAUD18_MON] = v & 1;
	return N2PMIF_OK;
}
static unsigned owner_calls;                     /* synchronous owner calls: the recording path must make none */
bool n2owner_ready(const struct n2owner *q) { (void)q; return true; }
void n2owner_poll(struct n2owner *q) { (void)q; }
bool n2owner_call(struct n2owner *q, int (*fn)(void *), void *arg, const struct n2owner_wait *w, unsigned ms, int *r)
{ (void)q; (void)w; (void)ms; owner_calls++; *r = fn(arg); return true; }

static uint32_t afe[0x4000 / 4], gpio[0x4000 / 4], topck[0x1000 / 4];
static unsigned refused;
static uint32_t *slot(uint32_t pa)
{
	if (pa >= 0x11210000u && pa < 0x11214000u) return &afe[(pa - 0x11210000u) / 4];
	if (pa >= 0x10004000u && pa < 0x10008000u) return &gpio[(pa - 0x10004000u) / 4];
	if (pa >= 0x10000000u && pa < 0x10001000u) return &topck[(pa - 0x10000000u) / 4];
	return NULL;
}
static unsigned cal_mon_reads;
static int io_read(void *c, uint32_t pa, unsigned w, uint32_t *v)
{
	(void)c; assert(w == 4);
	if (pa == 0x10000330u) {
		unsigned ph = pmic[0x23e4] & 0x7f;
		if (!(topck[0x32c / 4] & 1) || ++cal_mon_reads % 3) { *v = 0; return 0; }
		*v = (7u << 28) | ((ph < 10 ? 3u : 4u)) | ((ph < 12 ? 3u : 4u) << 4) | ((ph < 15 ? 5u : 6u) << 8);
		return 0;
	}
	uint32_t *s = slot(pa); if (!s) return -1; *v = *s; return 0;
}
static int io_write(void *c, uint32_t pa, unsigned w, uint32_t v)
{
	(void)c; assert(w == 4);
	if (!nac_write_allowed(pa)) { refused++; printf("refused write %x\n", pa); return -1; }
	uint32_t *s = slot(pa); if (!s) return -1; *s = v; printf("afe %08x=%08x\n", pa, v); return 0;
}
#define AFEW(o) afe[(o) / 4]
int main(void)
{
	static struct nau_hw hw;
	int16_t st[1024];
	pmic[P_HWCID] = 0x5930; pmic[P_AUDIO_TOP_CON0] = 0x00ff; pmic[P_AUD_TOP_CKPDN_CON0] = 0x0066;
	pmic[P_AUDDEC_ANA_CON13] = 0x0010; pmic[P_DCXO_CW12] = 0x8000;
	hw.io.read = io_read; hw.io.write = io_write; hw.status.running = 1;
	for (unsigned i = 0; i < sizeof afe / 4; i++) afe[i] = 0x5a5a0000u ^ i;
	AFEW(0x86c) = 0x11213040u; AFEW(0x384) = 0x11213800u;
	for (int call = 0; call < 2; call++) {
		printf("-- call %d\n", call);
		assert(nac_pmic_up() == 0);
		assert(nac_afe_up(&hw) == 0);
		for (int it = 0; it < 2000 && nac_cal_state; it++) { nac_tick(&hw); if (it % 2) nord2_audio_owner_poll(); }
		nac_set_ul_mute(0, 1); nac_tick(&hw); nac_set_ul_mute(0, 0); nac_tick(&hw);
		AFEW(0x86c) = 0x11213040u + 0x100; AFEW(0x384) = 0x11213800u + 0x200;
		(void)nac_dl_take(&hw, st, 512);
		assert(nac_afe_down(&hw) == 0 && nac_pmic_down() == 0);
	}
	return 0;
}
