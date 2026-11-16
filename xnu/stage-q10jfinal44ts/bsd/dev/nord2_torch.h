/*
 * nord2_torch.h -- 0496 (bright-fix-20261114): the flash LED as a torch (pure C policy, host-tested by test/torch_test.c).
 *
 * Hardware (Android nord2-current-kernel, mt6893_defconfig CONFIG_MTK_FLASHLIGHT_MT6360 + CONFIG_MT6360_PMU_FLED,
 * drivers/misc/mediatek/pmic/mt6360/pmu/mt6360_pmu_fled.c, flashlights-mt6360.c, flashlight/oplus/flashlight_custom.c for
 * projects 20817/20827/20831: both channels are driven together, DUAL_TORCH / DUAL_FLASH):
 *   MT6360 PMU, plain SMBus at 7-bit 0x34 (no CRC, unlike the LDO 0x64 / PMIC 0x1a) on I2C5 = GPIO202/203, the same software
 *   bus the input thread already uses for the MT6360 ID read and the touch rail (sweet_touch.c ni_power_on).
 *   FLED1_TOR_CTRL 0x75 / FLED2_TOR_CTRL 0x79 bits [4:0] ITOR: 25 mA + 12.5 mA * sel (0..0x1E = 25..400 mA)
 *   FLED_EN 0x7E: bit 3 TORCH, bit 2 STROBE, bit 1 FLCS1_EN, bit 0 FLCS2_EN.  Torch on = STROBE off, 500 us, CS1|CS2|TORCH on.
 *   CHG_STAT2 0xE1 bit 3 FLED_CHG_VINOVP (the Android driver's "not ready").  FLED_STAT1/2 0xE9/0xEA (faults, logged only).
 * Torch mode only (no STROBE: no hardware strobe timing on this port).  A "flash" here is a short torch pulse at a higher, still
 * torch-rated current, with a software duty limit.
 *
 * Userland ABI of /dev/redwood-torch (0666, write only; the camera already writes it: camera-userland rwcam_device.c torch_set):
 *   4 bytes  uint32 level in thousandths (0 = off, 1..1000)            -> torch at that fraction of kern.n2torch_maxsel
 *   8 bytes  uint32 level, uint32 ms (1..N2T_FLASH_MS_MAX)              -> flash pulse at that fraction of kern.n2torch_flashsel,
 *                                                                           off after ms; then N2T_FLASH_DUTY_DIV x ms cool-down
 *   the last close of the device turns the LED off.
 */
#ifndef NORD2_TORCH_H
#define NORD2_TORCH_H
#include <stdint.h>
#include <stdbool.h>

#define N2T_PMU_ADDR            0x34u
#define N2T_REG_FLED1_TOR       0x75u
#define N2T_REG_FLED2_TOR       0x79u
#define N2T_REG_FLED_EN         0x7Eu
#define N2T_REG_CHG_STAT2       0xE1u
#define N2T_REG_FLED_STAT1      0xE9u
#define N2T_REG_FLED_STAT2      0xEAu
#define N2T_EN_TORCH            0x08u
#define N2T_EN_STROBE           0x04u
#define N2T_EN_CS1              0x02u
#define N2T_EN_CS2              0x01u
#define N2T_EN_ON               (N2T_EN_TORCH | N2T_EN_CS1 | N2T_EN_CS2)
#define N2T_ITOR_MASK           0x1Fu
#define N2T_VINOVP              0x08u

