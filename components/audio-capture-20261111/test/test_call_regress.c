/* Host test for bsd/dev/audio_call.c (0280): speech message bytes, MD message parsing/acks, AWB reader, MT6359 op
 * tables through a fake PWRAP, AFE save/restore and allowlist coverage. */
#include "host_kernel.h"
#include <assert.h>
#include <stdlib.h>
#include "../src/bsd/dev/audio_call.c"

/* ---- kernel stubs ---- */
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

/* ---- fake MT6359 behind n2pmif ---- */
static uint16_t pmic[0x4000];
static unsigned pm_writes, pm_reads;
bool n2pmif_init(struct n2pmif *p, enum n2pmif_kind k, const struct n2pmif_ops *o, void *c)
{ memset(p, 0, sizeof *p); p->kind = k; p->ops = *o; p->ctx = c; return true; }
enum n2pmif_error n2pmif_pwrap_read(struct n2pmif *p, uint16_t a, uint16_t *v) { (void)p; pm_reads++; *v = pmic[a]; return N2PMIF_OK; }
enum n2pmif_error n2pmif_pwrap_write(struct n2pmif *p, uint16_t a, uint16_t v)
{
	(void)p; pm_writes++;
	/* SET/CLR aliases of GPIO_MODE2..4 */
	if (a == P_GPIO_MODE2_SET || a == P_GPIO_MODE3_SET || a == P_GPIO_MODE4_SET) pmic[a - 2] |= v;
	else if (a == P_GPIO_MODE2_CLR || a == P_GPIO_MODE3_CLR || a == P_GPIO_MODE4_CLR) pmic[a - 4] &= (uint16_t)~v;
	else pmic[a] = v;
	if (a == P_LDO_VAUD18_CON0) pmic[P_LDO_VAUD18_MON] = v & 1;   /* the LDO follows its enable */
	return N2PMIF_OK;
}
bool n2owner_ready(const struct n2owner *q) { (void)q; return true; }
void n2owner_poll(struct n2owner *q) { (void)q; }
bool n2owner_call(struct n2owner *q, int (*fn)(void *), void *arg, const struct n2owner_wait *w, unsigned ms, int *r)
{ (void)q; (void)w; (void)ms; *r = fn(arg); return true; }

/* ---- fake AFE behind nah_io ---- */
static uint32_t afe[0x4000 / 4], gpio[0x4000 / 4], topck[0x1000 / 4];
static unsigned afe_writes, refused;
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
	if (pa == 0x10000330u) {                     /* CKSYS_AUD_TOP_MON: test done when CFG bit0, cycles by codec phase */
		unsigned ph = pmic[0x23e4] & 0x7f; cal_mon_reads++;
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
	uint32_t *s = slot(pa); if (!s) return -1; *s = v; afe_writes++; return 0;
}

static void dump(const uint8_t *b, unsigned n) { for (unsigned i = 0; i < n; i++) printf("%02x%s", b[i], (i % 16 == 15) ? "\n" : " "); if (n % 16) printf("\n"); }

