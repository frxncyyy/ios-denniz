/* Bounded native sampler object for the texture compiler's sampler plan.
 *
 * The object retains the exact Metal descriptor state.  The old two-bit
 * linearFilters field is preserved only for the already-qualified legacy
 * sampler-word path; v2 consumers call tegu_sampler_pack_state, which packs the
 * full state through the shared arch9 helper.  Unsupported states are refused,
 * never reduced to nearest/clamp. */
#include "../nord2_texture_resources.h"

@interface TeguSamplerDescriptor : NSObject
- (NSUInteger)minFilter;
- (NSUInteger)magFilter;
- (NSUInteger)mipFilter;
- (NSUInteger) sAddressMode;
- (NSUInteger) tAddressMode;
- (NSUInteger) rAddressMode;
- (signed char) normalizedCoordinates;
- (NSUInteger)compareFunction;
- (NSUInteger)maxAnisotropy;
- (float)lodMinClamp;
- (float)lodMaxClamp;
- (signed char) lodAverage;
- (signed char)supportArgumentBuffers;
- (NSUInteger)reductionMode;
- (NSUInteger)borderColor;
@end
@interface NSObject (TeguSamplerSelectorQuery)
- (signed char)respondsToSelector:(SEL)selector;
@end
@interface TeguComputeDevice (Samplers)
- (id)newSamplerStateWithDescriptor:(TeguSamplerDescriptor *)descriptor;
/* Opt-in exact-state constructor for the v2 render sidecar.  The legacy
 * constructor above keeps its historical narrow admission so a state the old
 * sampler word cannot express is never silently accepted and then reduced. */
- (id)newSamplerStateWithExactDescriptor:(TeguSamplerDescriptor *)descriptor;
@end

@interface TeguSampler : NSObject {
@public TeguComputeDevice *owner; id debugLabel;
 /* Bit 0: minify linear, bit 1: magnify linear (Metal filter 1). */
 unsigned linearFilters;
 /* Exact descriptor state; compare_enabled is per use, set by the planner
  * through tegu_sampler_pack_state, not by the Metal object. */
 struct n2t_sampler_state state;
}
- (id)device;
@end

static int tegu_sampler_descriptor_state(TeguSamplerDescriptor *d, struct n2t_sampler_state *state) {
 if(!d||!state)return 0;
 /* Required selectors.  lodAverage, argument-buffer support, reduction mode
  * and borderColor are optional on older runtimes; absent means that feature
  * was not requested (border defaults to transparent black). */
 if(![d respondsToSelector:@selector(minFilter)] ||
    ![d respondsToSelector:@selector(magFilter)] ||
    ![d respondsToSelector:@selector(mipFilter)] ||
    ![d respondsToSelector:@selector(sAddressMode)] ||
    ![d respondsToSelector:@selector(tAddressMode)] ||
    ![d respondsToSelector:@selector(rAddressMode)] ||
    ![d respondsToSelector:@selector(normalizedCoordinates)] ||
    ![d respondsToSelector:@selector(compareFunction)] ||
    ![d respondsToSelector:@selector(maxAnisotropy)] ||
    ![d respondsToSelector:@selector(lodMinClamp)] ||
    ![d respondsToSelector:@selector(lodMaxClamp)]) return 0;
 memset(state,0,sizeof(*state));
 state->version=N2T_TEXTURE_RESOURCES_VERSION;
 state->min_filter=(uint32_t)[d minFilter];
 state->mag_filter=(uint32_t)[d magFilter];
 state->mip_filter=(uint32_t)[d mipFilter];
 state->address_s=(uint32_t)[d sAddressMode];
 state->address_t=(uint32_t)[d tAddressMode];
 state->address_r=(uint32_t)[d rAddressMode];
 state->normalized=!![d normalizedCoordinates];
 state->compare_enabled=0;
 state->compare_function=(uint32_t)[d compareFunction];
 state->max_anisotropy=(uint32_t)[d maxAnisotropy];
 state->reduction=(uint32_t)(([d respondsToSelector:@selector(reductionMode)])?[d reductionMode]:0);
 state->lod_algorithm=state->max_anisotropy>1?N2T_LOD_ANISOTROPIC:N2T_LOD_ISOTROPIC;
 state->clamp_integer_coords=0;
 state->clamp_integer_array=1;
 state->srgb_override=0;
 state->seamless_cube=1;
 state->magnify_cutoff=0;
 state->lod_min=[d lodMinClamp];
 state->lod_max=[d lodMaxClamp];
 state->lod_bias=0.0f;
 {
  NSUInteger border=([d respondsToSelector:@selector(borderColor)])?[d borderColor]:0;
  if(border>2)return 0;
  state->border_color_kind=(uint32_t)border;
 }
 /* Quality semantics that this driver cannot express are refusals, not
  * silent drops.  lodAverage changes which LOD is selected; argument-buffer
  * support changes how the object may be bound. */
 if([d respondsToSelector:@selector(lodAverage)]&&[d lodAverage])return 0;
 if([d respondsToSelector:@selector(supportArgumentBuffers)]&&[d supportArgumentBuffers])return 0;
 return n2t_sampler_validate(state)==N2T_OK;
}

