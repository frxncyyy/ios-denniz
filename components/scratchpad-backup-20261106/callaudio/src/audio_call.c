/* SPDX-License-Identifier: MIT
 * Nord2 CS call audio (patch 0280): what Android's MT6893 audio HAL + ASoC do for a handset voice call, reduced to
 * one fixed route.
 *
 * Modem (MediaTek speech messenger, vendor audio.primary.mt6893.so SpeechDriverNormal/SpeechMessengerNormal,
 * disassembled offline; Android kernel eccci port_cfg.c "ccci_aud" = CCCI_PCM_TX 5 / CCCI_PCM_RX 4, queue 0,
 * PORT_F_USER_HEADER):
 *   mailbox  {0xFFFFFFFF, msg<<16|param16, ch 5, param32}                       (16 bytes)
 *   payload  {0, total, ch 5, msg<<16|(size+6)} + {0xA2A2, type, size} + data   (AP->MD; MD->AP uses 0x1234 and a
 *            10-byte payload header)
 *   MSG_A2M_SPH_ON 0x2F20 payload type 0x19 (sph_info_t, 128 bytes), ack 0xAF20; SPH_OFF 0x2F21 mailbox, ack 0xAF21;
 *   MD notifications that need an AP ack: 0xAF78/0xAF90/0xAFA0/0xAFB5 -> 0x2F78/0x2F90/0x2FA0/0x2FB5 (getSyncType()
 *   0x91); 0xAFA0 (type 0x1a, 32 bytes) is the "modem alive" message the HAL answers with 0x2FA0
 *   (processModemAlive).  sph_info: app 0, rate enum 3 = 48000 (sph_sample_rate_value_to_enum), opendsp 0, no
 *   speech-parameter buffer (valid 0), smartPA config 2 (dual).  A normal (non-BT) call runs the modem PCM at 48 kHz
 *   (AudioALSASpeechPhoneCallController::open).
 * AFE (Android kernel sound/soc/mediatek/mt6885, audio_device.xml MIC1_MD1_TO_I2S3 / MD1_TO_*):
 *   PCM 2 = MD1 (mt6885-dai-pcm.c: PCM2_INTF_CON 0x53c, AFIFO, 32 BCK, MODE_B, 48K=3);
 *   mic: ADDA_UL_CH1 -> PCM_2_PB_CH1/CH2 (AFE_CONN17/18 bit 3); ADDA UL 48 kHz, IIR 35 Hz (mt6885-dai-adda.c
 *   hw_params), MTKAIF protocol 2 (no phase calibration), AUD_PAD_TOP 0x30 + RX FIFO, aud_adc gate
 *   (AUDIO_TOP_CON0 bit 24, clk-mt6885-audsys.c);
 *   downlink: PCM_2_CAP_CH1 -> AWB ch1 (AFE_CONN5 bit 14), AWB mono 16-bit 48 kHz into AFE SRAM 0x11213000 (4 KiB,
 *   the block after DL1's).  The worker copies it into the existing DL1 -> I2S3 -> TFA9874 path, so the call is
 *   heard through the calibrated software gain (Android uses the smart-PA receiver profile; the amps here run one
 *   fixed profile, so the PCM2 capture is NOT wired straight into I2S3 at 0 dB).
 *   AP pins GPIO214/215 AUD_CLK/SYNC_MOSI, 216/217 AUD_DAT_MOSI0/1, 218/219 AUD_DAT_MISO0/1 -> function 1 (mt6893.dts).
 * MT6359P codec (sound/soc/codecs/mt6359.c DAPM order for builtin_Mic_Mic1: AIN0 -> PGA_L -> ADC_L -> UL_SRC ->
 *   MISO0, mic type DCC (no "mediatek,mic-type" in the Nord2 DT), MICBIAS0 2.5 V (OPLUS), PGA 18 dB; register
 *   addresses from include/linux/mfd/mt6359p/registers.h).  MT6359 access goes through the input thread, the sole
 *   PWRAP SWINF2 owner (the same n2owner queue pattern Nord2GPU uses).
 * Ear safety: the handset route plays only the top speaker (the earpiece); its level kern.n2call.top (default 64 = -54 dBFS
 *   gain, 12 dB below the music calibration of that speaker); speaker route uses kern.n2call.bottom.
 *
 * v3 (q10cs: downlink works, uplink silent): MTKAIF now as Android runs it -- protocol 2 with CLK_P2 and the boot-time
 *   phase calibration (mt6885-mt6359.c mt6885_mt6359_mtkaif_calibration, done here once per boot at the first call,
 *   asynchronously so the 2 ms audio worker never stalls); kern.n2call.aif 1/2 = the previous protocol 2 / protocol 1.
 *   Uplink probe: ADDA_UL_CH1 is also captured by VUL12 into AFE SRAM 0x11213800 (AWB keeps 0x11213000, 2 KiB each),
 *   so the in-call status shows whether the mic reaches the AFE (mic peak) independently of the modem.
 *   MT6359 read-back after power-up and periodic codec MTKAIF monitors.  Call volume: hardware gains start at a fixed
 *   ceiling and the per-route level is a Q15 prescale of the modem PCM, stepped 3 dB by the volume keys during a call
 *   (iOS's call volume never reaches this kernel path); NSA ioctl 'r' 2 selects the route (for the HAL / speakerphone).
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <kern/clock.h>
#include <kern/locks.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <libkern/libkern.h>
#include "audio_hw.h"
#include "audio_call.h"
#include "nord2_gpu_owner.h"
#include "nord2_gpu_pmif.h"

extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
/* bb side (sweet_amfi.c, 0277 + the 0281 hunk): hook storage, NULL until nac_init */
extern void (*nord2_call_ccci_rx_hook)(const uint8_t *, uint32_t);
extern unsigned (*nord2_call_ccci_take_hook)(uint8_t *, unsigned);
extern void (*nord2_call_state_hook)(unsigned, unsigned);

/* ---- tunables (kern.n2call.*) ---- */
#define NAC_TOP_CAP 1024u                /* handset hardware gain ceiling (Q15): 12 dB over the top speaker's music level */
#define NAC_BOTTOM_CAP 16384u            /* speaker hardware gain ceiling */
static const uint16_t nac_top_steps[] = { 8, 11, 16, 23, 32, 45, 64, 90, 128, 181, 256, 362, 512, 724, 1024 };   /* 3 dB */
static const uint16_t nac_bottom_steps[] = { 724, 1024, 1448, 2048, 2896, 4096, 5793, 8192, 11585, 16384 };
static unsigned nac_enable = 1;          /* 0: calls stay silent (no speech/AFE/codec activity) */
static unsigned nac_ul_mute;             /* 1: the far end hears silence (iOS mute); reset at every call start */
static unsigned nac_route = NAC_ROUTE_HANDSET;
static unsigned nac_top = NAC_TOP_CAP;   /* handset level (Q15, <= NAC_TOP_CAP); q10ct users stepped it to the ceiling */
static unsigned nac_bottom = NAH_DEFAULT_BOTTOM_GAIN_Q15;   /* speaker level (<= NAC_BOTTOM_CAP) */
static unsigned nac_pga = 3;             /* MT6359 PGA L: 0..5 = 0..30 dB (6 dB steps; ul_pga_gain_map) */
static unsigned nac_force;               /* 1: act as in a call; 2: AFE+codec only, no modem messages */
static unsigned nac_aif;                 /* MTKAIF: 0 protocol 2 CLK_P2 + calibration (Android), 1 protocol 2, 2 protocol 1 */
static unsigned nac_bridge_want, nac_bridge_mode, nac_requests;
static unsigned nac_md_alive, nac_on_acks, nac_off_acks, nac_rx_msgs, nac_tx_msgs, nac_tx_fail, nac_ack_sent;
static unsigned nac_last_ack_p16, nac_phase, nac_dl_frames, nac_dl_peak, nac_dl_calls;
static unsigned nac_mic_peak, nac_mic_nz, nac_mic_frames, nac_vol_keys;
static int nac_last_rc;
static uint32_t nac_ul_mon, nac_ul_mon1, nac_aif_mon, nac_aif_mon1, nac_awb_cur, nac_vul_cur;
static uint64_t nac_on_sent_ms, nac_active_ms;
static uint8_t nac_md_caps[32];
/* calibration result (once per boot) */
static int nac_cal_phase[3] = { -1, -1, -1 }, nac_cal_cycle[3];
static unsigned nac_cal_state, nac_cal_done, nac_cal_ok, nac_cal_phases_tried, nac_cal_timeouts;
/* diagnostics printed by the modem thread */
static uint16_t nac_cmon[12];            /* MT6359 monitors, read asynchronously during the call */
static unsigned nac_cmon_reads;
static uint32_t nac_afe_rb[16];          /* AP AFE words after nac_afe_up */
static unsigned nac_afe_rb_valid, nac_afe_rb_printed;
static void nac_print_diag(void);

static int
nac_sysctl_gain(__unused struct sysctl_oid *oidp, void *arg1, int arg2, struct sysctl_req *req)
{
	unsigned v = *(unsigned *)arg1;
	int e = sysctl_handle_int(oidp, &v, 0, req);
	if (e || !req->newptr) return e;
	if (v > (unsigned)arg2) return EINVAL;   /* never above the route's ceiling */
	*(unsigned *)arg1 = v;
	return 0;
}
static int
nac_sysctl_small(__unused struct sysctl_oid *oidp, void *arg1, int arg2, struct sysctl_req *req)
{
	unsigned v = *(unsigned *)arg1;
	int e = sysctl_handle_int(oidp, &v, 0, req);
	if (e || !req->newptr) return e;
	if (v > (unsigned)arg2) return EINVAL;
	*(unsigned *)arg1 = v;
	return 0;
}
SYSCTL_NODE(_kern, OID_AUTO, n2call, CTLFLAG_RW | CTLFLAG_LOCKED, 0, "Nord2 call audio");
SYSCTL_PROC(_kern_n2call, OID_AUTO, enable, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, &nac_enable, 1, nac_sysctl_small, "IU", "");
SYSCTL_PROC(_kern_n2call, OID_AUTO, route, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, &nac_route, 2, nac_sysctl_small, "IU",
    "0 handset (top), 1 speaker (bottom), 2 both");
