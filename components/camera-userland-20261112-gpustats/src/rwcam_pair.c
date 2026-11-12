#include <string.h>
#include "rwcam_pair.h"
#include "rwcam_log.h"

static int
slot(int cam)
{
	return cam == RWCAM_BACK_WIDE ? 0 : cam == RWCAM_BACK_ULTRAWIDE ? 1 : -1;
}

void
rwcam_pair_init(struct rwcam_pair *p)
{
	memset(p, 0, sizeof(*p));
	pthread_mutex_init(&p->lock, NULL);
	p->master = RWCAM_BACK_WIDE;
	p->shown = RWCAM_BACK_WIDE;
	p->slave_ratio = 1;
}

/* lock held: the sensor to show while both run */
static int
pick(const struct rwcam_pair *p)
{
	return !p->zoom_known ? p->master : p->zoom < RWCAM_UW_SWITCH_ZOOM ? RWCAM_BACK_ULTRAWIDE : RWCAM_BACK_WIDE;
}

static bool warm(const struct rwcam_pair *p);

/* lock held */
static bool
apply(struct rwcam_pair *p)
{
	bool want = p->active[0] && p->active[1] && (p->started[0] || p->started[1]), ok = true;
	int before = p->mode;

	if (!want) {
		/* (only what is still the group's: a camera started outside the group since -- the front one -- has the reader now) */
		if (p->mode == RWCAM_PAIR_BOTH) {
			if (rwcam_core_pair_running()) {
				rwcam_core_pair_stop();
			}
		} else if (p->mode == RWCAM_PAIR_ALONE) {
			if (rwcam_core_owner() == p->master) {
				rwcam_core_stop(p->master);
			}
		}
		__atomic_store_n(&p->mode, RWCAM_PAIR_IDLE, __ATOMIC_RELAXED);
		p->no_pair = false;
	} else {
		if (p->mode == RWCAM_PAIR_BOTH && !rwcam_core_pair_running()) {
			p->no_pair = true;              /* (a node gave up: the reader went on alone by itself) */
			__atomic_store_n(&p->mode, RWCAM_PAIR_ALONE, __ATOMIC_RELAXED);
		}
		if (p->mode != RWCAM_PAIR_BOTH && !p->no_pair) {
			if (rwcam_core_pair_start(pick(p))) {
				__atomic_store_n(&p->mode, RWCAM_PAIR_BOTH, __ATOMIC_RELAXED);
			} else {
				p->no_pair = true;
			}
		}
		if (p->mode == RWCAM_PAIR_BOTH) {
			__atomic_store_n(&p->shown, pick(p), __ATOMIC_RELAXED);
			p->warm = warm(p);
			rwcam_core_pair_warm(p->warm);          /* (before the change of the sensor shown: it is read whole by then) */
			rwcam_core_pair_show(p->shown);
			rwcam_core_pair_slave_every(p->slave_outputs ? (p->slave_ratio > 0 ? p->slave_ratio : 1) : 0);
		} else {
			if (rwcam_core_owner() != p->master) {
				ok = rwcam_core_start(p->master);
			}
			__atomic_store_n(&p->shown, p->master, __ATOMIC_RELAXED);
			__atomic_store_n(&p->mode, ok ? RWCAM_PAIR_ALONE : RWCAM_PAIR_IDLE, __ATOMIC_RELAXED);
		}
	}
	if (before != p->mode) {
		static const char *const names[] = { "nothing runs", "BOTH back cameras run (the master's pictures; the other one's headers only)",
			"the master ALONE on the one node (no second node: a change of master is a change of sensor)" };

		p->changes++;
		rwcam_say("group: %s; master %s%s", names[p->mode], p->master == RWCAM_BACK_WIDE ? "wide" : "ultra-wide",
		    p->slave_outputs ? ", the slave's pictures asked for too" : "");
	}
	return ok;
}

/* lock held */
static bool
warm(const struct rwcam_pair *p)
{
	return p->zoom_known && p->zoom > RWCAM_PAIR_WARM_FROM && p->zoom < RWCAM_PAIR_WARM_TO;
}

void
rwcam_pair_set_active(struct rwcam_pair *p, bool wide, bool ultra)
{
	pthread_mutex_lock(&p->lock);
	p->active[0] = wide;
	p->active[1] = ultra;
	(void)apply(p);
	pthread_mutex_unlock(&p->lock);
}

