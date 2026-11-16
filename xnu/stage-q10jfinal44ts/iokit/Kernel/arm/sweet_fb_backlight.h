/*
 * sweet_fb_backlight.h -- 0495 (bright-fix-20261114): panel brightness policy for SweetFramebuffer.cpp (pure C, host-tested by
 * bright-fix-20261114/test/bl_test.c).  No kernel calls in here: the caller owns the locks, the clock and the DSI.
 *
 * What iOS sends (CoreBrightness, iOS 16.7, verified by disassembly of the 20H392 cache):
 *  -[CBDisplayContaineriOS findBacklight] matches {IOPropertyMatch: {backlight-control: true}} and keeps the service only if it
 *  conforms to the class AppleARMBacklight; DisplayCreateWithID reads IODisplayParameters.brightness.{min,max,value} (SInt32) from
 *  it; SetBLDriverNitsValue then sets "brightness-nits" on it and SetIOMFBBrightness sets "IOMFBBrightnessLevel" on the
 *  IOMobileFramebuffer service, both as SInt32 = nits * 65536 (CFNumber type 3).  The default curve without calibration
 *  properties spans 5 .. 400 nits; digital dimming stays off as long as "digital-dimming-min-nits" is not published.
 *
 * The panel (Samsung AMS643YE05, Android oplus20615_samsung_ams643ye05_1080p_dsi_cmd.c lcm_setbacklight_cmdq): DCS 0x51 with two
 * bytes MSB first, 4..2047 = normal range (DBV), 2048..4095 = HBM (needs the ELVSS sequence and 0x53 0xE0: never sent here).
 * The DBV is taken as linear in nits (Samsung DBV), full scale SWEET_BL_NITS_FULL (kern.n2bl_nits) = DBV 2047.
 *
 * 0501 (bltorch-20261114), after the first run on the phone (q10jch: one "brightness-nits 0.00" at start, nothing from the slider):
 *  DisplayCreateWithID takes the nits path ONLY when the backlight service has "new-backlight-architecture" = kOSBooleanTrue
 *  (0x1af0d88bc..0x1af0d8904 sets display+0x20); without it display+0x24 is set and every slider / factor change goes through
 *  __DisplaySetPhysicalBrightnessLegacyOnThread = IORegistryEntrySetCFProperties(backlight, {"brightness": SInt32 in
 *  IODisplayParameters.brightness.min..max, "FadePeriod": SInt32, "backlight-level": SInt32}), which 0495 accepted and dropped.
 *  The one "brightness-nits" of a legacy boot is a side path writing the never-computed nits value (0).
 *  Now: "brightness" (raw, min 0 max 2047 = the DBV scale published) is acted on in both architectures; the nits keys only when
 *  the new architecture is published (cfg.arch 1, boot-arg n2bl=2).  Until CoreBrightness has asked once for more than the safe
 *  level (kern.n2bl_safe, DBV 300) nothing below it is shown: a stuck-at-zero requester cannot leave a dark panel again.
 */
#ifndef SWEET_FB_BACKLIGHT_H
#define SWEET_FB_BACKLIGHT_H
#include <stdint.h>
#include <stdbool.h>

#define SWEET_BL_DBV_HW_MAX        2047u     /* normal range top; never HBM */
#define SWEET_BL_DBV_HW_MIN        4u        /* the Android driver's lowest non-AOD level */
#define SWEET_BL_FLOOR_DEFAULT     16u       /* kern.n2bl_floor: never darker than this (a request of 0 nits is not a black panel) */
#define SWEET_BL_MAX_DEFAULT       2047u     /* kern.n2bl_max */
#define SWEET_BL_SAFE_DEFAULT      300u      /* 0501 kern.n2bl_safe: the floor until a request above it has been seen (0 = no such rule) */
#define SWEET_BL_NITS_FULL         400u      /* kern.n2bl_nits default: CoreBrightness' uncalibrated slider top (400 nits) = DBV 2047 */
#define SWEET_BL_NITS_MIN          100u
#define SWEET_BL_NITS_MAX          1000u
#define SWEET_BL_MIN_GAP_US        8000u     /* at most one DCS 0x51 per 8 ms (a fade sends ~60 requests a second) */
/* thermal: battery gauge temperature (centi-C), the only temperature this port reads.  Caps the DBV, with 1 C hysteresis. */
#define SWEET_BL_WARM_CC           4200
#define SWEET_BL_HOT_CC            4500
#define SWEET_BL_HYST_CC           100
#define SWEET_BL_WARM_CAP          1434u     /* 70 % */
#define SWEET_BL_HOT_CAP           1024u     /* 50 % */
#define SWEET_BL_THERM_STALE_US    30000000ull   /* no reading for 30 s: keep the last level (never lift a cap on a missing reading) */

