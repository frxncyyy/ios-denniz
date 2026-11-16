#include "draw_reorder.inc"
/* Translate retained Metal bindings into the owned-handle selector16 contract.
 * This adapter supplies no GPU addresses. The kernel independently resolves
 * and retains every handle, validates ranges and constructs GPU descriptors. */
static int tegu_compositor_buffer_binding(const struct TeguCompositorBufferFact *f,
 TeguBuffer *buffer,TeguComputeDevice *owner,uint32_t expectedClass,
 uint32_t window,struct tegu_render_vertex_binding *out) {
 if(!buffer||buffer->owner!=owner||!buffer->handle||f->handle!=buffer->handle||
    f->classMask!=expectedClass||f->offset>buffer->logical||
    f->available!=buffer->logical-f->offset||f->bindingResidual>15||
    (f->offset&15)||f->bindingResidual>f->available||f->rangeOffset)return -1;
 if(window) {
  if(f->usageFlags!=5||f->rangeLength!=window||window>f->available-f->bindingResidual)return -1;
 } else if(f->usageFlags!=33||f->rangeLength)return -1;
 U64 offset=f->offset+f->bindingResidual,length=f->available-f->bindingResidual;
 if(offset>UINT32_MAX||!length||length>UINT32_MAX-offset)return -1;
 *out=(struct tegu_render_vertex_binding){f->handle,offset,length};return 0;
}

static int tegu_b3_private_mip0_texture(TeguTexture *t,TeguComputeDevice *owner) {
 static const struct tegu_texture_level exact[10]={
  {776,24,3104,0},{388,12,1552,74496},{194,6,776,93120},{97,3,388,97792},{48,1,192,99008},
  {24,1,96,99200},{12,1,48,99328},{6,1,24,99392},{3,1,12,99456},{1,1,4,99520}};
 if(!t||[t device]!=owner||t->surfaceImported||!t->buffer||t->buffer->owner!=owner||!t->buffer->handle||
    t->format!=80||t->textureWidth!=776||t->textureHeight!=24||t->rowBytes!=3104||
    t->textureUsage!=5||t->textureResourceOptions!=0x20||t->levelCount!=10||t->buffer->logical!=99524)return 0;
 for(unsigned i=0;i<10;i++)if(memcmp(&t->mip[i],&exact[i],sizeof(exact[i])))return 0;
 return 1;
}

static int tegu_fixed_ui_request_for_job(const struct Dispatch *j,TeguComputeDevice *owner,
 struct tegu_ui1_bind_request *out,U64 args[4]) {
 TeguRenderPipeline *pipeline=j?(TeguRenderPipeline *)j->renderPipeline:0;
 if(!j||!owner||!out||!args||!j->fixedUiSnapshot||!pipeline||
    !pipeline->fixedUiSnapshotOnly||
    j->fixedUiPlan.version!=1||j->fixedUiPlan.binding_count!=2||j->fixedUiPlan.push_count!=2||
    j->compositorVertexBufferMask!=((1u<<1)|(1u<<2))||j->compositorFragmentBufferMask!=(1u<<1)||
    j->compositorLoadAction>2||j->compositorStoreAction!=1)return -95;
 TeguTexture *target=j->renderTexture;TeguBuffer *vertex=j->compositorVertexBuffers[1];
 TeguBuffer *vb=j->compositorVertexBuffers[2],*fb=j->compositorFragmentBuffers[1];
 if(!target||[target device]!=owner||!vertex||!vb||!fb||vertex->owner!=owner||vb->owner!=owner||fb->owner!=owner)
  return -95;
 U64 targetHandle=target->surfaceImported?target->importedHandle:(target->buffer?target->buffer->handle:0);
 U64 targetLength=target->surfaceImported?target->surfaceAllocSize:(target->buffer?target->buffer->logical:0);
 if(!targetHandle||!targetLength||!vertex->handle||!vb->handle||!fb->handle)return -95;
 struct tegu_ui1_bind_request r={0};r.version=1;r.width=(uint32_t)target->textureWidth;
 r.height=(uint32_t)target->textureHeight;r.topology=TEGU_UI1_TOPOLOGY_TRIANGLE;
 /* Indexed compositor triangle lists are expanded into retained vertices
  * before this adapter runs.  Preserve that count instead of silently
  * submitting only the first triangle. */
 U64 vertexCount=j->compositorVertexFacts[1].rangeLength/48u;
 if(vertexCount<3||vertexCount>384||vertexCount%3||j->compositorVertexFacts[1].rangeLength!=vertexCount*48u)return -95;
 r.vertex_count=(uint32_t)vertexCount;r.instance_count=1;
 r.target.allocation=(struct tegu_ui1_allocation){targetHandle,targetHandle,targetLength,target->mip[0].offset};
 r.target.format=80;r.target.width=r.width;r.target.height=r.height;r.target.row_bytes=(uint32_t)target->rowBytes;
 const struct TeguCompositorBufferFact *vf=&j->compositorVertexFacts[1];
 r.vertex=(struct tegu_ui1_allocation){vertex->handle,vertex->handle,vertex->logical,vf->offset};
 r.vertex_slot=1;r.vertex_stride=48;
 TeguBuffer *buffers[2]={vb,fb};const struct TeguCompositorBufferFact *facts[2]={&j->compositorVertexFacts[2],&j->compositorFragmentFacts[1]};
 for(unsigned i=0;i<2;i++) {
  const TeguAIRFixedUiR80Binding *b=&j->fixedUiPlan.bindings[i];
  if(b->resource_length!=buffers[i]->logical||facts[i]->handle!=buffers[i]->handle||
     facts[i]->offset!=b->bind_offset||b->descriptor_offset>b->resource_length||
     b->descriptor_length>b->resource_length-b->descriptor_offset)return -95;
  r.resources[i].allocation=(struct tegu_ui1_allocation){buffers[i]->handle,buffers[i]->handle,
   buffers[i]->logical,b->bind_offset};
  r.resources[i].metal_stage=b->stage;r.resources[i].metal_slot=b->index;
  r.resources[i].descriptor_offset=b->descriptor_offset;r.resources[i].descriptor_length=b->descriptor_length;
 }
 memcpy(r.vertex_push,j->fixedUiPlan.push_bytes,4);memcpy(r.fragment_push,j->fixedUiPlan.push_bytes+4,8);
 r.pass.render_width=r.pass.viewport_width=r.width;
 r.pass.render_height=r.pass.viewport_height=r.height;
 r.pass.scissor_x=j->fixedUiScissor[0];r.pass.scissor_y=j->fixedUiScissor[1];
 r.pass.scissor_width=j->fixedUiScissor[2];r.pass.scissor_height=j->fixedUiScissor[3];
 r.pass.load_action=j->compositorLoadAction;r.pass.store_action=j->compositorStoreAction;
 r.pass.clear_color[0]=(uint32_t)j->renderArgs[3];
 U64 scalars[4]={pipeline->handle,targetHandle,target->mip[0].offset,j->renderArgs[3]};
 *out=r;memcpy(args,scalars,sizeof(scalars));return 0;
}


/* One scratch/LOAD image pair per device, reused by every TGJ1 job.  Jobs
 * submit synchronously (the call returns after GPU completion), so the lock
 * held across build and submit is the only ordering needed.  R128 lost jobs
 * to transient allocation failures while creating ~456 KiB per job. */
static unsigned tegu_job_images_lock;
static TeguComputeDevice *tegu_job_images_owner;
static TeguBuffer *tegu_job_images_scratch,*tegu_job_images_load;
static int tegu_job_images_acquire(TeguComputeDevice *d,TeguBuffer **scratch,TeguBuffer **load) {
 tegu_lock(&tegu_job_images_lock);
 if(tegu_job_images_owner!=d||!tegu_job_images_scratch||!tegu_job_images_load) {
  [tegu_job_images_scratch release];[tegu_job_images_load release];
  tegu_job_images_scratch=[d newBufferWithLength:TEGU_GENERIC_JOB_SCRATCH_BYTES options:0];
  tegu_job_images_load=[d newBufferWithLength:TEGU_GENERIC_JOB_LOAD_BYTES options:0];
  tegu_job_images_owner=d;
 }
 *scratch=tegu_job_images_scratch;*load=tegu_job_images_load;
 return *scratch&&*load&&(*scratch)->handle&&(*load)->handle&&tegu_buffer_bytes(*scratch)&&tegu_buffer_bytes(*load);
}
static void tegu_job_images_release(void) {tegu_unlock(&tegu_job_images_lock);}

/* TGJ1: build in userspace the job the XNU UI1/UI2 clients built, then submit
 * it through selector 19.  The images are copied by the kernel before
 * submission, so per-job allocations are released on return. */