void
rwcam_pair_set_master(struct rwcam_pair *p, int cam)
{
	if (slot(cam) < 0) {
		return;
	}
	pthread_mutex_lock(&p->lock);
	if (p->master != cam) {
		__atomic_store_n(&p->master, cam, __ATOMIC_RELAXED);
		p->masters++;
		(void)apply(p);
	}
	pthread_mutex_unlock(&p->lock);
}

void
rwcam_pair_set_zoom(struct rwcam_pair *p, float device_zoom)
{
	pthread_mutex_lock(&p->lock);
	bool moved = device_zoom - p->zoom > 0.0005f || p->zoom - device_zoom > 0.0005f;

	p->zoom = device_zoom;
	p->zoom_known = device_zoom >= 1;
	if (p->mode == RWCAM_PAIR_BOTH && (pick(p) != p->shown || warm(p) != p->warm)) {
		(void)apply(p);
	} else if (p->mode == RWCAM_PAIR_BOTH && p->warm && moved) {
		rwcam_core_pair_warm(true);             /* (still moving near the change: warm for RWCAM_WARM_MS more) */
	}
	pthread_mutex_unlock(&p->lock);
}

void
rwcam_pair_set_slave(struct rwcam_pair *p, bool outputs, unsigned ratio)
{
	pthread_mutex_lock(&p->lock);
	if (p->slave_outputs != outputs || p->slave_ratio != ratio) {
		__atomic_store_n(&p->slave_outputs, outputs, __ATOMIC_RELAXED);
		p->slave_ratio = ratio;
		(void)apply(p);
	}
	pthread_mutex_unlock(&p->lock);
}

bool
rwcam_pair_set_started(struct rwcam_pair *p, int cam, bool started)
{
	bool ok = true;

	if (slot(cam) < 0) {
		return false;
	}
	pthread_mutex_lock(&p->lock);
	if (p->started[slot(cam)] != started) {
		__atomic_store_n(&p->started[slot(cam)], started, __ATOMIC_RELAXED);
		ok = apply(p);
	}
	pthread_mutex_unlock(&p->lock);
	return ok;
}

bool
rwcam_pair_wanted(struct rwcam_pair *p)
{
	pthread_mutex_lock(&p->lock);
	bool both = p->active[0] && p->active[1];

	pthread_mutex_unlock(&p->lock);
	return both;
}

/* (asked by the reader's thread for every frame, also while apply() waits for that thread: no lock) */
int
rwcam_pair_route(struct rwcam_pair *p, int cam)
{
	int mode = __atomic_load_n(&p->mode, __ATOMIC_RELAXED), master = __atomic_load_n(&p->master, __ATOMIC_RELAXED);

	if (slot(cam) < 0 || mode == RWCAM_PAIR_IDLE) {
		return -1;
	}
	if (cam == (mode == RWCAM_PAIR_BOTH ? __atomic_load_n(&p->shown, __ATOMIC_RELAXED) : master)) {
		return __atomic_load_n(&p->started[slot(master)], __ATOMIC_RELAXED) ? master : -1;
	}
	/* the other sensor: its own stream's pictures when the slave's are asked for (and it is the slave's) */
	return mode == RWCAM_PAIR_BOTH && cam != master && __atomic_load_n(&p->slave_outputs, __ATOMIC_RELAXED) &&
	    __atomic_load_n(&p->started[slot(cam)], __ATOMIC_RELAXED) ? cam : -1;
}

bool
rwcam_pair_delivers(struct rwcam_pair *p, int cam)
{
	return rwcam_pair_route(p, cam) >= 0;
}

int
rwcam_pair_shown(struct rwcam_pair *p)
{
	return __atomic_load_n(&p->shown, __ATOMIC_RELAXED);
}

int
rwcam_pair_mode(struct rwcam_pair *p)
{
	pthread_mutex_lock(&p->lock);
	int mode = p->mode;

	pthread_mutex_unlock(&p->lock);
	return mode;
}

int
rwcam_pair_master(struct rwcam_pair *p)
{
	pthread_mutex_lock(&p->lock);
	int master = p->master;

	pthread_mutex_unlock(&p->lock);
	return master;
}