SYSCTL_PROC(_kern_n2call, OID_AUTO, top, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, &nac_top, NAC_TOP_CAP, nac_sysctl_gain, "IU", "");
SYSCTL_PROC(_kern_n2call, OID_AUTO, bottom, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, &nac_bottom, NAC_BOTTOM_CAP, nac_sysctl_gain, "IU", "");
SYSCTL_PROC(_kern_n2call, OID_AUTO, pga, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, &nac_pga, 5, nac_sysctl_small, "IU", "");
SYSCTL_PROC(_kern_n2call, OID_AUTO, force, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, &nac_force, 2, nac_sysctl_small, "IU", "");
SYSCTL_PROC(_kern_n2call, OID_AUTO, aif, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, &nac_aif, 2, nac_sysctl_small, "IU",
    "0 protocol 2 CLK_P2 + calibration, 1 protocol 2, 2 protocol 1 (next call)");
SYSCTL_PROC(_kern_n2call, OID_AUTO, ul_mute, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, &nac_ul_mute, 1, nac_sysctl_small, "IU", "");
SYSCTL_UINT(_kern_n2call, OID_AUTO, phase, CTLFLAG_RD | CTLFLAG_LOCKED, &nac_phase, 0, "");
SYSCTL_UINT(_kern_n2call, OID_AUTO, md_alive, CTLFLAG_RD | CTLFLAG_LOCKED, &nac_md_alive, 0, "");
SYSCTL_UINT(_kern_n2call, OID_AUTO, on_acks, CTLFLAG_RD | CTLFLAG_LOCKED, &nac_on_acks, 0, "");
SYSCTL_UINT(_kern_n2call, OID_AUTO, dl_frames, CTLFLAG_RD | CTLFLAG_LOCKED, &nac_dl_frames, 0, "");
SYSCTL_UINT(_kern_n2call, OID_AUTO, mic_frames, CTLFLAG_RD | CTLFLAG_LOCKED, &nac_mic_frames, 0, "");
SYSCTL_UINT(_kern_n2call, OID_AUTO, mic_peak, CTLFLAG_RD | CTLFLAG_LOCKED, &nac_mic_peak, 0, "");
SYSCTL_UINT(_kern_n2call, OID_AUTO, cal_ok, CTLFLAG_RD | CTLFLAG_LOCKED, &nac_cal_ok, 0, "");
SYSCTL_UINT(_kern_n2call, OID_AUTO, acks_sent, CTLFLAG_RD | CTLFLAG_LOCKED, &nac_ack_sent, 0, "");
SYSCTL_UINT(_kern_n2call, OID_AUTO, on_ack_p16, CTLFLAG_RD | CTLFLAG_LOCKED, &nac_last_ack_p16, 0, "");
SYSCTL_INT(_kern_n2call, OID_AUTO, last_rc, CTLFLAG_RD | CTLFLAG_LOCKED, &nac_last_rc, 0, "");

static uint64_t
nac_ms(void)
{
	uint64_t ns = 0;
	absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
	return ns / 1000000ull;
}

/* ------------------------------------------------------------------------------------------------ requests */
unsigned
nac_wanted(void)
{
	unsigned f = __atomic_load_n(&nac_force, __ATOMIC_RELAXED);
	if (!__atomic_load_n(&nac_enable, __ATOMIC_RELAXED)) return 0;
	return f ? 1u : (__atomic_load_n(&nac_bridge_want, __ATOMIC_ACQUIRE) ? 1u : 0u);
}

void
nac_request(unsigned want, unsigned mode)
{
	unsigned old = __atomic_exchange_n(&nac_bridge_want, want ? 1u : 0u, __ATOMIC_ACQ_REL);
	__atomic_store_n(&nac_bridge_mode, mode, __ATOMIC_RELAXED);
	if (old != (want ? 1u : 0u)) {
		nac_requests++;
		if (want) {
			/* Every call starts on the earpiece, unmuted: a route the HAL sent before the call (the media/ringtone
			 * speaker) is not the call's. iOS's own choices during the call arrive afterwards (ioctls 'r' 2/'r' 4). */
			__atomic_store_n(&nac_route, NAC_ROUTE_HANDSET, __ATOMIC_RELEASE);
			__atomic_store_n(&nac_ul_mute, 0u, __ATOMIC_RELEASE);
		}
		printf("NORD2 CALL: request %s (call mode %u, #%u, enable %u route %u)\n", want ? "speech ON" : "speech OFF", mode,
		    nac_requests, nac_enable, nac_route);
	}
}

/* Hardware gains of a call: the fixed ceilings; the audible level is the prescale applied in nac_dl_take. */
void
nac_gains(unsigned *top, unsigned *bottom)
{
	*top = NAC_TOP_CAP;
	*bottom = NAC_BOTTOM_CAP;
}

/* route from the HAL (ioctl) or sysctl: takes effect at once (prescale only) */
void
nac_set_route(unsigned route)
{
	if (route > NAC_ROUTE_BOTH) return;
	if (__atomic_exchange_n(&nac_route, route, __ATOMIC_ACQ_REL) != route) {
		printf("NORD2 CALL: route %s (top %u bottom %u)\n", route == NAC_ROUTE_HANDSET ? "handset" : route == NAC_ROUTE_SPEAKER ?
		    "speaker" : "both", nac_top, nac_bottom);
	}
}

static unsigned
nac_step(const uint16_t *steps, unsigned n, unsigned cur, int dir)
{
	unsigned i = 0;
	while (i + 1 < n && steps[i + 1] <= cur) i++;               /* largest step <= cur */
	if (dir > 0 && i + 1 < n) i++;
	else if (dir < 0 && i > 0 && steps[i] >= cur) i--;
	return steps[i];
}

/* iOS's call volume, from the HAL plug-in (ioctl 'r' 3): the volume control of the port the call plays on, in eighths of a dB
 * under its top (the plug-in's 48 dB scale).  port 0 = receiver (handset level), 1 = speaker (speaker level, only taken while a
 * call plays: outside calls that control carries the media volume, which iOS keeps at its top).  The top of the scale is the
 * route's ceiling; muted = the quietest step. */
static unsigned nac_ios_volumes, nac_ios_ports, nac_ios_below[2] = { ~0u, ~0u };   /* ports: bit 0 receiver, bit 1 speaker */
SYSCTL_UINT(_kern_n2call, OID_AUTO, ios_volumes, CTLFLAG_RD | CTLFLAG_LOCKED, &nac_ios_volumes, 0, "");

static unsigned
nac_level(unsigned cap, unsigned below, unsigned muted, unsigned floor)
{
	/* cap * 10^(-below/160) with an exact 3 dB-per-24 table: halve every 48 eighths (6.02 dB), interpolate in Q15 */
	uint64_t g = (uint64_t)cap << 16;
	if (muted || below >= 8 * 60) return floor;
	while (below >= 48) { g >>= 1; below -= 48; }
	g -= g * below / 96;                     /* linear within the 6 dB step (<0.5 dB error) */
	unsigned v = (unsigned)(g >> 16);
	return v < floor ? floor : v;
}

void
nac_set_call_volume(unsigned port, unsigned below, unsigned muted)
{
	if (port > 1) return;
	if (port == 1 && nac_phase != 5) return;
	nac_ios_volumes++;
	__atomic_or_fetch(&nac_ios_ports, 1u << port, __ATOMIC_RELAXED);
	nac_ios_below[port] = muted ? ~0u : below;
	if (port == 0) nac_top = nac_level(NAC_TOP_CAP, below, muted, 8);
	else nac_bottom = nac_level(NAC_BOTTOM_CAP, below, muted, 724);
	printf("NORD2 CALL: iOS call volume %s %u.%u dB under the top%s -> top %u bottom %u\n", port ? "speaker" : "receiver", below / 8,
	    (below % 8) * 125 / 100, muted ? " (muted)" : "", nac_top, nac_bottom);
}

/* input thread (ni_thread key edges): 1 = volume up, 2 = volume down; only while a call plays and only until iOS's call
 * volume arrives from the HAL (fallback for a plug-in without the call-volume ioctl) */
void
nac_volume_key(unsigned key)
{
	int dir = key == 1 ? 1 : key == 2 ? -1 : 0;
	if (!dir || nac_phase != 5) return;
	/* a port whose call volume iOS sends (ioctl 'r' 3) follows iOS only; the keys still step a port iOS never reported */
	unsigned r = nac_route, ios = __atomic_load_n(&nac_ios_ports, __ATOMIC_RELAXED);
	if (r == NAC_ROUTE_SPEAKER ? (ios & 2u) : r == NAC_ROUTE_HANDSET ? (ios & 1u) : (ios == 3u)) return;
	if (r != NAC_ROUTE_SPEAKER && !(ios & 1u)) nac_top = nac_step(nac_top_steps, sizeof nac_top_steps / sizeof nac_top_steps[0], nac_top, dir);
	if (r != NAC_ROUTE_HANDSET && !(ios & 2u)) nac_bottom = nac_step(nac_bottom_steps, sizeof nac_bottom_steps / sizeof nac_bottom_steps[0], nac_bottom, dir);
	nac_vol_keys++;
	printf("NORD2 CALL: volume %s -> top %u bottom %u (route %u)\n", dir > 0 ? "up" : "down", nac_top, nac_bottom, r);
}

/* Uplink mute (Phone app mute button): from the HAL's input mute control (ioctl 'r' 4) or the bb bridge.  Applied by the
 * worker in nac_tick by disconnecting ADDA_UL_CH1 from PCM_2_PB (AFE_CONN17/18 bit 3): the modem then sends silence. */
static unsigned nac_ul_mute_applied, nac_ul_mutes;
SYSCTL_UINT(_kern_n2call, OID_AUTO, ul_mutes, CTLFLAG_RD | CTLFLAG_LOCKED, &nac_ul_mutes, 0, "");
void
nac_set_ul_mute(unsigned source, unsigned on)
{
	on = on ? 1u : 0u;
	if (__atomic_exchange_n(&nac_ul_mute, on, __ATOMIC_ACQ_REL) != on) {
		nac_ul_mutes++;
		printf("NORD2 CALL: uplink %s (from the %s, call phase %u)\n", on ? "MUTED" : "unmuted", source ? "baseband" : "HAL", nac_phase);
	}
}

void
nac_note(int phase, int rc)
{
	nac_phase = (unsigned)phase;
	nac_last_rc = rc;
	if (phase == 5) nac_active_ms = nac_ms();
}

/* ------------------------------------------------------------------------------------- CCCI speech messages */
#define NAC_TXQ 8
#define NAC_PKT 176
static LCK_GRP_DECLARE(nac_grp, "nord2-call");
static LCK_MTX_DECLARE(nac_txq_mtx, &nac_grp);
static struct { uint16_t len; uint64_t t_ms; uint8_t b[NAC_PKT]; } nac_txq[NAC_TXQ];
static unsigned nac_txh, nac_txt;