static int tegu_generic_job_submit(const struct Dispatch *j,TeguComputeDevice *d,U64 *reply,unsigned *n) {
 TeguRenderPipeline *pipeline=j?j->renderPipeline:0;
 if(!d||!pipeline||!pipeline->package||!(d->capabilities&TEGU_GPU_CAP_GENERIC_JOB))return -95;
 TeguBuffer *scratch=0,*load=0;int images=tegu_job_images_acquire(d,&scratch,&load);
 struct tegu_generic_job_request *r=malloc(sizeof(*r));int error=-95;
 if(r)memset(r,0,sizeof(*r));
 if(images&&r) {
  struct tegu_generic_job_images jobImages={scratch->handle,load->handle,(uint8_t *)tegu_buffer_bytes(scratch),(uint8_t *)tegu_buffer_bytes(load),
   j->fixedUiSamplerLinear};
  int built=-1;
  if(j->fixedUiTextureSnapshot) {
   built=tegu_ui2_generic_job_build(&pipeline->fixedUiTextureInfo,pipeline->package,pipeline->packageBytes,
    &j->fixedUiTextureRequest,&jobImages,r);
#ifdef TEGU_RENDER_TRACE
   if(built) {
    const struct tegu_ui2_bind_request *q=&j->fixedUiTextureRequest;
    char detail[300];snprintf(detail,sizeof(detail),"ui2 build=%d target=%ux%u row=%u texture=%ux%u row=%u alloc=%llu vertices=%u load=%u",
     built,q->width,q->height,q->target.row_bytes,q->texture.width,q->texture.height,q->texture.row_bytes,
     (U64)q->texture.allocation.length,q->vertex_count,q->pass.load_action);
    tegu_render_generic_refused(-95,detail);
   }
#endif
  }
  else if(j->fixedUiSnapshot) {
   struct tegu_ui1_bind_request request;U64 args[4];struct tegu_ui1_info info;
   if(!tegu_fixed_ui_request_for_job(j,d,&request,args)&&!tegu_ui1_parse(pipeline->package,pipeline->packageBytes,&info))
    built=tegu_ui1_generic_job_build(&info,pipeline->package,pipeline->packageBytes,&request,&jobImages,r);
  }
  if(!built){r->flags|=j->batchFlags;*n=2;error=d->method(d->connection,TEGU_GENERIC_JOB_SELECTOR,0,0,r,sizeof(*r),reply,n,0,0);}
#ifdef TEGU_RENDER_TRACE
  if(!built&&!error)tegu_target_grid_trace(j,j->fixedUiSnapshot?"ui1":"ui2");
#endif
 }
 free(r);tegu_job_images_release();return error;
}

#ifdef TEGU_RENDER_TRACE
/* CPU read of three target pixels (centre, home-indicator band, top-left)
 * through the imported IOSurface, for before/after job comparison. */
static int tegu_generic_pixel_probe(id target,uint32_t out[3]) {
 TeguTexture *t=(TeguTexture *)target;const struct TeguIOSurfaceAPI *api=tegu_iosurface_api();uint32_t seed=0;
 if(!t||!api||!t->surfaceImported||!t->importedSurface||!t->textureWidth||!t->textureHeight)return 0;
 if(api->lock((void *)t->importedSurface,1,&seed))return 0;
 const unsigned char *base=api->getBaseAddress((void *)t->importedSurface);size_t row=api->getBytesPerRow((void *)t->importedSurface);
 NSUInteger w=t->textureWidth,h=t->textureHeight;
 NSUInteger xs[3]={w/2,w/2,8},ys[3]={h/2,h-20,8};
 for(unsigned i=0;i<3;i++)out[i]=base?*(const uint32_t *)(base+ys[i]*row+xs[i]*4):0;
 api->unlock((void *)t->importedSurface,1,&seed);return base!=0;
}
#endif
#ifdef TEGU_RENDER_TRACE
/* After the last job of a chain on an imported (display) target: how much
 * of a 16x32 grid has colour, and a few samples. Distinguishes "nothing
 * drawn" from "drawn opaque black" (R141-R143 screenshots: RGB 0, alpha 255). */
static void tegu_target_grid_trace(const struct Dispatch *j,const char *kind) {
 static unsigned grids;
 if(!j||(j->batchFlags&TEGU_GENERIC_JOB_TARGET_CONTINUES))return;
 TeguTexture *t=(TeguTexture *)j->renderTexture;const struct TeguIOSurfaceAPI *api=tegu_iosurface_api();uint32_t seed=0;
 if(!t||!t->textureWidth||!t->textureHeight||(t->format!=80&&t->format!=70))return;
 int imported=t->surfaceImported&&t->importedSurface&&api;
 if(!imported&&(!t->buffer||!tegu_buffer_bytes(t->buffer)||!t->rowBytes))return;
 /* First 60, then every 25th: later frames (home screen) stay visible. */
 {unsigned n=__atomic_fetch_add(&grids,1,__ATOMIC_RELAXED);if(n>=60&&n%25)return;}
 if(imported&&api->lock((void *)t->importedSurface,1,&seed))return;
 const unsigned char *base=imported?api->getBaseAddress((void *)t->importedSurface):(const unsigned char *)tegu_buffer_bytes(t->buffer);
 size_t row=imported?api->getBytesPerRow((void *)t->importedSurface):t->rowBytes;
 unsigned rgb=0,alpha=0;uint32_t sample[4]={0};
 for(unsigned gy=0;base&&gy<32;gy++)for(unsigned gx=0;gx<16;gx++) {
  uint32_t v=*(const uint32_t *)(base+(gy*t->textureHeight/32+t->textureHeight/64)*row+(gx*t->textureWidth/16+t->textureWidth/32)*4);
  rgb+=(v&0xffffffu)!=0;alpha+=(v>>24)!=0;
  if(gx==8&&(gy==4||gy==12||gy==20||gy==28))sample[gy/8]=v;
 }
 if(imported)api->unlock((void *)t->importedSurface,1,&seed);
 char line[256];int len=snprintf(line,sizeof(line),"TEGU_TARGET_GRID %s fs=%s id=%llu target=%ux%u rgb=%u alpha=%u/512 col8=%08x,%08x,%08x,%08x\n",
  kind,j->genericName,(U64)j->renderArgs[1],(unsigned)t->textureWidth,(unsigned)t->textureHeight,rgb,alpha,sample[0],sample[1],sample[2],sample[3]);
 if(len>0)write(2,line,(size_t)len<sizeof(line)?(size_t)len:sizeof(line)-1);
}
#endif
static U64 tegu_shadow_copy(TeguTexture *t,int toSurface) {
 const struct TeguIOSurfaceAPI *api=tegu_iosurface_api();uint32_t seed=0;
 /* Refresh skip: the surface is unchanged (same seed) since a sync after
  * which the shadow held its bytes, and no GPU job wrote the shadow since.
  * Saves the lock/unlock and the full copy (device: ~1.7 MB per command). */
 if(!toSurface&&t&&t->shadowSeedValid&&api&&api->getSeed&&t->shadowSurface&&
    api->getSeed((void *)t->shadowSurface)==t->shadowSeed)return 0;
 /* YCbCr decode shadows read both planes of a locked surface. */
 if(t&&t->shadowConvert==TEGU_SHADOW_CONVERT_YCBCR) {
  if(toSurface||!t->shadowSurface||!api||!t->buffer)return 0;
  void *ys=(void *)t->shadowSurface;unsigned char *backing=(unsigned char *)tegu_buffer_bytes(t->buffer);
  t->shadowSeedValid=0;
  if(!backing||api->lock(ys,1,&seed))return 0;
  int ok=tegu_ycbcr_shadow_decode(t,api,ys,backing);
  uint32_t after=seed;int unlocked=api->unlock(ys,1,&after);
  if(ok&&!unlocked&&api->getSeed){t->shadowSeed=seed;t->shadowSeedValid=1;}
  return ok?(U64)t->textureWidth*t->textureHeight*(tegu_texture_bytes_per_pixel(t->format)):0;
 }
 /* Byte-exact row copy of the shadow's own (uncompressed) format, or, for a
  * decode shadow (shadowConvert: Apple XR formats), the surface's XR texels
  * decoded to the RGBA16F backing; an XR shadow with render-target usage is
  * encoded back to the surface after a job rendered into it. */
 NSUInteger bpp=t?tegu_texture_bytes_per_pixel(t->format):0;
 NSUInteger sourceBpp=t&&t->shadowConvert?tegu_xr_source_bpp(t->shadowConvert):bpp;
 if(!t||!t->shadowSurface||!api||!t->buffer||!bpp||!sourceBpp||tegu_texture_is_astc(t->format)||
    (t->shadowConvert&&(t->format!=115||(toSurface&&!(t->textureUsage&4u))))||!t->textureWidth||!t->textureHeight)return 0;
 void *surface=(void *)t->shadowSurface;
 size_t row=0,sw=0,sh=0,salloc=0,width=t->textureWidth*bpp,height=t->textureHeight;
 size_t sourceWidth=t->textureWidth*sourceBpp;
 /* A plane shadow (shadowPlane) mirrors one plane and is sampled-only. */
 if((t->shadowPlane&&toSurface)||!tegu_shadow_geometry(api,surface,t->shadowPlane,0,&row,&sw,&sh,&salloc)||!row||
    sw<t->textureWidth||sh<height||row<sourceWidth||salloc/row<height||t->rowBytes<width||
    t->mip[0].offset+(height-1)*t->rowBytes+width>t->buffer->logical)return 0;
 t->shadowSeedValid=0;
 if(api->lock(surface,toSurface?0:1,&seed))return 0;
 unsigned char *s=0,*b=(unsigned char *)tegu_buffer_bytes(t->buffer);
 if(!tegu_shadow_geometry(api,surface,t->shadowPlane,&s,&row,&sw,&sh,&salloc))s=0;
 if(s&&b)for(size_t y=0;y<height;y++) {
  unsigned char *line=b+t->mip[0].offset+y*t->rowBytes;
  if(t->shadowConvert&&toSurface)tegu_xr_encode_row(t->shadowConvert,(const uint16_t *)(const void *)line,s+y*row,t->textureWidth);
  else if(t->shadowConvert)tegu_xr_decode_row(t->shadowConvert,s+y*row,(uint16_t *)(void *)line,t->textureWidth);
  else if(toSurface)memcpy(s+y*row,line,width);else memcpy(line,s+y*row,width);
 }
 uint32_t after=seed;int unlocked=api->unlock(surface,toSurface?0:1,&after);
 /* Read-only refresh: seed of the bytes copied. Write-back: the seed our own
  * unlock produced (read back, so a later writer is still detected). */
 if(s&&b&&!unlocked&&api->getSeed){t->shadowSeed=toSurface?api->getSeed(surface):seed;t->shadowSeedValid=1;}
 return s&&b?(U64)width*height:0;
}
/* Each shadow once per command buffer: refreshed from its IOSurface before
 * the jobs (sampled or loaded), written back when a job rendered into it.
 * Returns the bytes copied. */