struct sweet_bl_cfg {
	uint32_t mode;          /* kern.n2bl: 0 = requests recorded, no DCS; 1 = act */
	uint32_t floor_dbv;     /* kern.n2bl_floor */
	uint32_t max_dbv;       /* kern.n2bl_max */
	uint32_t nits_full;     /* kern.n2bl_nits */
	uint32_t safe_dbv;      /* 0501 kern.n2bl_safe */
	uint32_t arch;          /* 0501: 0 = legacy architecture published ("brightness" raw), 1 = new ("brightness-nits"); fixed at boot */
};

#define SWEET_BL_REQ_NONE          0u
#define SWEET_BL_REQ_NITS          1u
#define SWEET_BL_REQ_RAW           2u

struct sweet_bl {
	int32_t  req_q16;       /* last nits request, nits 16.16 (-1 = none yet) */
	uint32_t req_kind;      /* 0501: SWEET_BL_REQ_*: which request want_dbv comes from */
	uint32_t req_raw;       /* 0501: last legacy "brightness" (0..SWEET_BL_DBV_HW_MAX) */
	uint32_t ask_dbv;       /* 0501: the request mapped to the DBV scale, before floor / max */
	bool     trusted;       /* 0501: a request above cfg.safe_dbv has been seen: the ordinary floor applies from then on */
	uint32_t want_dbv;      /* request mapped, before the thermal cap */
	uint32_t sent_dbv;      /* last DBV the panel accepted (0 = none / unknown, e.g. after display on) */
	uint64_t last_us;       /* last DCS attempt */
	uint32_t therm;         /* 0 none, 1 warm, 2 hot */
	uint64_t therm_us;      /* last valid reading */
	int32_t  therm_cc;
	/* counters */
	uint64_t reqs, sends, fails, floored, capped, therm_changes, skipped_off;
	uint64_t raw_reqs, nits_reqs, nits_ignored;      /* 0501 */
};

static inline void
sweet_bl_cfg_default(struct sweet_bl_cfg *c)
{
	c->mode = 1; c->floor_dbv = SWEET_BL_FLOOR_DEFAULT; c->max_dbv = SWEET_BL_MAX_DEFAULT; c->nits_full = SWEET_BL_NITS_FULL;
	c->safe_dbv = SWEET_BL_SAFE_DEFAULT; c->arch = 0;
}

/* sanitises a configuration: floor in [HW_MIN, max], max in [HW_MIN, HW_MAX], nits in [NITS_MIN, NITS_MAX] */
static inline void
sweet_bl_cfg_fix(struct sweet_bl_cfg *c)
{
	if (c->max_dbv > SWEET_BL_DBV_HW_MAX) { c->max_dbv = SWEET_BL_DBV_HW_MAX; }
	if (c->max_dbv < SWEET_BL_DBV_HW_MIN) { c->max_dbv = SWEET_BL_DBV_HW_MIN; }
	if (c->floor_dbv < SWEET_BL_DBV_HW_MIN) { c->floor_dbv = SWEET_BL_DBV_HW_MIN; }
	if (c->floor_dbv > c->max_dbv) { c->floor_dbv = c->max_dbv; }
	if (c->nits_full < SWEET_BL_NITS_MIN) { c->nits_full = SWEET_BL_NITS_MIN; }
	if (c->nits_full > SWEET_BL_NITS_MAX) { c->nits_full = SWEET_BL_NITS_MAX; }
	c->mode = c->mode ? 1u : 0u;
	if (c->safe_dbv > c->max_dbv) { c->safe_dbv = c->max_dbv; }
	c->arch = c->arch ? 1u : 0u;
}

