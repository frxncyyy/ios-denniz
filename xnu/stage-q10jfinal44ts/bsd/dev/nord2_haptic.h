/*
 * nord2_haptic.h -- 0610 (haptic-20261114): the Awinic AW8697 LRA haptic driver (pure C: register sequences, effect table,
 * limits and watchdog policy; host-tested by test/haptic_test.c against a simulated AW8697).  The XNU side is nord2_haptic.inc.
 *
 * Hardware [V = verified from the sources / Android logs named]:
 *  - [V ios-sweet/dtb/nord2/live.dts, the live Nord 2 DT] i2c9@11CB1000 (SCL GPIO120, SDA GPIO121, 400 kHz) node aw8697_haptic@5A:
 *    compatible "awinic,aw8697_haptic", reg 0x5a, reset-gpio GPIO110 (0x6e), irq-gpio GPIO135 (0x87, input pull-up),
 *    qcom,device_id 815.  The same bus carries the alert-slider halls (nord2_slider.inc already bit-bangs it on the input thread).
 *  - [V Android dmesg baseband-recon-20261104/dmesg_vendor_debug.log, wifi/rescue-dmesg.txt] "aw8697_haptic 9-005a: ...
 *    device_id=815", "aw8697_read_chipid aw8697 detected", "aw8697_haptic_init get f0_pre=1700"; the RAM waveform file
 *    aw8697_haptic_170.bin is NOT on the phone ("failed to read aw8697_haptic_170.bin"), and no awinic firmware exists in the vendor
 *    dump (cam-vendor-20261109/vfs) either.  So this driver uses CONT mode only (the chip drives the LRA at its own resonance,
 *    closed loop on the back-EMF), which needs no waveform file.  RAM and RTP modes are not used (RTP would need ~24 kB/s, far
 *    beyond the ~8 kB/s of the software I2C anyway).
 *  - [V nord2-current-kernel drivers/misc/aw8697_haptic/aw8697.c v1.3.6, aw8697.h, aw8697_reg.h] chip id reg 0x00 = 0x97, soft reset
 *    = write 0xAA to reg 0x00; reset pin low 1-2 ms then high, 3.5-4 ms before I2C; init = aw8697_haptic_init (standby, PWM 24K,
 *    BSTDBG1..3 = 30/eb/d4, TSET 0x12, R_SPARE 0x68, IOC 4.65 A, peak current 3.5 A, motor protect "no action", manual boost,
 *    triggers off, offset calibration, CONT vbat HW compensation); CONT play = aw8697_haptic_cont with the 0815 constants
 *    F0_PRE 1700 (170 Hz), DRV_LVL 60, DRV_LVL_OV 125, TD 0x009a, ZC_THR 0x0ff1, NUM_BRK 3 (aw8697.h AW8697_0815_*);
 *    stop = GO 0, wait GLB_STATE[3:0] == 0 (Android: 200 x 2 ms, else soft reset + re-init), then standby.
 *  - CONT mode sets BST_MODE = BYPASS (aw8697_haptic_play_mode case CONT): the boost converter is never enabled by this driver,
 *    and BSTDBG4 (boost voltage) is never written.  No boost over-voltage is possible from here.
 *
 * Limits (all enforced here, host-tested): drive level <= N2H_DRV_HW_MAX (= the Android 0815 value, sysctl can only lower it);
 * one segment <= kern.n2hap_max_ms (default 1000, hard 3000); on-time budget (token bucket, N2H_BUDGET_MS full, refills at half the
 * elapsed time = 50 % long-run duty); battery >= 43 C half drive, >= 46 C off until < 44 C; a motor still on 50 ms past its segment
 * end is stopped (watchdog); a stop not confirmed within N2H_STOP_TIMEOUT_US -> soft reset + re-init; 3 I2C failures in a row ->
 * reset pin held low (the chip, and so the motor, is off without I2C) and the driver latched off for the boot.
 *
 * Userland ABI of /dev/redwood-haptic (0666, write only) [V haptic-20261114/re: RedwoodAudio.driver strings; its source
 * scratchpad-backup-20261106/halplug/d/audio_plugin.c actuator_write]: the HAL plug-in's 1-channel "Actuator" output device (where
 * iOS 16 plays every haptic as a waveform once MobileGestalt says DeviceSupportsClosedLoopHaptics: UIKit feedbacks, keyboard,
 * CoreHaptics, system haptics, the ringer's vibration) opens the node once O_WRONLY|O_NONBLOCK and writes each I/O cycle's
 * int16 mono samples (48 kHz).  Same contract as HackiOS redwood_haptic.c (haptic=5).  The write never waits: it only records the
 * envelope (n2h_env_feed); the input owner thread turns it into CONT drive: the motor runs while iOS's waveform is not silent
 * (|sample| > N2H_SILENT_BELOW), at least N2H_TAP_MIN_MS from an onset, at a drive that follows the waveform's peak (full drive
 * from a third of full scale up, five eighths of it for the softest: HackiOS gain_for), never longer than kern.n2hap_max_ms in one
 * go (then it rests as long).  The last close (mediaserverd gone) stops the motor; so does silence or no write for N2H_ACTIVE_US.
 */
#ifndef NORD2_HAPTIC_H
#define NORD2_HAPTIC_H
#include <stdint.h>
#include <stdbool.h>

/* ---- chip ---- */
#define N2H_ADDR                0x5Au
#define N2H_CHIPID              0x97u
#define N2H_GPIO_RESET          110u
#define N2H_GPIO_IRQ            135u