/* Whether tegu_shadow_sync_jobs(jobs,count,out) would copy anything (same
 * texture selection, no copy). */
static int tegu_shadow_jobs_any(struct Dispatch *const *jobs,unsigned count,int out) {
 for(unsigned i=0;i<count;i++) {
  const struct Dispatch *j=&TEGU_JOB(jobs,i);
  for(unsigned k=0;k<32;k++) {
   TeguTexture *t=(TeguTexture *)(k==31?j->renderTexture:j->compositorTextures[k]);
   if(t&&!(out&&k!=31)&&tegu_owned_class(t,"TeguTexture")&&t->shadowSurface)return 1;
  }
  for(unsigned k=0;k<31;k++) {
   TeguTexture *t=(TeguTexture *)j->v2Textures[k];
   if(t&&!(out&&!(t->textureUsage&2u))&&tegu_owned_class(t,"TeguTexture")&&t->shadowSurface)return 1;
  }
 }
 return 0;
}
static U64 tegu_shadow_sync_jobs(struct Dispatch *const *jobs,unsigned count,int out) {
 id done[64];unsigned seen=0;U64 bytes=0;
 for(unsigned i=0;i<count;i++) {
  const struct Dispatch *j=&TEGU_JOB(jobs,i);
  /* Graphics snapshots. Only the render target is written back. */
  for(unsigned k=0;k<32;k++) {
   TeguTexture *t=(TeguTexture *)(k==31?j->renderTexture:j->compositorTextures[k]);
   if(!t||!tegu_owned_class(t,"TeguTexture")||!t->shadowSurface||(out&&k!=31))continue;
   unsigned m=0;while(m<seen&&done[m]!=t)m++;
   if(m<seen)continue;
   if(seen<64)done[seen++]=t;
   bytes+=tegu_shadow_copy(t,out);
  }
  /* Before the jobs: a shadow any job may write (render target, MRT/resolve
   * slots) must be refreshed next time unless written back successfully. */
  if(!out)for(unsigned k=23;k<32;k++) {
   TeguTexture *t=(TeguTexture *)(k==31?j->renderTexture:j->compositorTextures[k]);
   if(t&&tegu_owned_class(t,"TeguTexture")&&t->shadowSurface)t->shadowSeedValid=0;
  }
  /* Compute v2 textures. Refresh every shadow texture before the jobs;
   * write back only textures whose Metal usage includes ShaderWrite. This
   * avoids clobbering sampled-only IOSurfaces while covering storage images.
   * If compute later exposes an exact per-descriptor access mask, replace
   * this conservative usage filter with it. */
  for(unsigned k=0;k<31;k++) {
   TeguTexture *t=(TeguTexture *)j->v2Textures[k];
   if(!t||!tegu_owned_class(t,"TeguTexture")||!t->shadowSurface||
      (out&&!(t->textureUsage&2u)))continue;
   unsigned m=0;while(m<seen&&done[m]!=t)m++;
   if(m<seen)continue;
   if(seen<64)done[seen++]=t;
   bytes+=tegu_shadow_copy(t,out);
  }
  if(!out)for(unsigned k=0;k<31;k++) {
   TeguTexture *t=(TeguTexture *)j->v2Textures[k];
   if(t&&(t->textureUsage&2u)&&tegu_owned_class(t,"TeguTexture")&&t->shadowSurface)t->shadowSeedValid=0;
  }
 }
 return bytes;
}
/* Reflection-driven draw: same per-job images, built from the pipeline's
 * compiled shader and the draw-time request. */
#ifdef NORD2_JM_TRANSPORT
/* Additive bridge entry; the committed bridge may not have it yet. A missing
 * symbol is an explicit refusal for the v2 classes, never a level0 fallback. */
#if defined(__APPLE__)
extern int n2mb_draw_v2(uintptr_t,const struct tegu_generic_draw_shader *,
 const struct tegu_generic_draw_request *,const struct n2r_texture_resources *,
 uint64_t *,unsigned *) __attribute__((weak_import));
#else
extern int n2mb_draw_v2(uintptr_t,const struct tegu_generic_draw_shader *,
 const struct tegu_generic_draw_request *,const struct n2r_texture_resources *,
 uint64_t *,unsigned *) __attribute__((weak));
#endif
#if defined(__APPLE__)
extern int n2mb_draw_v3(uintptr_t,const struct tegu_generic_draw_shader *,
 const struct tegu_generic_draw_request *,const struct n2r_texture_resources *,
 const struct n2r_draw_options *,uint64_t *,unsigned *) __attribute__((weak_import));
#else
extern int n2mb_draw_v3(uintptr_t,const struct tegu_generic_draw_shader *,
 const struct tegu_generic_draw_request *,const struct n2r_texture_resources *,
 const struct n2r_draw_options *,uint64_t *,unsigned *) __attribute__((weak));
#endif
#endif
/* Immutable per-job shader selection. renderArgs[0] is the 1-based pipeline
 * variant index; zero is the byte-identical default compile. A stale index is
 * a refusal, never a fallback to different code. */
