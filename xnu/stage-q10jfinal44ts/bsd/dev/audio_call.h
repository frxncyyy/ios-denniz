/* SPDX-License-Identifier: MIT
 * Nord2 CS call audio (patch 0280).  See audio_call.c for the design and sources.
 *
 * Threads:
 *  - bridge (Nord2Baseband.cpp) and sysctl: nac_request() through nord2_call_state_hook (any thread, lock-free)
 *  - modem thread (sweet_amfi.c steady loop): nac_ccci_rx() for CCCI_PCM_RX (ch 4) packets, nac_ccci_take() to
 *    fetch the next CCCI_PCM_TX (ch 5) packet it must send on queue 0
 *  - input thread (sweet_touch.c ni_thread, sole PWRAP owner): nord2_audio_owner_poll() runs queued MT6359 work
 *  - audio worker (audio_device.c): everything else (AFE MMIO through the worker-only nah_io)
 */
#ifndef NORD2_AUDIO_CALL_H
#define NORD2_AUDIO_CALL_H
#include <stdint.h>

struct nau_hw;

enum { NAC_ROUTE_HANDSET = 0, NAC_ROUTE_SPEAKER = 1, NAC_ROUTE_BOTH = 2 };

void nac_init(void);                          /* installs the bb hooks; once, from nord2_audio_initialize */
/* worker side */
unsigned nac_wanted(void);                    /* 1 while a call (or kern.n2call.force) wants speech */
void nac_gains(unsigned *top_q15, unsigned *bottom_q15);
int nac_pmic_up(void);                        /* MT6359 handset-mic uplink on (blocks on the input thread, <= ~200 ms) */
int nac_pmic_down(void);
int nac_afe_up(struct nau_hw *);              /* AFE: PCM2 <-> ADDA UL / AWB, MTKAIF RX; hardware must be running */
int nac_afe_down(struct nau_hw *);            /* restores every AFE/GPIO word nac_afe_up changed */
/* 0320: the microphone without a call (recording, /dev/redwood-mic); worker side, never blocking */
int nac_mic_pmic_submit(unsigned up);         /* queue nac_up (1) / nac_down (0) for the input thread: 0 queued, -1 busy */
int nac_mic_pmic_poll(int *error);            /* 1 = that table finished (*error its result), 0 = pending */
int nac_mic_afe_up(struct nau_hw *);          /* AFE: ADDA UL -> VUL12 only; hardware must be running; nac_afe_down undoes */
unsigned nac_mic_take(struct nau_hw *, int16_t *mono, unsigned max);   /* VUL12 samples (mono 16-bit 48 kHz); 0 if none */
void nac_sph_on(void);                        /* queues MSG_A2M_SPH_ON (0x2F20) for the modem thread */
void nac_sph_off(void);                       /* queues MSG_A2M_SPH_OFF (0x2F21) */
/* Modem downlink captured by AWB (mono 16-bit, 48 kHz) as stereo frames for nau_hw_feed; 0 if none. */
unsigned nac_dl_take(struct nau_hw *, int16_t *stereo, unsigned max_frames);
void nac_note(int phase, int rc);             /* worker-side outcome record, printed by the modem thread */
void nac_tick(struct nau_hw *);               /* every worker pass of an active call: calibration steps, monitors */
void nac_set_call_volume(unsigned port, unsigned below_eighth_db, unsigned muted);   /* HAL ioctl 'r' 3 */
void nac_set_route(unsigned route);           /* NAC_ROUTE_*: HAL ioctl / speakerphone; immediate (prescale) */
void nac_set_ul_mute(unsigned source, unsigned on);   /* 0 HAL ioctl 'r' 4 (input mute control), 1 bb bridge; any thread */
void nac_volume_key(unsigned key);            /* input thread key-down edges: 1 volume up, 2 volume down */
/* bb side (installed as hooks) */
void nac_request(unsigned want, unsigned mode);
void nac_ccci_rx(const uint8_t *pkt, uint32_t len);
unsigned nac_ccci_take(uint8_t *pkt, unsigned cap);
/* input thread */
void nord2_audio_owner_poll(void);
/* audio_hw_native.inc write allowlist extension (worker-only writes) */
int nac_write_allowed(uint32_t pa);

#endif