/* Historical admission used by the old sampler word: only states representable
 * as nearest/linear + clamp + no mips/compare/aniso/LOD range.  Kept separate
 * from exact-state admission so existing consumers cannot lose fields. */
/* Process-wide capability set by the v2 submit owner once the sidecar
 * transport is live.  Until then the ordinary Metal factory keeps the
 * historical narrow admission so old consumers cannot create a state they
 * would silently reduce. */
static signed char tegu_sampler_exact_factory_enabled;
static void __attribute__((unused)) tegu_sampler_enable_exact_factory(int enabled) {
 tegu_sampler_exact_factory_enabled=enabled?1:0;
}

static int tegu_sampler_descriptor_supported(TeguSamplerDescriptor *d) {
 if(!d)return 0;
 if(![d respondsToSelector:@selector(minFilter)] ||
    ![d respondsToSelector:@selector(magFilter)] ||
    ![d respondsToSelector:@selector(mipFilter)] ||
    ![d respondsToSelector:@selector(sAddressMode)] ||
    ![d respondsToSelector:@selector(tAddressMode)] ||
    ![d respondsToSelector:@selector(rAddressMode)] ||
    ![d respondsToSelector:@selector(normalizedCoordinates)] ||
    ![d respondsToSelector:@selector(compareFunction)] ||
    ![d respondsToSelector:@selector(maxAnisotropy)] ||
    ![d respondsToSelector:@selector(lodMinClamp)] ||
    ![d respondsToSelector:@selector(lodMaxClamp)]) return 0;
 float lodMax=[d lodMaxClamp];
 if([d minFilter]>1||[d magFilter]>1||[d mipFilter]!=0||
    [d sAddressMode]!=0||[d tAddressMode]!=0||[d rAddressMode]!=0||
    ![d normalizedCoordinates]||[d compareFunction]!=0||
    [d maxAnisotropy]!=1||[d lodMinClamp]!=0.0f||
    (lodMax!=0.0f&&lodMax!=__FLT_MAX__)) return 0;
 if([d respondsToSelector:@selector(lodAverage)]&&[d lodAverage])return 0;
 if([d respondsToSelector:@selector(supportArgumentBuffers)]&&[d supportArgumentBuffers])return 0;
 if([d respondsToSelector:@selector(reductionMode)]&&[d reductionMode]!=0)return 0;
 return 1;
}

static void tegu_sampler_refused(TeguSamplerDescriptor *d) {
 static unsigned reported;
 if(__atomic_fetch_add(&reported,1,__ATOMIC_RELAXED)>=16)return;
 MARK("UNSUPPORTED sampler_descriptor_policy");
#ifdef TEGU_RENDER_TRACE
 if(d&&[d respondsToSelector:@selector(minFilter)]&&[d respondsToSelector:@selector(magFilter)]&&
    [d respondsToSelector:@selector(mipFilter)]&&[d respondsToSelector:@selector(sAddressMode)]&&
    [d respondsToSelector:@selector(tAddressMode)]&&[d respondsToSelector:@selector(rAddressMode)]&&
    [d respondsToSelector:@selector(normalizedCoordinates)]&&[d respondsToSelector:@selector(compareFunction)]&&
    [d respondsToSelector:@selector(maxAnisotropy)]&&[d respondsToSelector:@selector(lodMinClamp)]&&
    [d respondsToSelector:@selector(lodMaxClamp)]) {
  unsigned lodAverage=[d respondsToSelector:@selector(lodAverage)]?!![d lodAverage]:0;
  unsigned argumentBuffers=[d respondsToSelector:@selector(supportArgumentBuffers)]?!![d supportArgumentBuffers]:0;
  NSUInteger reduction=[d respondsToSelector:@selector(reductionMode)]?[d reductionMode]:0;
  NSUInteger border=[d respondsToSelector:@selector(borderColor)]?[d borderColor]:0;
  char line[448];int n=snprintf(line,sizeof(line),
   "TEGU_SAMPLER_REFUSED min=%lu mag=%lu mip=%lu s=%lu t=%lu r=%lu normalized=%u compare=%lu anisotropy=%lu lod_min=%.9g lod_max=%.9g lod_average=%u argument_buffers=%u reduction=%lu border=%lu\n",
   (unsigned long)[d minFilter],(unsigned long)[d magFilter],(unsigned long)[d mipFilter],
   (unsigned long)[d sAddressMode],(unsigned long)[d tAddressMode],(unsigned long)[d rAddressMode],
   !![d normalizedCoordinates],(unsigned long)[d compareFunction],(unsigned long)[d maxAnisotropy],
   (double)[d lodMinClamp],(double)[d lodMaxClamp],lodAverage,argumentBuffers,(unsigned long)reduction,
   (unsigned long)border);
  if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
 }
#else
 (void)d;
#endif
}