#define N2H_REG_ID              0x00u
#define N2H_REG_SYSST           0x01u
#define N2H_REG_SYSINT          0x02u
#define N2H_REG_SYSINTM         0x03u
#define N2H_REG_SYSCTRL         0x04u
#define N2H_REG_GO              0x05u
#define N2H_REG_WAVSEQ1         0x07u     /* 0611: RAM playback (nord2_haptic_ram.h) */
#define N2H_REG_WAVSEQ2         0x08u
#define N2H_REG_WAVLOOP1        0x0Fu
#define N2H_REG_MAIN_LOOP       0x13u
#define N2H_REG_BASE_ADDRH      0x21u
#define N2H_REG_BASE_ADDRL      0x22u
#define N2H_REG_FIFO_AEH        0x23u
#define N2H_REG_FIFO_AEL        0x24u
#define N2H_REG_FIFO_AFH        0x25u
#define N2H_REG_FIFO_AFL        0x26u
#define N2H_REG_RAMADDRH        0x40u
#define N2H_REG_RAMADDRL        0x41u
#define N2H_REG_RAMDATA         0x42u     /* burst port: only through n2h_ram_write (RAMINIT on), never n2h_write */
#define N2H_REG_TRG_CFG2        0x1Cu
#define N2H_REG_DATCTRL         0x2Bu
#define N2H_REG_PWMPRC          0x2Du
#define N2H_REG_PWMDBG          0x2Eu
#define N2H_REG_BSTDBG1         0x31u
#define N2H_REG_BSTDBG2         0x32u
#define N2H_REG_BSTDBG3         0x33u
#define N2H_REG_BSTCFG          0x34u
#define N2H_REG_ANADBG          0x35u
#define N2H_REG_ANACTRL         0x36u
#define N2H_REG_DATDBG          0x39u
#define N2H_REG_BSTDBG4         0x3Au
#define N2H_REG_PRLVL           0x3Eu
#define N2H_REG_GLB_STATE       0x46u
#define N2H_REG_BST_AUTO        0x47u
#define N2H_REG_CONT_CTRL       0x48u
#define N2H_REG_F_PRE_H         0x49u
#define N2H_REG_F_PRE_L         0x4Au
#define N2H_REG_TD_H            0x4Bu
#define N2H_REG_TD_L            0x4Cu
#define N2H_REG_TSET            0x4Du
#define N2H_REG_TRIM_LRA        0x5Bu
#define N2H_REG_R_SPARE         0x5Du
#define N2H_REG_D2SCFG          0x5Eu
#define N2H_REG_DETCTRL         0x5Fu
#define N2H_REG_RLDET           0x60u
#define N2H_REG_VBATDET         0x62u
#define N2H_REG_ADCTEST         0x66u
#define N2H_REG_F_LRA_CONT_H    0x6Au
#define N2H_REG_F_LRA_CONT_L    0x6Bu
#define N2H_REG_ZC_THRSH_H      0x72u
#define N2H_REG_ZC_THRSH_L      0x73u
#define N2H_REG_BEMF_VTHH_H     0x74u
#define N2H_REG_BEMF_VTHH_L     0x75u
#define N2H_REG_BEMF_VTHL_H     0x76u
#define N2H_REG_BEMF_VTHL_L     0x77u
#define N2H_REG_BEMF_NUM        0x78u
#define N2H_REG_TIME_NZC        0x7Au
#define N2H_REG_DRV_LVL         0x7Bu
#define N2H_REG_DRV_LVL_OV      0x7Cu

#define N2H_SYSCTRL_RAMINIT     0x20u
#define N2H_SYSCTRL_PLAY_MASK   0x0Cu
#define N2H_SYSCTRL_PLAY_CONT   0x08u
#define N2H_SYSCTRL_PLAY_RAM    0x00u     /* 0611 */
#define N2H_SYSCTRL_BOOST       0x02u     /* never set by this driver */
#define N2H_SYSCTRL_STANDBY     0x01u
#define N2H_SYSINTM_UVLO        0x20u     /* 1 = UVLO interrupt masked (standby), 0 = enabled (active) */
#define N2H_GO                  0x01u
#define N2H_SOFTRESET           0xAAu
#define N2H_GLB_BUSY_MASK       0x0Fu
#define N2H_DETCTRL_RL          0x40u
#define N2H_DETCTRL_NOPROTECT   0x20u
#define N2H_DETCTRL_VBAT_GO     0x02u
#define N2H_DETCTRL_DIAG_GO     0x01u
#define N2H_ANACTRL_HD_PD       0x08u
#define N2H_D2SCFG_CLK_MASK     0xE0u
#define N2H_D2SCFG_CLK_1P5M     0x60u
#define N2H_D2SCFG_CLK_6M       0x20u
#define N2H_CONT_CTRL_VALUE     0x89u     /* ZC detect | wait 1 period | by GO | close loop | f0 detect off | O2C off | auto brake */

/* 0815 motor (Android aw8697.h AW8697_0815_*) */
#define N2H_F0_PRE              1700u     /* 0.1 Hz */
#define N2H_F0_COEFF            260u
#define N2H_TD                  0x009Au
#define N2H_ZC_THR              0x0FF1u
#define N2H_NUM_BRK             3u
#define N2H_DRV_HW_MAX          60u       /* DRV_LVL ceiling = the Android value */
#define N2H_DRV_OV_HW_MAX       125u      /* DRV_LVL_OV ceiling = the Android value */
#define N2H_DRV_MIN             18u       /* level 1/1000 maps here */

/* ---- register sequences (interpreted by n2h_run; mask = the bits changed) ---- */
enum { N2H_OP_W = 1, N2H_OP_RMW, N2H_OP_DLY_MS, N2H_OP_DRV, N2H_OP_DRV_OV, N2H_OP_WAIT_CLR, N2H_OP_GAIN, N2H_OP_WAVE };
/* 0611: N2H_OP_GAIN writes n2h_run's first argument (DATDBG gain), N2H_OP_WAVE its second (WAVSEQ1 waveform) */
struct n2h_op { uint8_t op, reg, mask, val; };

