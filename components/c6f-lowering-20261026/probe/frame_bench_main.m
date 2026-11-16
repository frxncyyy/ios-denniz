/* Host replica of the batch-features-v9 probe sequence (no GPU, no phone).
 * Spliced after frame_bench's prelude (real Objective-C frontend, n2mb
 * bridge, Rust AIR translator, Mali compiler, shared fake JM kernel). It
 * issues the same draws as metal/nord2_metal_probe.m features() through the
 * frontend's own classes and checks the STRUCTURE the device test relies on
 * (status, jobs per command, chain length, pre-frame mode, LD_TILE variant,
 * WAIT_SEQ reaping, command-pool reuse). Pixels are a device-only result.
 * argv[1] = linked probe bitcode (varying + compute + geometry + fetch). */
#include <stdarg.h>
static void *ph_read(const char *p,long *n){FILE *f=fopen(p,"rb");assert(f);fseek(f,0,SEEK_END);*n=ftell(f);rewind(f);
 void *b=malloc((size_t)*n);assert(b&&fread(b,1,(size_t)*n,f)==(size_t)*n);fclose(f);return b;}
static unsigned ph_fail,ph_count;
static void ph_result(const char *tag,int ok,const char *fmt,...){
 char text[256];va_list ap;va_start(ap,fmt);vsnprintf(text,sizeof(text),fmt,ap);va_end(ap);
 ph_count++;ph_fail+=!ok;printf("HOST feature=%s result=%s %s\n",tag,ok?"PASS":"FAIL",text);fflush(stdout);
}
static RenderDescriptor *ph_desc(TeguFunction *v,TeguFunction *f){
 RenderDescriptor *pd=[[RenderDescriptor alloc] init];
 pd->vertex=[v retain];pd->fragment=[f retain];pd->samples=1;pd->raster=1;pd->amplification=1;pd->topology=0;
 pd->colors=[[RenderColors alloc] init];
 for(unsigned i=0;i<8;i++){pd->colors->colors[i]=[[RenderColor alloc] init];pd->colors->colors[i]->mask=15;}
 pd->colors->colors[0]->format=80;
 RenderLayout *l=[[RenderLayout alloc] init];l->buffers=[[RenderLayout alloc] init];
 for(unsigned k=0;k<2;k++){RenderAttribute *a=[[RenderAttribute alloc] init];a->value=31;a->byteOffset=k*16;a->bufferSlot=1;l->entries[7+k]=a;}
 RenderAttribute *b=[[RenderAttribute alloc] init];b->layoutStride=32;b->step=1;b->rate=1;l->buffers->entries[1]=b;
 pd->layout=l;return pd;
}
static TeguTexture *ph_target(TeguComputeDevice *d,unsigned w,unsigned h){
 TextureDesc *desc=[[TextureDesc alloc] init];desc->format=80;desc->w=w;desc->h=h;desc->levels=1;desc->uses=5;desc->options=0x20;
 TeguTexture *t=[d newTextureWithDescriptor:(id)desc];[desc release];assert(t);return t;
}
static RenderPass *ph_pass(TeguTexture *t,unsigned load){
 RenderPass *p=[[RenderPass alloc] init];p->color=[[PassColor alloc] init];p->color->target=[t retain];
 p->color->load=load;p->color->store=1;p->passWidth=t->textureWidth;p->passHeight=t->textureHeight;p->arrayLength=1;p->sampleCount=1;
 return p;
}
static void ph_vtx(float *v,float x,float y,unsigned r,unsigned g,unsigned b){const float rec[8]={x,y,0,1,r/255.0f,g/255.0f,b/255.0f,1};memcpy(v,rec,sizeof(rec));}
static void ph_full(float *v,unsigned r,unsigned g,unsigned b){ph_vtx(v,-1,-1,r,g,b);ph_vtx(v+8,3,-1,r,g,b);ph_vtx(v+16,-1,3,r,g,b);}
/* Submit one command and report its kernel submissions. */
static unsigned ph_run(struct fake *f,TeguCommand *c,unsigned long *status){
 unsigned s0=f->submitted;[c commit];[c waitUntilCompleted];*status=(unsigned long)[c status];return f->submitted-s0;
}
int main(int argc,char **argv){
 assert(argc>=2);
 tegu_host_process_backboardd=1;
 static struct fake f;
 {const char *a=getenv("FB_ASYNC");if(a&&a[0]=='1'){f.seq_model=1;f.window=96;}}
 uintptr_t bridge=open_bridge(&f);
 TeguComputeDevice *d=[TeguComputeDevice alloc];
 d->connection=bridge;d->scalar=scalar_adapter;d->method=method_adapter;d->map=map_adapter;d->unmap=unmap_adapter;
 d->capabilities=(1ULL<<14)|(1ULL<<18)|256ULL|64ULL|32ULL|16ULL|(1ULL<<15);
 printf("HOST async=%u preframe=%u batch_cap=%u\n",n2mb_async(bridge,2),n2mb_preframe(bridge,2),n2mb_batch_capacity(bridge,0));
 long n=0;void *bc=ph_read(argv[1],&n);id err=0;
 TeguLibrary *lib=[d newTeguLibraryWithBytes:bc length:(NSUInteger)n error:&err];free(bc);
 if(!lib){printf("HOST FATAL library parse\n");return 3;}
 TeguFunction *vv=[lib newFunctionWithName:[NSString stringWithUTF8String:"varying_vertex"]];
 TeguFunction *vf=[lib newFunctionWithName:[NSString stringWithUTF8String:"varying_fragment"]];
 TeguFunction *ff=[lib newFunctionWithName:[NSString stringWithUTF8String:"nord2_fetch_fragment"]];
 if(!vv||!vf||!ff){printf("HOST FATAL functions %p %p %p\n",(void*)vv,(void*)vf,(void*)ff);return 3;}
 id error=0;
 TeguRenderPipeline *pv=[d newRenderPipelineStateWithDescriptor:(id)ph_desc(vv,vf) error:&error];
 TeguRenderPipeline *pf=[d newRenderPipelineStateWithDescriptor:(id)ph_desc(vv,ff) error:&error];
 if(!pv||!pf||!pv->genericDraw||!pf->genericDraw){printf("HOST FATAL pipelines pv=%p pf=%p\n",(void*)pv,(void*)pf);return 3;}
 /* The other probe functions of the same bitcode must translate and compile too. */
 {TeguFunction *cf=[lib newFunctionWithName:[NSString stringWithUTF8String:"nord2_compute_probe"]];
  TeguFunction *qv=[lib newFunctionWithName:[NSString stringWithUTF8String:"nord2_quad_vertex"]];
  id cp=cf?[d newComputePipelineStateWithFunction:cf error:&error]:0;
  RenderDescriptor *qd=qv?ph_desc(qv,vf):0;if(qd){[qd->layout release];qd->layout=0;}
  TeguRenderPipeline *qp=qd?[d newRenderPipelineStateWithDescriptor:(id)qd error:&error]:0;
  ph_result("bitcode-functions",cp&&qp&&qp->genericDraw,"compute=%d quad=%d",cp!=0,qp!=0);}
 TeguQueue *q=[d newCommandQueue];
 TeguBuffer *vb=[d newBufferWithLength:3*128*8*4 options:0];float *verts=(float *)[vb contents];
 unsigned long st=0;unsigned jobs;

 /* 1. tile-fetch batching */
 {TeguTexture *t=ph_target(d,64,64);ph_full(verts,255,0,0);ph_full(verts+24,0,0,255);
  TeguCommand *c=[q commandBuffer];RenderPass *p=ph_pass(t,1);TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)p];
  [e setVertexBuffer:vb offset:0 atIndex:1];
  static const unsigned ops[5][5]={{0,8,8,24,24},{1,0,0,64,64},{2,24,24,24,24},{1,0,0,40,40},{1,16,16,48,48}};
  for(unsigned i=0;i<5;i++){const unsigned *o=ops[i];[e setScissorRect:(TeguScissorRect){o[1],o[2],o[3],o[4]}];
   [e setRenderPipelineState:o[0]==1?pf:pv];[e drawPrimitives:3 vertexStart:o[0]==2?3:0 vertexCount:3];}
  [e endEncoding];jobs=ph_run(&f,c,&st);
  unsigned tile=(pf->genericShader.flags&TEGU_GENERIC_SHADER_TILE_FETCH)!=0;
  for(unsigned v=0;v<pf->genericVariantCount;v++)tile|=(pf->genericVariants[v].shader.flags&TEGU_GENERIC_SHADER_TILE_FETCH)!=0;
  ph_result("tilefetch-batch",st==4&&jobs==1&&tile&&f.last_chain==5,"status=%lu jobs=%u chain=%u ld_tile=%u",st,jobs,f.last_chain,tile);
  [p release];[t release];}
 /* 2. INTERSECT pre-frame partial LOAD */
 {TeguTexture *t=ph_target(d,128,128);ph_full(verts,10,220,30);ph_full(verts+24,200,40,90);
  TeguCommand *c=[q commandBuffer];RenderPass *p=ph_pass(t,1);TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)p];
  [e setRenderPipelineState:pv];[e setVertexBuffer:vb offset:0 atIndex:1];
  static const unsigned boxes[2][4]={{37,45,20,9},{100,4,10,30}};
  for(unsigned i=0;i<2;i++){[e setScissorRect:(TeguScissorRect){boxes[i][0],boxes[i][1],boxes[i][2],boxes[i][3]}];[e drawPrimitives:3 vertexStart:3*i vertexCount:3];}
  [e endEncoding];jobs=ph_run(&f,c,&st);
  ph_result("preframe-partial",st==4&&jobs==1&&f.last_preframe==2&&f.last_chain==2,"status=%lu jobs=%u preframe=%u chain=%u",st,jobs,f.last_preframe,f.last_chain);
  [p release];[t release];}
 /* 3. batch cap: 100 draws */
 {TeguTexture *t=ph_target(d,128,128);for(unsigned i=0;i<100;i++)ph_full(verts+24*i,(i*37+11)&255,(i*91+50)&255,(255-i*2)&255);
  TeguCommand *c=[q commandBuffer];RenderPass *p=ph_pass(t,1);TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)p];
  [e setRenderPipelineState:pv];[e setVertexBuffer:vb offset:0 atIndex:1];
  for(unsigned i=0;i<100;i++){[e setScissorRect:(TeguScissorRect){(i%10)*12+4,(i/10)*12+4,10,10}];[e drawPrimitives:3 vertexStart:3*i vertexCount:3];}
  [e endEncoding];jobs=ph_run(&f,c,&st);
  ph_result("batch-cap",st==4&&jobs>=1&&jobs<=4,"status=%lu jobs=%u chain=%u (1 job needs frontend cap 128; 4 = cap 32)",st,jobs,f.last_chain);
  [p release];[t release];}
 /* 4. native indexed: list and strip with restart */
 {TeguTexture *t=ph_target(d,64,64);
  ph_vtx(verts,-.5f,-.5f,40,200,120);ph_vtx(verts+8,.5f,-.5f,40,200,120);ph_vtx(verts+16,-.5f,.5f,40,200,120);ph_vtx(verts+24,.5f,.5f,40,200,120);
  const float sx[4]={-1,-.25f,.25f,1};
  for(unsigned k=0;k<2;k++){unsigned base=4+k*4;float l=sx[k*2],r=sx[k*2+1];
   ph_vtx(verts+base*8,l,-.5f,250,120,10);ph_vtx(verts+(base+1)*8,r,-.5f,250,120,10);ph_vtx(verts+(base+2)*8,l,.5f,250,120,10);ph_vtx(verts+(base+3)*8,r,.5f,250,120,10);}
  TeguBuffer *ib=[d newBufferWithLength:64 options:0];const uint16_t idx[16]={0,1,2,2,1,3,4,5,6,7,0xffff,8,9,10,11,0};memcpy([ib contents],idx,sizeof(idx));
  for(unsigned mode=0;mode<2;mode++){
   TeguCommand *c=[q commandBuffer];RenderPass *p=ph_pass(t,2);TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)p];
   [e setRenderPipelineState:pv];[e setVertexBuffer:vb offset:0 atIndex:1];
   if(!mode)[e drawIndexedPrimitives:3 indexCount:6 indexType:0 indexBuffer:ib indexBufferOffset:0];
   else [e drawIndexedPrimitives:4 indexCount:9 indexType:0 indexBuffer:ib indexBufferOffset:12];
   [e endEncoding];jobs=ph_run(&f,c,&st);
   ph_result(mode?"indexed-strip-restart":"indexed-attr",st==4&&jobs==1,"status=%lu jobs=%u",st,jobs);[p release];
  }
  [ib release];[t release];}
 /* 5. async: 32 commands committed back to back, one wait */
 {TeguTexture *t=ph_target(d,64,64);TeguTexture *sep[8];TeguCommand *last=0;unsigned s0=f.submitted,w0=f.seq_waits,ok=1;
  TeguCommand *cs[32];
  for(unsigned k=0;k<32;k++)ph_full(verts+24*k,(k*10)&255,(k*7)&255,(k*53)&255);
  for(unsigned k=0;k<32;k++){
   TeguTexture *tt=k<24?t:(sep[k-24]=ph_target(d,32,32));
   TeguCommand *c=[q commandBuffer];RenderPass *p=ph_pass(tt,k<24?1:2);TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)p];
   [e setRenderPipelineState:pv];[e setVertexBuffer:vb offset:0 atIndex:1];
   [e setScissorRect:k<24?(TeguScissorRect){0,k*2,64,2}:(TeguScissorRect){k-24,k-24,16,16}];
   [e drawPrimitives:3 vertexStart:3*k vertexCount:3];[e endEncoding];[c commit];cs[k]=[c retain];last=c;[p release];
  }
  [last waitUntilCompleted];
  for(unsigned k=0;k<32;k++){[cs[k] waitUntilCompleted];ok&=[cs[k] status]==4;[cs[k] release];}
  ph_result("async-many",ok&&f.submitted-s0==32,"commands=32 submits=%u seq_waits=%u async=%u",f.submitted-s0,f.seq_waits-w0,n2mb_async(bridge,2));
  for(unsigned k=0;k<8;k++)[sep[k] release];[t release];}
 /* 6. command pool reuse */
 {TeguTexture *t=ph_target(d,64,64);uint64_t h0=0,m0=0,h1=0,m1=0;n2mb_cmdpool_stats(bridge,&h0,&m0);unsigned ok=1;
  for(unsigned k=0;k<40;k++){unsigned draws=k&1?3:1;
   for(unsigned j=0;j<draws;j++)ph_full(verts+24*j,(k*7+j*60)&255,(k*13+40)&255,(j*90+k)&255);
   TeguCommand *c=[q commandBuffer];RenderPass *p=ph_pass(t,2);TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)p];
   [e setRenderPipelineState:pv];[e setVertexBuffer:vb offset:0 atIndex:1];
   for(unsigned j=0;j<draws;j++){[e setScissorRect:(TeguScissorRect){(k+j*17)%40,(k*3+j*11)%40,20,20}];[e drawPrimitives:3 vertexStart:3*j vertexCount:3];}
   [e endEncoding];ph_run(&f,c,&st);ok&=st==4;[p release];}
  n2mb_cmdpool_stats(bridge,&h1,&m1);
  ph_result("cmdpool-reuse",ok&&h1-h0>=30,"commands=40 pool_hits=%llu pool_misses=%llu",(unsigned long long)(h1-h0),(unsigned long long)(m1-m0));
  [t release];}
 printf("HOST summary features=%u failed=%u\n",ph_count,ph_fail);
 printf("FB_DONE\n");
 return ph_fail?1:0;
}