static void
nac_w32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static void
nac_w16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}
static uint32_t
nac_r32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint16_t
nac_r16(const uint8_t *p)
{
	return (uint16_t)(p[0] | p[1] << 8);
}

static int
nac_queue(const uint8_t *b, unsigned len)
{
	int rc = -1;
	if (len > NAC_PKT) return -1;
	lck_mtx_lock(&nac_txq_mtx);
	if (nac_txh - nac_txt < NAC_TXQ) {
		unsigned i = nac_txh % NAC_TXQ;
		memcpy(nac_txq[i].b, b, len);
		nac_txq[i].len = (uint16_t)len;
		nac_txq[i].t_ms = nac_ms();
		nac_txh++;
		rc = 0;
	}
	lck_mtx_unlock(&nac_txq_mtx);
	if (rc) nac_tx_fail++;
	return rc;
}

static int
nac_mailbox(uint16_t msg, uint16_t p16, uint32_t p32)
{
	uint8_t m[16];
	nac_w32(m, 0xFFFFFFFFu);                 /* port_char: header-only packet -> data[0] = CCCI_MAGIC_NUM */
	nac_w32(m + 4, (uint32_t)msg << 16 | p16);
	nac_w32(m + 8, 5);                       /* CCCI_PCM_TX; sweet_ring_tx adds seq/assert bits */
	nac_w32(m + 12, p32);
	return nac_queue(m, 16);
}

static int
nac_payload(uint16_t msg, uint16_t type, const void *data, uint16_t size)
{
	uint8_t m[NAC_PKT];
	unsigned total = 16u + 6u + size;
	if (total > sizeof m) return -1;
	nac_w32(m, 0);
	nac_w32(m + 4, total);                   /* port_char rewrites data[1] = written length */
	nac_w32(m + 8, 5);
	nac_w16(m + 12, (uint16_t)(size + 6));   /* SpeechMessengerNormal::speechMessageToCcciMessage */
	nac_w16(m + 14, msg);
	nac_w16(m + 16, 0xA2A2);
	nac_w16(m + 18, type);
	nac_w16(m + 20, size);
	memcpy(m + 22, data, size);
	return nac_queue(m, total);
}

void
nac_sph_on(void)
{
	uint8_t info[128];                       /* sph_info_t, configSpeechInfo() layout */
	memset(info, 0, sizeof info);
	info[0] = 0;                             /* application: normal */
	info[1] = 0;                             /* bt_info */
	info[2] = 3;                             /* sample rate enum: 48000 */
	info[3] = 0;                             /* opendsp_flag: the modem DSP does the speech enhancement */
	info[4] = 0;                             /* param path */
	info[5] = 0;                             /* param emi valid: no parameter buffer written */
	info[0x54] = 2;                          /* smartPA config: dual */
	if (__atomic_load_n(&nac_force, __ATOMIC_RELAXED) == 2) return;
	nac_on_sent_ms = nac_ms();
	(void)nac_payload(0x2F20, 0x19, info, sizeof info);
}

void
nac_sph_off(void)
{
	if (__atomic_load_n(&nac_force, __ATOMIC_RELAXED) == 2) return;
	(void)nac_mailbox(0x2F21, 0, 0);
}

/* modem thread: one CCCI_PCM_RX packet (ccci_header + payload) */
void
nac_ccci_rx(const uint8_t *p, uint32_t len)
{
	uint32_t d0, d1, resv;
	uint16_t msg, p16 = 0, type = 0, size = 0;
	if (!p || len < 16) return;
	d0 = nac_r32(p); d1 = nac_r32(p + 4); resv = nac_r32(p + 12);
	if (d0 == 0xFFFFFFFFu) {
		msg = (uint16_t)(d1 >> 16);
		p16 = (uint16_t)d1;
	} else {
		msg = (uint16_t)(resv >> 16);
		if (len >= 26) { type = nac_r16(p + 18); size = nac_r16(p + 20); }
	}
	nac_rx_msgs++;
	if (nac_rx_msgs <= 48) {
		printf("NORD2 CALL: md msg 0x%04x %s p16 0x%x p32 0x%x type 0x%x size %u (len %u)\n", msg,
		    d0 == 0xFFFFFFFFu ? "mailbox" : "payload", p16, d0 == 0xFFFFFFFFu ? resv : 0, type, size, len);
	}
	switch (msg) {
	case 0xAFA0:                             /* modem alive + capabilities (32 bytes) */
		nac_md_alive++;
		if (len >= 26u + sizeof nac_md_caps) memcpy(nac_md_caps, p + 26, sizeof nac_md_caps);
		/* FALLTHROUGH */
	case 0xAF78: case 0xAF90: case 0xAFB5:    /* notifications that need the AP's ack (sync type 0x91) */
		if (nac_mailbox((uint16_t)(msg & 0x7FFF), 0, 0) == 0) nac_ack_sent++;
		break;
	case 0xAF20:
		nac_on_acks++;
		nac_last_ack_p16 = p16;
		printf("NORD2 CALL: SPH_ON ack p16 0x%x after %llu ms\n", p16, (unsigned long long)(nac_ms() - nac_on_sent_ms));
		break;
	case 0xAF21:
		nac_off_acks++;
		printf("NORD2 CALL: SPH_OFF ack p16 0x%x\n", p16);
		break;
	default:
		break;
	}
}

/* modem thread: next packet to send on queue 0 (0 = nothing); also the in-call status line (the worker never prints
 * while DMA runs) */
unsigned
nac_ccci_take(uint8_t *pkt, unsigned cap)
{
	static uint64_t next_ms;
	static unsigned last_phase;
	unsigned n = 0, depth = 0, phase = nac_phase;
	uint64_t now = nac_ms(), queued_ms = 0;
	/* status: at every phase change and every 5 s while not idle; the counters are printed even when zero */
	if (phase != last_phase || (phase != 0 && now >= next_ms)) {
		next_ms = now + 5000;
		printf("NORD2 CALL: phase %u (was %u, rc %d) %llu s: dl %u frames peak %u (reads %u) awb %x ul_mon %x aif_mon %x md msgs %u alive %u acks on %u off %u, ap tx %u queue-full %u\n",
		    phase, last_phase, nac_last_rc, phase == 5 ? (unsigned long long)((now - nac_active_ms) / 1000) : 0ull, nac_dl_frames,
		    nac_dl_peak, nac_dl_calls, nac_awb_cur, nac_ul_mon, nac_aif_mon, nac_rx_msgs, nac_md_alive, nac_on_acks, nac_off_acks,
		    nac_tx_msgs, nac_tx_fail);
		last_phase = phase;
		nac_dl_peak = 0;
		if (phase != 0) nac_print_diag();
	}
	if (!pkt) return 0;
	lck_mtx_lock(&nac_txq_mtx);
	if (nac_txh != nac_txt) {
		unsigned i = nac_txt % NAC_TXQ;
		if (nac_txq[i].len <= cap) {
			n = nac_txq[i].len;
			memcpy(pkt, nac_txq[i].b, n);
			queued_ms = now - nac_txq[i].t_ms;
		}
		nac_txt++;
		depth = nac_txh - nac_txt;
	}
	lck_mtx_unlock(&nac_txq_mtx);
	if (n) {
		nac_tx_msgs++;
		if (nac_tx_msgs <= 200) {
			printf("NORD2 CALL: ap msg 0x%04x len %u (queued %llu ms ago, %u more) -> modem thread\n",
			    n == 16 ? (unsigned)(nac_r32(pkt + 4) >> 16) : nac_r16(pkt + 14), n, (unsigned long long)queued_ms, depth);
		}
	}
	return n;
}

/* --------------------------------------------------------------------------------- MT6359 (input thread) */
enum { PM_RMW = 1, PM_W, PM_DLY, PM_POLL, PM_SAVE, PM_RESTORE, PM_READ, PM_MON };
struct nac_pop { uint8_t kind, slot; uint16_t addr, mask, value, arg; };
#define RMW(a, m, v) { PM_RMW, 0, (a), (m), (v), 0 }
#define RMWS(s, a, m) { PM_RMW, (s), (a), (m), 0, 0 }   /* value computed at run time from slot s (see nac_pm_value) */
#define WR(a, v) { PM_W, 0, (a), 0xffff, (v), 0 }
#define DLY(us) { PM_DLY, 0, 0, 0, 0, (us) }
#define POLL(a, m, v, n) { PM_POLL, 0, (a), (m), (v), (n) }
#define SAVE(s, a) { PM_SAVE, (s), (a), 0xffff, 0, 0 }
#define RESTORE(s, a, m) { PM_RESTORE, (s), (a), (m), 0, 0 }
#define READ(s, a) { PM_READ, (s), (a), 0xffff, 0, 0 }
#define MON(s, a) { PM_MON, (s), (a), 0xffff, 0, 0 }
enum { SL_ID, SL_VAUD18, SL_CW12, SL_MODE2, SL_MODE3, SL_MODE4, SL_TOP0, SL_CK0, SL_CON0, SL_CON15,
       SL_R_CON0, SL_R_CON5, SL_R_CON15, SL_R_CON23, SL_R_CK0, SL_R_TOP0, SL_R_ULDL, SL_R_SRCL, SL_R_AIFCFG, SL_R_PAD,
       SL_R_MODE2, SL_R_MODE3, SL_R_MODE4, SL_R_DEC13, SL_R_CW12, SL_R_VAUDMON, SL_R_DIG, SL_R_DIG1, SL_N };
