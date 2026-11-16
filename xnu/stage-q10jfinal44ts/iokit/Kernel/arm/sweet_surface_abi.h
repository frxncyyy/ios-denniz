#ifndef SWEET_SURFACE_ABI_H
#define SWEET_SURFACE_ABI_H
/*
 * Pure, host-testable pieces of the built-in IOSurface ABI (no IOKit).
 *
 * Selector policy.  Every selector the framework may send gets a deliberate answer; names for 5-12, 20, 27 come from the Topaz
 * kernel's reverse engineering (references/ios-topaz 0132/0133), 13/34/35 from the original SweetSurface.
 * 40, 41 and 43 are unidentified: they are tolerated (success, zeroed output) and their call shape is logged once so
 * they can be named; the answer for an unidentified selector is NEVER an error, because the UI is known to run with them accepted.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

enum {
	SWEET_SEL_CREATE = 0, SWEET_SEL_LOOKUP = 4, SWEET_SEL_SET_YCBCR = 5, SWEET_SEL_WRAP_IMAGE = 6, SWEET_SEL_WRAP_MEMORY = 7,
	SWEET_SEL_SET_VALUE = 9, SWEET_SEL_COPY_VALUE = 10, SWEET_SEL_REMOVE_VALUE = 11, SWEET_SEL_BIND_ACCEL = 12,
	SWEET_SEL_LIMITS = 13, SWEET_SEL_SET_PURGEABLE = 20, SWEET_SEL_SET_BULK_ATTACH = 27,
	SWEET_SEL_LOOKUP_PORT = 34, SWEET_SEL_CREATE_PORT = 35,
};

enum sweet_sel_policy {
	SWEET_POLICY_HANDLED = 0,      /* implemented by the dispatcher */
	SWEET_POLICY_ACCEPT,           /* known, deliberately has no effect: success, zeroed output */
	SWEET_POLICY_PURGEABLE,        /* state kept per surface, old state returned */
	SWEET_POLICY_NOT_FOUND,        /* no such value: kIOReturnNotFound */
	SWEET_POLICY_UNSUPPORTED,      /* fail so the caller falls back (CoreAnimation uploads pixels instead) */
	SWEET_POLICY_UNKNOWN,          /* unidentified: tolerated, shape logged once */
};

static inline enum sweet_sel_policy
sweet_selector_policy(uint32_t sel)
{
	switch (sel) {
	case SWEET_SEL_CREATE: case SWEET_SEL_LOOKUP: case SWEET_SEL_LIMITS:
	case SWEET_SEL_LOOKUP_PORT: case SWEET_SEL_CREATE_PORT:
		return SWEET_POLICY_HANDLED;
	case SWEET_SEL_SET_YCBCR:        /* surface, matrix: only matters to video */
	case SWEET_SEL_SET_VALUE:        /* IOSurfaceSetValue: values are not kept (nothing reads them back; CopyValue says so) */
	case SWEET_SEL_REMOVE_VALUE:
	case SWEET_SEL_BIND_ACCEL:       /* surface, accelerator id, sub id */
	case SWEET_SEL_SET_BULK_ATTACH:  /* colour space / transfer function attachments */
		return SWEET_POLICY_ACCEPT;
	case SWEET_SEL_COPY_VALUE:
		return SWEET_POLICY_NOT_FOUND;
	case SWEET_SEL_SET_PURGEABLE:
		return SWEET_POLICY_PURGEABLE;
	case SWEET_SEL_WRAP_IMAGE:       /* IOSurfaceWrapClientImage / memory: not implemented -> fail, CA uploads instead */
	case SWEET_SEL_WRAP_MEMORY:
		return SWEET_POLICY_UNSUPPORTED;
	default:
		return SWEET_POLICY_UNKNOWN;
	}
}

/* IOSurfacePurgeable: 3 only asks; the previous state is always returned. */
static inline uint32_t
sweet_purgeable_update(uint32_t *state, uint32_t requested)
{
	uint32_t old = *state;
	if (requested != 3) {
		*state = requested;
	}
	return old;
}

/*
 * Per-plane record inside the lock result (at +0x68 + 0x80 * plane), as read by IOSurface.framework (U2-PROGRESS.md 63-66):
 * +0 width, +4 height, +0xc offset, +0x10 bytesPerRow, +0x14 size, +0x18 bytesPerElement (u16), +0x1a/+0x1b element
 * width/height.  +0x08 is not bytes-per-row (an earlier version wrote it there and IOSurfaceGetBytesPerRowOfPlane returned 0).
 */
struct sweet_plane_abi {
	uint32_t width, height, bytesPerRow, offset, size;
	uint16_t bytesPerElement;
	uint8_t elementWidth, elementHeight;
};
#define SWEET_PLANE_RECORD_BYTES 0x80

static inline void
sweet_plane_record(uint8_t *p, const struct sweet_plane_abi *pl)
{
	memset(p, 0, SWEET_PLANE_RECORD_BYTES);
	memcpy(p + 0x00, &pl->width, 4);
	memcpy(p + 0x04, &pl->height, 4);
	memcpy(p + 0x0c, &pl->offset, 4);
	memcpy(p + 0x10, &pl->bytesPerRow, 4);
	memcpy(p + 0x14, &pl->size, 4);
	memcpy(p + 0x18, &pl->bytesPerElement, 2);
	p[0x1a] = pl->elementWidth;
	p[0x1b] = pl->elementHeight;
}

/*
 * IOSurfaceAddress / IOSurfaceClientAddress: the creator hands over memory it already filled (CoreAnimation does this for
 * images without a pixel-format code).  The surface keeps a SNAPSHOT copy of `used` bytes taken at creation: correct for the
 * immutable images this path is used for (icons, layer contents), unlike zeros.  Returns 0 if the range is unusable.
 */
static inline uint64_t
sweet_client_range_bytes(uint64_t address, uint64_t offset, uint64_t rows_bytes, uint64_t plane_end, uint64_t alloc)
{
	uint64_t used = offset + rows_bytes;
	if (plane_end > used) {
		used = plane_end;
	}
	if (address == 0 || used == 0 || used > alloc || address + used < address) {
		return 0;
	}
	return used;
}
#endif