/* after the chip id check (aw8697_haptic_init order; the RAM / trigger / proc parts of Android are not used) */
static const struct n2h_op n2h_seq_init[] = {
	{ N2H_OP_RMW, N2H_REG_SYSINTM, N2H_SYSINTM_UVLO, N2H_SYSINTM_UVLO },       /* standby mode */
	{ N2H_OP_RMW, N2H_REG_SYSCTRL, N2H_SYSCTRL_STANDBY | N2H_SYSCTRL_BOOST, N2H_SYSCTRL_STANDBY },
	{ N2H_OP_RMW, N2H_REG_PWMDBG, 0x60, 0x40 },                                /* PWM 24K */
	{ N2H_OP_W, N2H_REG_BSTDBG1, 0xFF, 0x30 },
	{ N2H_OP_W, N2H_REG_BSTDBG2, 0xFF, 0xEB },
	{ N2H_OP_W, N2H_REG_BSTDBG3, 0xFF, 0xD4 },
	{ N2H_OP_W, N2H_REG_TSET, 0xFF, 0x12 },
	{ N2H_OP_W, N2H_REG_R_SPARE, 0xFF, 0x68 },
	{ N2H_OP_RMW, N2H_REG_ANADBG, 0x0C, 0x0C },                                /* IOC 4.65 A */
	{ N2H_OP_RMW, N2H_REG_BSTCFG, 0x07, 0x05 },                                /* boost peak current 3.5 A (boost unused) */
	{ N2H_OP_RMW, N2H_REG_DETCTRL, N2H_DETCTRL_NOPROTECT, N2H_DETCTRL_NOPROTECT },/* motor protect config 0 (Android) */
	{ N2H_OP_RMW, N2H_REG_PWMPRC, 0x80, 0x00 },
	{ N2H_OP_RMW, N2H_REG_PRLVL, 0x80, 0x00 },
	{ N2H_OP_RMW, N2H_REG_BST_AUTO, 0x07, 0x00 },                              /* manual boost, auto boost off for RAM/RTP */
	{ N2H_OP_RMW, N2H_REG_TRG_CFG2, 0x07, 0x00 },                              /* TRIG1..3 off (OPLUS "disable trig") */
	{ N2H_OP_RMW, N2H_REG_SYSCTRL, N2H_SYSCTRL_RAMINIT, N2H_SYSCTRL_RAMINIT },  /* offset calibration */
	{ N2H_OP_RMW, N2H_REG_DETCTRL, N2H_DETCTRL_DIAG_GO, N2H_DETCTRL_DIAG_GO },
	{ N2H_OP_WAIT_CLR, N2H_REG_DETCTRL, N2H_DETCTRL_DIAG_GO, 20 },              /* <= 20 reads */
	{ N2H_OP_RMW, N2H_REG_SYSCTRL, N2H_SYSCTRL_RAMINIT, 0 },
	{ N2H_OP_RMW, N2H_REG_ADCTEST, 0x40, 0x40 },                               /* CONT vbat HW compensation */
};

/* the CONT block (aw8697_haptic_cont minus play_mode / GO); written before every start */
static const struct n2h_op n2h_seq_cont[] = {
	{ N2H_OP_W, N2H_REG_F_PRE_H, 0xFF, (uint8_t)((1000000000u / (N2H_F0_PRE * N2H_F0_COEFF)) >> 8) },
	{ N2H_OP_W, N2H_REG_F_PRE_L, 0xFF, (uint8_t)(1000000000u / (N2H_F0_PRE * N2H_F0_COEFF)) },
	{ N2H_OP_RMW, N2H_REG_DATCTRL, 0xE0, 0xE0 },                               /* FC bits as Android leaves them | LPF on */
	{ N2H_OP_W, N2H_REG_CONT_CTRL, 0xFF, N2H_CONT_CTRL_VALUE },
	{ N2H_OP_W, N2H_REG_TD_H, 0xFF, (uint8_t)(N2H_TD >> 8) },
	{ N2H_OP_W, N2H_REG_TD_L, 0xFF, (uint8_t)N2H_TD },
	{ N2H_OP_W, N2H_REG_TSET, 0xFF, 0x12 },
	{ N2H_OP_W, N2H_REG_ZC_THRSH_H, 0xFF, (uint8_t)(N2H_ZC_THR >> 8) },
	{ N2H_OP_W, N2H_REG_ZC_THRSH_L, 0xFF, (uint8_t)N2H_ZC_THR },
	{ N2H_OP_W, N2H_REG_BEMF_VTHH_H, 0xFF, 0x10 },
	{ N2H_OP_W, N2H_REG_BEMF_VTHH_L, 0xFF, 0x08 },
	{ N2H_OP_W, N2H_REG_BEMF_VTHL_H, 0xFF, 0x03 },
	{ N2H_OP_W, N2H_REG_BEMF_VTHL_L, 0xFF, 0xF8 },
	{ N2H_OP_RMW, N2H_REG_BEMF_NUM, 0x0F, N2H_NUM_BRK },
	{ N2H_OP_W, N2H_REG_TIME_NZC, 0xFF, 0x23 },
};

/* start: drive levels, CONT play mode with the boost bypassed, active, UVLO irq on, GO */
static const struct n2h_op n2h_seq_start[] = {
	{ N2H_OP_DRV, N2H_REG_DRV_LVL, 0xFF, 0 },
	{ N2H_OP_DRV_OV, N2H_REG_DRV_LVL_OV, 0xFF, 0 },
	{ N2H_OP_RMW, N2H_REG_SYSCTRL, N2H_SYSCTRL_PLAY_MASK | N2H_SYSCTRL_BOOST | N2H_SYSCTRL_STANDBY | N2H_SYSCTRL_RAMINIT,
	  N2H_SYSCTRL_PLAY_CONT },
	{ N2H_OP_RMW, N2H_REG_SYSINTM, N2H_SYSINTM_UVLO, 0 },
	{ N2H_OP_RMW, N2H_REG_GO, N2H_GO, N2H_GO },
};

/* stop request (the brake runs in the chip; standby follows once GLB_STATE[3:0] == 0) */
static const struct n2h_op n2h_seq_stop[] = {
	{ N2H_OP_RMW, N2H_REG_GO, N2H_GO, 0 },
};

static const struct n2h_op n2h_seq_standby[] = {
	{ N2H_OP_RMW, N2H_REG_SYSINTM, N2H_SYSINTM_UVLO, N2H_SYSINTM_UVLO },
	{ N2H_OP_RMW, N2H_REG_SYSCTRL, N2H_SYSCTRL_STANDBY | N2H_SYSCTRL_BOOST, N2H_SYSCTRL_STANDBY },
};