enum { V_PGA = 1, V_AIFCFG, V_PAD1, V_PAD2, V_DIG, V_DIG1 };   /* RMWS value sources */
/* MT6359P addresses (mt6359p/registers.h) */
#define P_HWCID 0x0008
#define P_GPIO_DIR0 0x0088
#define P_GPIO_DIR1 0x008e
#define P_GPIO_MODE2 0x00d8
#define P_GPIO_MODE2_SET 0x00da
#define P_GPIO_MODE2_CLR 0x00dc
#define P_GPIO_MODE3 0x00de
#define P_GPIO_MODE3_SET 0x00e0
#define P_GPIO_MODE3_CLR 0x00e2
#define P_GPIO_MODE4 0x00e4
#define P_GPIO_MODE4_SET 0x00e6
#define P_GPIO_MODE4_CLR 0x00e8
#define P_DCXO_CW12 0x07a8
#define P_LDO_VAUD18_CON0 0x1c9a
#define P_LDO_VAUD18_MON 0x1c9e
#define P_AUD_TOP_CKPDN_CON0 0x230c
#define P_AFE_UL_DL_CON0 0x2388
#define P_AFE_UL_SRC_CON0_L 0x238e
#define P_AUDIO_TOP_CON0 0x2396
#define P_AFE_MON_DEBUG0 0x2398
#define P_AFE_ADDA_MTKAIF_MON0 0x23be
#define P_AFE_ADDA_MTKAIF_MON1 0x23c0
#define P_AFE_ADDA_MTKAIF_MON2 0x23c2
#define P_AFE_ADDA_MTKAIF_CFG0 0x23ca
#define P_AFE_DCCLK_CFG0 0x23e0
#define P_AFE_DCCLK_CFG1 0x23e2
#define P_AUDIO_DIG_CFG 0x23e4
#define P_AUDIO_DIG_CFG1 0x23e6
#define P_AFE_AUD_PAD_TOP 0x23e8
#define P_AFE_AUD_PAD_TOP_MON 0x23ea
#define P_AFE_AUD_PAD_TOP_MON1 0x23ec
#define P_AFE_MTKAIF_MUX_CFG 0x23fa
#define P_AUDENC_ANA_CON0 0x2508
#define P_AUDENC_ANA_CON5 0x2512
#define P_AUDENC_ANA_CON15 0x2526
#define P_AUDENC_ANA_CON23 0x2536
#define P_AUDDEC_ANA_CON13 0x25a2

/* up: DAPM supply order (SUPPLY_SEQ_*), then the PGA/ADC/MTKAIF/UL_SRC widgets; values from mt6359.c */
static const struct nac_pop nac_up[] = {
	READ(SL_ID, P_HWCID),
	SAVE(SL_VAUD18, P_LDO_VAUD18_CON0), SAVE(SL_CW12, P_DCXO_CW12), SAVE(SL_MODE2, P_GPIO_MODE2),
	SAVE(SL_MODE3, P_GPIO_MODE3), SAVE(SL_MODE4, P_GPIO_MODE4), SAVE(SL_TOP0, P_AUDIO_TOP_CON0),
	SAVE(SL_CK0, P_AUD_TOP_CKPDN_CON0), SAVE(SL_CON0, P_AUDENC_ANA_CON0), SAVE(SL_CON15, P_AUDENC_ANA_CON15),
	RMW(P_DCXO_CW12, 0x2000, 0x2000),                    /* CLK_BUF: RG_XO_AUDIO_EN_M */
	RMW(P_LDO_VAUD18_CON0, 0x0001, 0x0001),              /* vaud18 */
	POLL(P_LDO_VAUD18_MON, 0x0001, 0x0001, 50),
	RMW(P_AUDDEC_ANA_CON13, 0x0010, 0x0000),             /* AUDGLB: RG_AUDGLB_PWRDN_VA32 = 0 */
	WR(P_GPIO_MODE2_CLR, 0x0ffe), WR(P_GPIO_MODE2_SET, 0x0249),   /* playback_gpio_set: clk/data mosi */
	WR(P_GPIO_MODE3_CLR, 0x0006), WR(P_GPIO_MODE3_SET, 0x0001),   /* sync mosi */
	WR(P_GPIO_MODE3_CLR, 0x0e00), WR(P_GPIO_MODE3_SET, 0x0200),   /* capture_gpio_set: miso */
	WR(P_GPIO_MODE4_CLR, 0x003f), WR(P_GPIO_MODE4_SET, 0x0009),
	RMW(P_AUDENC_ANA_CON23, 0x0002, 0x0000),             /* CLKSQ: internal DCXO source */
	RMW(P_AUDENC_ANA_CON23, 0x0001, 0x0001),             /* RG_CLKSQ_EN */
	RMW(P_AUDENC_ANA_CON5, 0x0001, 0x0001),              /* ADC_CLKGEN 6.5 MHz: RSTB, source 0, sel 0, genmode 1 */
	RMW(P_AUDENC_ANA_CON5, 0x0018, 0x0000),
	RMW(P_AUDENC_ANA_CON5, 0x0006, 0x0000),
	RMW(P_AUDENC_ANA_CON5, 0x0060, 0x0020),
	RMW(P_AUD_TOP_CKPDN_CON0, 0x0004, 0x0000),           /* AUDIF_CK */
	RMW(P_AUD_TOP_CKPDN_CON0, 0x0002, 0x0000), DLY(250), /* AUD_CK */
	RMW(P_AFE_DCCLK_CFG0, 0xfff7, 0x2062), RMW(P_AFE_DCCLK_CFG0, 0xfff7, 0x2060),   /* DCC_CLK 50k (DCC mic) */
	RMW(P_AFE_DCCLK_CFG0, 0xfff7, 0x2061), WR(P_AFE_DCCLK_CFG1, 0x0100),
	RMW(P_AUDENC_ANA_CON15, 0xff00, 0x0000),             /* MIC_BIAS_0: DCC (not ECM), 2.5 V, normal power */
	RMW(P_AUDENC_ANA_CON15, 0x0070, 0x0050),
	RMW(P_AUDENC_ANA_CON15, 0x0004, 0x0000),
	RMW(P_AUDENC_ANA_CON15, 0x0001, 0x0001),
	RMW(P_AUDIO_TOP_CON0, 0x003f, 0x0000),               /* AUDIO_TOP ADC/ADDA6/I2S_DL/PWR_CLK/TESTMODEL/RESERVED */
	RMW(P_AUDIO_TOP_CON0, 0x0080, 0x0000), DLY(250),     /* AUDIO_TOP_AFE_CTL */
	RMW(P_AFE_UL_DL_CON0, 0x0001, 0x0001),               /* AFE_ON */
	RMW(P_AFE_MTKAIF_MUX_CFG, 0x000f, 0x0000),           /* MISO0/1 = UL1_CH1 */
	RMW(P_AFE_UL_SRC_CON0_L, 0x0002, 0x0000),            /* UL_SRC_MUX = AMIC */
	RMW(P_AUDENC_ANA_CON0, 0x00c0, 0x0040),              /* PGA_L_Mux = AIN0 */
	RMW(P_AUDENC_ANA_CON0, 0x6000, 0x4000),              /* ADC_L_Mux = Left Preamplifier */
	RMW(P_AUDENC_ANA_CON0, 0x0004, 0x0004),              /* PGA_L_EN pre: DCC precharge */
	RMW(P_AUDENC_ANA_CON0, 0x0001, 0x0001),              /* RG_AUDPREAMPLON */
	RMWS(V_PGA, P_AUDENC_ANA_CON0, 0x0700),              /* PGA gain (kern.n2call.pga) */
	RMW(P_AUDENC_ANA_CON0, 0x0002, 0x0002),              /* DCCEN */
	RMW(P_AUDENC_ANA_CON0, 0x1000, 0x1000), DLY(120),    /* ADC_L_EN: RG_AUDADCLPWRUP */
	RMW(P_AUDENC_ANA_CON0, 0x0004, 0x0000),              /* precharge off */
	RMWS(V_DIG, P_AUDIO_DIG_CFG, 0x7f7f),                /* MTKAIF phases (calibration result, 0 before) */
	RMWS(V_DIG1, P_AUDIO_DIG_CFG1, 0x007f),
	RMWS(V_AIFCFG, P_AFE_ADDA_MTKAIF_CFG0, 0xffff),      /* MTKAIF_TX: 0x0210 (protocol 2), 0 (protocol 1) */
	RMWS(V_PAD1, P_AFE_AUD_PAD_TOP, 0xff00),             /* TX FIFO: 0x3800 then 0x3900 (CLK_P2) or 0x3100 */
	RMWS(V_PAD2, P_AFE_AUD_PAD_TOP, 0xff00),
	RMW(P_AFE_UL_SRC_CON0_L, 0x0001, 0x0001),            /* UL_SRC */
	/* read-back for the log */
	READ(SL_R_CON0, P_AUDENC_ANA_CON0), READ(SL_R_CON5, P_AUDENC_ANA_CON5), READ(SL_R_CON15, P_AUDENC_ANA_CON15),
	READ(SL_R_CON23, P_AUDENC_ANA_CON23), READ(SL_R_CK0, P_AUD_TOP_CKPDN_CON0), READ(SL_R_TOP0, P_AUDIO_TOP_CON0),
	READ(SL_R_ULDL, P_AFE_UL_DL_CON0), READ(SL_R_SRCL, P_AFE_UL_SRC_CON0_L), READ(SL_R_AIFCFG, P_AFE_ADDA_MTKAIF_CFG0),
	READ(SL_R_PAD, P_AFE_AUD_PAD_TOP), READ(SL_R_MODE2, P_GPIO_MODE2), READ(SL_R_MODE3, P_GPIO_MODE3),
	READ(SL_R_MODE4, P_GPIO_MODE4), READ(SL_R_DEC13, P_AUDDEC_ANA_CON13), READ(SL_R_CW12, P_DCXO_CW12),
	READ(SL_R_VAUDMON, P_LDO_VAUD18_MON), READ(SL_R_DIG, P_AUDIO_DIG_CFG), READ(SL_R_DIG1, P_AUDIO_DIG_CFG1),
};
static const struct nac_pop nac_down[] = {
	RMW(P_AFE_UL_SRC_CON0_L, 0x0001, 0x0000),
	RMW(P_AFE_AUD_PAD_TOP, 0xff00, 0x3000),              /* mtkaif_tx_disable */
	RMW(P_AUDIO_DIG_CFG, 0x8080, 0x0000), RMW(P_AUDIO_DIG_CFG1, 0x0080, 0x0000),   /* no calibration loopback left on */
	RMW(P_AUDENC_ANA_CON0, 0x1000, 0x0000),
	RMW(P_AUDENC_ANA_CON0, 0x0002, 0x0000),
	RMW(P_AUDENC_ANA_CON0, 0x0001, 0x0000),
	RESTORE(SL_CON0, P_AUDENC_ANA_CON0, 0x77c7),         /* muxes, gain, precharge back to the saved word */
	RMW(P_AFE_UL_DL_CON0, 0x0001, 0x0000),
	RESTORE(SL_TOP0, P_AUDIO_TOP_CON0, 0x00bf),
	RESTORE(SL_CK0, P_AUD_TOP_CKPDN_CON0, 0x0006),
	RMW(P_AFE_DCCLK_CFG0, 0xfff7, 0x2060), RMW(P_AFE_DCCLK_CFG0, 0xfff7, 0x2062),
	RMW(P_AUDENC_ANA_CON15, 0x0001, 0x0000),
	RESTORE(SL_CON15, P_AUDENC_ANA_CON15, 0xffff),
	RMW(P_AUDENC_ANA_CON5, 0x0018, 0x0000), RMW(P_AUDENC_ANA_CON5, 0x0006, 0x0000),
	RMW(P_AUDENC_ANA_CON5, 0x0060, 0x0000), RMW(P_AUDENC_ANA_CON5, 0x0001, 0x0000),
	RMW(P_AUDENC_ANA_CON23, 0x0001, 0x0000),
	WR(P_GPIO_MODE3_CLR, 0x0e00), WR(P_GPIO_MODE4_CLR, 0x003f),   /* capture_gpio_reset */
	RMW(P_GPIO_DIR0, 0xe000, 0x0000), RMW(P_GPIO_DIR1, 0x0003, 0x0000),
	RMW(P_AUDDEC_ANA_CON13, 0x0010, 0x0010),
	RESTORE(SL_CW12, P_DCXO_CW12, 0x2000),
	RESTORE(SL_VAUD18, P_LDO_VAUD18_CON0, 0x0001),
};
/* calibration helpers (async, input thread) */
static const struct nac_pop nac_cmon_ops[] = {
	MON(0, P_AFE_MON_DEBUG0), MON(1, P_AFE_ADDA_MTKAIF_MON0), MON(2, P_AFE_ADDA_MTKAIF_MON1), MON(3, P_AFE_ADDA_MTKAIF_MON2),
	MON(4, P_AFE_AUD_PAD_TOP_MON), MON(5, P_AFE_AUD_PAD_TOP_MON1), MON(6, P_AUDENC_ANA_CON0), MON(7, P_AUDENC_ANA_CON15),
	MON(8, P_AFE_UL_SRC_CON0_L), MON(9, P_AFE_AUD_PAD_TOP), MON(10, P_AUDIO_DIG_CFG), MON(11, P_AFE_ADDA_MTKAIF_CFG0),
};

