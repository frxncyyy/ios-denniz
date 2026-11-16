#ifndef TEGU_DEVICE_RENDER_QUERY_CAPS_H
#define TEGU_DEVICE_RENDER_QUERY_CAPS_H

#include <stdint.h>

/* Metal's featureProfile is a donor-specific profile identifier, not a
 * feature-bit mask. UINT64_MAX is deliberately outside the captured donor's
 * recognized profile set; it keeps the Apple-family list empty. */
#define TEGU_METAL_FEATURE_PROFILE_UNQUALIFIED UINT64_MAX

static inline signed char tegu_supports_render_sample_count(uint64_t count,
                                                            int render_backend_built) {
    return (signed char)(render_backend_built && count == 1);
}

/* The graphics frontend implements the Apple1 baseline and the Apple2 floor
 * used by iOS Metal Performance Shaders. This is a compatibility contract,
 * not a claim that the underlying Mali GPU is Apple silicon. Later families
 * remain false until their individual requirements are implemented. */
/* Apple3 stays false: on iOS 16 it also makes backboardd build its display
 * overdrive LUT pipeline in a 10-bit format (R133 crash loop), not only
 * larger draw batches. */
static inline signed char tegu_supports_gpu_family(int64_t family,
                                                   int render_backend_built) {
    return (signed char)(render_backend_built && (family == 1001 || family == 1002));
}

#endif