#define N2T_SEL_HW_MAX          0x0Eu     /* hard ceiling here: 200 mA per channel (the part allows 0x1E = 400 mA) */
#define N2T_MAXSEL_DEFAULT      0x06u     /* kern.n2torch_maxsel: 100 mA per channel, 200 mA for the LED */
#define N2T_FLASHSEL_DEFAULT    0x0Eu     /* kern.n2torch_flashsel: 200 mA per channel for a pulse */
#define N2T_ON_S_DEFAULT        300u      /* kern.n2torch_max_s: the torch turns itself off after 5 min (a new request relights it) */
#define N2T_ON_S_MIN            10u
#define N2T_ON_S_MAX            1800u
#define N2T_FLASH_MS_MAX        1000u
#define N2T_FLASH_DUTY_DIV      4u        /* cool-down = 4 x the pulse: duty <= 20 % */
/* thermal (battery gauge, centi-C): >= 43 C lowest current (sel 0 = 25 mA per channel), >= 46 C off until < 44 C */
#define N2T_WARM_CC             4300
#define N2T_HOT_CC              4600
#define N2T_HOT_CLEAR_CC        4400
#define N2T_THERM_PERIOD_US     5000000ull
#define N2T_FAULT_LATCH         3u        /* consecutive I2C failures: torch disabled for the boot */
/*
 * 0560 (camflash-20261114): the camera flash as a real MT6360 STROBE (Android FLASHLIGHT_MODE_DUAL_FLASH, mt6360_pmu_fled.c):
 *   FLED1_STRB_CTRL2 0x74 / FLED2_STRB_CTRL2 0x78: bit 7 UTRAL_ISTRB (fine range), bits [6:0] ISTRB: 25 mA + 6.25 mA * n (n <= 0x74 = 750 mA);
 *   FLED_STRB_CTRL 0x73 bits [6:0] STRB_TO: the hardware ends the strobe after 64 ms + 32 ms * n (Android 20827: 2400 ms; here pulse + >= 48 ms);
 *   FLED_EN 0x7E: STROBE off, 400 us, CS1|CS2|STROBE on (the LEDs light at once); off = CS1/CS2/STROBE cleared.
 * Oplus 20817/20827 strobe table goes to 1200 mA per channel; here the ceiling is 750 mA per channel (fine range only) and the default
 * 500 mA per channel (1 A for the LED), at most N2T_STROBE_MS_MAX per strobe, the same 1/5 duty cool-down as a 0496 pulse, no strobe when
 * the battery is warm (>= 43 C: a lowest-current torch pulse instead) or hot (off).  A strobe request inside the cool-down or with
 * kern.n2torch_strobe=0 is served as a 0496 torch pulse.
 *   12 bytes  uint32 level (thousandths of kern.n2torch_strobesel), uint32 ms (1..N2T_STROBE_MS_MAX), uint32 flags (bit 0: strobe)
 *   read()    struct n2t_status (below): what the LED does and WHEN it went on / off, in mach_absolute_time() units -- the camera picks
 *             the frames whose whole exposure lies inside the strobe.
 */
#define N2T_REG_FLED_STRB_CTRL  0x73u
#define N2T_REG_FLED1_STRB2     0x74u
#define N2T_REG_FLED2_STRB2     0x78u
#define N2T_UTRAL_ISTRB         0x80u
#define N2T_ISTRB_MASK          0x7Fu
#define N2T_STRB_TO_MASK        0x7Fu
#define N2T_STRB_HW_MAX         0x74u     /* 750 mA per channel (top of the fine range) */
#define N2T_STROBESEL_DEFAULT   0x4Cu     /* 500 mA per channel */
#define N2T_STROBE_MS_MAX       250u
#define N2T_STRB_TO_MARGIN_MS   48u

struct n2t_cfg {
	uint32_t mode;        /* kern.n2torch: 1 allow, 0 off (and kept off) */
	uint32_t maxsel;      /* kern.n2torch_maxsel 0..N2T_SEL_HW_MAX */
	uint32_t flashsel;    /* kern.n2torch_flashsel 0..N2T_SEL_HW_MAX */
	uint32_t max_on_s;    /* kern.n2torch_max_s */
	uint32_t strobe;      /* 0560 kern.n2torch_strobe: 1 = a 12-byte strobe request uses the MT6360 STROBE mode, 0 = it becomes a 0496 pulse */
	uint32_t strobesel;   /* 0560 kern.n2torch_strobesel: ISTRB ceiling 0..N2T_STRB_HW_MAX (25 mA + 6.25 mA x n per channel) */
};

