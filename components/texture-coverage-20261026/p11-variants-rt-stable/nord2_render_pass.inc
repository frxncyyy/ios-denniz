/* Nord2 pass admission: every executable attachment is rendered; no observer
 * passes, implicit format substitutions or silent discarded MRT output.
 *
 * A refused descriptor must still return an encoder. RenderBox obtains the
 * encoder, caches its IMPs, and calls them without a nil check; returning nil
 * here killed chronod and caused the widget respawn/flicker storm. Keep a
 * bounded diagnostic, poison the command, and hand back an inert encoder. */
static TeguRenderEncoder *nord2_render_pass_inert(TeguCommand *c,int line,const char *reason) {
 tegu_command_invalidate(c,"nord2_render_pass",(unsigned)line);
 {
  static unsigned emitted;
  if(__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED)<16) {
   char text[320];int n=snprintf(text,sizeof(text),"TEGU_PASS_REFUSED line=%d reason=%.260s\n",
    line,reason?reason:"unsupported pass");
   if(n>0)write(2,text,(size_t)n<sizeof(text)?(size_t)n:sizeof(text)-1);
  }
 }
 TeguRenderEncoder *e=[[TeguRenderEncoder alloc] initWithCommand:c target:0 clear:0 loadAction:1];
 if(!e){TEGU_INVALID(c);return 0;}
 e->inert=1;e->skipPass=1;e->ended=1;
 c->encoding=1;return [e autorelease];
}
static int nord2_pass_aux_attachment(TeguPassAttachment *a,TeguTexture *color,TeguComputeDevice *owner,
 TeguTexture **out,NSUInteger *format,const char **reason) {
 TeguTexture *t=a?[a texture]:0;
 if(!t)return 0;
 if(!tegu_owned_class(t,"TeguTexture")||[t device]!=owner||t->textureKind!=2||
    t->textureSamples!=1||t->textureDepth!=1||t->levelCount!=1||
    t->textureWidth!=color->textureWidth||t->textureHeight!=color->textureHeight||
    ((t->format<250||t->format>255)&&t->format!=260)||
    [a level]||[a slice]||[a depthPlane]||
    [a resolveTexture]||[a loadAction]>2||[a storeAction]) {
  if(reason)*reason="depth/stencil attachment outside DontCare exact class";
  return -1;
 }
 *out=t;*format=t->format;return 1;
}
/* NORD2_METAL_DONTCARE=1: a DontCare load stays DontCare (load 0) instead
 * of becoming a Clear. Its first job then neither clears nor writes back
 * tiles without geometry (n2r: clean tile writes only for Clear); Metal
 * leaves the contents undefined, so keeping the old bytes is valid. Off by
 * default until a device A/B. */
