/* Included by a host Objective-C harness after TestLabel and frontend fixtures. */
extern const char *sel_getName(SEL selector);
@interface SamplerTestDescriptor : NSObject {
@public NSUInteger min,mag,mip,s,t,r,compare,anisotropy,reduction; float lodMin,lodMax;
 signed char normalized,average,hasAverage,supportArguments,hasArguments,hasReduction;
}
@end
@implementation SamplerTestDescriptor
- (NSUInteger)minFilter{return min;} - (NSUInteger)magFilter{return mag;}
- (NSUInteger)mipFilter{return mip;} - (NSUInteger)sAddressMode{return s;}
- (NSUInteger)tAddressMode{return t;} - (NSUInteger)rAddressMode{return r;}
- (signed char)normalizedCoordinates{return normalized;}
- (NSUInteger)compareFunction{return compare;} - (NSUInteger)maxAnisotropy{return anisotropy;}
- (float)lodMinClamp{return lodMin;} - (float)lodMaxClamp{return lodMax;}
- (signed char)lodAverage{return average;}
- (signed char)supportArgumentBuffers{return supportArguments;}
- (NSUInteger)reductionMode{return reduction;}
- (signed char)respondsToSelector:(SEL)sel {
 const char *name=sel_getName(sel);if(!name)return 0;
 if(!strcmp(name,"lodAverage"))return hasAverage;
 if(!strcmp(name,"supportArgumentBuffers"))return hasArguments;
 if(!strcmp(name,"reductionMode"))return hasReduction;
 return !strcmp(name,"minFilter")||!strcmp(name,"magFilter")||!strcmp(name,"mipFilter")||
  !strcmp(name,"sAddressMode")||!strcmp(name,"tAddressMode")||!strcmp(name,"rAddressMode")||
  !strcmp(name,"normalizedCoordinates")||!strcmp(name,"compareFunction")||
  !strcmp(name,"maxAnisotropy")||!strcmp(name,"lodMinClamp")||!strcmp(name,"lodMaxClamp");
}
@end

@implementation NSObject (TeguSamplerHostSelectorQuery)
- (signed char)respondsToSelector:(SEL)sel{return 0;}
@end

/* Exact factory (default unless TEGU_SAMPLER_EXACT_FACTORY=0): every Metal
 * field is admitted and recorded; the listed remainder refuses. */
static void test_exact_sampler_factory(TeguComputeDevice *device) {
 tegu_sampler_enable_exact_factory(1);
 SamplerTestDescriptor *d=[[SamplerTestDescriptor alloc] init];
 d->normalized=1;d->anisotropy=1;d->lodMax=__FLT_MAX__;
#define ACCEPT(field,value,reset,check) do { d->field=(value); TeguSampler *x=[device newSamplerStateWithDescriptor:(id)d]; \
  assert(x&&(check));[x release]; d->field=(reset); } while(0)
#define REJECT(field,value,reset) do { d->field=(value); assert(![device newSamplerStateWithDescriptor:(id)d]); d->field=(reset); } while(0)
 for(NSUInteger v=1;v<=2;v++)ACCEPT(mip,v,0,x->state.mip_filter==v);
 for(NSUInteger v=1;v<=5;v++){ACCEPT(s,v,0,x->state.address_s==v);ACCEPT(t,v,0,x->state.address_t==v);ACCEPT(r,v,0,x->state.address_r==v);}
 for(NSUInteger v=1;v<=7;v++)ACCEPT(compare,v,0,x->state.compare_function==v&&!x->state.compare_enabled);
 for(NSUInteger v=2;v<=16;v++)ACCEPT(anisotropy,v,1,x->state.max_anisotropy==v&&x->state.lod_algorithm==N2T_LOD_ANISOTROPIC);
 ACCEPT(lodMin,1.5f,0.0f,x->state.lod_min==1.5f);ACCEPT(lodMax,3.25f,__FLT_MAX__,x->state.lod_max==3.25f);
 ACCEPT(lodMax,1000.0f,__FLT_MAX__,x->state.lod_max==1000.0f);
 ACCEPT(normalized,0,1,!x->state.normalized);
 ACCEPT(min,1,0,x->linearFilters==1);ACCEPT(mag,1,0,x->linearFilters==2);
 REJECT(min,2,0);REJECT(mag,2,0);REJECT(mip,3,0);REJECT(s,6,0);REJECT(t,6,0);REJECT(r,6,0);
 REJECT(compare,8,0);REJECT(anisotropy,17,1);REJECT(anisotropy,0,1);REJECT(lodMin,-1.0f,0.0f);
 d->lodMin=4.0f;d->lodMax=2.0f;assert(![device newSamplerStateWithDescriptor:(id)d]);d->lodMin=0.0f;d->lodMax=__FLT_MAX__;
 d->average=1;d->hasAverage=1;assert(![device newSamplerStateWithDescriptor:(id)d]);d->average=0;d->hasAverage=0;
 d->supportArguments=1;d->hasArguments=1;assert(![device newSamplerStateWithDescriptor:(id)d]);d->supportArguments=0;d->hasArguments=0;
 d->reduction=1;d->hasReduction=1;assert(![device newSamplerStateWithDescriptor:(id)d]);d->reduction=0;d->hasReduction=0;
 /* Exact-only states never reach a legacy sampler word. */
 d->s=2;{TeguSampler *x=[device newSamplerStateWithDescriptor:(id)d];assert(x&&!tegu_sampler_descriptor_supported((id)d));[x release];}d->s=0;