/* a request as written by userland (seq changes on every write; level 0 = off) */
struct n2t_req {
	uint32_t seq, level, ms;
	uint32_t strobe;      /* 0560: 1 = strobe request (12-byte write with flag bit 0) */
};

struct n2t_state {
	uint32_t seen_seq;        /* last request taken */
	uint32_t level, ms;       /* that request */
	uint64_t req_us;          /* when it was taken */
	bool     expired;         /* the on-time limit or the pulse length ran out: dark until a new request */
	uint64_t cool_until_us;   /* no flash current before this */
	uint32_t ceiling;         /* current ceiling of the request taken (maxsel, or flashsel for a pulse outside its cool-down) */
	uint32_t therm;           /* 0 ok, 1 warm, 2 hot */
	uint64_t therm_us;
	int32_t  therm_cc;
	uint32_t faults;          /* consecutive I2C failures */
	bool     latched;         /* N2T_FAULT_LATCH reached */
	/* what the hardware has (owner thread) */
	bool     hw_on;
	uint32_t hw_sel;
	/* counters */
	uint64_t reqs, ons, offs, timeouts, flashes, flash_cooled, therm_derates, therm_offs, io_fails;
	/* 0560: strobe */
	bool     strobe;          /* the request taken is a strobe (cleared when its cool-down or the configuration turns it into a pulse) */
	bool     hw_strobe;       /* the hardware is in STROBE mode now */
	uint64_t strobes, strobe_cooled, strobe_as_pulse;
};

static inline void
n2t_cfg_default(struct n2t_cfg *c)
{
	c->mode = 1; c->maxsel = N2T_MAXSEL_DEFAULT; c->flashsel = N2T_FLASHSEL_DEFAULT; c->max_on_s = N2T_ON_S_DEFAULT;
	c->strobe = 1; c->strobesel = N2T_STROBESEL_DEFAULT;      /* 0560 */
}

static inline void
n2t_cfg_fix(struct n2t_cfg *c)
{
	c->mode = c->mode ? 1u : 0u;
	if (c->maxsel > N2T_SEL_HW_MAX) { c->maxsel = N2T_SEL_HW_MAX; }
	if (c->flashsel > N2T_SEL_HW_MAX) { c->flashsel = N2T_SEL_HW_MAX; }
	if (c->max_on_s < N2T_ON_S_MIN) { c->max_on_s = N2T_ON_S_MIN; }
	if (c->max_on_s > N2T_ON_S_MAX) { c->max_on_s = N2T_ON_S_MAX; }
	c->strobe = c->strobe ? 1u : 0u;                                   /* 0560 */
	if (c->strobesel > N2T_STRB_HW_MAX) { c->strobesel = N2T_STRB_HW_MAX; }
}

static inline void
n2t_init(struct n2t_state *s)
{
	struct n2t_state z = { 0 };
	*s = z;
}

/* userland bytes -> request; false = malformed (wrong size, level > 1000, ms out of range) */
static inline bool
n2t_parse(const uint8_t *b, unsigned len, uint32_t *level, uint32_t *ms)
{
	uint32_t l, m = 0;
	if (len != 4 && len != 8) { return false; }
	l = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
	if (len == 8) { m = (uint32_t)b[4] | (uint32_t)b[5] << 8 | (uint32_t)b[6] << 16 | (uint32_t)b[7] << 24; }
	if (l > 1000) { return false; }
	if (len == 8 && (m == 0 || m > N2T_FLASH_MS_MAX)) { return false; }
	*level = l; *ms = m;
	return true;
}

/* level (1..1000) of a current ceiling sel -> ITOR selector, rounded, never above the ceiling */
static inline uint32_t
n2t_level_sel(uint32_t level, uint32_t ceiling)
{
	if (ceiling > N2T_SEL_HW_MAX) { ceiling = N2T_SEL_HW_MAX; }
	uint32_t s = (level * ceiling + 500u) / 1000u;
	return s > ceiling ? ceiling : s;
}

