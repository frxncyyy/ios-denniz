/* Group independent generic draws within ONE render encoder.  All admitted
 * generic shader buffers are read-only (mali_graphics_compiler.c rejects
 * writable SSBOs/storage images); the active target is the only GPU write.
 * Targets use whole-allocation hazards; read and expansion slices retain
 * their ranges so independent slices of the same private arena can move.
 * IOSurface IDs canonicalize distinct imports of the same backing.  CPU
 * preclears, MRT cross-target snapshot copies, unknown resources and
 * encoder boundaries are barriers.  A framebuffer fetch (n2r ATTACHMENT:
 * in place or a snapshot of the job's OWN target; TILE_ATTACHMENT: LD_TILE
 * of its own tile buffer) reads only the job's own target, which the target
 * write already orders against every same-target job.  No heap limit, shader, job ABI or fetch rule changes here. */
static unsigned tegu_draw_reorder_enabled;
/* TEGU_DRAW_REORDER=2: a Load draw may also move back across whole other
 * passes (ansuz E4 pass merging): every job it crosses must be an admitted,
 * independent draw (blits, compute, clear-only passes, CPU work, fetches of
 * other targets and unknown resources stay barriers), so its target holds
 * exactly the same contents when it runs and the frame-level batcher then
 * chains it with the earlier same-target run. 1 keeps moves inside a pass. */