/* registers this driver may write (everything else, e.g. BSTDBG4 boost voltage, RAM, RTP FIFO, TRIM_LRA, is refused) */
static inline bool
n2h_reg_writable(uint8_t reg)
{
	switch (reg) {
	case N2H_REG_ID:          /* soft reset 0xAA only (checked by the caller) */
	case N2H_REG_SYSINTM: case N2H_REG_SYSCTRL: case N2H_REG_GO: case N2H_REG_TRG_CFG2: case N2H_REG_DATCTRL:
	case N2H_REG_PWMPRC: case N2H_REG_PWMDBG: case N2H_REG_BSTDBG1: case N2H_REG_BSTDBG2: case N2H_REG_BSTDBG3:
	case N2H_REG_BSTCFG: case N2H_REG_ANADBG: case N2H_REG_ANACTRL: case N2H_REG_PRLVL: case N2H_REG_BST_AUTO:
	case N2H_REG_CONT_CTRL: case N2H_REG_F_PRE_H: case N2H_REG_F_PRE_L: case N2H_REG_TD_H: case N2H_REG_TD_L:
	case N2H_REG_TSET: case N2H_REG_R_SPARE: case N2H_REG_D2SCFG: case N2H_REG_DETCTRL: case N2H_REG_ADCTEST:
	case N2H_REG_ZC_THRSH_H: case N2H_REG_ZC_THRSH_L: case N2H_REG_BEMF_VTHH_H: case N2H_REG_BEMF_VTHH_L:
	case N2H_REG_BEMF_VTHL_H: case N2H_REG_BEMF_VTHL_L: case N2H_REG_BEMF_NUM: case N2H_REG_TIME_NZC:
	case N2H_REG_DRV_LVL: case N2H_REG_DRV_LVL_OV:
	/* 0611: RAM playback (waveform select, loops, gain, RAM layout); RAMDATA itself is not here (burst only) */
	case N2H_REG_WAVSEQ1: case N2H_REG_WAVSEQ2: case N2H_REG_WAVLOOP1: case N2H_REG_MAIN_LOOP: case N2H_REG_DATDBG:
	case N2H_REG_BASE_ADDRH: case N2H_REG_BASE_ADDRL: case N2H_REG_FIFO_AEH: case N2H_REG_FIFO_AEL: case N2H_REG_FIFO_AFH:
	case N2H_REG_FIFO_AFL: case N2H_REG_RAMADDRH: case N2H_REG_RAMADDRL:
		return true;
	default:
		return false;
	}
}

/* bus callbacks (the XNU side: the I2C9 software bus on the input thread; the host test: the simulated chip) */
struct n2h_bus {
	void *ctx;
	bool (*rd)(void *ctx, uint8_t reg, uint8_t *v);
	bool (*wr)(void *ctx, uint8_t reg, uint8_t v);
	void (*sleep_ms)(void *ctx, unsigned ms);
	/* 0611: bursts on one register (the RAMDATA port; n <= N2H_BURST_MAX) */
	bool (*wrn)(void *ctx, uint8_t reg, const uint8_t *v, unsigned n);
	bool (*rdn)(void *ctx, uint8_t reg, uint8_t *v, unsigned n);
};
#define N2H_BURST_MAX           62u       /* ni_bus_read's limit; writes use the same size */

/* 0611 limits checked in n2h_write */
#define N2H_GAIN_HW_MAX         0xBBu     /* DATDBG ceiling = the Android limit 128 x 4400 / 3000 (its RAM vbat compensation) */
#define N2H_W_COUNT             3u        /* waveform kinds: tick, click, heavy */
#ifndef N2H_WSEQ_MAX
#ifdef N2H_VENDOR_RAM
#include "nord2_haptic_vendor.h"
#define N2H_WSEQ_MAX            N2H_VENDOR_NWAVES
#else
#define N2H_WSEQ_MAX            N2H_W_COUNT
#endif
#endif

static inline bool
n2h_write(const struct n2h_bus *b, uint8_t reg, uint8_t v)
{
	if (!n2h_reg_writable(reg)) { return false; }
	if (reg == N2H_REG_ID && v != N2H_SOFTRESET) { return false; }
	if (reg == N2H_REG_SYSCTRL && (v & N2H_SYSCTRL_BOOST)) { return false; }     /* the boost is never enabled */
	if (reg == N2H_REG_DRV_LVL && v > N2H_DRV_HW_MAX) { return false; }
	if (reg == N2H_REG_DRV_LVL_OV && v > N2H_DRV_OV_HW_MAX) { return false; }
	if (reg == N2H_REG_DATDBG && v > N2H_GAIN_HW_MAX) { return false; }                  /* 0611 */
	if (reg == N2H_REG_WAVSEQ1 && v > N2H_WSEQ_MAX) { return false; }                    /* 0611: only loaded waveforms */
	if ((reg == N2H_REG_WAVSEQ2 || reg == N2H_REG_MAIN_LOOP || reg == N2H_REG_WAVLOOP1) && v != 0) { return false; } /* 0611: play once */
	return b->wr(b->ctx, reg, v);
}

/* runs a sequence; drv / drv_ov feed the N2H_OP_DRV* entries. Returns the index of the failing op + 1, 0 on success. */
static inline unsigned
n2h_run(const struct n2h_bus *b, const struct n2h_op *ops, unsigned n, uint8_t drv, uint8_t drv_ov)
{
	for (unsigned i = 0; i < n; i++) {
		const struct n2h_op *o = &ops[i];
		uint8_t v = 0;
		bool ok = true;
		switch (o->op) {
		case N2H_OP_W:      ok = n2h_write(b, o->reg, o->val); break;
		case N2H_OP_DRV:    ok = n2h_write(b, o->reg, drv); break;
		case N2H_OP_DRV_OV: ok = n2h_write(b, o->reg, drv_ov); break;
		case N2H_OP_GAIN:   ok = o->reg == N2H_REG_DATDBG && n2h_write(b, o->reg, drv); break;
		case N2H_OP_WAVE:   ok = o->reg == N2H_REG_WAVSEQ1 && drv_ov >= 1 && n2h_write(b, o->reg, drv_ov); break;
		case N2H_OP_RMW:
			ok = b->rd(b->ctx, o->reg, &v) && n2h_write(b, o->reg, (uint8_t)((v & ~o->mask) | (o->val & o->mask)));
			break;
		case N2H_OP_DLY_MS: b->sleep_ms(b->ctx, o->val); break;
		case N2H_OP_WAIT_CLR:
			ok = false;
			for (unsigned k = 0; k < o->val; k++) {
				if (!b->rd(b->ctx, o->reg, &v)) { break; }
				if (!(v & o->mask)) { ok = true; break; }
				b->sleep_ms(b->ctx, 1);
			}
			break;
		default: ok = false; break;
		}
		if (!ok) { return i + 1; }
	}
	return 0;
}
#define N2H_RUN(b, seq, d, o) n2h_run((b), (seq), (unsigned)(sizeof(seq) / sizeof((seq)[0])), (d), (o))