/* takes a new request (owner thread) */
static inline void
n2t_take(struct n2t_state *s, const struct n2t_req *r, uint64_t now_us)
{
	if (r->seq == s->seen_seq) { return; }
	/* 0560: a pulse / strobe cut short by this request still earns its cool-down (duty <= 1/5 also for back-to-back requests) */
	if (s->ms != 0 && s->level != 0 && !s->expired && s->req_us != 0 && now_us > s->req_us) {
		uint64_t end = s->req_us + (uint64_t)s->ms * 1000u;
		if (end > now_us) { end = now_us; }
		uint64_t cool = end + (end - s->req_us) * N2T_FLASH_DUTY_DIV;
		if (cool > s->cool_until_us) { s->cool_until_us = cool; }
	}
	s->seen_seq = r->seq; s->level = r->level; s->ms = r->ms; s->req_us = now_us; s->expired = false; s->reqs++;
	s->strobe = r->strobe != 0 && r->ms != 0 && r->level != 0;              /* 0560 */
	s->ceiling = 0xffffffffu;                                /* resolved against the configuration in n2t_decide */
	if (r->ms != 0 && r->level != 0) {
		if (now_us < s->cool_until_us) { s->flash_cooled++; s->ceiling = 0xfffffffeu; if (s->strobe) { s->strobe_cooled++; s->strobe = false; } }   /* inside the cool-down: torch current */
		else { s->flashes++; }
	}
}

/* thermal level from a battery reading; invalid readings keep the level.  true = changed */
static inline bool
n2t_therm_update(struct n2t_state *s, bool ok, int32_t cc, uint64_t now_us)
{
	if (!ok) { return false; }
	uint32_t l = s->therm;
	if (cc >= N2T_HOT_CC) { l = 2; }
	else if (l == 2 && cc >= N2T_HOT_CLEAR_CC) { l = 2; }
	else if (cc >= N2T_WARM_CC) { l = 1; }
	else if (l >= 1 && cc >= N2T_WARM_CC - 100) { l = 1; }
	else { l = 0; }
	s->therm_us = now_us; s->therm_cc = cc;
	if (l != s->therm) {
		if (l == 2) { s->therm_offs++; } else if (l == 1 && s->therm == 0) { s->therm_derates++; }
		s->therm = l;
		return true;
	}
	return false;
}

/* the decision: what the LED should be now.  *sel valid when true.  Marks expiry (time limit / end of pulse). */
static inline bool
n2t_decide(struct n2t_state *s, const struct n2t_cfg *c, uint64_t now_us, uint32_t *sel)
{
	if (!c->mode || s->latched || s->level == 0 || s->expired || s->therm >= 2) { return false; }
	uint64_t age = now_us - s->req_us;
	if (s->ms != 0) {                                        /* flash pulse */
		if (age >= (uint64_t)s->ms * 1000u) {
			s->expired = true;
			uint64_t cool = now_us + (uint64_t)s->ms * 1000u * N2T_FLASH_DUTY_DIV;    /* 0560: never shortens a running cool-down */
			if (cool > s->cool_until_us) { s->cool_until_us = cool; }
			return false;
		}
		*sel = n2t_level_sel(s->level, s->ceiling == 0xfffffffeu ? c->maxsel : c->flashsel);
	} else {
		if (age >= (uint64_t)c->max_on_s * 1000000u) { s->expired = true; s->timeouts++; return false; }
		*sel = n2t_level_sel(s->level, c->maxsel);
	}
	if (s->therm == 1) { *sel = 0; }                        /* warm: lowest current */
	return true;
}
/* ---- 0560: strobe ---- */
/* 12-byte request -> level, ms, strobe flag; false = malformed */
static inline bool
n2t_parse12(const uint8_t *b, unsigned len, uint32_t *level, uint32_t *ms, uint32_t *strobe)
{
	if (len != 12) { return false; }
	uint32_t l = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
	uint32_t m = (uint32_t)b[4] | (uint32_t)b[5] << 8 | (uint32_t)b[6] << 16 | (uint32_t)b[7] << 24;
	uint32_t f = (uint32_t)b[8] | (uint32_t)b[9] << 8 | (uint32_t)b[10] << 16 | (uint32_t)b[11] << 24;
	if (l > 1000 || (f & ~1u) != 0) { return false; }
	if (m == 0 || m > ((f & 1u) ? N2T_STROBE_MS_MAX : N2T_FLASH_MS_MAX)) { return false; }
	*level = l; *ms = m; *strobe = f & 1u;
	return true;
}