static const struct tegu_generic_draw_shader *tegu_generic_job_shader(const struct Dispatch *j) {
 TeguRenderPipeline *p=j?(TeguRenderPipeline *)j->renderPipeline:0;
 if(!p)return 0;
 uint64_t selected=j->renderArgs[0];
 if(!selected)return &p->genericShader;
 if(selected>__atomic_load_n(&p->genericVariantCount,__ATOMIC_ACQUIRE))return 0;
 return &p->genericVariants[selected-1].shader;
}
static int tegu_generic_draw_submit(const struct Dispatch *j,TeguComputeDevice *d,U64 *reply,unsigned *n) {
 TeguRenderPipeline *pipeline=j?j->renderPipeline:0;
 if(!d||!pipeline||!pipeline->genericDraw||!j->genericDrawSnapshot||!(d->capabilities&TEGU_GPU_CAP_GENERIC_JOB))return -95;
 const struct tegu_generic_draw_shader *shader=tegu_generic_job_shader(j);
 if(!shader){tegu_render_generic_refused(-95,"selected shader variant is unavailable");return -95;}
#ifdef NORD2_JM_TRANSPORT
 int v2need=tegu_generic_v2_required(j);
 struct tegu_render_generic_sidecar *sc=0;
 if(j->compositorPlan.bytes&&j->compositorPlan.length>=sizeof(*sc)) {
  struct tegu_render_generic_sidecar *candidate=(void *)j->compositorPlan.bytes;
  if(candidate->magic==TEGU_RENDER_SIDECAR_MAGIC&&candidate->version==TEGU_RENDER_SIDECAR_VERSION)sc=candidate;
 }
 int trueMrt=sc&&sc->count>1;
 int msaa=sc&&sc->msaa;
 int useV4=trueMrt||msaa;
 uint32_t tls=sc?sc->tls:0;
 int v3need=j->genericDrawFlags||j->genericDrawRequest.target_format==10||tls;
 if(useV4||v3need||v2need) {
  struct tegu_generic_v2_sidecar sidecar;char why[160]={0};
  if(tegu_generic_v2_build_job(j,&sidecar,j->genericDrawFlags&&!v2need,why,sizeof(why))) {
   tegu_render_generic_refused(-95,why);return -95;
  }
  struct n2r_texture_resources resources={N2R_TEXTURE_RESOURCES_VERSION,sidecar.count,sidecar.bindings};
  struct n2r_draw_options options={N2R_DRAW_OPTIONS_VERSION,j->genericDrawFlags,
   j->genericVertexBase,j->genericBaseInstance,tls,0};
  if(useV4) {
#if defined(N2MB_RENDER_TARGETS_MSAA_VERSION)
   if(msaa) {
    struct n2mb_render_targets targets={N2MB_RENDER_TARGETS_MSAA_VERSION,sc->count,sc->targets,
     sc->blends,sc->sample_counts,sc->resolves};
    return n2mb_draw_v4(d->connection,shader,&j->genericDrawRequest,&resources,&options,&targets,reply,n);
   }
#endif
#if defined(N2MB_RENDER_TARGETS_BLEND_VERSION)
   struct n2mb_render_targets targets={N2MB_RENDER_TARGETS_BLEND_VERSION,sc->count,sc->targets,sc->blends,NULL,NULL};
#else
   struct n2mb_render_targets targets={N2MB_RENDER_TARGETS_VERSION,sc->count,sc->targets};
#endif
   return n2mb_draw_v4(d->connection,shader,&j->genericDrawRequest,&resources,&options,&targets,reply,n);
  }
  if(v3need) {
   if(!n2mb_draw_v3) {
    tegu_render_generic_refused(-95,"n2mb_draw_v3 unavailable");return -95;
   }
   return n2mb_draw_v3(d->connection,shader,&j->genericDrawRequest,&resources,&options,reply,n);
  }
  if(!n2mb_draw_v2) {
   tegu_render_generic_refused(-95,"n2mb_draw_v2 unavailable");return -95;
  }
  return n2mb_draw_v2(d->connection,shader,&j->genericDrawRequest,
   &resources,reply,n);
 }
 return n2mb_draw(d->connection,shader,&j->genericDrawRequest,reply,n);
#endif
 TeguBuffer *scratch=0,*load=0;int images=tegu_job_images_acquire(d,&scratch,&load);
#ifdef TEGU_RENDER_TRACE
 if(!images) {
  char detail[160];snprintf(detail,sizeof(detail),"job image allocation failed scratch=%d load=%d allocated=%llu",
   scratch!=0,load!=0,(U64)[d currentAllocatedSize]);
  tegu_render_generic_refused(-97,detail);
 }
#endif
 struct tegu_generic_job_request *r=malloc(sizeof(*r));int error=-95;
 if(r)memset(r,0,sizeof(*r));
 if(images&&r) {
  struct tegu_generic_job_images jobImages={scratch->handle,load->handle,(uint8_t *)tegu_buffer_bytes(scratch),(uint8_t *)tegu_buffer_bytes(load),0};
  int built=tegu_generic_draw_job_build(shader,&j->genericDrawRequest,&jobImages,r);
#ifdef TEGU_RENDER_TRACE
  /* Do GPU-rendered sources hold data when the sampling job is submitted? */
  static unsigned sources;
  for(unsigned b=0;!built&&b<31;b++) {
   TeguTexture *t=(TeguTexture *)j->compositorTextures[b];
   if(!t||!t->buffer||t->textureWidth<16||!t->rowBytes||(t->format!=80&&t->format!=70))continue;
   {unsigned n=__atomic_fetch_add(&sources,1,__ATOMIC_RELAXED);if(n>=60&&n%25)continue;}
   const unsigned char *base=(const unsigned char *)tegu_buffer_bytes(t->buffer);unsigned rgb=0;
   for(unsigned gy=0;base&&gy<16;gy++)for(unsigned gx=0;gx<16;gx++)
    rgb+=(*(const uint32_t *)(base+t->mip[0].offset+(gy*t->textureHeight/16)*t->rowBytes+(gx*t->textureWidth/16)*4)&0xffffffu)!=0;
   char line[240];int len=snprintf(line,sizeof(line),"TEGU_SOURCE_AT_SUBMIT fs=%s target=%llu slot=%u tex=%ux%u row=%u off=%llu handle=%llu usage=%lu shadow=%u rgb=%u/256\n",
    j->genericName,(U64)j->renderArgs[1],b,(unsigned)t->textureWidth,(unsigned)t->textureHeight,(unsigned)t->rowBytes,
    (U64)t->mip[0].offset,(U64)t->buffer->handle,(unsigned long)t->textureUsage,t->shadowSurface!=0,rgb);
   if(len>0)write(2,line,(size_t)len<sizeof(line)?(size_t)len:sizeof(line)-1);
  }
  uint32_t before[3]={0},after[3]={0};int probe=0;
  static unsigned probes;
  if(!built&&__atomic_fetch_add(&probes,1,__ATOMIC_RELAXED)<12)probe=tegu_generic_pixel_probe(j->renderTexture,before);
#endif
  if(!built) {
   r->flags|=j->batchFlags;*n=2;error=d->method(d->connection,TEGU_GENERIC_JOB_SELECTOR,0,0,r,sizeof(*r),reply,n,0,0);
#ifdef TEGU_RENDER_TRACE
   if(probe&&!error&&tegu_generic_pixel_probe(j->renderTexture,after)) {
   char line[256];int len=snprintf(line,sizeof(line),
    "TEGU_GENERIC_PIXELS fs=%s center=%08x->%08x bar=%08x->%08x corner=%08x->%08x\n",j->genericName,
    before[0],after[0],before[1],after[1],before[2],after[2]);
   if(len>0)write(2,line,(size_t)len<sizeof(line)?(size_t)len:sizeof(line)-1);
   }
   if(!error)tegu_target_grid_trace(j,"generic");
#endif
  } else {
   error=-96;
   {
   const struct tegu_generic_draw_request *q=&j->genericDrawRequest;
   char detail[300];int at=snprintf(detail,sizeof(detail),"build=%d vertex=%llu",built,(U64)q->vertex.bytes);
   for(uint32_t k=0;k<q->binding_count&&at>0&&(size_t)at<sizeof(detail);k++)
    at+=snprintf(detail+at,sizeof(detail)-(size_t)at," b%u:k%u:%llu",q->bindings[k].binding,q->bindings[k].kind,(U64)q->bindings[k].memory.bytes);
   tegu_render_generic_refused(-96,detail);
   }
  }
 }
 free(r);tegu_job_images_release();return error;
}
/* Consecutive generic draws of one target that continue it (LOAD) go to the
 * kernel as ONE job: one tiler pass, one load and one store of the target
 * instead of one full-target pass per draw.  *consumed is the number of
 * jobs submitted (0: build jobs[first] alone).  Blits, preclears, other
 * targets, CLEAR passes and framebuffer-fetch draws end the run.
 * 0204 kernels (TEGU_GPU_CAP_GENERIC_JOB2) take the whole run as one TGJ2
 * job (selector 20: 32 allocations, 400 commands, 64 draws); older kernels
 * get TGJ1 with 8 allocations and 160 (CAP_GENERIC_JOB_160) or 128 commands. */
static unsigned tegu_batch_traces;
/* Heap estimate scale in percent: raised when a TGJ2 job reports more tiler
 * heap use than the estimate, so later batches stay inside the 2 MiB. */
static unsigned tegu_batch_heap_scale=100;
static unsigned tegu_batch_small_heap;
static unsigned tegu_small_heap_traces[2];
/* Batch merge (0 = off, the default; TEGU_BATCH_HEAP_SLACK_BYTES in
 * --batch-merge builds unless TEGU_BATCH_HEAP=0): bytes of the heap budget
 * kept back for the estimate's additive error.  A job only raises the scale
 * when it used more than its scaled estimate PLUS this slack.  R205: 64x64 and
 * 105x71 jobs used 4-8 KB against 3-5 KB estimated.  That raised the
 * process-wide scale to ~170% and cut full-screen batches at ~3 draws ("heap"
 * ended 16.5% of jobs), while no full-screen job came near its estimate
 * (max 400 KB used vs 563+ KB estimated).  The planned budget shrinks by the
 * slack, so a job may still be off by the slack without passing the old
 * budget. */