/* LRA DC resistance in milliohm (Android aw8697_lra_resistance_show: RLDET x 298), motor test without moving it.
 * Leaves the chip in standby with the boost bypassed. */
static inline bool
n2h_measure_lra(const struct n2h_bus *b, uint32_t *mohm)
{
	uint8_t v = 0;
	bool ok = N2H_RUN(b, n2h_seq_stop, 0, 0) == 0;
	static const struct n2h_op pre[] = {
		{ N2H_OP_RMW, N2H_REG_SYSCTRL, N2H_SYSCTRL_RAMINIT | N2H_SYSCTRL_BOOST, N2H_SYSCTRL_RAMINIT },
		{ N2H_OP_RMW, N2H_REG_ANACTRL, N2H_ANACTRL_HD_PD, 0 },
		{ N2H_OP_RMW, N2H_REG_D2SCFG, N2H_D2SCFG_CLK_MASK, N2H_D2SCFG_CLK_1P5M },
		{ N2H_OP_RMW, N2H_REG_DETCTRL, N2H_DETCTRL_RL, N2H_DETCTRL_RL },
		{ N2H_OP_RMW, N2H_REG_DETCTRL, N2H_DETCTRL_DIAG_GO, N2H_DETCTRL_DIAG_GO },
		{ N2H_OP_DLY_MS, 0, 0, 3 },
	};
	static const struct n2h_op post[] = {
		{ N2H_OP_RMW, N2H_REG_ANACTRL, N2H_ANACTRL_HD_PD, N2H_ANACTRL_HD_PD },
		{ N2H_OP_RMW, N2H_REG_D2SCFG, N2H_D2SCFG_CLK_MASK, N2H_D2SCFG_CLK_6M },
		{ N2H_OP_RMW, N2H_REG_SYSCTRL, N2H_SYSCTRL_RAMINIT | N2H_SYSCTRL_BOOST, 0 },
	};
	ok = ok && N2H_RUN(b, pre, 0, 0) == 0 && b->rd(b->ctx, N2H_REG_RLDET, &v);
	bool ok2 = N2H_RUN(b, post, 0, 0) == 0;
	*mohm = 298u * v;
	return ok && ok2;
}

/* battery voltage seen by the chip, mV (Android aw8697_haptic_get_vbat: VBATDET x 6100 / 256) */
static inline bool
n2h_measure_vbat(const struct n2h_bus *b, uint32_t *mv)
{
	uint8_t v = 0;
	static const struct n2h_op pre[] = {
		{ N2H_OP_RMW, N2H_REG_SYSCTRL, N2H_SYSCTRL_RAMINIT | N2H_SYSCTRL_BOOST, N2H_SYSCTRL_RAMINIT },
		{ N2H_OP_RMW, N2H_REG_DETCTRL, N2H_DETCTRL_VBAT_GO, N2H_DETCTRL_VBAT_GO },
		{ N2H_OP_WAIT_CLR, N2H_REG_DETCTRL, N2H_DETCTRL_VBAT_GO, 20 },
	};
	static const struct n2h_op post[] = {
		{ N2H_OP_RMW, N2H_REG_SYSCTRL, N2H_SYSCTRL_RAMINIT | N2H_SYSCTRL_BOOST, 0 },
	};
	bool ok = N2H_RUN(b, pre, 0, 0) == 0 && b->rd(b->ctx, N2H_REG_VBATDET, &v);
	bool ok2 = N2H_RUN(b, post, 0, 0) == 0;
	*mv = 6100u * v / 256u;
	return ok && ok2;
}

/* CONT drive frequency the chip tracked during the last play, 0.1 Hz (Android aw8697_haptic_read_cont_f0); 0 = none */
static inline uint32_t
n2h_cont_f0(uint8_t h, uint8_t l)
{
	uint32_t r = (uint32_t)h << 8 | l;
	return r ? 1000000000u / (r * N2H_F0_COEFF) : 0u;
}