/* level (1..1000) of an ISTRB ceiling -> ISTRB selector (>= 1 for a lit level), never above the ceiling */
static inline uint32_t
n2t_strobe_sel(uint32_t level, uint32_t ceiling)
{
	if (ceiling > N2T_STRB_HW_MAX) { ceiling = N2T_STRB_HW_MAX; }
	uint32_t s = (level * ceiling + 500u) / 1000u;
	if (s == 0 && level != 0) { s = 1; }
	return s > ceiling ? ceiling : s;
}

/* STRB_TO selector: the hardware timeout (64 + 32 n ms) is at least ms + N2T_STRB_TO_MARGIN_MS */
static inline uint32_t
n2t_strobe_to_sel(uint32_t ms)
{
	uint32_t want = ms + N2T_STRB_TO_MARGIN_MS;
	if (want <= 64u) { return 0; }
	uint32_t n = (want - 64u + 31u) / 32u;
	return n > 74u ? 74u : n;
}

/* per channel mA of an ISTRB selector (fine range) */
static inline uint32_t
n2t_strobe_ma(uint32_t sel)
{
	return 25u + sel * 25u / 4u;
}

/*
 * The decision with its kind: 0 dark, 1 torch mode (torch or 0496 pulse) at *sel (ITOR), 2 strobe at *sel (ISTRB).  A strobe request
 * becomes a pulse (kind 1) when kern.n2torch_strobe=0, inside the cool-down (n2t_take) or with a warm battery; expiry as n2t_decide.
 */
static inline uint32_t
n2t_decide_kind(struct n2t_state *s, const struct n2t_cfg *c, uint64_t now_us, uint32_t *sel)
{
	bool strobe = s->strobe;
	if (!n2t_decide(s, c, now_us, sel)) { return 0; }
	if (strobe && c->strobe && s->therm == 0) {
		*sel = n2t_strobe_sel(s->level, c->strobesel);
		return 2;
	}
	if (strobe && s->level != 0) { s->strobe = false; s->strobe_as_pulse++; }
	return 1;
}

/* what read() of /dev/redwood-torch gives (little endian, 64 bytes) */
#define N2T_STATUS_MAGIC        0x5354324eu       /* "N2TS" */
struct n2t_status {
	uint32_t magic, version;          /* N2T_STATUS_MAGIC, 1 */
	uint32_t seq;                     /* the last request taken by the owner thread */
	uint32_t state;                   /* 0 dark, 1 torch mode, 2 strobe */
	uint64_t on_time;                 /* mach_absolute_time() when the LED last went on (after the enabling write) */
	uint64_t off_time;                /* ... when it last went off (0: not since it went on) */
	uint32_t sel;                     /* ITOR or ISTRB selector of the lit state */
	uint32_t flags;                   /* bit 0 strobe allowed (kern.n2torch_strobe), 1 warm, 2 hot, 3 fault latch, 4 kern.n2torch=0 */
	uint32_t strobesel, strobe_ms_max;
	uint32_t lit_seq;                 /* the request the on_time belongs to */
	uint32_t reserved[3];
};
#endif