#define TEGU_BATCH_HEAP_SLACK_BYTES (64u<<10)
static unsigned tegu_batch_heap_slack;
/* *why (TEGU_PERF_BATCH, counting only): why the job that runs jobs[first]
 * ends before the next job, whether it is this batch or (consumed 0) the
 * single-draw job the caller builds. */
#ifdef NORD2_JM_TRANSPORT
/* JM render-pass batching (n2mb_draw_batch): consecutive generic draws of one
 * render-pass segment become ONE job - chained MALLOC_VERTEX tiler jobs and a
 * single fragment job (one preload + one writeback of the target instead of
 * one per draw). Gate: TEGU_JM_BATCH=1 enables, =0 disables; the build
 * default is off unless TEGU_JM_BATCH_DEFAULT is defined. A batch the bridge
 * refuses before submission falls back to the exact per-draw path. */
/* TEGU_JM_BATCH is an A/B toggle (ab_schedule.inc; same env/build default):
 * encode reads the command's word, execution the executing command's. */
static int tegu_jm_batch_enabled_for(unsigned abWord) {
 return tegu_ab_value(abWord,TEGU_AB_JM_BATCH);
}
static int tegu_jm_batch_enabled(void) {
 return tegu_jm_batch_enabled_for(tegu_ab_exec_set?tegu_ab_exec_word:tegu_ab_current());
}
#ifdef TEGU_FRONTEND_HOST
static int tegu_jm_batch_force=-1; /* host tests: -1 env/default, 0 off, 1 on */
#endif
struct tegu_jm_batch_slot {
 struct tegu_generic_v2_sidecar sidecar;struct n2r_texture_resources resources;struct n2r_draw_options options;
};
/* A draw that may share a frame with its neighbours: single target, no
 * thread storage, no MRT/MSAA, no read of the target it renders. Returns 0
 * when it can, else the TEGU_PERF_BREAK reason it cannot. */
#define TEGU_JM_BREAK_READS (TEGU_BREAK_BUILDER+TEGU_BATCH_STOP_READS_TARGET)
#define TEGU_JM_BREAK_SHAPE (TEGU_BREAK_BUILDER+TEGU_BATCH_STOP_SHAPE)
#define TEGU_JM_BREAK_SPANS (TEGU_BREAK_BUILDER+TEGU_BATCH_STOP_SPANS)
/* TEGU_BREAK_DETAIL: once per (reason, pipeline) - which pipelines split
 * render passes (c6l: memfetch and mrt were ~8 of 16 kernel jobs per
 * command). Capped; no addresses. */
