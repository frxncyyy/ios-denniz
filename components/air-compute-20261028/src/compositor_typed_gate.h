#ifndef TEGU_COMPOSITOR_TYPED_GATE_H
#define TEGU_COMPOSITOR_TYPED_GATE_H

#include "air_bridge.h"

/* Exact input-use contract shared by the captured four-tap and eight-tap
 * compositor classes. The typed range for FS UBO slot 1 is class-specific:
 * 72 bytes for four-tap, 32 bytes for eight-tap. This is distinct from the
 * Mali descriptor declaration size (96/48 bytes respectively). */
static inline const char *tegu_compositor_typed_plan_error(
    const TeguAIRRenderTypedPlan *p, uint32_t vertex_slot, unsigned taps)
{
    if ((taps != 4 && taps != 8) || !p || p->version != 1 || p->count != 7 ||
        p->attachment_count != 2 || p->diagnostic_count || p->neutral_count)
        return "typed plan header mismatch";
    if (p->attachments[0].format != 80 || p->attachments[0].write_mask != 15 ||
        p->attachments[1].format != 80 || p->attachments[1].write_mask != 0)
        return "typed plan attachment mismatch";

    static const uint32_t kind[7] = {1,1,1,1,1,2,3};
    static const uint32_t stage[7] = {1,1,1,2,2,2,2};
    static const uint32_t mask[7] = {16,2,2,2,2,4,0};
    static const uint32_t flags[7] = {33,5,5,5,5,33,24};
    static const uint32_t window[7] = {0,64,16,0,2,0,0};
    const uint32_t index[7] = {vertex_slot,2,3,1,6,3,0};
    const uint32_t fs_window1 = taps == 4 ? 72 : 32;
    unsigned seen = 0;
    for (unsigned i = 0; i < 7; ++i) {
        const TeguAIRRenderResourceUse *u = &p->uses[i];
        unsigned bit = 7;
        for (unsigned j = 0; j < 7; ++j)
            if (u->kind == kind[j] && u->stage == stage[j] && u->index == index[j]) {
                bit = j;
                break;
            }
        if (bit == 7 || (seen & (1u << bit)))
            return "typed plan unknown or duplicated resource";
        seen |= 1u << bit;
        if (u->class_mask != mask[bit]) return "typed plan class mask changed";
        if (u->flags != flags[bit] || u->reserved) return "typed plan access/dependency flags changed";
        uint64_t length = bit == 3 ? fs_window1 : window[bit];
        if (u->range_offset || u->range_length != length)
            return "typed plan resource range changed";
    }
    return seen == 0x7f ? 0 : "typed plan resource set mismatch";
}

#endif