/* ---- effects: the iOS feedback kinds (UIFeedbackGenerator / AudioServices / system haptics) as CONT segments ---- */
enum {
	N2H_FX_STOP = 0,
	N2H_FX_IMPACT_LIGHT = 1, N2H_FX_IMPACT_MEDIUM = 2, N2H_FX_IMPACT_HEAVY = 3, N2H_FX_IMPACT_SOFT = 4, N2H_FX_IMPACT_RIGID = 5,
	N2H_FX_SELECTION = 6,
	N2H_FX_NOTIFY_SUCCESS = 7, N2H_FX_NOTIFY_WARNING = 8, N2H_FX_NOTIFY_ERROR = 9,
	N2H_FX_KEYBOARD = 10, N2H_FX_RINGER_SWITCH = 11, N2H_FX_PEEK = 12, N2H_FX_POP = 13,
	N2H_FX_VIBRATE = 14,           /* kSystemSoundID_Vibrate 4095 */
	N2H_FX_RING = 15,              /* one ringtone vibration cycle */
	N2H_FX_RAW = 16,               /* ms + level from the request */
	N2H_FX_COUNT
};
#define N2H_SEG_MAX 4
struct n2h_seg { uint16_t on_ms, level, off_ms; };
struct n2h_fx { const char *name; uint8_t n; struct n2h_seg seg[N2H_SEG_MAX]; };
static const struct n2h_fx n2h_fx_table[N2H_FX_COUNT] = {
	[N2H_FX_STOP]           = { "stop", 0, {{0, 0, 0}} },
	[N2H_FX_IMPACT_LIGHT]   = { "impact-light", 1, {{12, 450, 0}} },
	[N2H_FX_IMPACT_MEDIUM]  = { "impact-medium", 1, {{16, 700, 0}} },
	[N2H_FX_IMPACT_HEAVY]   = { "impact-heavy", 1, {{22, 1000, 0}} },
	[N2H_FX_IMPACT_SOFT]    = { "impact-soft", 1, {{24, 500, 0}} },
	[N2H_FX_IMPACT_RIGID]   = { "impact-rigid", 1, {{10, 1000, 0}} },
	[N2H_FX_SELECTION]      = { "selection", 1, {{8, 500, 0}} },
	[N2H_FX_NOTIFY_SUCCESS] = { "success", 2, {{14, 700, 90}, {16, 1000, 0}} },
	[N2H_FX_NOTIFY_WARNING] = { "warning", 2, {{18, 1000, 140}, {14, 700, 0}} },
	[N2H_FX_NOTIFY_ERROR]   = { "error", 3, {{14, 850, 70}, {14, 850, 70}, {20, 1000, 0}} },
	[N2H_FX_KEYBOARD]       = { "keyboard", 1, {{8, 550, 0}} },
	[N2H_FX_RINGER_SWITCH]  = { "ringer-switch", 1, {{30, 1000, 0}} },
	[N2H_FX_PEEK]           = { "peek", 1, {{20, 800, 0}} },
	[N2H_FX_POP]            = { "pop", 1, {{26, 1000, 0}} },
	[N2H_FX_VIBRATE]        = { "vibrate", 1, {{400, 1000, 0}} },
	[N2H_FX_RING]           = { "ring", 1, {{800, 1000, 0}} },
	[N2H_FX_RAW]            = { "raw", 1, {{0, 0, 0}} },
};

/* ---- policy ---- */
#define N2H_MAX_MS_DEFAULT      1000u     /* kern.n2hap_max_ms: one segment */
#define N2H_MAX_MS_HW           3000u
#define N2H_MIN_ON_MS           6u
#define N2H_BUDGET_MS           8000u     /* on-time bucket */
#define N2H_REFILL_DIV          2u        /* +1 us of budget per 2 us elapsed (50 % long-run duty) */
#define N2H_WARM_CC             4300
#define N2H_HOT_CC              4600
#define N2H_HOT_CLEAR_CC        4400
#define N2H_THERM_PERIOD_US     5000000ull
#define N2H_OVERRUN_US          50000ull  /* motor on this long past its segment end: watchdog stop */
#define N2H_STOP_TIMEOUT_US     100000ull /* GLB_STATE not idle this long after GO=0: soft reset + re-init */
#define N2H_FAULT_LATCH         3u

struct n2h_cfg {
	uint32_t mode;        /* kern.n2hap: 1 allow, 0 off (and kept off) */
	uint32_t drv_max;     /* kern.n2hap_drvmax: DRV_LVL at level 1000, N2H_DRV_MIN..N2H_DRV_HW_MAX */
	uint32_t max_ms;      /* kern.n2hap_max_ms */
};

static inline void
n2h_cfg_fix(struct n2h_cfg *c)
{
	if (c->mode > 1) { c->mode = 1; }
	if (c->drv_max > N2H_DRV_HW_MAX) { c->drv_max = N2H_DRV_HW_MAX; }
	if (c->drv_max < N2H_DRV_MIN) { c->drv_max = N2H_DRV_MIN; }
	if (c->max_ms > N2H_MAX_MS_HW) { c->max_ms = N2H_MAX_MS_HW; }
	if (c->max_ms < 50) { c->max_ms = 50; }
}

/* request word (userland -> owner thread): seq:32 | effect:8 | level:10 | ms:14 */
struct n2h_req { uint32_t seq, effect, level, ms; };

static inline uint64_t
n2h_req_pack(uint32_t seq, uint32_t effect, uint32_t level, uint32_t ms)
{
	if (level > 1000) { level = 1000; }
	if (ms > 0x3fff) { ms = 0x3fff; }
	return (uint64_t)seq << 32 | (uint64_t)(effect & 0xffu) << 24 | (uint64_t)(level & 0x3ffu) << 14 | (ms & 0x3fffu);
}

static inline struct n2h_req
n2h_req_unpack(uint64_t w)
{
	struct n2h_req r = { (uint32_t)(w >> 32), (uint32_t)(w >> 24) & 0xffu, (uint32_t)(w >> 14) & 0x3ffu, (uint32_t)w & 0x3fffu };
	return r;
}

enum { N2H_PH_IDLE = 0, N2H_PH_ON, N2H_PH_GAP };

struct n2h_state {
	/* pattern being played */
	uint32_t effect, level_req;
	uint8_t nseg, seg, phase;
	struct n2h_seg segs[N2H_SEG_MAX];
	uint64_t phase_end_us;
	/* limits */
	int64_t budget_us;
	uint64_t budget_t_us;
	uint32_t therm;       /* 0 normal, 1 warm (half drive), 2 hot (off) */
	int therm_cc;
	bool latched;
	uint32_t seen_seq;
	/* counters */
	uint64_t reqs, plays, segs_on, throttled, therm_refused, clamped, bad_effect;
};

static inline void
n2h_init(struct n2h_state *s, uint64_t now)
{
	*s = (struct n2h_state){ 0 };
	s->budget_us = (int64_t)N2H_BUDGET_MS * 1000;
	s->budget_t_us = now;
}

/* keeps the bucket current; on = the motor was driven since the last call */
static inline void
n2h_budget(struct n2h_state *s, uint64_t now, bool on)
{
	if (now <= s->budget_t_us) { return; }
	uint64_t dt = now - s->budget_t_us;
	s->budget_t_us = now;
	s->budget_us += (int64_t)(dt / N2H_REFILL_DIV);
	if (on) { s->budget_us -= (int64_t)dt; }
	if (s->budget_us > (int64_t)N2H_BUDGET_MS * 1000) { s->budget_us = (int64_t)N2H_BUDGET_MS * 1000; }
	if (s->budget_us < -(int64_t)N2H_BUDGET_MS * 1000) { s->budget_us = -(int64_t)N2H_BUDGET_MS * 1000; }
}

