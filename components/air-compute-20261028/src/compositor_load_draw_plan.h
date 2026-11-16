#ifndef TEGU_COMPOSITOR_LOAD_DRAW_PLAN_H
#define TEGU_COMPOSITOR_LOAD_DRAW_PLAN_H
#include <stdint.h>

/* The MTL render pass clears once. Subsequent public draw requests map to
 * LOAD in Tegu's retained-target selector; each receives its own texture
 * snapshot. */
#define TEGU_PUBLIC_CLEAR_ACTION 2u
#define TEGU_PUBLIC_LOAD_ACTION 1u
struct tegu_public_draw_plan { uint32_t count; uint32_t action[2]; uint32_t texture_snapshot[2]; };
static inline int tegu_public_parse_alpha(const char *s,uint32_t *alpha) {
    uint32_t parsed=0,digits=0;
    if(!s||!alpha)return -1;
    while(*s&&digits<3&&*s>='0'&&*s<='9'){parsed=parsed*10u+(uint32_t)(*s++-'0');digits++;}
    if(*s||!digits||(parsed!=0&&parsed!=64&&parsed!=128&&parsed!=192&&parsed!=255))return -1;
    *alpha=parsed;return 0;
}
static inline int tegu_public_make_draw_plan(int load_two_draws,struct tegu_public_draw_plan *out) {
    if(!out)return -1;
    struct tegu_public_draw_plan plan={0};
    plan.count=load_two_draws?2u:1u;
    plan.action[0]=TEGU_PUBLIC_CLEAR_ACTION;
    plan.texture_snapshot[0]=0;
    if(load_two_draws){plan.action[1]=TEGU_PUBLIC_LOAD_ACTION;plan.texture_snapshot[1]=1;}
    *out=plan;
    return 0;
}
static inline int tegu_public_byte_matches(unsigned actual,unsigned expected,int blend_mode) {
    unsigned delta=actual>expected?actual-expected:expected-actual;
    return blend_mode?delta<=1u:delta==0u;
}
#endif
