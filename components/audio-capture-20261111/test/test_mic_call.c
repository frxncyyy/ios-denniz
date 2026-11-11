/* Host test for 0320 (audio_call.c, the microphone without a call): MT6359 tables through the async slot, the
 * capture-only AFE (no PCM2/AWB/CONN17/18/CONN5, VUL12_ON only), calibration on the first use, VUL12 drain, exact
 * restore; then a call after a recording still programs what it did.  Same fakes as callaudio-d/test/test_call.c. */
#include "host_kernel.h"
#include <assert.h>
#include <stdlib.h>
#include "../src/bsd/dev/audio_call.c"

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
	(void)p; pm_writes++;
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
	uint32_t *s = slot(pa); if (!s) return -1; *s = v; return 0;
}
#define AFEW(o) afe[(o) / 4]

int main(void)
{
	static struct nau_hw hw;
	static uint32_t before_afe[sizeof afe / 4], before_gpio[sizeof gpio / 4], before_topck[sizeof topck / 4];
	int err = 0;

	/* 1. MT6359 up through the async slot: nothing happens until the input thread polls; the worker never waits */
	pmic[P_HWCID] = 0x5930; pmic[P_AUDIO_TOP_CON0] = 0x00ff; pmic[P_AUD_TOP_CKPDN_CON0] = 0x0066;
	pmic[P_AUDDEC_ANA_CON13] = 0x0010; pmic[P_LDO_VAUD18_CON0] = 0; pmic[P_DCXO_CW12] = 0x8000;
	assert(nac_mic_pmic_submit(1) == 0 && pm_writes == 0);
	assert(nac_mic_pmic_poll(&err) == 0);
	assert(nac_mic_pmic_submit(1) == -1);                              /* slot busy */
	nord2_audio_owner_poll();
	assert(nac_mic_pmic_poll(&err) == 1 && err == 0 && owner_calls == 0);
	assert(pmic[P_DCXO_CW12] == 0xa000 && pmic[P_LDO_VAUD18_CON0] == 1 && (pmic[P_AUDDEC_ANA_CON13] & 0x10) == 0);
	assert(pmic[P_AUDENC_ANA_CON0] == (0x0040 | 0x4000 | 0x0001 | 0x0300 | 0x0002 | 0x1000));
	assert(pmic[P_AUDENC_ANA_CON15] == 0x0051 && pmic[P_AFE_AUD_PAD_TOP] == 0x3900 && pmic[P_AFE_UL_SRC_CON0_L] == 1);
	printf("mt6359 up (async, whole table): %u writes\n", pm_writes);

	/* 2. capture-only AFE */
	hw.io.read = io_read; hw.io.write = io_write; hw.status.running = 1;
	for (unsigned i = 0; i < sizeof afe / 4; i++) afe[i] = 0x5a5a0000u ^ i;
	AFEW(0x010) = 0x00000006u;                                          /* DL1 running (speaker path), AWB/VUL12 off */
	gpio[(0x100054a0u - 0x10004000u) / 4] = 0x00345678u; gpio[(0x100054b0u - 0x10004000u) / 4] = 0x87650000u;
	topck[0x32c / 4] = 0x00001230u;
	AFEW(0x86c) = 0x11213040u; AFEW(0x384) = 0x11213800u + 0x100;
	memcpy(before_afe, afe, sizeof afe); memcpy(before_gpio, gpio, sizeof gpio); memcpy(before_topck, topck, sizeof topck);
	assert(nac_mic_afe_up(&hw) == 0 && refused == 0 && nac_mode == NAC_MODE_MIC);
	/* untouched: PCM2, CONN17/18 (mic -> modem), CONN5 (modem -> AWB), AWB */
	assert(AFEW(0x53c) == before_afe[0x53c / 4] && AFEW(0x460) == before_afe[0x460 / 4] && AFEW(0x464) == before_afe[0x464 / 4]);
	assert(AFEW(0x044) == before_afe[0x44 / 4]);
	for (unsigned o = 0x85c; o <= 0x874; o += 4) assert(AFEW(o) == before_afe[o / 4]);
	/* the microphone: ADDA UL on, VUL12 mono 48 kHz at 0x11213800 (2 KiB), CONN21 bit 3, VUL12_ON only, DL1 kept */
	assert((AFEW(0x114) & 1) && (AFEW(0x124) & 1) && AFEW(0xe40) == 0x39 && AFEW(0xe00) == 0x10000 && nac_cal_state == CAL_START);
	assert(AFEW(0x37c) == 0x11213800u && AFEW(0x38c) == 0x11213fffu && AFEW(0x378) == 0 && AFEW(0x388) == 0);
	assert(((AFEW(0x374) >> 24) & 0xf) == 10 && (AFEW(0x374) & 0x901) == 0x100 && (AFEW(0x470) & 8));
	assert(AFEW(0x010) == (0x00000006u | 0x80000000u));
	assert(gpio[(0x100054a0u - 0x10004000u) / 4] == 0x11345678u && gpio[(0x100054b0u - 0x10004000u) / 4] == 0x87651111u);
	assert(nac_vul_rd == 0x100);                                         /* reading starts where the DMA is */

	/* 3. calibration (first use of the boot) runs from the worker's ticks exactly as in a call */
	for (int it = 0; it < 2000 && !nac_cal_done; it++) { nac_tick(&hw); if (it % 2) nord2_audio_owner_poll(); }
	assert(nac_cal_done && nac_cal_ok && nac_cal_phase[0] == 9 && nac_cal_phase[1] == 11 && nac_cal_phase[2] == 14);
	assert(AFEW(0xe00) == (0x10000u | (1u << 31)) && (topck[0x32c / 4] & 1) == 0);
	/* the uplink mute (Phone app) never wires ADDA UL into PCM2 while recording */
	nac_set_ul_mute(0, 1); nac_tick(&hw); nac_set_ul_mute(0, 0); nac_tick(&hw);
	assert(AFEW(0x460) == before_afe[0x460 / 4] && AFEW(0x464) == before_afe[0x464 / 4]);
	/* monitors still run (async) */
	nac_mon_next_ms = 0; nac_tick(&hw); nord2_audio_owner_poll(); nac_tick(&hw);
	assert(nac_cmon_reads >= 1);

	/* 4. VUL12 drain: 2 KiB ring, words of two samples, wrap */
	int16_t mono[1024];
	for (unsigned k = 0; k < 512; k++) AFEW(0x3800 + 4 * k) = ((uint32_t)(uint16_t)(-(int)(2 * k + 1)) << 16) | (uint16_t)(2 * k);
	AFEW(0x384) = 0x11213800u + 0x300;                                  /* 0x100 .. 0x300: 256 samples */
	unsigned n = nac_mic_take(&hw, mono, 1024);
	assert(n == 256 && mono[0] == 0x80 && mono[1] == -0x81 && mono[255] == -(0x80 + 255));
	assert(nac_mic_take(&hw, mono, 1024) == 0);
	AFEW(0x384) = 0x11213800u + 0x40;                                   /* wrapped: 0x300..0x800 + 0..0x40 */
	unsigned total = 0;
	while ((n = nac_mic_take(&hw, mono, 100)) != 0) { assert(n <= 100 && n % 2 == 0); total += n; }
	assert(total == (0x800 - 0x300 + 0x40) / 2 && nac_vul_rd == 0x40);
	assert(nac_mic_frames == 256 + total && nac_mic_peak == 1023);
	/* the AFE pointer outside the ring: nothing taken, nothing crashes */
	AFEW(0x384) = 0x11212000u; assert(nac_mic_take(&hw, mono, 1024) == 0); AFEW(0x384) = 0x11213840u;

	/* 5. exact restore */
	assert(nac_afe_down(&hw) == 0 && refused == 0);
	assert(nac_mic_take(&hw, mono, 1024) == 0);
	AFEW(0x384) = before_afe[0x384 / 4];
	memcpy(&afe[0x3000 / 4], &before_afe[0x3000 / 4], 0x1000);
	assert(memcmp(afe, before_afe, sizeof afe) == 0 && memcmp(gpio, before_gpio, sizeof gpio) == 0 && memcmp(topck, before_topck, sizeof topck) == 0);

	/* 6. MT6359 down through the async slot, after a monitor read left the slot DONE */
	(void)nac_async_poll(NULL);
	if (nac_as.state == AS_IDLE) assert(nac_async_submit(nac_cmon_ops, 2) == 0);   /* a monitor read queued */
	assert(nac_as.state == AS_QUEUED && nac_mic_pmic_submit(0) == -1);         /* busy: the worker tries next pass */
	nord2_audio_owner_poll(); assert(nac_as.state == AS_DONE);                  /* DONE, nobody polled it */
	assert(nac_mic_pmic_submit(0) == 0);
	nord2_audio_owner_poll();
	assert(nac_mic_pmic_poll(&err) == 1 && err == 0 && owner_calls == 0);
	assert(pmic[P_DCXO_CW12] == 0x8000 && pmic[P_LDO_VAUD18_CON0] == 0 && (pmic[P_AUDDEC_ANA_CON13] & 0x10));
	assert(pmic[P_AUDIO_TOP_CON0] == 0x00ff && pmic[P_AUD_TOP_CKPDN_CON0] == 0x0066 && pmic[P_AUDENC_ANA_CON0] == 0);
	assert(pmic[P_AFE_AUD_PAD_TOP] == 0x3000 && pmic[P_AUDIO_DIG_CFG] == ((11 << 8) | 9));   /* phases kept for later */

	/* 7. a call after the recording: the call AFE as before (PCM2, CONN17/18, AWB, both DAC_CON0 bits), no calibration */
	assert(nac_pmic_up() == 0 && owner_calls == 1);
	assert(nac_afe_up(&hw) == 0 && nac_mode == NAC_MODE_CALL && nac_cal_state == CAL_OFF);
	assert(AFEW(0x53c) == 0x5f && (AFEW(0x460) & 8) && (AFEW(0x464) & 8) && (AFEW(0x044) & (1u << 14)));
	assert((AFEW(0x010) & 0x80010000u) == 0x80010000u && AFEW(0x864) == 0x11213000u);
	nac_set_ul_mute(0, 1); nac_tick(&hw); assert(!(AFEW(0x460) & 8)); nac_set_ul_mute(0, 0); nac_tick(&hw); assert(AFEW(0x460) & 8);
	assert(nac_mic_take(&hw, mono, 1024) == 0);                         /* the recording path is off during a call */
	assert(nac_afe_down(&hw) == 0 && nac_pmic_down() == 0);
	printf("all 0320 microphone (audio_call.c) host tests passed\n");
	return 0;
}