static void tegu_jm_break_detail(const struct Dispatch *j,unsigned reason) {
 static struct {const void *p;unsigned reason;} seen[64];static unsigned seenCount,lock;
 TeguRenderPipeline *p=j?(TeguRenderPipeline *)j->renderPipeline:0;
 if(!p)return;
 while(__atomic_exchange_n(&lock,1,__ATOMIC_ACQUIRE)){}
 unsigned fresh=seenCount<64;
 for(unsigned i=0;i<seenCount;i++)if(seen[i].p==p&&seen[i].reason==reason){fresh=0;break;}
 if(fresh){seen[seenCount].p=p;seen[seenCount].reason=reason;seenCount++;}
 __atomic_store_n(&lock,0,__ATOMIC_RELEASE);
 if(!fresh)return;
 char inputs[48]="";size_t at=0;
 for(unsigned i=0;i<p->genericCompile.count&&i<16;i++)if(p->genericCompile.bindings[i].kind==TEGU_MALI_GENERIC_INPUT_ATTACHMENT){
  int k=snprintf(inputs+at,sizeof(inputs)-at,"%s%u",at?",":"",p->genericCompile.bindings[i].input_attachment_index);
  if(k>0&&(size_t)k<sizeof(inputs)-at)at+=(size_t)k;
 }
 const struct tegu_generic_draw_request *q=&j->genericDrawRequest;
 const struct tegu_generic_draw_shader *sh=tegu_generic_job_shader(j);
 unsigned kinds=0;for(unsigned i=0;i<q->binding_count&&i<32;i++)kinds|=1u<<q->bindings[i].kind;
 char line[300];int n=snprintf(line,sizeof(line),
  "TEGU_BREAK_DETAIL reason=%s fs=%.40s rts=%u main=%u active=0x%x true_mrt=%u mrt=%u fetch=%u inputs=%s tile=%u target=%ux%u f%u load=%u kinds=0x%x\n",
  tegu_break_names[reason<TEGU_BREAK_REASONS?reason:0],j->genericName[0]?j->genericName:"-",p->genericRtCount,p->genericMainRt,
  p->genericRtActive,p->genericTrueMrt,p->genericMrt,p->genericCompile.reads_framebuffer,inputs[0]?inputs:"-",
  sh?(sh->flags&TEGU_GENERIC_SHADER_TILE_FETCH)!=0:0,q->width,q->height,q->target_format,q->load_action,kinds);
 if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
}
static unsigned tegu_jm_batch_draw_reason(const struct Dispatch *j,const struct Dispatch *j0,int member) {
 TeguRenderPipeline *p=j?(TeguRenderPipeline *)j->renderPipeline:0;
 if(!j)return TEGU_BREAK_END;
 if(j->blit)return TEGU_BREAK_BLIT;
 if(j->nord2ClearOnly)return TEGU_BREAK_CLEAR;
 if(!j->genericDrawSnapshot||!p||!p->genericDraw||!tegu_generic_job_shader(j))return TEGU_BREAK_OTHER_JOB;
 /* Tile-fetch (LD_TILE) draws read their own pixel from the tile buffer, so
  * they share a frame (bridge batch_compatible admits TILE_ATTACHMENT). */
 const struct tegu_render_generic_sidecar *sc=0;
 if(j->compositorPlan.bytes&&j->compositorPlan.length>=sizeof(*sc)) {
  const struct tegu_render_generic_sidecar *c=(const void *)j->compositorPlan.bytes;
  if(c->magic==TEGU_RENDER_SIDECAR_MAGIC&&c->version==TEGU_RENDER_SIDECAR_VERSION)sc=c;
 }
 if(sc&&sc->count>1){tegu_jm_break_detail(j,TEGU_BREAK_MRT);return TEGU_BREAK_MRT;}
 if(sc&&sc->msaa){tegu_jm_break_detail(j,TEGU_BREAK_MSAA);return TEGU_BREAK_MSAA;}
 const struct tegu_generic_draw_request *q=&j->genericDrawRequest,*q0=&j0->genericDrawRequest;
 if((sc&&sc->tls)||q->target_format==10||!q->target_token)return TEGU_JM_BREAK_SHAPE;
 for(unsigned i=0;i<q->binding_count;i++) {
  unsigned kind=q->bindings[i].kind;
  if(kind==TEGU_GENERIC_DRAW_ATTACHMENT){tegu_jm_break_detail(j,TEGU_BREAK_MEMFETCH);return TEGU_BREAK_MEMFETCH;} /* in-place memory fetch: per job */
  if(q->bindings[i].memory.token&&q->bindings[i].memory.token==q->target_token){tegu_jm_break_detail(j,TEGU_BREAK_OWNTEX);return TEGU_BREAK_OWNTEX;}
 }
 if(!member)return 0;
 if(j->nord2CopySources[0]||j->nord2CopySources[1])return TEGU_BREAK_PRECLEAR;
 if(q->target_token!=q0->target_token||q->target_offset!=q0->target_offset||q->target_length!=q0->target_length||
    q->target_format!=q0->target_format||q->width!=q0->width||q->height!=q0->height||q->row_bytes!=q0->row_bytes||
    q->load_action!=1)return TEGU_BREAK_TARGET;
 return 0;
}
static int tegu_jm_draw_batch_submit(struct Dispatch *const *jobs,unsigned first,unsigned count,
 TeguComputeDevice *d,U64 *reply,unsigned *n,unsigned *consumed,unsigned *why) {
 int enabled=tegu_jm_batch_enabled();
#ifdef TEGU_FRONTEND_HOST
 if(tegu_jm_batch_force>=0)enabled=tegu_jm_batch_force;
#endif
 if(!enabled||!d||first>=count)return 0;
 struct Dispatch *j0=&TEGU_JOB(jobs,first);
 unsigned reason=tegu_jm_batch_draw_reason(j0,j0,0);
 if(reason){*why=reason;return 0;} /* this draw cannot share a frame */
 /* The bridge's runtime draws-per-job cap (NORD2_METAL_BATCH_MAX, A/B). */
 unsigned cap=n2mb_batch_capacity(d->connection,0);if(cap<2||cap>N2MB_BATCH_CAPACITY)cap=N2MB_BATCH_MAX;
 unsigned run=1;reason=TEGU_BREAK_END;
 while(run<cap&&first+run<count&&!(reason=tegu_jm_batch_draw_reason(&TEGU_JOB(jobs,first+run),j0,1)))run++;
 if(run==cap)reason=TEGU_BREAK_MAX_DRAWS;else if(first+run>=count)reason=TEGU_BREAK_END;
 if(run<2){*why=reason;return 0;}
 struct tegu_jm_batch_slot *slots=calloc(run,sizeof(*slots));
 struct n2mb_batch_draw draws[N2MB_BATCH_CAPACITY];
 if(!slots)return 0;
 unsigned built=0;
 for(;built<run;built++) {
  struct Dispatch *j=&TEGU_JOB(jobs,first+built);
  struct tegu_jm_batch_slot *x=&slots[built];
  int v2need=tegu_generic_v2_required(j),v3need=j->genericDrawFlags!=0;
  draws[built]=(struct n2mb_batch_draw){tegu_generic_job_shader(j),&j->genericDrawRequest,0,0};
  if(v2need||v3need) {
   char whyText[160]={0};
   if(tegu_generic_v2_build_job(j,&x->sidecar,j->genericDrawFlags&&!v2need,whyText,sizeof(whyText)))break;
   x->resources=(struct n2r_texture_resources){N2R_TEXTURE_RESOURCES_VERSION,x->sidecar.count,x->sidecar.bindings};
   draws[built].resources=&x->resources;
   if(v3need) {
    x->options=(struct n2r_draw_options){N2R_DRAW_OPTIONS_VERSION,j->genericDrawFlags,
     j->genericVertexBase,j->genericBaseInstance,0,0};
    draws[built].options=&x->options;
   }
  }
 }
 if(built<2){free(slots);return 0;}
 /* The exec loop re-expands only the job it is at; members run now. */
 for(unsigned k=1;k<built;k++){struct Dispatch *j=&TEGU_JOB(jobs,first+k);if(j->expandOut)tegu_dispatch_reexpand(j);}
 int e=n2mb_draw_batch(d->connection,draws,built,(uint64_t *)reply,n);
 free(slots);
 if(e)return 0; /* refused before or by submission: exact per-draw fallback */
 unsigned submitted=n2mb_batch_last_count(d->connection); /* the prefix that fit one submission */
 if(submitted<2){*n=0;return -1;}
 *consumed=submitted;
 *why=submitted<built?TEGU_JM_BREAK_SPANS:built<run?TEGU_JM_BREAK_SHAPE:reason;
 return 0;
}
#endif
static int tegu_generic_draw_batch_submit(struct Dispatch *const *jobs,unsigned first,unsigned count,
 TeguComputeDevice *d,U64 *reply,unsigned *n,unsigned *consumed,unsigned *why) {
 *consumed=0;*why=TEGU_BREAK_NO_BATCH;
#ifdef NORD2_JM_TRANSPORT
 /* JM: render-pass segment batching through n2mb_draw_batch (gated). */
 return tegu_jm_draw_batch_submit(jobs,first,count,d,reply,n,consumed,why);
#endif
#ifdef TEGU_NO_DRAW_BATCH
 (void)jobs;(void)first;(void)count;(void)d;(void)reply;(void)n;return 0;
#else
 const struct Dispatch *j0=&TEGU_JOB(jobs,first);
 if(!d||!(d->capabilities&TEGU_GPU_CAP_GENERIC_JOB)||!j0->genericDrawSnapshot||!j0->renderArgs[1])return 0;
 const int tgj2=(d->capabilities&TEGU_GPU_CAP_GENERIC_JOB2)!=0;
 unsigned scale=__atomic_load_n(&tegu_batch_heap_scale,__ATOMIC_RELAXED);
 const U64 slack=__atomic_load_n(&tegu_batch_heap_slack,__ATOMIC_RELAXED)<TEGU_GENERIC_BATCH_HEAP_BUDGET/2?
  __atomic_load_n(&tegu_batch_heap_slack,__ATOMIC_RELAXED):TEGU_GENERIC_BATCH_HEAP_BUDGET/2;
 const struct tegu_generic_batch_limits limits={tgj2?TEGU_GENERIC_JOB2_VERSION:TEGU_GENERIC_JOB_VERSION,
  tgj2?TEGU_GENERIC_JOB2_MAX_COMMANDS:(d->capabilities&TEGU_GPU_CAP_GENERIC_JOB_160)?TEGU_GENERIC_JOB_MAX_COMMANDS:TEGU_GENERIC_JOB_SUBMIT_COMMANDS,
  tgj2?TEGU_GENERIC_BATCH_MAX_DRAWS:TEGU_GENERIC_BATCH_TGJ1_DRAWS,(U64)(TEGU_GENERIC_BATCH_HEAP_BUDGET-slack)*100u/(scale?scale:100u),
  /* Preserve initial feedback/calibration for legacy workloads. */
  tgj2&&scale>=200&&tegu_generic_tile_pixels()==1024&&__atomic_load_n(&tegu_batch_small_heap,__ATOMIC_RELAXED)?TEGU_GENERIC_SMALL_HEAP_BUDGET:0};
 const struct tegu_generic_draw_shader *shaders[TEGU_GENERIC_BATCH_MAX_DRAWS];
 const struct tegu_generic_draw_request *requests[TEGU_GENERIC_BATCH_MAX_DRAWS];
 unsigned run=0,b=first,ended=TEGU_BREAK_END;
 for(;b<count&&run<limits.max_draws;b++) {
  struct Dispatch *j=&TEGU_JOB(jobs,b);TeguRenderPipeline *p=j->renderPipeline;
  if(j->blit||!j->genericDrawSnapshot||!p||!p->genericDraw||j->renderArgs[1]!=j0->renderArgs[1]||
     j->renderArgs[2]!=j0->renderArgs[2]||(b>first&&(j->genericPreclear||j->genericDrawRequest.load_action!=1))) {
   ended=j->blit?TEGU_BREAK_BLIT:!j->genericDrawSnapshot||!p||!p->genericDraw?TEGU_BREAK_OTHER_JOB:
    j->renderArgs[1]!=j0->renderArgs[1]||j->renderArgs[2]!=j0->renderArgs[2]?TEGU_BREAK_TARGET:
    j->genericPreclear?TEGU_BREAK_PRECLEAR:TEGU_BREAK_CLEAR;
   break;
  }
  /* The main loop re-expands only the job it submits. */
  if(b>first&&j->expandOut)tegu_dispatch_reexpand(j);
  if(j->renderTexture!=j0->renderTexture){ended=TEGU_BREAK_TARGET;break;}
  const struct tegu_generic_draw_shader *selected=tegu_generic_job_shader(j);
  if(!selected){ended=TEGU_BREAK_OTHER_JOB;break;}
  shaders[run]=selected;requests[run++]=&j->genericDrawRequest;
 }
 if(b<count&&run>=limits.max_draws&&ended==TEGU_BREAK_END)ended=TEGU_BREAK_MAX_DRAWS;
 *why=ended;
 if(run<2)return 0;
 TeguBuffer *scratch=0,*load=0;int images=tegu_job_images_acquire(d,&scratch,&load);
 *why=TEGU_BREAK_NO_IMAGES;
 struct tegu_generic_job2_request *r=images?malloc(sizeof(*r)):0;
 struct tegu_generic_job_request *v1=images&&!tgj2?malloc(sizeof(*v1)):0;int error=0;
 if(r&&(tgj2||v1)) {
  memset(r,0,sizeof(*r));
  struct tegu_generic_job_images jobImages={scratch->handle,load->handle,(uint8_t *)tegu_buffer_bytes(scratch),(uint8_t *)tegu_buffer_bytes(load),0};
  struct tegu_generic_batch_info info={0,0,TEGU_BATCH_STOP_BUILD,0};
  int built=tegu_generic_draw_batch_build2(shaders,requests,run,&jobImages,&limits,r,&info);
  /* Sample accepted model batches and heap-rejected shapes independently.
   * Early boot alone is not representative of the replay's blur workloads. */
  if(limits.small_heap_budget&&(info.small_heap||info.stop==TEGU_BATCH_STOP_HEAP)) {
   unsigned kind=info.small_heap?1u:0u;
   unsigned sample=__atomic_fetch_add(&tegu_small_heap_traces[kind],1,__ATOMIC_RELAXED);
   if(sample<8||(sample<3072&&!(sample%128))) {
    unsigned at=built>=0&&(unsigned)built<run?(unsigned)built:0;
    const struct tegu_generic_draw_request *q=requests[at];
    char line[256];int len=snprintf(line,sizeof(line),
     "TEGU_HEAP_CLASS model=%u sample=%u built=%d run=%u stop=%u at=%u vertices=%u primitive=%u index=%u varying=%u target=%ux%u estimate=%llu\n",
     kind,sample,built,run,info.stop,at,q->vertex_count,q->primitive,q->index_size,
     shaders[at]->varying_bytes,q->width,q->height,(U64)info.heap_estimate);
    if(len>0)write(2,line,(size_t)len<sizeof(line)?(size_t)len:sizeof(line)-1);
   }
  }
  /* All offered draws fit: the run's own end; else the builder's limit. */
  *why=built==(int)run&&info.stop==TEGU_BATCH_STOP_NONE?ended:TEGU_BREAK_BUILDER+info.stop;
  if(built>=2&&(tgj2||!tegu_generic_job_to_v1(r,v1))) {
   const struct Dispatch *last=&TEGU_JOB(jobs,first+(unsigned)built-1);
   /* CLEAN as the first draw's job would have been, CONTINUES as the last's. */
   uint32_t flags=(j0->batchFlags&TEGU_GENERIC_JOB_TARGET_CLEAN)|(last->batchFlags&TEGU_GENERIC_JOB_TARGET_CONTINUES);
   U64 heap=UINT64_MAX;
   if(tgj2) {
    U64 out[TEGU_GENERIC_JOB2_OUTPUTS]={0};unsigned outs=TEGU_GENERIC_JOB2_OUTPUTS;r->flags|=flags;
    error=d->method(d->connection,TEGU_GENERIC_JOB2_SELECTOR,0,0,r,sizeof(*r),out,&outs,0,0);
    if(!error&&outs!=TEGU_GENERIC_JOB2_OUTPUTS)error=-2;
    reply[0]=out[0];reply[1]=out[1];*n=2;heap=out[2];
    /* A job that used more heap than estimated scales later estimates up. */
    if(!error&&heap!=UINT64_MAX&&info.small_heap&&heap>info.heap_estimate) {
     __atomic_store_n(&tegu_batch_small_heap,0,__ATOMIC_RELAXED);
     char line[160];int len=snprintf(line,sizeof(line),"TEGU_SMALL_HEAP disabled estimate=%llu used=%llu\n",(U64)info.heap_estimate,heap);
     if(len>0)write(2,line,(size_t)len<sizeof(line)?(size_t)len:sizeof(line)-1);
    }
    if(!error&&heap!=UINT64_MAX&&!info.small_heap&&info.heap_estimate&&heap*100u>info.heap_estimate*(U64)scale+slack*100u) {
     U64 want=heap*100u/info.heap_estimate+10u;if(want>1000u)want=1000u;
     if(want>scale) {
      __atomic_store_n(&tegu_batch_heap_scale,(unsigned)want,__ATOMIC_RELAXED);
      char line[192];int length=snprintf(line,sizeof(line),
       "TEGU_HEAP_FEEDBACK target=%ux%u draws=%d estimate=%llu used=%llu scale=%u->%u\n",
       requests[0]->width,requests[0]->height,built,(U64)info.heap_estimate,heap,scale,(unsigned)want);
      if(length>0)write(2,line,(size_t)length<sizeof(line)?(size_t)length:sizeof(line)-1);
     }
    }
   } else {
    v1->flags|=flags;*n=2;
    error=d->method(d->connection,TEGU_GENERIC_JOB_SELECTOR,0,0,v1,sizeof(*v1),reply,n,0,0);
   }
   *consumed=(unsigned)built;
   unsigned trace=__atomic_fetch_add(&tegu_batch_traces,1,__ATOMIC_RELAXED);
   if(trace<32) {char line[320];int length=snprintf(line,sizeof(line),
    "TEGU_BATCH n=%u tgj=%u draws=%d run=%u cmds=%u spans=%u load=%u target=%ux%u flags=0x%x heap_est_kb=%llu heap_kb=%lld "
    "result=%d fs=%s\n",
    trace,tgj2?2u:1u,built,run,r->command_count,r->span_count,requests[0]->load_action,requests[0]->width,requests[0]->height,
    r->flags|flags,(U64)(info.heap_estimate>>10),heap==UINT64_MAX?-1LL:(long long)(heap>>10),error,j0->genericName);
    if(length>0)write(2,line,(size_t)length<sizeof(line)?(size_t)length:sizeof(line)-1);}
#ifdef TEGU_RENDER_TRACE
   if(!error)tegu_target_grid_trace(last,"batch");
#endif
  } else if(built>=2)*why=TEGU_BREAK_BUILDER+TEGU_BATCH_STOP_BUILD;
 }
 free(r);free(v1);tegu_job_images_release();
 return error;
#endif
}

