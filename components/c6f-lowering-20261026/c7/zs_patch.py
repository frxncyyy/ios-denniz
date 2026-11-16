#!/usr/bin/env python3
# Frontend half of in-pass depth/stencil (run from gpu/).
def edit(p,pairs):
    s=open(p).read()
    for o,n in pairs:
        assert s.count(o)==1,(p,o[:80],s.count(o)); s=s.replace(o,n)
    open(p,'w').write(s)
edit('metal/tegu/compute_frontend.m',[(""" float genericBlendConstant[4]; /* setBlendColor at the draw (N2R_DRAW_BLEND_CONSTANT) */""",""" float genericBlendConstant[4]; /* setBlendColor at the draw (N2R_DRAW_BLEND_CONSTANT) */
#ifdef NORD2_JM_TRANSPORT
 struct n2r_depth_stencil genericZs; /* tile-buffer depth/stencil state (N2R_DRAW_DEPTH_STENCIL) */
#endif""")])
edit('metal/nord2_render_pass.inc',[
("""static int nord2_pass_aux_attachment(TeguPassAttachment *a,TeguTexture *color,TeguComputeDevice *owner,
 TeguTexture **out,NSUInteger *format,const char **reason) {""","""@interface NSObject (Nord2PassDepthStencil)
- (double)clearDepth;
- (uint32_t)clearStencil;
@end
static int nord2_pass_aux_attachment(TeguPassAttachment *a,TeguTexture *color,TeguComputeDevice *owner,
 TeguTexture **out,NSUInteger *format,const char **reason) {"""),
(""" e->hasDepthAttachment=!!depthTexture;e->hasStencilAttachment=!!stencilTexture;""",""" e->hasDepthAttachment=!!depthTexture;e->hasStencilAttachment=!!stencilTexture;
 if(depthTexture){TeguPassAttachment *da=[d depthAttachment];e->passDepthLoad=(unsigned)[da loadAction];
  e->passDepthClear=[(id)da respondsToSelector:@selector(clearDepth)]?(float)[(id)da clearDepth]:1.0f;}
 if(stencilTexture){TeguPassAttachment *sa=[d stencilAttachment];e->passStencilLoad=(unsigned)[sa loadAction];
  e->passStencilClear=[(id)sa respondsToSelector:@selector(clearStencil)]?[(id)sa clearStencil]:0u;}"""),
])
edit('metal/tegu/render_encoder.inc',[
(""" NSUInteger passDepthFormat,passStencilFormat;""",""" NSUInteger passDepthFormat,passStencilFormat;
 unsigned passDepthLoad,passStencilLoad,passStencilClear;float passDepthClear;
 unsigned nord2ZsWritten; /* a job of this pass wrote depth/stencil: later ones must continue its frame */"""),
("""static int tegu_render_depth_stencil_exact(TeguRenderEncoder *e) {
 if(!e||(!e->hasDepthAttachment&&!e->hasStencilAttachment))return 1;
 TeguDepthStencilState *s=e->depthState;
 if(!s)return 1;
 if(e->hasDepthAttachment&&(s->state.compare!=7||s->state.write))return 0;
 if(e->hasStencilAttachment&&(s->state.front.enabled||s->state.back.enabled))return 0;
 return 1;
}""","""/* Whether this draw tests or writes the pass's depth/stencil. */
static int tegu_render_zs_observable(TeguRenderEncoder *e) {
 TeguDepthStencilState *s=e?e->depthState:0;
 if(!e||!s||(!e->hasDepthAttachment&&!e->hasStencilAttachment))return 0;
 return (e->hasDepthAttachment&&(s->state.compare!=7||s->state.write))||
  (e->hasStencilAttachment&&(s->state.front.enabled||s->state.back.enabled));
}
/* Observable depth/stencil runs only on the generic JM draw, in the tile
 * buffer (the pass admits Store=DontCare attachments only), for a Clear or
 * DontCare pass load; a Load of depth/stencil memory has no preload. */
static int tegu_render_depth_stencil_exact(TeguRenderEncoder *e) {
 if(!tegu_render_zs_observable(e))return 1;
 TeguRenderPipeline *p=e->pipeline;
 if(!p||!p->genericDraw||p->fixedUiSnapshotOnly||p->fixedUiTextureSnapshotOnly||p->compositorSnapshotOnly||
    p->attachmentClearSnapshotOnly||p->homeAffordanceObserverOnly)return 0;
 if((e->hasDepthAttachment&&e->passDepthLoad==1)||(e->hasStencilAttachment&&e->passStencilLoad==1))return 0;
 return 1;
}
#ifdef NORD2_JM_TRANSPORT
/* Fill job->genericZs: Clear/DontCare starts the tile buffer unless an earlier
 * job of the pass wrote depth/stencil (then continue: load 1, the job must
 * join that job's frame, compositor batch; the bridge refuses it alone). */
static void tegu_render_zs_job(TeguRenderEncoder *e,struct Dispatch *job) {
 if(!tegu_render_zs_observable(e)&&!e->nord2ZsWritten)return;
 TeguDepthStencilState *s=e->depthState;
 struct n2r_depth_stencil *z=&job->genericZs;memset(z,0,sizeof(*z));
 z->version=N2R_DEPTH_STENCIL_VERSION;
 z->format=(uint32_t)(e->hasDepthAttachment?e->passDepthFormat:e->passStencilFormat);
 if(e->hasDepthAttachment&&e->hasStencilAttachment&&e->passStencilFormat==253&&e->passDepthFormat!=255&&e->passDepthFormat!=260)
  z->format=e->passDepthFormat==250?255:260; /* separate S8 + depth: one combined tile plane */
 unsigned passLoad=e->hasDepthAttachment?e->passDepthLoad:e->passStencilLoad;
 z->load=e->nord2ZsWritten?1u:(passLoad==2?2u:0u);
 z->clear_depth=e->hasDepthAttachment?e->passDepthClear:1.0f;z->clear_stencil=e->hasStencilAttachment?e->passStencilClear&255u:0u;
 z->depth_compare=7;
 if(s&&e->hasDepthAttachment){z->depth_compare=s->state.compare;z->depth_write=s->state.write?1u:0u;}
 if(s&&e->hasStencilAttachment&&(s->state.front.enabled||s->state.back.enabled)) {
  z->stencil_enable=1;
  const struct TeguStencilFace *f[2]={&s->state.front,&s->state.back};struct n2r_stencil_face *o[2]={&z->front,&z->back};
  for(unsigned i=0;i<2;i++){
   if(f[i]->enabled)*o[i]=(struct n2r_stencil_face){f[i]->compare,f[i]->fail,f[i]->depthFail,f[i]->pass,f[i]->readMask,f[i]->writeMask,e->nord2StencilRef[i]};
   else *o[i]=(struct n2r_stencil_face){7,0,0,0,255,0,e->nord2StencilRef[i]};
  }
 }
 if(e->hasDepthAttachment){z->bias_units=e->nord2DepthBias[0];z->bias_slope=e->nord2DepthBias[1];z->bias_clamp=e->nord2DepthBias[2];}
 job->genericDrawFlags|=N2R_DRAW_DEPTH_STENCIL;
 int writes=z->depth_write||(z->stencil_enable&&((z->front.write_mask&255u)||(z->back.write_mask&255u)));
 if(writes)e->nord2ZsWritten=1;
}
#endif"""),
(""" if(e->nord2DepthClamp)job->genericDrawFlags|=N2R_DRAW_DEPTH_CLAMP;""",""" if(e->nord2DepthClamp)job->genericDrawFlags|=N2R_DRAW_DEPTH_CLAMP;
#ifdef NORD2_JM_TRANSPORT
 tegu_render_zs_job(e,job);
#endif"""),
])
edit('metal/tegu/compositor_submit.inc',[
("""  struct n2r_draw_options options={N2R_DRAW_OPTIONS_VERSION,j->genericDrawFlags,
   j->genericVertexBase,j->genericBaseInstance,tls,0,
   {j->genericBlendConstant[0],j->genericBlendConstant[1],j->genericBlendConstant[2],j->genericBlendConstant[3]}};""","""  struct n2r_draw_options options={N2R_DRAW_OPTIONS_VERSION,j->genericDrawFlags,
   j->genericVertexBase,j->genericBaseInstance,tls,0,
   {j->genericBlendConstant[0],j->genericBlendConstant[1],j->genericBlendConstant[2],j->genericBlendConstant[3]},
   (j->genericDrawFlags&N2R_DRAW_DEPTH_STENCIL)?&j->genericZs:NULL};"""),
("""    x->options=(struct n2r_draw_options){N2R_DRAW_OPTIONS_VERSION,j->genericDrawFlags,
     j->genericVertexBase,j->genericBaseInstance,0,0,
     {j->genericBlendConstant[0],j->genericBlendConstant[1],j->genericBlendConstant[2],j->genericBlendConstant[3]}};""","""    x->options=(struct n2r_draw_options){N2R_DRAW_OPTIONS_VERSION,j->genericDrawFlags,
     j->genericVertexBase,j->genericBaseInstance,0,0,
     {j->genericBlendConstant[0],j->genericBlendConstant[1],j->genericBlendConstant[2],j->genericBlendConstant[3]},
     (j->genericDrawFlags&N2R_DRAW_DEPTH_STENCIL)?&j->genericZs:NULL};"""),
(""" if(!member)return 0;
 if(j->nord2CopySources[0]||j->nord2CopySources[1])return TEGU_BREAK_PRECLEAR;""",""" /* Tile-buffer depth/stencil: a continuing job (load 1) only joins the
  * frame of the same pass; a depth-starting job opens a new frame. */
 {unsigned zj=(j->genericDrawFlags&N2R_DRAW_DEPTH_STENCIL)!=0,z0=(j0->genericDrawFlags&N2R_DRAW_DEPTH_STENCIL)!=0;
  if(member&&(zj!=z0||(zj&&(j->genericZs.load!=1||j->perfPass!=j0->perfPass||j->genericZs.format!=j0->genericZs.format||
     j->genericZs.clear_depth!=j0->genericZs.clear_depth||j->genericZs.clear_stencil!=j0->genericZs.clear_stencil))))
   return TEGU_BREAK_TARGET;
  if(!member&&zj&&j->genericZs.load==1)return TEGU_JM_BREAK_SHAPE;}
 if(!member)return 0;
 if(j->nord2CopySources[0]||j->nord2CopySources[1])return TEGU_BREAK_PRECLEAR;"""),
])
print('applied')
edit('metal/tegu/draw_reorder.inc',[(""" if(!j->perfPass||j->blit||j->genericPreclear||!j->genericDrawSnapshot||!p||!p->genericDraw||""",""" /* Tile-buffer depth/stencil jobs stay in place: their frame membership is
  * the pass order (render_encoder tegu_render_zs_job). */
 if(j->genericDrawFlags&N2R_DRAW_DEPTH_STENCIL)return a;
 if(!j->perfPass||j->blit||j->genericPreclear||!j->genericDrawSnapshot||!p||!p->genericDraw||""")])
print('reorder ok')
