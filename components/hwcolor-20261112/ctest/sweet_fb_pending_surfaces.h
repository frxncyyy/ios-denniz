#ifndef SWEET_FB_PENDING_SURFACES_H
#define SWEET_FB_PENDING_SURFACES_H
/*
 * Surface references captured at SwapEnd and held until the panel copy for
 * that record has finished (adapted from ios-tegu 0142).  Pure bookkeeping,
 * host-testable: it never touches IOKit.  All lifecycle transitions are
 * called with the owner's queue lock held; retain/release callbacks run
 * outside it.
 */
#include <stdbool.h>
#include <stdint.h>

enum { SWEET_FB_PENDING_SURFACE_COUNT = 4 };
typedef struct sweet_fb_pending_surfaces {
	void *object[SWEET_FB_PENDING_SURFACE_COUNT];
} sweet_fb_pending_surfaces;
typedef void *(*sweet_fb_retain_id_fn)(uint32_t id);
typedef void (*sweet_fb_release_fn)(void *object);

typedef struct sweet_fb_swap_lifecycle {
	bool stopped;
	sweet_fb_pending_surfaces pending;
} sweet_fb_swap_lifecycle;

static inline void
sweet_fb_pending_surfaces_init(sweet_fb_pending_surfaces *set)
{
	for (unsigned i = 0; i < SWEET_FB_PENDING_SURFACE_COUNT; ++i) set->object[i] = 0;
}

static inline bool
sweet_fb_pending_surfaces_any(const sweet_fb_pending_surfaces *set)
{
	for (unsigned i = 0; i < SWEET_FB_PENDING_SURFACE_COUNT; ++i)
		if (set->object[i] != 0) return true;
	return false;
}

/* Retain only surfaces referenced by the enabled layer mask. A NULL result
 * (id already gone) stays NULL and falls back to a late lookup at present. */
static inline void
sweet_fb_pending_surfaces_capture(sweet_fb_pending_surfaces *set,
    const uint32_t ids[SWEET_FB_PENDING_SURFACE_COUNT], uint32_t mask,
    sweet_fb_retain_id_fn retain_id)
{
	sweet_fb_pending_surfaces_init(set);
	for (unsigned i = 0; i < SWEET_FB_PENDING_SURFACE_COUNT; ++i)
		if ((mask & (1u << i)) && ids[i] != 0) set->object[i] = retain_id(ids[i]);
}

static inline void
sweet_fb_pending_surfaces_release(sweet_fb_pending_surfaces *set,
    sweet_fb_release_fn release_object)
{
	for (unsigned i = 0; i < SWEET_FB_PENDING_SURFACE_COUNT; ++i) {
		void *object = set->object[i];
		set->object[i] = 0;
		if (object != 0) release_object(object);
	}
}

static inline void
sweet_fb_swap_lifecycle_init(sweet_fb_swap_lifecycle *state)
{
	state->stopped = false;
	sweet_fb_pending_surfaces_init(&state->pending);
}

/* New SwapEnd: `incoming` refs move into the queue, the superseded set moves
 * to `replaced` (release it outside the lock). After stop, incoming is handed
 * back in `replaced` so nothing leaks. Returns whether the record was queued. */
static inline bool
sweet_fb_swap_lifecycle_queue(sweet_fb_swap_lifecycle *state,
    sweet_fb_pending_surfaces *incoming, sweet_fb_pending_surfaces *replaced)
{
	if (state->stopped) {
		*replaced = *incoming;
		sweet_fb_pending_surfaces_init(incoming);
		return false;
	}
	*replaced = state->pending;
	state->pending = *incoming;
	sweet_fb_pending_surfaces_init(incoming);
	return true;
}

/* Detach the queued set for presentation; the caller owns the references. */
static inline bool
sweet_fb_swap_lifecycle_take(sweet_fb_swap_lifecycle *state,
    sweet_fb_pending_surfaces *taken)
{
	if (state->stopped) return false;
	*taken = state->pending;
	sweet_fb_pending_surfaces_init(&state->pending);
	return true;
}

/* Present could not start (display ownership failed): put the references back
 * for the retry unless a newer SwapEnd already superseded this record (then
 * `taken` keeps the stale refs and the caller releases them). */
static inline bool
sweet_fb_swap_lifecycle_requeue(sweet_fb_swap_lifecycle *state, bool newer_pending,
    sweet_fb_pending_surfaces *taken)
{
	if (state->stopped || newer_pending) return false;
	state->pending = *taken;
	sweet_fb_pending_surfaces_init(taken);
	return true;
}

static inline void
sweet_fb_swap_lifecycle_stop(sweet_fb_swap_lifecycle *state,
    sweet_fb_pending_surfaces *taken)
{
	state->stopped = true;
	*taken = state->pending;
	sweet_fb_pending_surfaces_init(&state->pending);
}

#endif