/* Only MT6359 audio words may ever be written here. */
static int
nac_pm_allowed(uint16_t a, uint16_t mask)
{
	if (a >= 0x2300 && a <= 0x25ff) return 1;            /* audio digital (AFE/AUDIO_TOP) + AUDENC/AUDDEC analog */
	switch (a) {
	case P_DCXO_CW12: return mask == 0x2000;
	case P_LDO_VAUD18_CON0: return mask == 0x0001;
	case P_GPIO_MODE2_SET: case P_GPIO_MODE2_CLR: case P_GPIO_MODE3_SET: case P_GPIO_MODE3_CLR:
	case P_GPIO_MODE4_SET: case P_GPIO_MODE4_CLR: return 1;
	case P_GPIO_DIR0: return mask == 0xe000;
	case P_GPIO_DIR1: return mask == 0x0003;
	default: return 0;
	}
}

#ifndef NAC_DMB_LD                              /* host tests define these empty */
#define NAC_DMB_LD() __asm__ volatile("dmb oshld" ::: "memory")
#define NAC_DMB_ST() __asm__ volatile("dmb oshst" ::: "memory")
#define NAC_DSB() __asm__ volatile("dsb osh" ::: "memory")
#endif
struct nac_bank { volatile uint32_t *p; };
static uint32_t
nac_bank_read(void *ctx, uint32_t off)
{
	struct nac_bank *b = ctx;
	if (!b->p || (off & 3) || off >= 0x1000) return 0;
	uint32_t v = b->p[off / 4];
	NAC_DMB_LD();
	return v;
}
static void
nac_bank_write(void *ctx, uint32_t off, uint32_t v)
{
	struct nac_bank *b = ctx;
	if (!b->p || (off & 3) || off >= 0x1000) return;
	NAC_DMB_ST();
	b->p[off / 4] = v;
	NAC_DSB();
}
static void
nac_bank_udelay(__unused void *ctx, unsigned us)
{
	delay(us);
}

static struct n2owner nac_owner;
static struct nac_bank nac_pwrap_bank;
static struct n2pmif nac_pm;
static unsigned nac_pm_ready;
static uint16_t nac_slot[SL_N];
struct nac_job { const struct nac_pop *ops; unsigned n, done, verify_miss; int error; uint16_t fail_addr; };

static uint16_t
nac_pm_value(const struct nac_pop *o)
{
	unsigned aif = nac_aif;
	int p0 = nac_cal_phase[0] < 0 ? 0 : nac_cal_phase[0], p1 = nac_cal_phase[1] < 0 ? 0 : nac_cal_phase[1];
	int p2 = nac_cal_phase[2] < 0 ? 0 : nac_cal_phase[2];
	switch (o->slot) {
	case V_PGA: return (uint16_t)((nac_pga > 5 ? 5 : nac_pga) << 8);
	case V_AIFCFG: return aif == 2 ? 0x0000 : 0x0210;
	case V_PAD1: return aif == 0 ? 0x3800 : 0x3100;
	case V_PAD2: return aif == 0 ? 0x3900 : 0x3100;
	case V_DIG: return aif == 0 ? (uint16_t)((p1 << 8) | p0) : 0;
	case V_DIG1: return aif == 0 ? (uint16_t)p2 : 0;
	default: return o->value;
	}
}

static int
nac_pm_rmw(uint16_t a, uint16_t mask, uint16_t value, struct nac_job *j)
{
	uint16_t cur = 0, want;
	if (!nac_pm_allowed(a, mask)) return -100;
	if (mask == 0xffff) {
		want = value;
	} else {
		if (n2pmif_pwrap_read(&nac_pm, a, &cur) != N2PMIF_OK) return -101;
		want = (uint16_t)((cur & ~mask) | (value & mask));
		if (want == cur) return 0;
	}
	if (n2pmif_pwrap_write(&nac_pm, a, want) != N2PMIF_OK) return -102;
	if (mask != 0xffff && n2pmif_pwrap_read(&nac_pm, a, &cur) == N2PMIF_OK && (cur & mask) != (want & mask)) j->verify_miss++;
	return 0;
}

/* runs on the input thread (n2owner_poll or the async slot) */
static int
nac_pm_run(void *arg)
{
	struct nac_job *j = arg;
	if (!nac_pm_ready) {
		static const struct n2pmif_ops ops = { nac_bank_read, nac_bank_write, nac_bank_udelay };
		if (!nac_pwrap_bank.p) nac_pwrap_bank.p = (volatile uint32_t *)ml_io_map(N2PMIF_PWRAP_BASE, 0x1000);
		if (!nac_pwrap_bank.p || !n2pmif_init(&nac_pm, N2PMIF_PWRAP, &ops, &nac_pwrap_bank)) return j->error = -110;
		nac_pm_ready = 1;
	}
	for (j->done = 0; j->done < j->n; j->done++) {
		const struct nac_pop *o = &j->ops[j->done];
		uint16_t v = 0;
		int e = 0;
		switch (o->kind) {
		case PM_RMW:
			e = nac_pm_rmw(o->addr, o->mask, nac_pm_value(o), j);
			break;
		case PM_W:
			e = nac_pm_rmw(o->addr, 0xffff, o->value, j);
			break;
		case PM_DLY:
			delay(o->arg);
			break;
		case PM_POLL:
			for (unsigned i = 0;; i++) {
				if (n2pmif_pwrap_read(&nac_pm, o->addr, &v) != N2PMIF_OK) { e = -103; break; }
				if ((v & o->mask) == o->value) break;
				if (i >= o->arg) { e = -104; break; }
				delay(100);
			}
			break;
		case PM_SAVE: case PM_READ:
			if (n2pmif_pwrap_read(&nac_pm, o->addr, &v) != N2PMIF_OK) e = -105;
			else nac_slot[o->slot] = v;
			break;
		case PM_MON:
			if (n2pmif_pwrap_read(&nac_pm, o->addr, &v) != N2PMIF_OK) e = -105;
			else if (o->slot < sizeof nac_cmon / sizeof nac_cmon[0]) nac_cmon[o->slot] = v;
			break;
		case PM_RESTORE:
			e = nac_pm_rmw(o->addr, o->mask, nac_slot[o->slot], j);
			break;
		default:
			e = -106;
		}
		if (e) {
			j->error = e;
			j->fail_addr = o->addr;
			if (o->kind != PM_POLL) return e;  /* a slow VAUD18 status is logged, not fatal */
			j->error = 0;
		}
	}
	return 0;
}

/* One asynchronous job slot: the worker queues, the input thread runs it at its next pass, the worker polls. */
#define NAC_ASYNC_OPS 12
enum { AS_IDLE, AS_QUEUED, AS_RUNNING, AS_DONE };
static struct { unsigned state; struct nac_job j; struct nac_pop ops[NAC_ASYNC_OPS]; } nac_as;

static int
nac_async_submit(const struct nac_pop *ops, unsigned n)
{
	if (__atomic_load_n(&nac_as.state, __ATOMIC_ACQUIRE) != AS_IDLE || n > NAC_ASYNC_OPS || !n2owner_ready(&nac_owner)) return -1;
	memcpy(nac_as.ops, ops, n * sizeof ops[0]);
	nac_as.j = (struct nac_job){ nac_as.ops, n, 0, 0, 0, 0 };
	__atomic_store_n(&nac_as.state, AS_QUEUED, __ATOMIC_RELEASE);
	return 0;
}
/* 1 = finished (slot idle again, result in *error), 0 = still pending */
static int
nac_async_poll(int *error)
{
	if (__atomic_load_n(&nac_as.state, __ATOMIC_ACQUIRE) != AS_DONE) return 0;
	if (error) *error = nac_as.j.error;
	__atomic_store_n(&nac_as.state, AS_IDLE, __ATOMIC_RELEASE);
	return 1;
}