int main(void)
{
	uint8_t pkt[256];
	unsigned n;

	/* 1. MD alive (the q10cm log: 04035c34 0000003a afa0002a | 001a1234 00010020 ...) -> 0x2FA0 ack queued */
	uint8_t alive[58] = {0};
	nac_w32(alive, 0x04035c34); nac_w32(alive + 4, 58); nac_w32(alive + 8, 4); nac_w32(alive + 12, 0xafa0002a);
	nac_w32(alive + 16, 0x001a1234); nac_w32(alive + 20, 0x00010020);
	for (unsigned i = 26; i < 58; i++) alive[i] = (uint8_t)i;
	nac_ccci_rx(alive, sizeof alive);
	assert(nac_md_alive == 1 && nac_md_caps[0] == 26);
	n = nac_ccci_take(pkt, sizeof pkt);
	assert(n == 16 && nac_r32(pkt) == 0xffffffff && nac_r32(pkt + 4) == 0x2fa00000 && nac_r32(pkt + 8) == 5 && nac_r32(pkt + 12) == 0);
	/* mailbox 0xAF70 (no ack) */
	uint8_t mb[16]; nac_w32(mb, 0xffffffff); nac_w32(mb + 4, 0xaf700000); nac_w32(mb + 8, 4); nac_w32(mb + 12, 0);
	nac_ccci_rx(mb, 16);
	assert(nac_ccci_take(pkt, sizeof pkt) == 0);

	/* 2. SPH_ON payload: HAL layout (speechMessageToCcciMessage): {0, total, 5, size+6 | msg<<16} {a2a2, 19, 128} info */
	nac_sph_on();
	n = nac_ccci_take(pkt, sizeof pkt);
	printf("SPH_ON (%u bytes):\n", n); dump(pkt, 32);
	assert(n == 16 + 6 + 128);
	assert(nac_r32(pkt) == 0 && nac_r32(pkt + 4) == n && nac_r32(pkt + 8) == 5);
	assert(nac_r16(pkt + 12) == 134 && nac_r16(pkt + 14) == 0x2f20);
	assert(nac_r16(pkt + 16) == 0xa2a2 && nac_r16(pkt + 18) == 0x19 && nac_r16(pkt + 20) == 128);
	assert(pkt[22 + 2] == 3 && pkt[22 + 3] == 0 && pkt[22 + 5] == 0 && pkt[22 + 0x54] == 2);
	/* ack 0xAF20 */
	nac_w32(mb + 4, 0xaf200000); nac_ccci_rx(mb, 16); assert(nac_on_acks == 1);
	nac_sph_off();
	n = nac_ccci_take(pkt, sizeof pkt);
	assert(n == 16 && nac_r32(pkt + 4) == 0x2f210000);
	/* queue overflow is counted, never blocks */
	for (int i = 0; i < 12; i++) nac_sph_off();
	assert(nac_tx_fail == 4);
	while (nac_ccci_take(pkt, sizeof pkt)) {}
	/* force=2 sends nothing */
	nac_force = 2; nac_sph_on(); nac_sph_off(); assert(nac_ccci_take(pkt, sizeof pkt) == 0); nac_force = 0;

	/* 3. requests, gains (hardware ceilings), route, volume keys */
	assert(nac_top == NAC_TOP_CAP);                                   /* the earpiece starts at its ceiling */
	nac_set_route(NAC_ROUTE_SPEAKER); nac_set_ul_mute(0, 1); assert(nac_ul_mute == 1);   /* HAL before the call: media speaker */
	assert(!nac_wanted()); nac_request(1, 3); assert(nac_wanted()); nac_enable = 0; assert(!nac_wanted()); nac_enable = 1;
	assert(nac_route == NAC_ROUTE_HANDSET && nac_ul_mute == 0);     /* every call starts on the earpiece, unmuted */
	nac_set_route(NAC_ROUTE_SPEAKER); nac_request(1, 3); assert(nac_route == NAC_ROUTE_SPEAKER);   /* in-call choice kept */
	nac_set_route(NAC_ROUTE_HANDSET); nac_top = 64;
	unsigned t, b; nac_gains(&t, &b); assert(t == NAC_TOP_CAP && b == NAC_BOTTOM_CAP);
	nac_phase = 0; nac_volume_key(1); assert(nac_top == 64);                  /* outside a call: ignored */
	nac_phase = 5; nac_volume_key(1); assert(nac_top == 90 && nac_bottom == NAH_DEFAULT_BOTTOM_GAIN_Q15);
	for (int k = 0; k < 20; k++) nac_volume_key(1);
	assert(nac_top == NAC_TOP_CAP);
	for (int k = 0; k < 30; k++) nac_volume_key(2);
	assert(nac_top == 8);
	nac_top = 70; nac_volume_key(2); assert(nac_top == 64); nac_volume_key(1); assert(nac_top == 90); nac_top = 64;
	nac_set_route(NAC_ROUTE_SPEAKER); nac_volume_key(1); assert(nac_top == 64 && nac_bottom == 8192); nac_bottom = NAH_DEFAULT_BOTTOM_GAIN_Q15;
	nac_set_route(NAC_ROUTE_HANDSET);
	/* iOS call volume (ioctl 'r' 3): receiver always, speaker only in a call; keys stop stepping once it arrives */
	nac_phase = 5;
	nac_set_call_volume(0, 0, 0); assert(nac_top == NAC_TOP_CAP);
	nac_set_call_volume(0, 48, 0); assert(nac_top == 512);
	nac_set_call_volume(0, 72, 0); assert(nac_top == 384);       /* 9 dB: linear within the 6 dB step */
	nac_set_call_volume(0, 8 * 60, 0); assert(nac_top == 8);
	nac_set_call_volume(0, 0, 1); assert(nac_top == 8);
	nac_set_call_volume(1, 96, 0); assert(nac_bottom == 4096);
	nac_volume_key(1); assert(nac_top == 8 && nac_bottom == 4096);   /* fallback off: both ports reported */
	nac_phase = 0; nac_set_call_volume(1, 0, 0); assert(nac_bottom == 4096);
	/* only the speaker port reported (q10dv): the keys still step the earpiece */
	nac_ios_volumes = 0; nac_ios_ports = 0; nac_phase = 5; nac_top = 64; nac_bottom = NAH_DEFAULT_BOTTOM_GAIN_Q15;
	nac_set_call_volume(1, 0, 0); assert(nac_bottom == NAC_BOTTOM_CAP);
	nac_volume_key(1); assert(nac_top == 90 && nac_bottom == NAC_BOTTOM_CAP);
	nac_set_route(NAC_ROUTE_SPEAKER); nac_volume_key(2); assert(nac_top == 90 && nac_bottom == NAC_BOTTOM_CAP); nac_set_route(NAC_ROUTE_HANDSET);
	nac_ios_volumes = 0; nac_ios_ports = 0; nac_phase = 0; nac_top = 64; nac_bottom = NAH_DEFAULT_BOTTOM_GAIN_Q15;

	/* 4. MT6359 up/down through the fake PWRAP: every op allowed, codec words as Android leaves them */
	pmic[P_HWCID] = 0x5930; pmic[P_AUDIO_TOP_CON0] = 0x00ff; pmic[P_AUD_TOP_CKPDN_CON0] = 0x0066;
	pmic[P_AUDDEC_ANA_CON13] = 0x0010; pmic[P_LDO_VAUD18_CON0] = 0; pmic[P_DCXO_CW12] = 0x8000;
	int rc = nac_pmic_up();
	assert(rc == 0);
	assert(pmic[P_DCXO_CW12] == 0xa000 && pmic[P_LDO_VAUD18_CON0] == 1 && (pmic[P_AUDDEC_ANA_CON13] & 0x10) == 0);
	assert((pmic[P_AUDIO_TOP_CON0] & 0xbf) == 0 && (pmic[P_AUD_TOP_CKPDN_CON0] & 6) == 0);
	assert(pmic[P_AUDENC_ANA_CON0] == (0x0040 | 0x4000 | 0x0001 | 0x0300 | 0x0002 | 0x1000));
	assert(pmic[P_AUDENC_ANA_CON15] == 0x0051 && pmic[P_AFE_ADDA_MTKAIF_CFG0] == 0x0210 && pmic[P_AFE_AUD_PAD_TOP] == 0x3900);
	assert(pmic[P_AFE_UL_SRC_CON0_L] == 1 && pmic[P_AFE_UL_DL_CON0] == 1 && pmic[P_GPIO_MODE2] == 0x0249);
	assert(pmic[P_GPIO_MODE3] == 0x0201 && pmic[P_GPIO_MODE4] == 0x0009 && pmic[P_AFE_DCCLK_CFG0] == 0x2061);
	assert(nac_slot[SL_R_PAD] == 0x3900 && nac_slot[SL_R_CON0] == pmic[P_AUDENC_ANA_CON0]);
	printf("mt6359 up: %u writes %u reads\n", pm_writes, pm_reads);
	struct nac_job j = { 0 }; assert(nac_pm_rmw(0x1c9c, 0x0001, 1, &j) == -100 && nac_pm_rmw(0x07a8, 0xffff, 0, &j) == -100);

	/* 5. AFE up (first CLK_P2 call: calibration runs), split SRAM, mic probe and route prescale */
	static struct nau_hw hw;
	hw.io.read = io_read; hw.io.write = io_write; hw.status.running = 1;
	for (unsigned i = 0; i < sizeof afe / 4; i++) afe[i] = 0x5a5a0000u ^ i;
	gpio[(0x100054a0u - 0x10004000u) / 4] = 0x00345678u; gpio[(0x100054b0u - 0x10004000u) / 4] = 0x87650000u;
	topck[0x32c / 4] = 0x00001230u;
	afe[0x86c / 4] = 0x11213040u; afe[0x384 / 4] = 0x11213800u;
	static uint32_t before_afe[sizeof afe / 4], before_gpio[sizeof gpio / 4], before_topck[sizeof topck / 4];
	memcpy(before_afe, afe, sizeof afe); memcpy(before_gpio, gpio, sizeof gpio); memcpy(before_topck, topck, sizeof topck);
	assert(nac_afe_up(&hw) == 0 && refused == 0);
	assert(afe[0x53c / 4] == 0x5f && afe[0xe40 / 4] == 0x39 && afe[0xe00 / 4] == 0x10000 && nac_cal_state == CAL_START);
	assert(afe[0x864 / 4] == 0x11213000u && afe[0x874 / 4] == 0x112137ffu && afe[0x37c / 4] == 0x11213800u && afe[0x38c / 4] == 0x11213fffu);
	assert(((afe[0x374 / 4] >> 24) & 0xf) == 10 && (afe[0x374 / 4] & 0x901) == 0x100 && (afe[0x470 / 4] & 8));
	assert((afe[0x10 / 4] & 0x80010000u) == 0x80010000u && afe[0x460 / 4] & 8 && afe[0x464 / 4] & 8 && afe[0x44 / 4] & (1u << 14));
	assert(gpio[(0x100054a0u - 0x10004000u) / 4] == 0x11345678u && gpio[(0x100054b0u - 0x10004000u) / 4] == 0x87651111u);
	/* calibration: worker ticks + input-thread polls interleaved */
	for (int it = 0; it < 2000 && !nac_cal_done; it++) { nac_tick(&hw); if (it % 2) nord2_audio_owner_poll(); }
	printf("calibration: done %u ok %u phases %d/%d/%d cycles %d/%d/%d tried %u timeouts %u mon reads %u\n", nac_cal_done, nac_cal_ok,
	    nac_cal_phase[0], nac_cal_phase[1], nac_cal_phase[2], nac_cal_cycle[0], nac_cal_cycle[1], nac_cal_cycle[2],
	    nac_cal_phases_tried, nac_cal_timeouts, cal_mon_reads);
	assert(nac_cal_done && nac_cal_ok && nac_cal_phase[0] == 9 && nac_cal_phase[1] == 11 && nac_cal_phase[2] == 14);
	assert(nac_cal_cycle[0] == 3 && nac_cal_cycle[1] == 3 && nac_cal_cycle[2] == 5);
	assert(pmic[P_AUDIO_DIG_CFG] == ((11 << 8) | 9) && pmic[P_AUDIO_DIG_CFG1] == 14 && pmic[P_AFE_AUD_PAD_TOP] == 0x3900);
	assert(afe[0xe00 / 4] == (0x10000u | (1u << 31)) && ((afe[0xe28 / 4] >> 8) & 1) == 0 && ((afe[0xe28 / 4] >> 12) & 0xf) == 0);
	assert((topck[0x32c / 4] & 1) == 0);
	/* uplink mute: CONN17/18 bit 3 off while muted, back on unmute; re-applied after a new nac_afe_up */
	nac_set_ul_mute(0, 1); nac_tick(&hw); assert(!(afe[0x460 / 4] & 8) && !(afe[0x464 / 4] & 8));
	nac_tick(&hw); nac_set_ul_mute(1, 0); nac_tick(&hw); assert((afe[0x460 / 4] & 8) && (afe[0x464 / 4] & 8) && nac_ul_mutes == 3);
	/* codec monitors are submitted after calibration */
	nac_mon_next_ms = 0; nac_tick(&hw); nord2_audio_owner_poll(); nac_tick(&hw);
	assert(nac_cmon_reads >= 1 && nac_cmon[6] == pmic[P_AUDENC_ANA_CON0]);

	/* 6. rings: AWB (2 KiB, downlink with prescale) and VUL12 (2 KiB, mic statistics) */
	int16_t st[2 * 512];
	for (unsigned k = 0; k < 512; k++) afe[(0x3000 + 4 * k) / 4] = ((uint32_t)(uint16_t)(2 * k + 1) << 16) | (uint16_t)(2 * k);
	for (unsigned k = 0; k < 512; k++) afe[(0x3800 + 4 * k) / 4] = ((uint32_t)(uint16_t)(k == 7 ? -1200 : 3) << 16) | 2u;
	afe[0x86c / 4] = 0x11213040u + 0x100;                          /* 128 downlink samples */
	afe[0x384 / 4] = 0x11213800u + 0x200;                          /* 256 mic samples */
	nac_top = 1024; n = nac_dl_take(&hw, st, 512);                 /* top at the ceiling: unity prescale */
	assert(n == 128 && st[0] == 0x20 && st[1] == 0 && st[2] == 0x21 && st[254] == (int16_t)(0x20 + 127) && st[255] == 0);
	assert(nac_mic_frames == 256 && nac_mic_peak == 1200 && nac_mic_nz == 1);
	nac_top = 64; nac_set_route(NAC_ROUTE_BOTH);
	afe[(0x3140) / 4] = ((uint32_t)(uint16_t)-32768 << 16) | 16384u;
	afe[0x86c / 4] = 0x11213144u;
	n = nac_dl_take(&hw, st, 512);
	assert(n == 2 && st[0] == 16384 * 64 / 1024 && st[1] == (int16_t)(16384 * (NAH_DEFAULT_BOTTOM_GAIN_Q15 * 32768u / NAC_BOTTOM_CAP) / 32768));
	assert(st[2] == -32768 / 16);
	nac_set_route(NAC_ROUTE_HANDSET);
	afe[0x86c / 4] = 0x11213000u + 0x20;                           /* wrapped: 0x144..0x800 + 0..0x20 */
	unsigned total = 0;
	while ((n = nac_dl_take(&hw, st, 256)) != 0) total += n;
	assert(total == (0x800 - 0x144 + 0x20) / 2 && nac_awb_rd == 0x20);
	nac_print_diag();

	assert(nac_afe_down(&hw) == 0 && refused == 0);
	afe[0x86c / 4] = before_afe[0x86c / 4]; afe[0x384 / 4] = before_afe[0x384 / 4];
	memcpy(&afe[0x3000 / 4], &before_afe[0x3000 / 4], 0x1000);    /* SRAM contents are data, not configuration */
	assert(memcmp(afe, before_afe, sizeof afe) == 0 && memcmp(gpio, before_gpio, sizeof gpio) == 0 && memcmp(topck, before_topck, sizeof topck) == 0);
	assert(nac_dl_take(&hw, st, 256) == 0);
	assert(nac_pmic_down() == 0);
	assert(pmic[P_DCXO_CW12] == 0x8000 && pmic[P_LDO_VAUD18_CON0] == 0 && (pmic[P_AUDDEC_ANA_CON13] & 0x10));
	assert(pmic[P_AUDIO_TOP_CON0] == 0x00ff && pmic[P_AUD_TOP_CKPDN_CON0] == 0x0066 && pmic[P_AUDENC_ANA_CON0] == 0);
	assert(pmic[P_AUDENC_ANA_CON15] == 0 && pmic[P_AFE_UL_SRC_CON0_L] == 0 && pmic[P_AFE_AUD_PAD_TOP] == 0x3000);
	assert((pmic[P_AUDIO_DIG_CFG] & 0x8080) == 0 && pmic[P_AUDIO_DIG_CFG] == ((11 << 8) | 9));   /* phases kept, loopback off */
	/* second call of the boot: no calibration, CLK_P2 RX directly */
	afe[0x86c / 4] = 0x11213040u; afe[0x384 / 4] = 0x11213800u;
	assert(nac_pmic_up() == 0 && pmic[P_AUDIO_DIG_CFG] == ((11 << 8) | 9) && pmic[P_AUDIO_DIG_CFG1] == 14);
	assert(nac_afe_up(&hw) == 0 && nac_cal_state == CAL_OFF && afe[0xe00 / 4] == (0x10000u | (1u << 31)) && afe[0xe40 / 4] == 0x39);
	assert(nac_afe_down(&hw) == 0 && nac_pmic_down() == 0);
	/* protocol 2 without calibration (aif 1) */
	nac_aif = 1; assert(nac_pmic_up() == 0 && pmic[P_AFE_AUD_PAD_TOP] == 0x3100 && pmic[P_AUDIO_DIG_CFG] == 0);
	assert(nac_afe_up(&hw) == 0 && afe[0xe40 / 4] == 0x31 && afe[0xe00 / 4] == 0x10000 && nac_cal_state == CAL_OFF);
	assert(nac_afe_down(&hw) == 0 && nac_pmic_down() == 0);
	printf("afe: %u writes, restore exact; all call-audio host tests passed\n", afe_writes);
	return 0;
}