static inline void
sweet_bl_init(struct sweet_bl *b)
{
	b->req_q16 = -1; b->want_dbv = 0; b->sent_dbv = 0; b->last_us = 0; b->therm = 0; b->therm_us = 0; b->therm_cc = 0;
	b->reqs = b->sends = b->fails = b->floored = b->capped = b->therm_changes = b->skipped_off = 0;
	b->req_kind = SWEET_BL_REQ_NONE; b->req_raw = 0; b->ask_dbv = 0; b->trusted = false;
	b->raw_reqs = b->nits_reqs = b->nits_ignored = 0;
}

/* 0501: the floor in force: the safe level until a request above it has been seen, then the ordinary floor */
static inline uint32_t
sweet_bl_floor(const struct sweet_bl *b, const struct sweet_bl_cfg *c)
{
	uint32_t f = c->floor_dbv;
	if (!b->trusted && c->safe_dbv > f) { f = c->safe_dbv; }
	if (f > c->max_dbv) { f = c->max_dbv; }
	return f;
}

/* 0501: a request on the DBV scale (already mapped): trust, floor, max.  Returns want_dbv. */
static inline uint32_t
sweet_bl_apply(struct sweet_bl *b, const struct sweet_bl_cfg *c, uint32_t ask, bool count)
{
	if (ask > SWEET_BL_DBV_HW_MAX) { ask = SWEET_BL_DBV_HW_MAX; }
	b->ask_dbv = ask;
	if (c->safe_dbv != 0 && ask > c->safe_dbv) { b->trusted = true; }
	uint32_t f = sweet_bl_floor(b, c), v = ask;
	if (v < f) { v = f; if (count) { b->floored++; } }
	if (v > c->max_dbv) { v = c->max_dbv; }
	b->want_dbv = v;
	return v;
}

/* nits 16.16 -> the DBV scale, rounded, no floor (0501) */
static inline uint32_t
sweet_bl_q16_to_ask(int32_t q16, const struct sweet_bl_cfg *c)
{
	uint64_t full = (uint64_t)c->nits_full << 16;
	uint64_t v = q16 <= 0 ? 0 : ((uint64_t)(uint32_t)q16 * SWEET_BL_DBV_HW_MAX + full / 2) / full;
	return v > SWEET_BL_DBV_HW_MAX ? SWEET_BL_DBV_HW_MAX : (uint32_t)v;
}

/* a new nits request (any thread; the caller serialises).  Returns the mapped DBV; 0 = ignored (0501: the legacy architecture is
 * published, where CoreBrightness' nits writes carry no level: counted, the panel keeps what "brightness" asked). */
static inline uint32_t
sweet_bl_request(struct sweet_bl *b, const struct sweet_bl_cfg *c, int32_t q16)
{
	b->reqs++; b->nits_reqs++;
	if (!c->arch) { b->nits_ignored++; return 0; }
	b->req_q16 = q16 < 0 ? 0 : q16;
	b->req_kind = SWEET_BL_REQ_NITS;
	return sweet_bl_apply(b, c, sweet_bl_q16_to_ask(b->req_q16, c), true);
}

/* 0501: a legacy request: "brightness" in IODisplayParameters.brightness.min..max = 0..SWEET_BL_DBV_HW_MAX.  Returns the mapped DBV. */
static inline uint32_t
sweet_bl_request_raw(struct sweet_bl *b, const struct sweet_bl_cfg *c, int32_t raw)
{
	b->reqs++; b->raw_reqs++;
	b->req_raw = raw < 0 ? 0u : (uint32_t)raw > SWEET_BL_DBV_HW_MAX ? SWEET_BL_DBV_HW_MAX : (uint32_t)raw;
	b->req_kind = SWEET_BL_REQ_RAW;
	return sweet_bl_apply(b, c, b->req_raw, true);
}