#ifdef TEGU_RENDER_TRACE
static int tegu_fixed_ui_local_bind_for_job(const struct Dispatch *j,
 const struct tegu_ui1_bind_request *request) {
 TeguRenderPipeline *pipeline=j?(TeguRenderPipeline *)j->renderPipeline:0;
 if(!pipeline||!request||!pipeline->package||!pipeline->packageBytes)return TEGU_UI1_EINVAL;
 struct tegu_ui1_info info={0};struct tegu_ui1_binding binding={0};
 int result=tegu_ui1_parse(pipeline->package,pipeline->packageBytes,&info);
 return result?result:tegu_ui1_bind(&info,request,&binding);
}

static void tegu_fixed_ui_trace_bytes(const char *kind,unsigned command,unsigned job,
 const void *contents,NSUInteger offset,NSUInteger length) {
 static unsigned emitted;
 if(__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED)>=24)return;
 const unsigned char *p=contents?(const unsigned char *)contents+offset:0;
 U64 hash=UINT64_C(1469598103934665603),first=UINT64_MAX,nonzero=0,lanes[4]={0,0,0,0};
 unsigned char value=0;
 for(NSUInteger i=0;p&&i<length;i++) {
  hash^=p[i];hash*=UINT64_C(1099511628211);
  if(p[i]){nonzero++;lanes[i&3]++;if(first==UINT64_MAX){first=i;value=p[i];}}
 }
 char line[320];int n=snprintf(line,sizeof(line),
  "TEGU_UI_DATA command=%u job=%u kind=%s bytes=%llu available=%u nonzero=%llu lanes=%llu/%llu/%llu/%llu first=%llu value=%u hash=0x%llx\n",
  command,job,kind,(U64)length,p?1u:0u,nonzero,lanes[0],lanes[1],lanes[2],lanes[3],
  first,(unsigned)value,hash);
 if(n>0&&(NSUInteger)n<sizeof(line))write(2,line,(NSUInteger)n);
}

static void tegu_fixed_ui_trace_ui1(const struct Dispatch *j,const struct tegu_ui1_bind_request *r,
 unsigned command,unsigned job) {
 TeguBuffer *vertex=j->compositorVertexBuffers[1],*vb=j->compositorVertexBuffers[2],
  *fb=j->compositorFragmentBuffers[1];
 tegu_fixed_ui_trace_bytes("ui1_vertex",command,job,[vertex contents],(NSUInteger)r->vertex.offset,
  (NSUInteger)r->vertex_count*r->vertex_stride);
 tegu_fixed_ui_trace_bytes("ui1_vs_descriptor",command,job,[vb contents],
  (NSUInteger)r->resources[0].descriptor_offset,(NSUInteger)r->resources[0].descriptor_length);
 tegu_fixed_ui_trace_bytes("ui1_fs_descriptor",command,job,[fb contents],
  (NSUInteger)r->resources[1].descriptor_offset,(NSUInteger)r->resources[1].descriptor_length);
}

static void tegu_fixed_ui_trace_ui2(const struct Dispatch *j,const struct tegu_ui2_bind_request *r,
 unsigned command,unsigned job) {
 TeguBuffer *vertex=j->compositorVertexBuffers[1];TeguTexture *sample=j->compositorTextures[3];
 tegu_fixed_ui_trace_bytes("ui2_vertex",command,job,[vertex contents],(NSUInteger)r->vertex.offset,
  (NSUInteger)r->vertex_count*r->vertex_stride);
 tegu_fixed_ui_trace_bytes("ui2_texture",command,job,sample?[sample->buffer contents]:0,
  (NSUInteger)r->texture.allocation.offset,(NSUInteger)r->texture.row_bytes*r->texture.height);
 for(unsigned i=0;i<4;i++) {
  TeguBuffer *buffer=i<2?j->compositorVertexBuffers[2+i]:j->compositorFragmentBuffers[i-1];
  static const char *kind[4]={"ui2_vs0_descriptor","ui2_vs1_descriptor","ui2_fs0_descriptor","ui2_fs1_descriptor"};
  tegu_fixed_ui_trace_bytes(kind[i],command,job,[buffer contents],
   (NSUInteger)r->resources[i].descriptor_offset,(NSUInteger)r->resources[i].descriptor_length);
 }
}
#endif