/* a new request replaces the pattern (latest wins, as on iOS) */
static inline void
n2h_take(struct n2h_state *s, const struct n2h_req *r, const struct n2h_cfg *c, uint64_t now)
{
	if (r->seq == 0 || r->seq == s->seen_seq) { return; }
	s->seen_seq = r->seq;
	s->reqs++;
	if (r->effect >= N2H_FX_COUNT) { s->bad_effect++; return; }
	const struct n2h_fx *fx = &n2h_fx_table[r->effect];
	s->effect = r->effect; s->level_req = r->level;
	s->nseg = fx->n; s->seg = 0; s->phase = N2H_PH_IDLE;
	for (unsigned i = 0; i < fx->n && i < N2H_SEG_MAX; i++) {
		struct n2h_seg g = fx->seg[i];
		if (r->effect == N2H_FX_RAW) { g.on_ms = (uint16_t)r->ms; g.level = (uint16_t)(r->level ? r->level : 1000); }
		else if (r->level) { g.level = (uint16_t)((uint32_t)g.level * r->level / 1000u); }
		if (g.on_ms > c->max_ms) { g.on_ms = (uint16_t)c->max_ms; s->clamped++; }
		if (g.on_ms < N2H_MIN_ON_MS) { g.on_ms = N2H_MIN_ON_MS; }
		if (g.level == 0) { g.level = 1; }
		s->segs[i] = g;
	}
	if (s->nseg) { s->plays++; s->phase_end_us = now; }
}

/* the thermal level from a battery reading (centi-C), hysteresis on the hot side; returns true when it changed */
static inline bool
n2h_therm_update(struct n2h_state *s, bool ok, int cc)
{
	uint32_t t = s->therm;
	if (!ok) { return false; }
	s->therm_cc = cc;
	if (cc >= N2H_HOT_CC) { t = 2; }
	else if (t == 2 && cc >= N2H_HOT_CLEAR_CC) { t = 2; }
	else if (cc >= N2H_WARM_CC) { t = 1; }
	else { t = 0; }
	bool changed = t != s->therm;
	s->therm = t;
	return changed;
}

static inline uint8_t
n2h_drv_for(uint32_t level, const struct n2h_cfg *c, uint32_t therm)
{
	if (level > 1000) { level = 1000; }
	uint32_t top = c->drv_max > N2H_DRV_HW_MAX ? N2H_DRV_HW_MAX : c->drv_max;
	if (top < N2H_DRV_MIN) { top = N2H_DRV_MIN; }
	uint32_t d = N2H_DRV_MIN + (top - N2H_DRV_MIN) * level / 1000u;
	if (therm == 1) { d = d / 2u < N2H_DRV_MIN ? N2H_DRV_MIN : d / 2u; }
	return (uint8_t)d;
}

static inline uint8_t
n2h_drv_ov_for(uint8_t drv)
{
	uint32_t ov = (uint32_t)drv * N2H_DRV_OV_HW_MAX / N2H_DRV_HW_MAX;
	return (uint8_t)(ov > N2H_DRV_OV_HW_MAX ? N2H_DRV_OV_HW_MAX : ov);
}

/* the decision for this tick: true = the motor should be driven at *drv until *until_us */
static inline bool
n2h_decide(struct n2h_state *s, const struct n2h_cfg *c, uint64_t now, bool hw_on, uint8_t *drv, uint64_t *until_us)
{
	n2h_budget(s, now, hw_on);
	if (!c->mode || s->latched || s->therm >= 2) {
		if (s->nseg && s->therm >= 2) { s->therm_refused++; }
		s->nseg = 0; s->phase = N2H_PH_IDLE;
		return false;
	}
	for (;;) {
		if (s->nseg == 0 || s->seg >= s->nseg) { s->nseg = 0; s->phase = N2H_PH_IDLE; return false; }
		struct n2h_seg *g = &s->segs[s->seg];
		if (s->phase == N2H_PH_IDLE) {
			if (s->budget_us < (int64_t)g->on_ms * 1000) { s->throttled++; s->nseg = 0; return false; }
			s->phase = N2H_PH_ON;
			s->phase_end_us = now + (uint64_t)g->on_ms * 1000u;
			s->segs_on++;
		}
		if (s->phase == N2H_PH_ON) {
			if (s->budget_us <= 0) { s->throttled++; s->nseg = 0; s->phase = N2H_PH_IDLE; return false; }
			if (now < s->phase_end_us) {
				*drv = n2h_drv_for(g->level, c, s->therm);
				*until_us = s->phase_end_us;
				return true;
			}
			s->phase = N2H_PH_GAP;
			s->phase_end_us = now + (uint64_t)g->off_ms * 1000u;
		}
		if (s->phase == N2H_PH_GAP) {
			if (now < s->phase_end_us) { return false; }
			s->seg++; s->phase = N2H_PH_IDLE;
		}
	}
}

/* ---- the iOS stream (HAL "Actuator" samples) ---- */
#define N2H_SILENT_BELOW        256u      /* of 32768: no larger = silence (HackiOS SILENT_BELOW) */
#define N2H_ACTIVE_US           25000ull  /* no non-silent write for this long = silence (a lost close, a stalled HAL) */
#define N2H_TAP_MIN_MS          12u       /* an onset drives the motor at least this long (a 1-buffer transient is felt) */
#define N2H_PEAK_FULL           10922u    /* 32767 / 3: full drive from here up */

/* written by the HAL's I/O thread (the write handler), read by the owner thread; each field is one aligned 64-bit word */
struct n2h_env {
	volatile uint64_t heard_us;   /* last non-silent write */
	volatile uint64_t quiet_us;   /* last silent write */
	volatile uint64_t onset_us;   /* first non-silent write after silence */
	volatile uint64_t peak;       /* largest |sample| since the onset or since the owner last took it */
	volatile uint64_t onsets, frames, writes;
	volatile uint64_t zc, act_frames;    /* 0611: zero crossings / non-silent frames since the onset (iOS waveform frequency) */
	volatile uint64_t peak_recent;       /* 0613: largest |sample| since the owner last cleared it (train / CONT decisions) */
	volatile int64_t last_sign;
};