/* the configuration changed: re-map the last request */
static inline void
sweet_bl_remap(struct sweet_bl *b, const struct sweet_bl_cfg *c)
{
	if (b->req_kind == SWEET_BL_REQ_NITS) { (void)sweet_bl_apply(b, c, sweet_bl_q16_to_ask(b->req_q16, c), false); }
	else if (b->req_kind == SWEET_BL_REQ_RAW) { (void)sweet_bl_apply(b, c, b->req_raw, false); }
}

/* thermal level from a battery reading (ok = valid).  Hysteresis: a level drops only SWEET_BL_HYST_CC under its threshold.
 * An invalid reading keeps the level.  Returns true when the level changed. */
static inline bool
sweet_bl_therm_update(struct sweet_bl *b, bool ok, int32_t cc, uint64_t now_us)
{
	if (!ok) { return false; }
	uint32_t lvl = b->therm;
	if (cc >= SWEET_BL_HOT_CC) { lvl = 2; }
	else if (cc >= SWEET_BL_WARM_CC) { lvl = lvl == 2 && cc >= SWEET_BL_HOT_CC - SWEET_BL_HYST_CC ? 2u : 1u; }
	else if (lvl == 2) { lvl = cc >= SWEET_BL_HOT_CC - SWEET_BL_HYST_CC ? 2u : cc >= SWEET_BL_WARM_CC - SWEET_BL_HYST_CC ? 1u : 0u; }
	else if (lvl == 1) { lvl = cc >= SWEET_BL_WARM_CC - SWEET_BL_HYST_CC ? 1u : 0u; }
	b->therm_us = now_us; b->therm_cc = cc;
	if (lvl != b->therm) { b->therm = lvl; b->therm_changes++; return true; }
	return false;
}

static inline uint32_t
sweet_bl_cap(const struct sweet_bl *b)
{
	return b->therm >= 2 ? SWEET_BL_HOT_CAP : b->therm == 1 ? SWEET_BL_WARM_CAP : SWEET_BL_DBV_HW_MAX;
}

/* the DBV the panel should show now (0 = no request yet: leave the panel at its boot level) */
static inline uint32_t
sweet_bl_target(const struct sweet_bl *b, const struct sweet_bl_cfg *c)
{
	if (b->req_kind == SWEET_BL_REQ_NONE || b->want_dbv == 0) { return 0; }
	uint32_t t = b->want_dbv, cap = sweet_bl_cap(b), f = sweet_bl_floor(b, c);
	if (t > cap) { t = cap; }
	if (t < f) { t = f; }      /* a cap never goes under the floor (both are user settable) */
	if (t > SWEET_BL_DBV_HW_MAX) { t = SWEET_BL_DBV_HW_MAX; }
	return t;
}

/* what to send now: 0 = nothing (mode 0, no request, already shown, or too soon after the last DCS) */
static inline uint32_t
sweet_bl_due(const struct sweet_bl *b, const struct sweet_bl_cfg *c, uint64_t now_us)
{
	if (!c->mode) { return 0; }
	uint32_t t = sweet_bl_target(b, c);
	if (t == 0 || t == b->sent_dbv) { return 0; }
	if (b->last_us != 0 && now_us - b->last_us < SWEET_BL_MIN_GAP_US) { return 0; }
	return t;
}

/* result of a DCS attempt for dbv */
static inline void
sweet_bl_sent(struct sweet_bl *b, uint32_t dbv, bool ok, uint64_t now_us)
{
	b->last_us = now_us;
	if (ok) { b->sent_dbv = dbv; b->sends++; if (dbv < b->want_dbv) { b->capped++; } }
	else { b->fails++; }
}

/* the DCS packet: 0x51, DBV high byte, low byte (MSB first, as lcm_setbacklight_cmdq) */
static inline void
sweet_bl_packet(uint32_t dbv, uint8_t p[3])
{
	if (dbv > SWEET_BL_DBV_HW_MAX) { dbv = SWEET_BL_DBV_HW_MAX; }
	p[0] = 0x51; p[1] = (uint8_t)(dbv >> 8); p[2] = (uint8_t)dbv;
}
#endif