static int tegu_compositor_request_for_job(const struct Dispatch *j,TeguComputeDevice *owner,
 struct tegu_compositor_request *out,U64 args[4]) {
 _Static_assert(sizeof(struct tegu_compositor_request)==TEGU_COMPOSITOR_REQUEST_BYTES,"compositor request ABI");
 if(!j||!owner||!out||!args||!owner->method||!(owner->capabilities&TEGU_GPU_CAP_COMPOSITOR))return -95;
 TeguRenderPipeline *pipeline=j->renderPipeline;TeguTexture *target=j->renderTexture;
 int b3=pipeline&&pipeline->compositorB3;
 int b3Target=b3&&tegu_b3_private_mip0_texture(target,owner);
 TeguTexture *b3RT1=b3?(TeguTexture *)j->attachmentClearTargets[1]:0;
 int b3SecondTarget=b3&&b3RT1&&b3RT1!=target&&tegu_b3_private_mip0_texture(b3RT1,owner);
 if(!j->compositorSnapshot||!pipeline||pipeline->owner!=owner||!pipeline->handle||
    !pipeline->compositorSnapshotOnly||pipeline->colorFormat!=80||
    (pipeline->compositorB3?0:(pipeline->compositorV7?!(owner->capabilities&TEGU_GPU_CAP_COMPOSITOR_V7):!pipeline->compositorV6))||
    !target||[target device]!=owner||target->format!=80||(b3&&(!b3Target||!b3SecondTarget))||
    (!b3Target&&(target->surfaceImported ? (target->surfaceTargetVersion!=1||
      !(owner->capabilities&TEGU_GPU_CAP_COMPOSITOR_IOSURFACE)||!tegu_iosurface_render_target_usage(target->textureUsage)||
      target->surfaceRowBytes!=target->rowBytes||target->surfaceAllocSize<target->mip[0].offset) : !target->buffer))||
    pipeline->vertexFetch.count!=2||pipeline->vertexFetch.attributes[0].buffer_index>1||
    pipeline->vertexFetch.attributes[1].buffer_index!=pipeline->vertexFetch.attributes[0].buffer_index||
    j->compositorVertexBufferMask!=((1u<<pipeline->vertexFetch.attributes[0].buffer_index)|(1u<<2)|(1u<<3))||
    j->compositorFragmentBufferMask!=((1u<<1)|(1u<<6))||
    j->compositorTextureMask!=(1u<<3)||j->compositorSamplerMask!=1||
    !j->compositorSamplers[0]||j->compositorAttachmentCount!=2||
    j->compositorAttachments[0].format!=80||j->compositorAttachments[0].writeMask!=15||
    j->compositorAttachments[1].format!=80||j->compositorAttachments[1].writeMask||
    (j->compositorLoadAction!=2&&j->compositorLoadAction!=1)||j->compositorStoreAction!=1)return -95;
 const TeguAIRRenderBlend *b=&pipeline->blend;
 int clear_blend=b->version==1&&!b->enabled&&b->rgb_op==1&&b->rgb_src==1&&b->rgb_dst==0&&b->alpha_op==1&&b->alpha_src==1&&b->alpha_dst==0&&b->write_mask==15;
 int v7_disabled_blend=(pipeline->compositorV7||pipeline->compositorB3)&&b->version==1&&!b->enabled&&b->rgb_op==0&&b->rgb_src==1&&b->rgb_dst==0&&b->alpha_op==0&&b->alpha_src==1&&b->alpha_dst==0&&b->write_mask==15;
 int sourceover_blend=b->version==1&&b->enabled&&b->rgb_op==0&&b->rgb_src==1&&b->rgb_dst==5&&b->alpha_op==0&&b->alpha_src==1&&b->alpha_dst==5&&b->write_mask==15;
 if(!clear_blend&&!v7_disabled_blend&&!sourceover_blend)return -95;
 if(j->compositorLoadAction==1&&(!(owner->capabilities&TEGU_GPU_CAP_COMPOSITOR_LOAD)||
    (!sourceover_blend&&!b3)))return -95;
 NSUInteger width=target->textureWidth,height=target->textureHeight,stride=target->rowBytes;
 if(width<16||height<16||width>4096||height>4096||stride<width*4||
    stride>65536||stride*height>(16u<<20)||(!b3Target&&((stride&63)||
    target->levelCount!=1||target->mip[0].width!=width||target->mip[0].height!=height||
    target->mip[0].stride!=stride||(target->mip[0].offset&4095))))return -95;
 uint64_t targetLength=target->surfaceImported?target->surfaceAllocSize:target->buffer->logical;
 if(target->mip[0].offset>targetLength||stride*height>targetLength-target->mip[0].offset||
    stride*height>(16u<<20)||(target->surfaceImported&&
    (target->surfaceTargetVersion!=1||target->textureWidth>4096||target->textureHeight>4096||
     stride>65536||target->surfaceAllocSize>((16u<<20)+4095))))return -95;
 struct tegu_compositor_request request={0};
 request.version=j->compositorLoadAction==1?TEGU_COMPOSITOR_REQUEST_VERSION_LOAD:TEGU_COMPOSITOR_REQUEST_VERSION;request.width=(uint32_t)width;
 request.height=(uint32_t)height;request.row_stride=(uint32_t)stride;
 request.load_action=j->compositorLoadAction;request.store_action=1;request.attachment_mask=1;
 const unsigned vertexSlot=pipeline->vertexFetch.attributes[0].buffer_index;
 request.vertex.version=1;request.vertex.buffer_mask=1u<<vertexSlot;
 request.vertex.draw=(struct tegu_vertex_draw){0,3,1,0};
 if(tegu_compositor_buffer_binding(&j->compositorVertexFacts[vertexSlot],j->compositorVertexBuffers[vertexSlot],
     owner,16,0,&request.vertex.buffers[vertexSlot]))return -95;
 const unsigned stage[4]={1,1,2,2},index[4]={2,3,1,6};
 const unsigned window[4]={64,16,pipeline->compositorB3?82u:(pipeline->compositorV7?32u:72u),2};
 for(unsigned i=0;i<4;i++) {
  unsigned n=index[i];
  const struct TeguCompositorBufferFact *f=stage[i]==1?&j->compositorVertexFacts[n]:&j->compositorFragmentFacts[n];
  TeguBuffer *b=stage[i]==1?j->compositorVertexBuffers[n]:j->compositorFragmentBuffers[n];
  if(tegu_compositor_buffer_binding(f,b,owner,2,window[i],&request.ubos[i]))return -95;
 }
 TeguTexture *texture=j->compositorTextures[3];
 int b3Source=b3&&tegu_b3_private_mip0_texture(texture,owner);
 if(!texture||[texture device]!=owner||texture->surfaceImported||!texture->buffer||
    (b3&&!b3Source)||
    j->compositorTextureFacts[3].handle!=texture->buffer->handle||
    (b3Source&&(j->compositorTextureFacts[3].format!=80||j->compositorTextureFacts[3].width!=776||
    j->compositorTextureFacts[3].height!=24||j->compositorTextureFacts[3].stride!=3104||
    j->compositorTextureFacts[3].span!=74496))||
    (!b3Source&&(j->compositorTextureFacts[3].format!=70||j->compositorTextureFacts[3].width!=16||
    j->compositorTextureFacts[3].height!=16||j->compositorTextureFacts[3].stride!=64||
    j->compositorTextureFacts[3].span!=1024))||(j->compositorTextureFacts[3].offset&4095)||
    j->compositorTextureFacts[3].offset>texture->buffer->logical||
    j->compositorTextureFacts[3].span>texture->buffer->logical-j->compositorTextureFacts[3].offset)return -95;
 request.texture=(struct tegu_render_vertex_binding){j->compositorTextureFacts[3].handle,
  j->compositorTextureFacts[3].offset,j->compositorTextureFacts[3].span};
 request.texture_format=j->compositorTextureFacts[3].format;
 request.texture_width=j->compositorTextureFacts[3].width;request.texture_height=j->compositorTextureFacts[3].height;
 request.texture_stride=j->compositorTextureFacts[3].stride;
 request.sampler_class=TEGU_COMPOSITOR_SAMPLER_NEAREST_CLAMP_LOD0;
 U64 targetHandle=target->surfaceImported?target->importedHandle:target->buffer->handle;
 if(!targetHandle)return -95;
 U64 scalars[4]={pipeline->handle,targetHandle,target->mip[0].offset,j->renderArgs[3]};
 *out=request;memcpy(args,scalars,sizeof(scalars));return 0;
}