static inline uint32_t
n2h_peak_of(const int16_t *s, unsigned n)
{
	uint32_t p = 0;
	for (unsigned i = 0; i < n; i++) {
		uint32_t v = s[i] < 0 ? (uint32_t)(-(int32_t)s[i]) : (uint32_t)s[i];
		p = v > p ? v : p;
	}
	return p;
}

static inline void
n2h_env_feed(struct n2h_env *e, uint32_t peak, unsigned frames, uint64_t now)
{
	e->writes++;
	e->frames += frames;
	if (peak <= N2H_SILENT_BELOW) { e->quiet_us = now; return; }
	if (e->heard_us == 0 || e->quiet_us > e->heard_us || now - e->heard_us >= N2H_ACTIVE_US) {
		e->onset_us = now;
		e->peak = 0;
		e->onsets++;
		e->zc = 0;
		e->last_sign = 0;
		e->peak_recent = 0;
		e->act_frames = 0;
	}
	e->act_frames += frames;
	e->heard_us = now;
	if (peak > e->peak) { e->peak = peak; }
	if (peak > e->peak_recent) { e->peak_recent = peak; }
}

/* the drive for a stream peak (HackiOS gain_for, in DRV_LVL terms; thermal halving on top) */
static inline uint8_t
n2h_drv_for_peak(uint32_t peak, const struct n2h_cfg *c, uint32_t therm)
{
	uint32_t level = peak >= N2H_PEAK_FULL ? 1000u : 625u + 375u * peak / N2H_PEAK_FULL;
	return n2h_drv_for(level, c, therm);
}

struct n2h_stream {
	uint64_t onset_seen;          /* onset_us of the waveform being played */
	uint64_t run_start_us;        /* the motor has run continuously since (stream) */
	uint64_t rest_until_us;       /* after a capped run */
	uint32_t peak_hold;           /* peak of the waveform being played */
	uint64_t taps, runs, capped, idle_stops;
	/* 0611: with the RAM clicks loaded (ram_ok) an onset is a RAM click (n2h_click_take) and CONT drive is only for a waveform
	 * still sounding N2H_SUSTAIN_MS after its onset (iOS continuous events, the ringer's vibration) */
	bool ram_ok;
	uint64_t click_onset, clicks, stale, wave_n[N2H_W_COUNT + 1];
	/* 0613: tick train + per-onset path record (n2h_click_take / the owner's per-waveform log) */
	uint64_t train_last_us, trains, onset_peak, lookahead;
	uint64_t ons_onset, ons_clicks, ons_cont, ons_skip, ons_peak_first;
	uint64_t path_ram, path_train, path_cont, path_skip, path_none, dur_hist[5];
};
#define N2H_SUSTAIN_MS          300u      /* 0613: CONT only for a waveform still sounding 300 ms after its onset (was 50) */
enum { N2H_SKIP_NONE = 0, N2H_SKIP_STALE, N2H_SKIP_BUDGET, N2H_SKIP_OFF, N2H_SKIP_TAIL };

/*
 * The whole decision for a tick: an internal pattern (boot pulse, test) first, else the stream.
 * Returns true = drive at *drv; *until_us = the latest time the motor may still be on (the watchdog's limit).
 */
static inline bool
n2h_decide_all(struct n2h_state *s, struct n2h_stream *t, const struct n2h_env *e, const struct n2h_cfg *c, uint64_t now,
    bool hw_on, uint8_t *drv, uint64_t *until_us)
{
	if (s->nseg) { return n2h_decide(s, c, now, hw_on, drv, until_us); }
	n2h_budget(s, now, hw_on);
	uint64_t heard = e->heard_us, quiet = e->quiet_us, onset = e->onset_us;
	uint32_t peak = (uint32_t)e->peak;
	bool active = heard != 0 && now >= heard && now - heard < N2H_ACTIVE_US && quiet <= heard;
	bool in_tap = !t->ram_ok && onset != 0 && now >= onset && now - onset < (uint64_t)N2H_TAP_MIN_MS * 1000u && heard >= onset;
	if (t->ram_ok && active && !(onset != 0 && now >= onset && now - onset >= (uint64_t)N2H_SUSTAIN_MS * 1000u)) {
		active = false;              /* 0611: a short waveform is a RAM click, not CONT */
	}
	bool want = c->mode && !s->latched && s->therm < 2 && (active || in_tap);
	if (want && onset != t->onset_seen) {
		t->onset_seen = onset;
		t->peak_hold = 0;
		t->taps++;
	}
	if (want && now < t->rest_until_us) { want = false; }
	if (want && s->budget_us <= 0) { s->throttled++; want = false; }
	if (!want) {
		if (hw_on && !active && !in_tap) { t->idle_stops++; }
		if (!active && !in_tap) { t->run_start_us = 0; }
		if (hw_on && s->therm >= 2) { s->therm_refused++; }
		return false;
	}
	if (t->run_start_us == 0) { t->run_start_us = now; t->runs++; }
	if (now - t->run_start_us >= (uint64_t)c->max_ms * 1000u) {
		t->capped++;
		t->rest_until_us = now + (uint64_t)c->max_ms * 1000u;
		t->run_start_us = 0;
		return false;
	}
	if (peak > t->peak_hold) { t->peak_hold = peak; }
	*drv = n2h_drv_for_peak(t->peak_hold, c, s->therm);
	uint64_t end_a = heard + N2H_ACTIVE_US, end_b = onset + (uint64_t)N2H_TAP_MIN_MS * 1000u;
	uint64_t end_c = t->run_start_us + (uint64_t)c->max_ms * 1000u;
	uint64_t end = t->ram_ok ? end_a : end_a > end_b ? end_a : end_b;
	*until_us = end < end_c ? end : end_c;
	return true;
}

#include "nord2_haptic_ram.h"      /* 0611: RAM clicks (waveform synthesis, upload, play) */

#endif /* NORD2_HAPTIC_H */