/* Per-use exact packing.  compare_enabled comes from the compiled plan; the
 * same Metal sampler object can be used for a regular and a compare sample.
 * A null format is admissible only when no ClampToZero/border case needs the
 * format's alpha/integer class. */
static int __attribute__((unused)) tegu_sampler_pack_state(TeguSampler *s,unsigned compare_enabled,
 const struct n2t_format_info *format,unsigned char out[N2T_TEXTURE_SAMPLER_BYTES]) {
 if(!s||!out)return N2T_E_INVALID;
 struct n2t_sampler_state state=s->state;
 state.compare_enabled=compare_enabled?1u:0u;
 return n2t_sampler_pack_for_format(&state,format,out);
}

/* True only when the historical two-bit sampler word is an exact projection
 * of this state.  Render must call this before using the old word; false means
 * route through the v2 sidecar or refuse the draw. */
static int __attribute__((unused)) tegu_sampler_legacy_word_ok(TeguSampler *s) {
 if(!s)return 0;
 const struct n2t_sampler_state *st=&s->state;
 return st->min_filter<=1&&st->mag_filter<=1&&st->mip_filter==N2T_MIP_NOT_MIPMAPPED&&
  st->address_s==N2T_ADDRESS_CLAMP_EDGE&&st->address_t==N2T_ADDRESS_CLAMP_EDGE&&
  st->address_r==N2T_ADDRESS_CLAMP_EDGE&&st->normalized==1&&
  st->compare_enabled==0&&st->compare_function==N2T_COMPARE_NEVER&&
  st->max_anisotropy==1&&st->reduction==N2T_REDUCTION_AVERAGE&&
  st->lod_algorithm==N2T_LOD_ISOTROPIC&&st->clamp_integer_coords==0&&
  st->clamp_integer_array==1&&st->srgb_override==0&&st->seamless_cube==1&&
  st->magnify_cutoff==0&&st->border_color_kind==N2T_BORDER_TRANSPARENT_BLACK&&
  st->lod_min==0.0f&&(st->lod_max==0.0f||st->lod_max==__FLT_MAX__)&&
  st->lod_bias==0.0f;
}

static int __attribute__((unused)) tegu_sampler_get_state(TeguSampler *s,struct n2t_sampler_state *out) {
 if(!s||!out)return N2T_E_INVALID;
 *out=s->state;
 return N2T_OK;
}

@implementation TeguSampler
- (id)device{return owner;}
- (id)label{return debugLabel;}
- (void)setLabel:(id)value{id copy=[value copy];[debugLabel release];debugLabel=copy;}
- (void)dealloc{[debugLabel release];[owner release];[super dealloc];}
@end

@implementation TeguComputeDevice (Samplers)
- (id)newSamplerStateWithDescriptor:(TeguSamplerDescriptor *)d {
 struct n2t_sampler_state state;
 if(tegu_sampler_exact_factory_enabled) {
  if(!tegu_sampler_descriptor_state(d,&state)){tegu_sampler_refused(d);return 0;}
 } else {
  if(!tegu_sampler_descriptor_supported(d)){tegu_sampler_refused(d);return 0;}
  if(!tegu_sampler_descriptor_state(d,&state)){tegu_sampler_refused(d);return 0;}
 }
 TeguSampler *s=[[TeguSampler alloc] init];if(!s)return 0;
 s->owner=[self retain];
 s->linearFilters=(state.min_filter==N2T_FILTER_LINEAR?1u:0u)|
                  (state.mag_filter==N2T_FILTER_LINEAR?2u:0u);
 s->state=state;
 return s;
}
- (id)newSamplerStateWithExactDescriptor:(TeguSamplerDescriptor *)d {
 struct n2t_sampler_state state;
 if(!tegu_sampler_descriptor_state(d,&state)){tegu_sampler_refused(d);return 0;}
 TeguSampler *s=[[TeguSampler alloc] init];if(!s)return 0;
 s->owner=[self retain];
 s->linearFilters=(state.min_filter==N2T_FILTER_LINEAR?1u:0u)|
                  (state.mag_filter==N2T_FILTER_LINEAR?2u:0u);
 s->state=state;
 return s;
}
@end