static int tegu_draw_reorder_cross=-1;
static int tegu_reorder_cross_pass(void) {
 if(tegu_draw_reorder_cross<0){const char *e=getenv("TEGU_DRAW_REORDER");tegu_draw_reorder_cross=e&&e[0]=='2'&&!e[1];}
 return tegu_draw_reorder_cross;
}
struct tegu_reorder_key { U64 token;uint32_t surface;U64 offset,bytes; };
struct tegu_reorder_access {
 struct tegu_reorder_key target,expanded,reads[TEGU_GENERIC_DRAW_MAX_BINDINGS+4];
 unsigned count,pass,valid;
};
static int tegu_reorder_alias(struct tegu_reorder_key a,struct tegu_reorder_key b) {
 if(!((a.token&&a.token==b.token)||(a.surface&&a.surface==b.surface)))return 0;
 /* bytes=0 denotes the whole allocation. Avoid adding range endpoints. */
 if(!a.bytes||!b.bytes)return 1;
 return a.offset<=b.offset?b.offset-a.offset<a.bytes:a.offset-b.offset<b.bytes;
}
static int tegu_reorder_key_for(const struct Dispatch *j,U64 token,struct tegu_reorder_key *key) {
 *key=(struct tegu_reorder_key){token,0,0,0};
 if(!token)return 0;
 for(unsigned k=0;k<32;k++) {
  TeguTexture *t=k==31?j->renderTexture:j->compositorTextures[k];
  if(!t)continue;
  if(!tegu_owned_class(t,"TeguTexture"))return 0;
  U64 handle=t->surfaceImported?t->importedHandle:t->buffer?t->buffer->handle:0;
  if(handle!=token)continue;
  if(t->shadowSurface)return 0;
  if(t->surfaceImported) {
   const struct TeguIOSurfaceAPI *api=tegu_iosurface_api();
   if(!api||!t->importedSurface||!(key->surface=api->getID(t->importedSurface)))return 0;
  }
 }
 return 1;
}
static struct tegu_reorder_access tegu_reorder_access_for(const struct Dispatch *j) {
 struct tegu_reorder_access a={0};
 const struct tegu_generic_draw_request *q=&j->genericDrawRequest;
 TeguRenderPipeline *p=j->renderPipeline;
 /* A pass CLEAR job (lazy-clear flush or a draw-less Clear pass) writes only
  * its target and reads nothing: independent draws of other targets may
  * cross it (ansuz passmerge moves clear-only passes of other targets). */
 if(j->perfPass&&j->nord2ClearOnly&&!j->blit&&!j->genericPreclear&&q->target_token) {
  if(tegu_reorder_key_for(j,q->target_token,&a.target)){a.pass=j->perfPass;a.valid=1;}
  return a;
 }
 /* Tile-buffer depth/stencil jobs stay in place: their frame membership is
  * the pass order (render_encoder tegu_render_zs_job). */
 if(j->genericDrawFlags&N2R_DRAW_DEPTH_STENCIL)return a;
 if(!j->perfPass||j->blit||j->genericPreclear||!j->genericDrawSnapshot||!p||!p->genericDraw||
    j->nord2CopySources[0]||j->nord2CopySources[1]||
    q->binding_count>TEGU_GENERIC_DRAW_MAX_BINDINGS||q->target_token!=j->renderArgs[1]||
    !tegu_reorder_key_for(j,q->target_token,&a.target))return a;
 /* True-MRT and MSAA jobs write more than their main target (the other
  * attachments, resolve targets): barriers. A TLS-only sidecar is fine. */
 if(j->compositorPlan.bytes) {
  const struct tegu_render_generic_sidecar *sc=(const void *)j->compositorPlan.bytes;
  if(j->compositorPlan.length<sizeof(*sc)||sc->magic!=TEGU_RENDER_SIDECAR_MAGIC||
     sc->version!=TEGU_RENDER_SIDECAR_VERSION||sc->count>1||sc->msaa)return a;
 }
 a.pass=j->perfPass;
 struct tegu_generic_draw_memory memories[TEGU_GENERIC_DRAW_MAX_BINDINGS+4];unsigned n=0;
 if(q->vertex.token)memories[n++]=q->vertex;
 if(q->index.token)memories[n++]=q->index;
 /* Expansion writes a private arena slice immediately before drawing. The
  * paired MRT draw reads that SAME slice; later draws use disjoint slices.
  * Track this CPU write and its index/source reads, rather than moving the
  * expansion ahead of GPU/CPU producers elsewhere in the command buffer. */
 if(j->expandOut) {
  U64 bytes=(U64)j->expandCount*j->expandStride;
  if(!j->expandIndex||!j->expandSource||!bytes||!j->expandOut->handle||
     (j->expandElement!=2&&j->expandElement!=4)||
     j->expandOutOffset>j->expandOut->logical||bytes>j->expandOut->logical-j->expandOutOffset||
     q->vertex.token!=j->expandOut->handle||q->vertex.offset!=j->expandOutOffset||q->vertex.bytes!=bytes||
     !tegu_reorder_key_for(j,j->expandOut->handle,&a.expanded))return a;
  a.expanded.offset=j->expandOutOffset;a.expanded.bytes=bytes;
  if(tegu_reorder_alias(a.target,a.expanded))return a;
  memories[n++]=(struct tegu_generic_draw_memory){j->expandIndex->handle,0,0};
  memories[n++]=(struct tegu_generic_draw_memory){j->expandSource->handle,0,0};
 }
 for(unsigned k=0;k<q->binding_count;k++) {
  const struct tegu_generic_draw_binding *b=&q->bindings[k];
  switch(b->kind) {
   case TEGU_GENERIC_DRAW_BUFFER:case TEGU_GENERIC_DRAW_TEXTURE:
   case TEGU_GENERIC_DRAW_COMBINED:case TEGU_GENERIC_DRAW_COMBINED_IMMUTABLE:
    if(!b->memory.token)return a;
    memories[n++]=b->memory;break;
   case TEGU_GENERIC_DRAW_CONSTANT:case TEGU_GENERIC_DRAW_SAMPLER:
   case TEGU_GENERIC_DRAW_NEUTRAL_IMAGE:case TEGU_GENERIC_DRAW_NEUTRAL_BUFFER:break;
   case TEGU_GENERIC_DRAW_ATTACHMENT:case TEGU_GENERIC_DRAW_TILE_ATTACHMENT:break; /* own target */
   default:return a;
  }
 }
 for(unsigned k=0;k<n;k++) {
  if(!tegu_reorder_key_for(j,memories[k].token,&a.reads[a.count]))return a;
  a.reads[a.count].offset=memories[k].offset;a.reads[a.count].bytes=memories[k].bytes;
  if(tegu_reorder_alias(a.target,a.reads[a.count]))return a;
  a.count++;
 }
 a.valid=1;return a;
}
static int tegu_reorder_independent(const struct tegu_reorder_access *a,const struct tegu_reorder_access *b) {
 if(!a->valid||!b->valid||(a->pass!=b->pass&&!tegu_reorder_cross_pass()))return 0;
 struct tegu_reorder_key aw[2]={a->target,a->expanded},bw[2]={b->target,b->expanded};
 for(unsigned w=0;w<2;w++) {
  for(unsigned v=0;v<2;v++)if(tegu_reorder_alias(aw[w],bw[v]))return 0;
  for(unsigned k=0;k<a->count;k++)if(tegu_reorder_alias(a->reads[k],bw[w]))return 0;
  for(unsigned k=0;k<b->count;k++)if(tegu_reorder_alias(b->reads[k],aw[w]))return 0;
 }
 return 1;
}
static unsigned tegu_reorder_draws(struct Dispatch *const *jobs,unsigned count) {
 if((!tegu_draw_reorder_enabled&&!tegu_reorder_cross_pass())||count<3)return 0;
 struct tegu_reorder_access *access=calloc(count,sizeof(*access));
 struct Dispatch *tmp=malloc(sizeof(*tmp));
 if(!access||!tmp){free(access);free(tmp);return 0;}
 for(unsigned k=0;k<count;k++)access[k]=tegu_reorder_access_for(&TEGU_JOB(jobs,k));
 unsigned moved=0,eligible=0,expanded=0;
 for(unsigned k=0;k<count;k++){eligible+=access[k].valid;expanded+=TEGU_JOB(jobs,k).expandOut!=0;}
 for(unsigned i=1;i<count;i++) {
  const struct Dispatch *previous=&TEGU_JOB(jobs,i-1);
  if(!access[i-1].valid)continue;
  /* Bounded lookahead avoids a quadratic search across a large command. */
  unsigned end=count-i>32?i+32:count;
  for(unsigned k=i;k<end;k++) {
   struct Dispatch *candidate=&TEGU_JOB(jobs,k);
   if(!access[k].valid||(access[k].pass!=access[i-1].pass&&!tegu_reorder_cross_pass()))break;
   if(!tegu_reorder_alias(access[i-1].target,access[k].target))continue;
   /* Keep same-target order, even if the first candidate cannot move. */
   if(k==i||candidate->genericDrawRequest.load_action!=1||
      candidate->renderArgs[1]!=previous->renderArgs[1]||candidate->renderArgs[2]!=previous->renderArgs[2]||
      candidate->renderTexture!=previous->renderTexture)break;
   unsigned b=i;for(;b<k;b++)if(!tegu_reorder_independent(&access[k],&access[b]))break;
   if(b!=k)break;
   *tmp=*candidate;struct tegu_reorder_access saved=access[k];
   for(b=k;b>i;b--){TEGU_JOB(jobs,b)=TEGU_JOB(jobs,b-1);access[b]=access[b-1];}
   TEGU_JOB(jobs,i)=*tmp;access[i]=saved;moved++;break;
  }
 }
 free(tmp);free(access);
 static unsigned traces;
 if(__atomic_fetch_add(&traces,1,__ATOMIC_RELAXED)<90) {
  char line[128];int n=snprintf(line,sizeof(line),"TEGU_REORDER jobs=%u eligible=%u expanded=%u moved=%u\n",count,eligible,expanded,moved);
  if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
 }
 return moved;
}