static int nord2_dontcare_enabled(void) {
 static int state=-1;
 if(state<0){extern char *getenv(const char *);const char *v=getenv("NORD2_METAL_DONTCARE");state=v&&v[0]=='1'&&!v[1];}
 return state;
}
static TeguRenderEncoder *nord2_render_pass(TeguCommand *c,TeguPassDescriptor *d) {
#ifdef TEGU_ENC_SCOPE
 TEGU_ENC_SCOPE(TEGU_ENC_PASS);__atomic_fetch_add(&tegu_enc_passes,1,__ATOMIC_RELAXED);
#endif
 int failLine=__LINE__;const char *failReason="unsupported pass";
 TeguComputeDevice *owner=c->queue->owner;
 id colors=[d colorAttachments];TeguTexture *targets[4]={0},*resolves[4]={0},*anchor=0;
 unsigned load[4]={0},rawLoad[4]={0},rawStore[4]={0};uint32_t clearWords[4][4]={{0}};
 NSUInteger requestedSamples=[d defaultRasterSampleCount];
 if(!requestedSamples)requestedSamples=1;
 TeguTexture *depthTexture=0,*stencilTexture=0;NSUInteger depthFormat=0,stencilFormat=0;
 if(!colors){failLine=__LINE__;failReason="missing color attachments";goto fail;}
 /* Metal takes the raster sample count from the attachments; the
  * descriptor's defaultRasterSampleCount only governs attachment-less
  * passes (c6k: a SpringBoard 4x MSAA pass with a resolve was refused as
  * "single-sample ... samples=4"). The first owned colour attachment
  * decides; every attachment is then checked against it below. */
 for(unsigned i=0;i<8;i++){
  TeguTexture *t=[[colors objectAtIndexedSubscript:i] texture];
  if(t&&tegu_owned_class(t,"TeguTexture")&&[t device]==owner){if(t->textureSamples>1)requestedSamples=t->textureSamples;break;}
 }
 for(unsigned i=0;i<8;i++){
  TeguPassAttachment *a=[colors objectAtIndexedSubscript:i];TeguTexture *t=[a texture];
  if(!t)continue;
  if(i>=4){failLine=__LINE__;failReason="more than four color attachments";goto fail;}
  for(unsigned j=0;j<i;j++)if(targets[j]==t){
   failLine=__LINE__;failReason="same color texture bound to multiple attachments";goto fail;}
  TeguTexture *resolve=[a resolveTexture];
  if(!tegu_owned_class(t,"TeguTexture")||[t device]!=owner||!t->format||!n2r_bpp(t->format)||
     !t->textureWidth||!t->textureHeight||t->textureWidth>4096||t->textureHeight>4096||
     t->rowBytes>65536||(t->rowBytes&63)||t->rowBytes<t->textureWidth*n2r_bpp(t->format)||
     [a loadAction]>2||[a level]||[a slice]||[a depthPlane]){
   /* Values, not just the class: the quiet device build has no other trace
    * of which clause refused (c6ec: 12 backboardd passes). rowBytes must be
    * 64-byte aligned for an arch9 linear colour target (Mesa 81daea66
    * pan_linear_or_tiled_row_align_req, arch>=7 -> 64); the allocator and
    * IOSurface import guarantee it or substitute an owned shadow. */
   static __thread char detail[200];
   int owned=tegu_owned_class(t,"TeguTexture");
   snprintf(detail,sizeof(detail),"color attachment outside exact JM class (rt=%u owned=%d dev=%d fmt=%lu bpp=%u %lux%lu row=%lu imp=%d mips=%lu load=%lu level=%lu slice=%lu plane=%lu)",
    i,owned,owned&&[t device]==owner,(unsigned long)(owned?t->format:0),owned?n2r_bpp((unsigned)t->format):0u,
    (unsigned long)(owned?t->textureWidth:0),(unsigned long)(owned?t->textureHeight:0),(unsigned long)(owned?t->rowBytes:0),
    owned?(int)t->surfaceImported:0,(unsigned long)(owned?t->levelCount:0),(unsigned long)[a loadAction],
    (unsigned long)[a level],(unsigned long)[a slice],(unsigned long)[a depthPlane]);
   failLine=__LINE__;failReason=detail;goto fail;
  }
  if(requestedSamples==1) {
   /* storeAction 4 (MTLStoreActionUnknown) defers the choice to
    * setColorStoreAction:atIndex:. This pass always stores the target, which
    * satisfies both a later Store and a later DontCare. */
   NSUInteger store=[a storeAction];
   if(t->textureKind!=2||t->textureSamples!=1||resolve||(store>1&&store!=4)){
    static __thread char detail[160];
    snprintf(detail,sizeof(detail),"single-sample color attachment outside exact JM class (kind=%lu samples=%lu resolve=%d store=%lu)",
     (unsigned long)t->textureKind,(unsigned long)t->textureSamples,resolve?1:0,(unsigned long)store);
    failLine=__LINE__;failReason=detail;goto fail;
   }
  } else {
   if(t->textureKind!=4||t->textureSamples!=4||!resolve||
      ([a storeAction]!=2&&[a storeAction]!=3)){
    failLine=__LINE__;failReason="multisample color attachment outside exact resolve class";goto fail;
   }
   if(!tegu_owned_class(resolve,"TeguTexture")||[resolve device]!=owner||
      resolve->textureKind!=2||resolve->textureSamples!=1||resolve->format!=t->format||
      resolve->textureWidth!=t->textureWidth||resolve->textureHeight!=t->textureHeight||
      resolve->rowBytes>65536||(resolve->rowBytes&63)||resolve->rowBytes<resolve->textureWidth*n2r_bpp(resolve->format)||
      [a resolveLevel]||[a resolveSlice]||[a resolveDepthPlane]){
    failLine=__LINE__;failReason="multisample resolve outside exact shared class";goto fail;
   }
  }
  if(t->surfaceImported&&(t->format!=80||t->surfaceTargetVersion!=1)){
   failLine=__LINE__;failReason="imported color target outside exact JM class";goto fail;
  }
  U64 length=t->surfaceImported?t->surfaceAllocSize:(t->buffer?t->buffer->logical:0);
  U64 offset=t->mip[0].offset,bytes=(U64)t->rowBytes*t->textureHeight;
  if(requestedSamples==4)bytes*=4;
  if((offset&4095)||offset>length||bytes>length-offset){
   static __thread char spanDetail[200];
   snprintf(spanDetail,sizeof(spanDetail),"color attachment storage outside target span (rt=%u fmt=%lu %lux%lu row=%lu off=%llu bytes=%llu len=%llu imp=%d buf=%d)",
    i,(unsigned long)t->format,(unsigned long)t->textureWidth,(unsigned long)t->textureHeight,(unsigned long)t->rowBytes,
    (unsigned long long)offset,(unsigned long long)bytes,(unsigned long long)length,t->surfaceImported?1:0,t->buffer?1:0);
   failLine=__LINE__;failReason=spanDetail;goto fail;
  }
  if(requestedSamples==4) {
   U64 rlen=resolve->surfaceImported?resolve->surfaceAllocSize:(resolve->buffer?resolve->buffer->logical:0);
   U64 roff=resolve->mip[0].offset,rbytes=(U64)resolve->rowBytes*resolve->textureHeight;
   if(!resolve->buffer||(roff&4095)||roff>rlen||rbytes>rlen-roff){
    failLine=__LINE__;failReason="resolve storage outside target span";goto fail;
   }
  }
  if(anchor&&(t->textureWidth!=anchor->textureWidth||t->textureHeight!=anchor->textureHeight)){
   failLine=__LINE__;failReason="mismatched color attachments";goto fail;
  }
  if(!anchor)anchor=t;
  /* An integer/snorm (raw, not load-exact) target keeps DontCare: its clear
   * job would be refused (n2r_rt_load_exact), undefined contents are valid. */
  targets[i]=t;resolves[i]=resolve;load[i]=[a loadAction]==1?1:([a loadAction]==0&&(nord2_dontcare_enabled()||
   (t&&!n2r_rt_load_exact((unsigned)t->format))))?0:2;
  rawLoad[i]=(unsigned)[a loadAction];rawStore[i]=(unsigned)[a storeAction]; /* TEGU_PERF_PASS only */
  TeguClearColor value=[a loadAction]==2?[a clearColor]:(TeguClearColor){0,0,0,0};
  double rgba[4]={value.red,value.green,value.blue,value.alpha};
  if(n2r_clear(t->format,rgba,clearWords[i])){
   failLine=__LINE__;failReason="unsupported color clear format";goto fail;
  }
 }
 if(!anchor){failLine=__LINE__;failReason="missing color target";goto fail;}
 if(requestedSamples==4)for(unsigned rt=0;rt<4;rt++)if(targets[rt]&&load[rt]==1){
  failLine=__LINE__;failReason="multisample Load unsupported";goto fail;}
 if([d renderTargetArrayLength]>1){failLine=__LINE__;failReason="render target array length unsupported";goto fail;}
 if([d visibilityResultBuffer]){failLine=__LINE__;failReason="visibility result buffer unsupported";goto fail;}
 /* 4x MSAA: a pass is ONE JM job (its later draws continue the first job's
  * samples through n2mb_draw_batch_mrt; the samples never reach memory, so
  * a draw that cannot join that job is refused by the bridge instead of
  * re-clearing), and n2r refuses colour allocations over the tile-buffer
  * budget. TEGU_DISABLE_MSAA4 restores the old 1x-only gate. */
#ifndef TEGU_DISABLE_MSAA4
 if(requestedSamples!=1&&requestedSamples!=4){failLine=__LINE__;failReason="render sample count unsupported";goto fail;}
#else
 if(requestedSamples!=1){failLine=__LINE__;failReason="render sample count unsupported";goto fail;}
#endif
 /* Depth/stencil is exact here only when the attachment cannot be observed:
  * no resolve and a DontCare store. The texture shape/format is validated, but
  * no depth/stencil hardware operation is claimed. The encoder still tracks
  * the state so a draw that would require a depth/stencil test/write is
  * refused instead of silently producing different pixels. */
 int auxStatus=nord2_pass_aux_attachment([d depthAttachment],anchor,owner,&depthTexture,&depthFormat,&failReason);
 if(auxStatus<0){failLine=__LINE__;goto fail;}
 auxStatus=nord2_pass_aux_attachment([d stencilAttachment],anchor,owner,&stencilTexture,&stencilFormat,&failReason);
 if(auxStatus<0){failLine=__LINE__;goto fail;}
 unsigned clearCount=0;for(unsigned rt=0;rt<4;rt++)if(targets[rt]&&targets[rt]->textureSamples==1&&load[rt]==2)clearCount++;
 if(c->count>TEGU_COMMAND_MAX_JOBS-clearCount){failLine=__LINE__;failReason="render job capacity";goto fail;}
 TeguRenderEncoder *e=[[TeguRenderEncoder alloc] initWithCommand:c target:anchor clear:clearWords[0][0] loadAction:load[0]];
 if(!e){failLine=__LINE__;failReason="render encoder allocation";goto fail;}
 e->storeAction=1;e->renderTargetsValid=1;e->rasterSampleCount=(unsigned)requestedSamples;
 memcpy(e->renderClears,clearWords,sizeof(clearWords));memcpy(e->nord2Clear,clearWords,sizeof(clearWords));
 for(unsigned rt=0;rt<4;rt++){e->renderTargets[rt]=targets[rt];e->resolveTargets[rt]=[resolves[rt] retain];e->renderLoads[rt]=load[rt];e->renderJobs[rt]=0;
#ifdef TEGU_ENC_SCOPE /* the full encoder (standalone pass tests use a smaller stub) */
  e->perfRawLoad[rt]=rawLoad[rt];e->perfRawStore[rt]=rawStore[rt];
#endif
 }
 (void)rawLoad;(void)rawStore;
 e->hasDepthAttachment=!!depthTexture;e->hasStencilAttachment=!!stencilTexture;
 e->passDepthFormat=depthFormat;e->passStencilFormat=stencilFormat;
 if(targets[1]){e->mrtTarget=[targets[1] retain];e->mrtLoad=load[1];e->mrtClear=clearWords[1][0];}
 if(targets[2]){e->mrtTarget2=[targets[2] retain];e->mrtLoad2=load[2];e->mrtClear2=clearWords[2][0];}
 if(targets[3]){e->mrtTarget3=[targets[3] retain];e->mrtLoad3=load[3];e->mrtClear3=clearWords[3][0];}
 if([d renderTargetWidth]&&[d renderTargetWidth]<e->scissor.width)e->scissor.width=[d renderTargetWidth];
 if([d renderTargetHeight]&&[d renderTargetHeight]<e->scissor.height)e->scissor.height=[d renderTargetHeight];
 /* Lazy clears (with JM batching): no CLEAR job now. A target's first draw
  * clears in its own frame; true MRT, cross-target fetch and other paths
  * queue the jobs first (tegu_render_flush_clears); endEncoding queues the
  * Clear of a target no draw consumed; DontCare needs none (its contents are
  * undefined before and, unless drawn, unchanged is a valid result). */
#ifdef TEGU_NORD2_LAZY_CLEARS
 int lazy=tegu_jm_batch_enabled_for(c->abWord);
#ifdef TEGU_FRONTEND_HOST
 if(tegu_jm_batch_force>=0)lazy=tegu_jm_batch_force;
#endif
 if(lazy) {
  for(unsigned rt=0;rt<4;rt++)if(targets[rt]&&targets[rt]->textureSamples==1&&load[rt]==2) {
   e->nord2PendingClear|=1u<<rt;e->nord2LazyMask|=1u<<rt;
   if(![[colors objectAtIndexedSubscript:rt] loadAction])e->nord2DontCareMask|=1u<<rt;
  }
  c->encoding=1;return [e autorelease];
 }
#endif
 /* Clear every colour attachment before either MRT output can fetch the other.
  * These are ordered GPU jobs; subsequent draws use LOAD snapshots. */
 for(unsigned rt=0;rt<4;rt++)if(targets[rt]&&targets[rt]->textureSamples==1&&load[rt]==2){
  TeguTexture *t=targets[rt];struct Dispatch *j=tegu_command_job(c,c->count++);
  j->nord2ClearOnly=1;j->renderTexture=[t retain];
  struct tegu_generic_draw_request *q=&j->genericDrawRequest;
  q->target_format=t->format;q->width=t->textureWidth;q->height=t->textureHeight;q->row_bytes=t->rowBytes;
  q->target_token=t->surfaceImported?t->importedHandle:t->buffer->handle;
  q->target_length=t->surfaceImported?t->surfaceAllocSize:t->buffer->logical;q->target_offset=t->mip[0].offset;
  memcpy(q->clear_words,clearWords[rt],sizeof(q->clear_words));
  e->renderJobs[rt]++;if(rt==0)e->rt0Jobs++;else if(rt==1)e->mrtDraws++;else if(rt==2)e->mrtDraws2++;else e->mrtDraws3++;
 }
 c->encoding=1;return [e autorelease];
fail:
 return nord2_render_pass_inert(c,failLine,failReason);
}