#undef ACCEPT
#undef REJECT
 [d release];
 tegu_sampler_enable_exact_factory(0);
}
static void test_native_sampler_state(TeguComputeDevice *device) {
 test_exact_sampler_factory(device);
 /* Legacy admission (TEGU_SAMPLER_EXACT_FACTORY=0). */
 tegu_sampler_enable_exact_factory(0);
 SamplerTestDescriptor *d=[[SamplerTestDescriptor alloc] init];
 d->normalized=1;d->anisotropy=1;d->s=d->t=d->r=0;d->hasAverage=1;d->lodMax=1000.0f;
 assert(![device newSamplerStateWithDescriptor:(id)d]);
 d->lodMax=__FLT_MAX__;
 id defaultRange=[device newSamplerStateWithDescriptor:(id)d];
 assert(defaultRange);[defaultRange release];
 d->lodMax=0.0f;
 id sampler=[device newSamplerStateWithDescriptor:(id)d];
 assert(sampler&&[sampler device]==device);
 d->min=1;[d release];
 TestLabel *label=[[TestLabel alloc] init];label->value=27;[(TeguSampler *)sampler setLabel:(id)label];
 label->value=99;assert(((TestLabel *)[(TeguSampler *)sampler label])->value==27);[label release];
 assert([(TeguSampler *)sampler label]!=0);
 [(TeguSampler *)sampler setLabel:0];assert(![(TeguSampler *)sampler label]);
 [(TeguSampler *)sampler release];

 /* Older donors may omit lodAverage. Every other policy field is exact. */
 d=[[SamplerTestDescriptor alloc] init];d->normalized=1;d->anisotropy=1;d->s=d->t=d->r=0;
 id valid=[device newSamplerStateWithDescriptor:(id)d];assert(valid);[valid release];
#define REJECT(field,value,reset) do { d->field=(value); assert(![device newSamplerStateWithDescriptor:(id)d]); d->field=(reset); } while(0)
 /* Linear min/mag (Metal 1) are accepted and recorded; other filters refuse. */
 d->min=1;d->mag=1;{TeguSampler *linear=[device newSamplerStateWithDescriptor:(id)d];assert(linear&&linear->linearFilters==3);[linear release];}
 d->mag=0;{TeguSampler *linear=[device newSamplerStateWithDescriptor:(id)d];assert(linear&&linear->linearFilters==1);[linear release];}
 d->min=0;{TeguSampler *nearest=[device newSamplerStateWithDescriptor:(id)d];assert(nearest&&!nearest->linearFilters);[nearest release];}
 REJECT(min,2,0);REJECT(mag,2,0);REJECT(mip,1,0);
 REJECT(s,1,0);REJECT(s,2,0);REJECT(t,1,0);REJECT(t,2,0);REJECT(r,1,0);REJECT(r,2,0);
 REJECT(compare,1,0);REJECT(anisotropy,2,1);REJECT(lodMin,1.0f,0.0f);REJECT(lodMax,1.0f,0.0f);
 d->normalized=0;assert(![device newSamplerStateWithDescriptor:(id)d]);d->normalized=1;
 d->average=1;d->hasAverage=1;assert(![device newSamplerStateWithDescriptor:(id)d]);
 d->average=0;d->hasAverage=0;valid=[device newSamplerStateWithDescriptor:(id)d];assert(valid);[valid release];
 d->supportArguments=1;d->hasArguments=1;assert(![device newSamplerStateWithDescriptor:(id)d]);
 d->supportArguments=0;d->reduction=1;d->hasReduction=1;assert(![device newSamplerStateWithDescriptor:(id)d]);
 d->reduction=0;valid=[device newSamplerStateWithDescriptor:(id)d];assert(valid);[valid release];
 assert(![device newSamplerStateWithDescriptor:0]);
 assert(![device newSamplerStateWithDescriptor:(id)device]);
#undef REJECT
 [d release];
 (void)device;
 unsigned liveBefore=live;
 TeguComputeDevice *held=[TeguComputeDevice alloc];SamplerTestDescriptor *lifeDescriptor=[[SamplerTestDescriptor alloc] init];
 lifeDescriptor->normalized=1;lifeDescriptor->anisotropy=1;lifeDescriptor->s=lifeDescriptor->t=lifeDescriptor->r=0;
 assert(tegu_sampler_descriptor_supported((id)lifeDescriptor));
 TeguComputeDevice *identity=held;
 id life=[held newSamplerStateWithDescriptor:(id)lifeDescriptor];[lifeDescriptor release];
 assert(life);assert([(TeguSampler *)life device]==held);[held release];
 assert(live==liveBefore+2&&[(TeguSampler *)life device]==identity);
 [life release];assert(live==liveBefore);
 tegu_sampler_enable_exact_factory(TEGU_SAMPLER_EXACT_FACTORY);
}