void
nord2_audio_owner_poll(void)
{
	unsigned q = AS_QUEUED;
	n2owner_poll(&nac_owner);
	if (__atomic_compare_exchange_n(&nac_as.state, &q, AS_RUNNING, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		(void)nac_pm_run(&nac_as.j);
		__atomic_store_n(&nac_as.state, AS_DONE, __ATOMIC_RELEASE);
	}
}

static uint64_t
nac_wait_now(__unused void *ctx)
{
	return nac_ms();
}
static void
nac_wait_sleep(__unused void *ctx, unsigned ms)
{
	static int ev;
	uint64_t deadline;
	clock_interval_to_deadline(ms, NSEC_PER_MSEC, &deadline);
	assert_wait_deadline((event_t)&ev, THREAD_UNINT, deadline);
	thread_block(THREAD_CONTINUE_NULL);
}

static int
nac_pm_exec(const struct nac_pop *ops, unsigned n, const char *what)
{
	static const struct n2owner_wait w = { nac_wait_now, nac_wait_sleep, NULL };
	struct nac_job j = { ops, n, 0, 0, 0, 0 };
	int result = 0;
	if (!n2owner_ready(&nac_owner)) {
		printf("NORD2 CALL: mt6359 %s: input-thread PWRAP owner not polling (sweet_touch.c without 0280?)\n", what);
		return -120;
	}
	/* the async slot is idle between calls; never interleave its job with this one */
	for (unsigned i = 0; i < 100 && __atomic_load_n(&nac_as.state, __ATOMIC_ACQUIRE) != AS_IDLE; i++) {
		(void)nac_async_poll(NULL);
		nac_wait_sleep(NULL, 2);
	}
	if (!n2owner_call(&nac_owner, nac_pm_run, &j, &w, 400, &result)) {
		printf("NORD2 CALL: mt6359 %s: owner queue timeout\n", what);
		return -121;
	}
	printf("NORD2 CALL: mt6359 %s rc=%d ops %u/%u fail_addr %#x verify_miss %u id %04x aif %u cal %s/%d/%d/%d saved vaud18 %x cw12 %x top0 %x ck0 %x\n",
	    what, result ? result : j.error, j.done, n, j.fail_addr, j.verify_miss, nac_slot[SL_ID], nac_aif, nac_cal_done ? (nac_cal_ok ?
	    "ok" : "failed") : "none", nac_cal_phase[0], nac_cal_phase[1], nac_cal_phase[2], nac_slot[SL_VAUD18], nac_slot[SL_CW12],
	    nac_slot[SL_TOP0], nac_slot[SL_CK0]);
	if (ops == nac_up) {
		printf("NORD2 CALL: mt6359 readback: enc0 %04x enc5 %04x enc15 %04x enc23 %04x ckpdn %04x top0 %04x uldl %04x ulsrc %04x aifcfg %04x pad %04x mode2/3/4 %04x/%04x/%04x dec13 %04x cw12 %04x vaud18mon %04x dig %04x/%04x\n",
		    nac_slot[SL_R_CON0], nac_slot[SL_R_CON5], nac_slot[SL_R_CON15], nac_slot[SL_R_CON23], nac_slot[SL_R_CK0],
		    nac_slot[SL_R_TOP0], nac_slot[SL_R_ULDL], nac_slot[SL_R_SRCL], nac_slot[SL_R_AIFCFG], nac_slot[SL_R_PAD],
		    nac_slot[SL_R_MODE2], nac_slot[SL_R_MODE3], nac_slot[SL_R_MODE4], nac_slot[SL_R_DEC13], nac_slot[SL_R_CW12],
		    nac_slot[SL_R_VAUDMON], nac_slot[SL_R_DIG], nac_slot[SL_R_DIG1]);
	}
	return result ? result : j.error;
}

int
nac_pmic_up(void)
{
	return nac_pm_exec(nac_up, sizeof nac_up / sizeof nac_up[0], "uplink on");
}

int
nac_pmic_down(void)
{
	return nac_pm_exec(nac_down, sizeof nac_down / sizeof nac_down[0], "uplink off");
}

/* ------------------------------------------------------------------------------------ AFE (audio worker) */
#define A(o) (0x11210000u + (o))
#define AFE_TOP_CON0 A(0x000)
#define AFE_DAC_CON0 A(0x010)
#define AFE_CONN5 A(0x044)
#define AFE_ADDA_UL_SRC_CON0 A(0x114)
#define AFE_ADDA_TOP_CON0 A(0x120)
#define AFE_ADDA_UL_DL_CON0 A(0x124)
#define AFE_ADDA_UL_SRC_MON0 A(0x148)
#define AFE_ADDA_UL_SRC_MON1 A(0x14c)
#define AFE_ADDA_IIR_COEF_02_01 A(0x290)
#define AFE_VUL12_CON0 A(0x374)
#define AFE_VUL12_BASE_MSB A(0x378)
#define AFE_VUL12_BASE A(0x37c)
#define AFE_VUL12_CUR A(0x384)
#define AFE_VUL12_END_MSB A(0x388)
#define AFE_VUL12_END A(0x38c)
#define AFE_CONN17 A(0x460)
#define AFE_CONN18 A(0x464)
#define AFE_CONN21 A(0x470)
#define PCM2_INTF_CON A(0x53c)
#define AFE_AWB_CON0 A(0x85c)
#define AFE_AWB_BASE_MSB A(0x860)
#define AFE_AWB_BASE A(0x864)
#define AFE_AWB_CUR A(0x86c)
#define AFE_AWB_END_MSB A(0x870)
#define AFE_AWB_END A(0x874)
#define AFE_ADDA_MTKAIF_CFG0 A(0xe00)
#define AFE_ADDA_MTKAIF_RX_CFG0 A(0xe20)
#define AFE_ADDA_MTKAIF_RX_CFG2 A(0xe28)
#define AFE_ADDA_MTKAIF_MON0 A(0xe34)
#define AFE_ADDA_MTKAIF_MON1 A(0xe38)
#define AFE_AUD_PAD_TOP A(0xe40)
#define CKSYS_AUD_TOP_CFG 0x1000032cu            /* topckgen MTKAIF calibration test (mt6885-afe-clk.h) */
#define CKSYS_AUD_TOP_MON 0x10000330u
#define NAC_AWB_SRAM 0x11213000u                 /* AFE SRAM: DL1 owns 0x11212000; AWB (modem downlink) 2 KiB */
#define NAC_AWB_BYTES 0x800u
#define NAC_VUL_SRAM 0x11213800u                 /* VUL12 (mic probe) 2 KiB */
#define NAC_VUL_BYTES 0x800u
#define GPIO_MODE26 0x100054a0u                  /* GPIO208..215: 214 AUD_CLK_MOSI, 215 AUD_SYNC_MOSI */
#define GPIO_MODE27 0x100054b0u                  /* GPIO216..223: 216/217 AUD_DAT_MOSI0/1, 218/219 AUD_DAT_MISO0/1 */

static const uint32_t nac_afe_words[] = {
	AFE_TOP_CON0, AFE_DAC_CON0, AFE_CONN5, AFE_ADDA_UL_SRC_CON0, AFE_ADDA_TOP_CON0, AFE_ADDA_UL_DL_CON0,
	AFE_ADDA_IIR_COEF_02_01, AFE_ADDA_IIR_COEF_02_01 + 4, AFE_ADDA_IIR_COEF_02_01 + 8, AFE_ADDA_IIR_COEF_02_01 + 12,
	AFE_ADDA_IIR_COEF_02_01 + 16, AFE_CONN17, AFE_CONN18, PCM2_INTF_CON, AFE_AWB_CON0, AFE_AWB_BASE_MSB, AFE_AWB_BASE,
	AFE_AWB_END_MSB, AFE_AWB_END, AFE_ADDA_MTKAIF_CFG0, AFE_ADDA_MTKAIF_RX_CFG0, AFE_AUD_PAD_TOP, GPIO_MODE26, GPIO_MODE27,
	AFE_VUL12_CON0, AFE_VUL12_BASE_MSB, AFE_VUL12_BASE, AFE_VUL12_END_MSB, AFE_VUL12_END, AFE_CONN21, AFE_ADDA_MTKAIF_RX_CFG2,
	CKSYS_AUD_TOP_CFG,
};
#define NAC_AFE_N (sizeof nac_afe_words / sizeof nac_afe_words[0])
static uint32_t nac_afe_saved[NAC_AFE_N];
static unsigned nac_afe_saved_ok, nac_afe_live;
static uint32_t nac_awb_rd, nac_vul_rd;          /* byte offsets of the next unread sample */

int
nac_write_allowed(uint32_t pa)
{
	for (unsigned i = 0; i < NAC_AFE_N; i++) if (nac_afe_words[i] == pa) return 1;
	return 0;
}

static int
nac_rd(struct nau_hw *h, uint32_t pa, uint32_t *v)
{
	return h->io.read(h->cookie, pa, 4, v);
}
static int
nac_wr(struct nau_hw *h, uint32_t pa, uint32_t v)
{
	return h->io.write(h->cookie, pa, 4, v);
}
static int
nac_rmw(struct nau_hw *h, uint32_t pa, uint32_t mask, uint32_t value)
{
	uint32_t v = 0;
	if (nac_rd(h, pa, &v)) return -1;
	return nac_wr(h, pa, (v & ~mask) | (value & mask));
}

/* AP side of MTKAIF RX for the selected protocol (mt6885-dai-adda.c pad_top / mtkaif_cfg events) */
static int
nac_afe_mtkaif(struct nau_hw *h, unsigned calibrating)
{
	int e = 0;
	unsigned aif = nac_aif;
	e |= nac_wr(h, AFE_AUD_PAD_TOP, aif == 0 ? 0x38 : 0x30);
	e |= nac_rmw(h, AFE_AUD_PAD_TOP, 1u, 1u);                          /* RG_RX_FIFO_ON */
	if (aif == 0 && !calibrating) {
		int d0 = nac_cal_cycle[0], d1 = nac_cal_cycle[1];
		unsigned data, cycle;
		if (nac_cal_phase[0] < 0 || nac_cal_phase[1] < 0) { d0 = d1 = 0; }
		if (d0 >= d1) { data = 0; cycle = (unsigned)(d0 - d1); } else { data = 1; cycle = (unsigned)(d1 - d0); }
		e |= nac_wr(h, AFE_ADDA_MTKAIF_CFG0, 0x00010000u | (1u << 31));  /* protocol 2, RXIF_CLKINV_ADC */
		e |= nac_rmw(h, AFE_ADDA_MTKAIF_RX_CFG2, (1u << 8) | (0xfu << 12), (data << 8) | ((cycle & 0xf) << 12));
	} else {
		e |= nac_wr(h, AFE_ADDA_MTKAIF_CFG0, aif == 2 ? 0 : 0x00010000u);
	}
	return e;
}

int
nac_afe_up(struct nau_hw *h)
{
	int e = 0;
	if (!h || !h->status.running) return -1;
	for (unsigned i = 0; i < NAC_AFE_N; i++) if (nac_rd(h, nac_afe_words[i], &nac_afe_saved[i])) return -2;
	nac_afe_saved_ok = 1;
	nac_afe_live = 1;
	nac_ul_mute_applied = 0;                 /* CONN17/18 bit 3 just set: nac_tick re-applies a mute */
	/* AP pins: AUD_CLK/SYNC_MOSI, AUD_DAT_MOSI0/1, AUD_DAT_MISO0/1 = function 1 (mt6893.dts aud_*_on) */
	e |= nac_rmw(h, GPIO_MODE26, 0xff000000u, 0x11000000u);
	e |= nac_rmw(h, GPIO_MODE27, 0x0000ffffu, 0x00001111u);
	e |= nac_rmw(h, AFE_TOP_CON0, 1u << 24, 0);                       /* aud_adc clock */
	/* ADDA UL 48 kHz, IIR on (35 Hz high-pass coefficients), internal ADC, AMIC data mode */
	e |= nac_wr(h, AFE_ADDA_IIR_COEF_02_01, 0x00000000u);
	e |= nac_wr(h, AFE_ADDA_IIR_COEF_02_01 + 4, 0x00003FB8u);
	e |= nac_wr(h, AFE_ADDA_IIR_COEF_02_01 + 8, 0x3FB80000u);
	e |= nac_wr(h, AFE_ADDA_IIR_COEF_02_01 + 12, 0x3FB80000u);
	e |= nac_wr(h, AFE_ADDA_IIR_COEF_02_01 + 16, 0x0000C048u);
	e |= nac_wr(h, AFE_ADDA_UL_SRC_CON0, (3u << 17) | (1u << 10));
	e |= nac_rmw(h, AFE_ADDA_TOP_CON0, 1u, 0);
	e |= nac_rmw(h, AFE_ADDA_MTKAIF_RX_CFG0, 1u, 0);
	/* supplies in SUPPLY_SEQ order: ADDA_AFE_ON, AUD_PAD_TOP (+RX FIFO), MTKAIF_CFG, UL_ON */
	e |= nac_rmw(h, AFE_ADDA_UL_DL_CON0, 1u, 1u);
	nac_cal_state = (nac_aif == 0 && !nac_cal_done) ? 1 : 0;           /* first CLK_P2 call: calibrate (nac_tick) */
	e |= nac_afe_mtkaif(h, nac_cal_state != 0);
	e |= nac_rmw(h, AFE_ADDA_UL_SRC_CON0, 1u, 1u);
	/* modem PCM 2: AFIFO, 32 BCK, 48 kHz (mode 3), PCM mode B, then enable */
	e |= nac_rmw(h, PCM2_INTF_CON, 0xfffffffeu, 0x5eu);
	e |= nac_rmw(h, PCM2_INTF_CON, 1u, 1u);
	/* mic -> modem: ADDA_UL_CH1 into PCM_2_PB_CH1/CH2 */
	e |= nac_rmw(h, AFE_CONN17, 1u << 3, 1u << 3);
	e |= nac_rmw(h, AFE_CONN18, 1u << 3, 1u << 3);
	/* modem -> AWB: PCM_2_CAP_CH1 into UL2_CH1, AWB mono 16-bit 48 kHz */
	e |= nac_wr(h, AFE_AWB_BASE_MSB, 0);
	e |= nac_wr(h, AFE_AWB_END_MSB, 0);
	e |= nac_wr(h, AFE_AWB_BASE, NAC_AWB_SRAM);
	e |= nac_wr(h, AFE_AWB_END, NAC_AWB_SRAM + NAC_AWB_BYTES - 1u);
	e |= nac_rmw(h, AFE_AWB_CON0, (0xfu << 24) | (1u << 8) | 1u, (10u << 24) | (1u << 8));
	e |= nac_rmw(h, AFE_CONN5, 1u << 14, 1u << 14);
	/* mic probe: ADDA_UL_CH1 into UL1_CH1, VUL12 mono 16-bit 48 kHz */
	e |= nac_wr(h, AFE_VUL12_BASE_MSB, 0);
	e |= nac_wr(h, AFE_VUL12_END_MSB, 0);
	e |= nac_wr(h, AFE_VUL12_BASE, NAC_VUL_SRAM);
	e |= nac_wr(h, AFE_VUL12_END, NAC_VUL_SRAM + NAC_VUL_BYTES - 1u);
	e |= nac_rmw(h, AFE_VUL12_CON0, (0xfu << 24) | (1u << 11) | (1u << 8) | 1u, (10u << 24) | (1u << 8));
	e |= nac_rmw(h, AFE_CONN21, 1u << 3, 1u << 3);
	e |= nac_rmw(h, AFE_DAC_CON0, (1u << 16) | (1u << 31), (1u << 16) | (1u << 31));
	{
		uint32_t cur = 0;
		(void)nac_rd(h, AFE_AWB_CUR, &cur);
		nac_awb_rd = (cur >= NAC_AWB_SRAM && cur < NAC_AWB_SRAM + NAC_AWB_BYTES) ? (cur - NAC_AWB_SRAM) & ~3u : 0;
		cur = 0;
		(void)nac_rd(h, AFE_VUL12_CUR, &cur);
		nac_vul_rd = (cur >= NAC_VUL_SRAM && cur < NAC_VUL_SRAM + NAC_VUL_BYTES) ? (cur - NAC_VUL_SRAM) & ~3u : 0;
	}
	{
		static const uint32_t rb[16] = { PCM2_INTF_CON, AFE_CONN17, AFE_CONN18, AFE_CONN5, AFE_CONN21, AFE_ADDA_UL_SRC_CON0,
		    AFE_ADDA_UL_DL_CON0, AFE_AUD_PAD_TOP, AFE_ADDA_MTKAIF_CFG0, AFE_ADDA_MTKAIF_RX_CFG0, AFE_TOP_CON0, AFE_DAC_CON0,
		    GPIO_MODE26, GPIO_MODE27, AFE_VUL12_CON0, AFE_AWB_CON0 };
		for (unsigned i = 0; i < 16; i++) (void)nac_rd(h, rb[i], &nac_afe_rb[i]);
		nac_afe_rb_valid = 1;
		nac_afe_rb_printed = 0;
	}
	nac_dl_frames = nac_dl_peak = nac_dl_calls = nac_mic_frames = nac_mic_peak = nac_mic_nz = 0;
	return e ? -3 : 0;
}

int
nac_afe_down(struct nau_hw *h)
{
	int e = 0;
	if (!nac_afe_live) return 0;
	nac_afe_live = 0;
	if (nac_cal_state) { nac_cal_state = 0; }                         /* an unfinished calibration is retried next call */
	if (!h || !h->status.running || !nac_afe_saved_ok) return 0;  /* AUDIO MTCMOS already off: registers are gone */
	/* stop the sources first, then put every word back exactly */
	e |= nac_rmw(h, PCM2_INTF_CON, 1u, 0);
	e |= nac_rmw(h, AFE_DAC_CON0, (1u << 16) | (1u << 31), 0);
	e |= nac_rmw(h, AFE_ADDA_UL_SRC_CON0, 1u, 0);
	delay(125);                                                        /* 1/fs at 8 kHz before AFE off (mtk_adda_ul_event) */
	for (unsigned i = NAC_AFE_N; i-- > 0;) e |= nac_wr(h, nac_afe_words[i], nac_afe_saved[i]);
	return e ? -4 : 0;
}

/* ---- worker tick: MTKAIF calibration (first CLK_P2 call of the boot) and codec monitors, never blocking ---- */
enum { CAL_OFF, CAL_START, CAL_PHASE_WAIT, CAL_MEASURE, CAL_FINISH_WAIT };
static unsigned nac_cal_p, nac_cal_tries;
static int nac_cal_prev[3];
static uint64_t nac_mon_next_ms;

static void
nac_cal_apply_finish(struct nau_hw *h)
{
	struct nac_pop ops[8];
	unsigned n = 0;
	int p0 = nac_cal_phase[0] < 0 ? 0 : nac_cal_phase[0], p1 = nac_cal_phase[1] < 0 ? 0 : nac_cal_phase[1];
	int p2 = nac_cal_phase[2] < 0 ? 0 : nac_cal_phase[2];
	(void)nac_rmw(h, CKSYS_AUD_TOP_CFG, 1u, 0);
	ops[n++] = (struct nac_pop)RMW(P_AUDIO_DIG_CFG, 0xffff, (uint16_t)((p1 << 8) | p0));   /* loopbacks off, phases */
	ops[n++] = (struct nac_pop)RMW(P_AUDIO_DIG_CFG1, 0x00ff, (uint16_t)p2);
	ops[n++] = (struct nac_pop)RMW(P_AFE_AUD_PAD_TOP, 0xff00, 0x3800);                    /* TX FIFO restart */
	ops[n++] = (struct nac_pop)RMW(P_AFE_AUD_PAD_TOP, 0xff00, 0x3900);
	(void)nac_async_submit(ops, n);
}

void
nac_tick(struct nau_hw *h)
{
	int err = 0;
	uint64_t now;
	if (!nac_afe_live || !h) return;
	{
		unsigned m = __atomic_load_n(&nac_ul_mute, __ATOMIC_ACQUIRE);
		if (m != nac_ul_mute_applied) {
			int e = nac_rmw(h, AFE_CONN17, 1u << 3, m ? 0 : 1u << 3) | nac_rmw(h, AFE_CONN18, 1u << 3, m ? 0 : 1u << 3);
			if (!e) nac_ul_mute_applied = m;
			printf("NORD2 CALL: uplink %s applied (conn17/18 bit 3 %s, rc %d)\n", m ? "mute" : "unmute", m ? "off" : "on", e);
		}
	}
	switch (nac_cal_state) {
	case CAL_START: {                                                   /* codec loopback on (calibration_enable) */
		(void)nac_async_poll(NULL);                                       /* drain a finished monitor read */
		struct nac_pop ops[4] = { RMW(P_AUDIO_DIG_CFG, 0xffff, 0x8080), RMW(P_AUDIO_DIG_CFG1, 0x00ff, 0x0080) };
		if (nac_async_submit(ops, 2)) return;
		(void)nac_wr(h, CKSYS_AUD_TOP_CFG, 0x4);                          /* test type: synchronizer pulse */
		nac_cal_phase[0] = nac_cal_phase[1] = nac_cal_phase[2] = -1;
		nac_cal_p = 0; nac_cal_tries = 0; nac_cal_phases_tried = 0;
		nac_cal_state = CAL_PHASE_WAIT;
		return;
	}
	case CAL_PHASE_WAIT:                                                /* phase write done: measure */
		if (!nac_async_poll(&err)) return;
		if (err) { nac_cal_state = CAL_FINISH_WAIT; nac_cal_apply_finish(h); return; }
		(void)nac_rmw(h, CKSYS_AUD_TOP_CFG, 1u, 1u);
		nac_cal_tries = 0;
		nac_cal_state = CAL_MEASURE;
		/* FALLTHROUGH */
	case CAL_MEASURE: {
		uint32_t mon = 0;
		int c[3] = { -1, -1, -1 }, done = 0;
		for (unsigned i = 0; i < 4000; i++) {                             /* bounded: a few ms per worker pass */
			if (nac_rd(h, CKSYS_AUD_TOP_MON, &mon)) break;
			if (((mon >> 28) & 7u) == 7u) { done = 1; break; }
		}
		if (!done && ++nac_cal_tries < 8) return;                         /* keep the 2 ms cadence; try again next pass */
		if (done) {
			c[0] = (int)(mon & 0xf); c[1] = (int)((mon >> 4) & 0xf); c[2] = (int)((mon >> 8) & 0xf);
		} else {
			nac_cal_timeouts++;
		}
		(void)nac_rmw(h, CKSYS_AUD_TOP_CFG, 1u, 0);
		nac_cal_phases_tried++;
		if (nac_cal_p == 0) { nac_cal_prev[0] = c[0]; nac_cal_prev[1] = c[1]; nac_cal_prev[2] = c[2]; }
		for (unsigned k = 0; k < 3; k++) {
			if (c[k] != nac_cal_prev[k] && nac_cal_phase[k] < 0) { nac_cal_phase[k] = (int)nac_cal_p - 1; nac_cal_cycle[k] = nac_cal_prev[k]; }
		}
		if (!done || nac_cal_p >= 42 || (nac_cal_phase[0] >= 0 && nac_cal_phase[1] >= 0 && nac_cal_phase[2] >= 0)) {
			nac_cal_state = CAL_FINISH_WAIT;
			nac_cal_apply_finish(h);
			return;
		}
		nac_cal_p++;
		{
			struct nac_pop ops[2] = { RMW(P_AUDIO_DIG_CFG, 0x7f7f, (uint16_t)((nac_cal_p << 8) | nac_cal_p)),
			    RMW(P_AUDIO_DIG_CFG1, 0x007f, (uint16_t)nac_cal_p) };
			if (nac_async_submit(ops, 2)) { nac_cal_state = CAL_FINISH_WAIT; nac_cal_apply_finish(h); return; }
		}
		nac_cal_state = CAL_PHASE_WAIT;
		return;
	}
	case CAL_FINISH_WAIT:
		if (__atomic_load_n(&nac_as.state, __ATOMIC_ACQUIRE) == AS_IDLE) nac_cal_apply_finish(h);   /* submit was refused */
		if (!nac_async_poll(&err)) return;
		nac_cal_done = 1;
		nac_cal_ok = nac_cal_phase[0] >= 0 && nac_cal_phase[1] >= 0 && nac_cal_phase[2] >= 0;
		(void)nac_afe_mtkaif(h, 0);                                       /* AP: CLK_P2 RX with the measured delay */
		nac_cal_state = CAL_OFF;
		return;
	default:
		break;
	}
	now = nac_ms();
	if (now >= nac_mon_next_ms) {                                         /* codec monitors every 2 s (async) */
		int e2 = 0;
		(void)nac_async_poll(&e2);
		if (nac_async_submit(nac_cmon_ops, sizeof nac_cmon_ops / sizeof nac_cmon_ops[0]) == 0) {
			nac_cmon_reads++;
			nac_mon_next_ms = now + 2000;
		}
	} else {
		(void)nac_async_poll(&err);
	}
}

/* drain one capture ring; returns samples copied (mono) */
static unsigned
nac_ring_take(struct nau_hw *h, uint32_t cur_reg, uint32_t base, uint32_t bytes, uint32_t *rd, int16_t *out, unsigned max,
    uint32_t *cur_seen)
{
	uint32_t cur = 0, wr, avail, n, i;
	if (nac_rd(h, cur_reg, &cur) || cur < base || cur >= base + bytes) return 0;
	*cur_seen = cur;
	wr = (cur - base) & ~3u;
	avail = (wr + bytes - *rd) % bytes;
	n = avail / 2;
	if (n > max) n = max & ~1u;
	for (i = 0; i + 1 < n; i += 2) {
		uint32_t w = 0;
		if (nac_rd(h, base + *rd, &w)) break;
		*rd = (*rd + 4) % bytes;
		out[i] = (int16_t)(w & 0xffff);
		out[i + 1] = (int16_t)(w >> 16);
	}
	return i;
}

unsigned
nac_dl_take(struct nau_hw *h, int16_t *stereo, unsigned max_frames)
{
	int16_t mono[512];
	unsigned n, i, pt, pb, r;
	if (!nac_afe_live || !h || !stereo) return 0;
	nac_dl_calls++;
	if ((nac_dl_calls & 255u) == 1u) {
		(void)nac_rd(h, AFE_ADDA_UL_SRC_MON0, &nac_ul_mon);
		(void)nac_rd(h, AFE_ADDA_UL_SRC_MON1, &nac_ul_mon1);
		(void)nac_rd(h, AFE_ADDA_MTKAIF_MON0, &nac_aif_mon);
		(void)nac_rd(h, AFE_ADDA_MTKAIF_MON1, &nac_aif_mon1);
	}
	/* mic probe: consume VUL12, keep peak / nonzero statistics only */
	n = nac_ring_take(h, AFE_VUL12_CUR, NAC_VUL_SRAM, NAC_VUL_BYTES, &nac_vul_rd, mono, 512, &nac_vul_cur);
	for (i = 0; i < n; i++) {
		unsigned a = (unsigned)(mono[i] < 0 ? -mono[i] : mono[i]);
		if (a > nac_mic_peak) nac_mic_peak = a;
		if (a > 4) nac_mic_nz++;
	}
	nac_mic_frames += n;
	/* modem downlink */
	if (max_frames > 512) max_frames = 512;
	n = nac_ring_take(h, AFE_AWB_CUR, NAC_AWB_SRAM, NAC_AWB_BYTES, &nac_awb_rd, mono, max_frames, &nac_awb_cur);
	r = nac_route;
	pt = r == NAC_ROUTE_SPEAKER ? 0 : (nac_top >= NAC_TOP_CAP ? 32768u : nac_top * 32768u / NAC_TOP_CAP);
	pb = r == NAC_ROUTE_HANDSET ? 0 : (nac_bottom >= NAC_BOTTOM_CAP ? 32768u : nac_bottom * 32768u / NAC_BOTTOM_CAP);
	for (i = 0; i < n; i++) {
		int32_t s = mono[i];
		unsigned a = (unsigned)(s < 0 ? -s : s);
		if (a > nac_dl_peak) nac_dl_peak = a;
		stereo[2 * i] = (int16_t)(s * (int32_t)pt / 32768);
		stereo[2 * i + 1] = (int16_t)(s * (int32_t)pb / 32768);
	}
	nac_dl_frames += n;
	return n;
}

/* modem thread: the uplink diagnostics part of the status line */
static void
nac_print_diag(void)
{
	if (nac_afe_rb_valid && !nac_afe_rb_printed) {
		nac_afe_rb_printed = 1;
		printf("NORD2 CALL: afe readback pcm2 %x conn17 %x conn18 %x conn5 %x conn21 %x ulsrc %x uldl %x pad %x aifcfg %x aifrx %x top0 %x dac0 %x gpio26 %x gpio27 %x vul12 %x awb %x\n",
		    nac_afe_rb[0], nac_afe_rb[1], nac_afe_rb[2], nac_afe_rb[3], nac_afe_rb[4], nac_afe_rb[5], nac_afe_rb[6], nac_afe_rb[7],
		    nac_afe_rb[8], nac_afe_rb[9], nac_afe_rb[10], nac_afe_rb[11], nac_afe_rb[12], nac_afe_rb[13], nac_afe_rb[14], nac_afe_rb[15]);
	}
	printf("NORD2 CALL: uplink: mic %u frames peak %u loud %u vul %x | ap ul_mon %x/%x aif_mon %x/%x | aif %u cal %s state %u tried %u timeouts %u phase %d/%d/%d cycle %d/%d/%d | codec mon(%u) dbg %04x aif %04x/%04x/%04x pad %04x/%04x enc0 %04x enc15 %04x ulsrc %04x padtop %04x dig %04x aifcfg %04x | vol top %u bottom %u keys %u ios %u (below %d/%d) route %u\n",
	    nac_mic_frames, nac_mic_peak, nac_mic_nz, nac_vul_cur, nac_ul_mon, nac_ul_mon1, nac_aif_mon, nac_aif_mon1, nac_aif,
	    nac_cal_done ? (nac_cal_ok ? "ok" : "failed") : (nac_cal_state ? "running" : "none"), nac_cal_state, nac_cal_phases_tried,
	    nac_cal_timeouts, nac_cal_phase[0], nac_cal_phase[1], nac_cal_phase[2], nac_cal_cycle[0], nac_cal_cycle[1], nac_cal_cycle[2],
	    nac_cmon_reads, nac_cmon[0], nac_cmon[1], nac_cmon[2], nac_cmon[3], nac_cmon[4], nac_cmon[5], nac_cmon[6], nac_cmon[7],
	    nac_cmon[8], nac_cmon[9], nac_cmon[10], nac_cmon[11], nac_top, nac_bottom, nac_vol_keys, nac_ios_volumes,
	    (int)nac_ios_below[0], (int)nac_ios_below[1], nac_route);
	nac_mic_peak = 0;
	nac_mic_nz = 0;
}

/* --------------------------------------------------------------------------------------------- hooks */
static void
nac_state_hook(unsigned want, unsigned mode)
{
	nac_request(want, mode);
}

void
nac_init(void)
{
	nord2_call_ccci_rx_hook = nac_ccci_rx;
	nord2_call_ccci_take_hook = nac_ccci_take;
	nord2_call_state_hook = nac_state_hook;
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	printf("NORD2 CALL: hooks installed (CCCI_PCM ch4/ch5, call state); route %u top %u bottom %u pga %u aif %u\n", nac_route, nac_top,
	    nac_bottom, nac_pga, nac_aif);
}
