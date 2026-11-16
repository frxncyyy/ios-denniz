#include <pthread.h>
#define TEGU_IOSURFACE_TEST_LOADER 1
struct TeguIOSurfaceAPI;
void *tegu_test_iosurface_dlopen(const char *,int);
void *tegu_test_iosurface_dlsym(void *,const char *);
#define TEGU_GRAPHICS_BRIDGE 1
#define TEGU_RUNTIME_MAIN compute_runtime_test_main
#include "runtime_frontend_host_test.m"
@interface RenderColor : NSObject { @public NSUInteger format,mask,rgbOp,rgbSrc,rgbDst,alphaOp,alphaSrc,alphaDst; signed char blend; }
- (NSUInteger)writeMask;
@end
@implementation RenderColor
- (id)init {self=[super init];if(self){rgbSrc=alphaSrc=1;rgbDst=alphaDst=0;}return self;}
- (NSUInteger)pixelFormat{return format;}
- (NSUInteger)writeMask{return mask;}
- (signed char)isBlendingEnabled{return blend;}
- (NSUInteger)rgbBlendOperation{return rgbOp;}
- (NSUInteger)sourceRGBBlendFactor{return rgbSrc;}
- (NSUInteger)destinationRGBBlendFactor{return rgbDst;}
- (NSUInteger)alphaBlendOperation{return alphaOp;}
- (NSUInteger)sourceAlphaBlendFactor{return alphaSrc;}
- (NSUInteger)destinationAlphaBlendFactor{return alphaDst;}
@end
@interface RenderColors : NSObject { @public RenderColor *colors[8]; }
@end
@implementation RenderColors
- (id)objectAtIndexedSubscript:(NSUInteger)i {assert(i<8);return colors[i];}
- (void)dealloc {for(unsigned i=0;i<8;i++)[colors[i] release];[super dealloc];}
@end
@interface RenderAttribute : NSObject { @public NSUInteger value,byteOffset,bufferSlot,layoutStride,step,rate; }
@end
@implementation RenderAttribute
- (NSUInteger)format{return value;}
- (NSUInteger)offset{return byteOffset;}
- (NSUInteger)bufferIndex{return bufferSlot;}
- (NSUInteger)stride{return layoutStride;}
- (NSUInteger)stepFunction{return step;}
- (NSUInteger)stepRate{return rate;}
@end
@interface RenderLayout : NSObject { @public RenderAttribute *entries[31];RenderLayout *buffers; }
@end
@implementation RenderLayout
- (id)attributes{return self;}
- (id)layouts{return buffers;}
- (id)objectAtIndexedSubscript:(NSUInteger)i {assert(i<31);return entries[i];}
- (void)dealloc{for(unsigned i=0;i<31;i++)[entries[i] release];[buffers release];[super dealloc];}
@end
@interface RenderDescriptor : NSObject {
@public TeguFunction *vertex,*fragment;RenderColors *colors;id debugLabel,layout;
 NSUInteger samples,depth,stencil,topology,amplification;
 signed char coverage,alpha,raster,indirect;
}
@end
@implementation RenderDescriptor
- (id)vertexFunction{return vertex;}
- (id)fragmentFunction{return fragment;}
- (id)vertexDescriptor{return layout;}
- (id)colorAttachments{return colors;}
- (NSUInteger)rasterSampleCount{return samples;}
- (NSUInteger)depthAttachmentPixelFormat{return depth;}
- (NSUInteger)stencilAttachmentPixelFormat{return stencil;}
- (signed char)isAlphaToCoverageEnabled{return coverage;}
- (signed char)isAlphaToOneEnabled{return alpha;}
- (signed char)isRasterizationEnabled{return raster;}
- (NSUInteger)inputPrimitiveTopology{return topology;}
- (NSUInteger)maxVertexAmplificationCount{return amplification;}
- (signed char)supportIndirectCommandBuffers{return indirect;}
- (id)label{return debugLabel;}
- (void)dealloc{[vertex release];[fragment release];[colors release];[debugLabel release];[layout release];[super dealloc];}
@end
#include "render_encoder_host_test.inc"
#include "heap_frontend_host_test.inc"
#include "sampler_state_host_test.inc"
#include "render_fragment_bindings_host_test.inc"
#include "ws_selector_compat_host_test.inc"
static void test_compute_texture_bindings(TeguComputeDevice *device) {
 NSUInteger baseline=[device currentAllocatedSize];
 TextureDesc *descriptor=[[TextureDesc alloc] init];descriptor->format=70;descriptor->w=descriptor->h=16;descriptor->uses=1;
 TeguTexture *first=[device newTextureWithDescriptor:(id)descriptor];
 TeguTexture *second=[device newTextureWithDescriptor:(id)descriptor];assert(first&&second);
 assert([device currentAllocatedSize]==baseline+32768);
 TeguQueue *queue=[device newCommandQueue];TeguCommand *command=[queue commandBuffer];TeguEncoder *encoder=[command computeCommandEncoder];
 [encoder setTexture:first atIndex:0];
 TeguTexture *batch[2]={second,0};[encoder setTextures:batch withRange:(TeguComputeRange){1,2}];
 [first release];[second release];assert([device currentAllocatedSize]==baseline+32768);
 [encoder endEncoding];[command commit];assert([command status]==4&&![command error]);
 [queue release];[descriptor release];drain();assert([device currentAllocatedSize]==baseline);
}
static unsigned renderImports,renderReleases,failImport,textureDraws,loadDraws,iosurfaceImports,iosurfaceReleases,importedTargetDraws,iosurfaceReplyMode,compositorIOSurfaceImports,compositorIOSurfaceReplyMode;static U64 importedDrawHandle;
static int failImportStatus;
static const U64 compositorIOSurfaceHandle=0xa0000011u;
static const U64 compositorIOSurfaceOffset=4096u;
static U64 compositorIOSurfaceWidth=1080u,compositorIOSurfaceHeight=2424u,compositorIOSurfaceRow=4352u,compositorIOSurfaceAlloc=(4096u+4352u*2424u+4095u)&~4095u;
static unsigned compositorImportedTargetDraws;
static struct tegu_render_texture_request lastTextureRequest;
static struct tegu_render_target_request lastTargetRequest;
static unsigned compositorDraws,compositorPartialImport,compositorPsoMode,compositorV7PsoMode,compositorB3PsoMode,attachmentClearPsoMode,attachmentClearDraws,compositorExpectedVertexSlot=1,compositorExpectedVertexLength=144;
static unsigned fixedUiPsoMode,fixedUiDraws,genericJobs,genericLastDcd0,genericLastSampler,genericLastPlaneStride,genericLastTextureSize;static struct tegu_ui1_info fixedUiInfo;static struct tegu_ui2_info fixedUiTextureInfo;
static unsigned genericBatchCalls,genericBatchIdvs[8],genericBatchFragments[8],genericBatchFlags[8],genericBatchCommands[8];
/* Selector of each recorded job (19 TGJ1, 20 TGJ2) and the heap use the fake
 * TGJ2 kernel reports. */
static unsigned genericBatchSelector[8],genericJobs2;static U64 genericFakeHeap=UINT64_MAX;
static struct tegu_attachment_clear_request lastAttachmentClearRequest;
static unsigned compositorExpectedFsUboWindow=72;
static unsigned compositorMultiDrawMode,compositorMultiDrawStart,compositorMultiDrawFail,compositorMultiDrawInitialLoad;
static U64 compositorExpectedVertex2Handle,compositorExpectedTexture2Handle,compositorExpectedPipelineSecondHandle,compositorExpectedVertexFetch2Handle;
static unsigned compositorExpectedVertexLength2;
static U64 compositorExpectedPipelineHandle=0xc0000001u;
static int render_scalar(unsigned,unsigned,const U64 *,unsigned,U64 *,unsigned *);
#include "render_vertex_host_test.inc"
#include "render_varying_host_test.inc"
#include "render_blend_host_test.inc"
#include "bgra_frontend_host_test.inc"
#include "render_load_frontend_host_test.inc"
static int render_method(unsigned c,unsigned selector,const U64 *args,unsigned argc,const void *data,NSUInteger size,U64 *out,unsigned *n,void *unused,NSUInteger *unusedSize) {
 (void)c;(void)args;(void)unused;(void)unusedSize;
 if(selector==TEGU_ATTACHMENT_CLEAR_SELECTOR) {
  assert(argc==1&&*n==2&&size==TEGU_ATTACHMENT_CLEAR_REQUEST_BYTES&&args[0]>=0xc0000001u);
  memcpy(&lastAttachmentClearRequest,data,size);const struct tegu_attachment_clear_request *r=&lastAttachmentClearRequest;
  assert(r->version==1&&r->width==776&&r->height==24&&r->sample_count==1&&r->attachment_count==2&&
   r->target_mask==2&&r->draw_write_mask==2&&!r->draw_read_mask);
  for(unsigned i=0;i<2;i++) {
   assert(r->targets[i].memory.handle<4096&&memory[r->targets[i].memory.handle]);
   assert(!r->targets[i].memory.offset&&r->targets[i].memory.length==77824u);
   assert(r->targets[i].format==80&&r->targets[i].row_stride==3136&&!r->targets[i].reserved[0]&&!r->targets[i].reserved[1]);
  }
  assert(r->targets[0].memory.handle!=r->targets[1].memory.handle);
  assert(!r->targets[0].write_mask&&!r->targets[0].load_action&&!r->targets[0].store_action&&!r->targets[0].clear_rgba8);
  assert(r->targets[1].write_mask==15&&r->targets[1].load_action==2&&r->targets[1].store_action==1&&r->targets[1].clear_rgba8==0x44332211);
  assert(r->rect.handle<4096&&memory[r->rect.handle]&&r->rect.offset==4&&r->rect.length==16);
  assert(r->matrix.handle<4096&&memory[r->matrix.handle]&&r->matrix.offset==16&&r->matrix.length==320&&r->rect.handle!=r->matrix.handle);
  assert(r->primitive_type==3&&!r->vertex_start&&r->vertex_count==3&&r->instance_count==1&&!r->base_instance&&!r->indexed&&!r->reserved[0]&&!r->reserved[1]);
  out[0]=++attachmentClearDraws;out[1]=attachmentClearDraws;return 0;
 }
 if(selector==TEGU_GENERIC_JOB2_SELECTOR) {
  /* TGJ2: the same contract with 32 spans, 400 commands, three outputs. */
  assert(fixedUiPsoMode&&argc==0&&*n==TEGU_GENERIC_JOB2_OUTPUTS&&size==sizeof(struct tegu_generic_job2_request));
  const struct tegu_generic_job2_request *r=data;struct tegu_generic_job_plan plan;
  assert(!tegu_generic_job2_plan(r,&plan)&&r->command_count<=TEGU_GENERIC_JOB2_MAX_COMMANDS);
  assert(r->scratch_token<4096&&memory[r->scratch_token]&&r->target_token<4096&&memory[r->target_token]);
  for(unsigned i=0;i<r->span_count;i++)assert(r->spans[i].token<4096&&memory[r->spans[i].token]);
  const unsigned char *image=memory[r->scratch_token];
  uint64_t rt=0;memcpy(&rt,image+160,8);assert(rt==plan.target_va);
  assert(r->commands[r->command_count-1]==(34ull<<56));
  if(genericBatchCalls<8){unsigned c=genericBatchCalls++;genericBatchIdvs[c]=genericBatchFragments[c]=0;genericBatchSelector[c]=selector;
   genericBatchFlags[c]=r->flags;genericBatchCommands[c]=r->command_count;
   for(unsigned i=0;i<r->command_count;i++){genericBatchIdvs[c]+=(r->commands[i]>>56)==6;genericBatchFragments[c]+=(r->commands[i]>>56)==7;}}
  genericJobs++;genericJobs2++;out[0]=++fixedUiDraws;out[1]=fixedUiDraws;out[2]=genericFakeHeap;return 0;
 }
 if(selector==TEGU_GENERIC_JOB_SELECTOR) {
  /* TGJ1 carries only handles, a screened stream and plan-derived VAs. */
  assert(fixedUiPsoMode&&argc==0&&*n==2&&size==sizeof(struct tegu_generic_job_request));
  const struct tegu_generic_job_request *r=data;struct tegu_generic_job_plan plan;
  assert(!tegu_generic_job_plan(r,&plan));
  assert(r->scratch_token<4096&&memory[r->scratch_token]&&r->target_token<4096&&memory[r->target_token]);
  for(unsigned i=0;i<r->span_count;i++)assert(r->spans[i].token<4096&&memory[r->spans[i].token]);
  const unsigned char *image=memory[r->scratch_token];
  uint64_t rt=0;memcpy(&rt,image+160,8);assert(rt==plan.target_va);
  memcpy(&genericLastSampler,image+0x7100+192,4);memcpy(&genericLastPlaneStride,image+0x7200+16,4);
  memcpy(&genericLastTextureSize,image+0x7100+160+4,4);
  assert(r->commands[r->command_count-1]==(34ull<<56));
  /* Last MOVE32 to IDVS register 57 (DCD flags 0): 3 opaque, 2 reads destination. */
  for(unsigned i=0;i<r->command_count;i++)if((r->commands[i]>>56)==2&&((r->commands[i]>>48)&255)==57)genericLastDcd0=(unsigned)r->commands[i];
  if(genericBatchCalls<8){unsigned c=genericBatchCalls++;genericBatchIdvs[c]=genericBatchFragments[c]=0;genericBatchSelector[c]=selector;
   genericBatchFlags[c]=r->flags;genericBatchCommands[c]=r->command_count;
   for(unsigned i=0;i<r->command_count;i++){genericBatchIdvs[c]+=(r->commands[i]>>56)==6;genericBatchFragments[c]+=(r->commands[i]>>56)==7;}}
  genericJobs++;out[0]=++fixedUiDraws;out[1]=fixedUiDraws;return 0;
 }
 if(selector==16) {
  if(size==sizeof(struct tegu_ui2_bind_request)) {
   assert(fixedUiPsoMode&&argc==4&&*n==2&&args[0]>=0xc0000001u);
   const struct tegu_ui2_bind_request *r=data;struct tegu_ui2_binding binding;
   assert(!tegu_ui2_bind(&fixedUiTextureInfo,r,&binding));
   assert(args[1]==r->target.allocation.token&&args[2]==r->target.allocation.offset&&args[3]==r->pass.clear_color[0]);
   assert(r->width==16&&r->height==16&&r->target.format==80&&r->target.row_bytes==64&&
    r->vertex_slot==1&&r->vertex_stride==48&&r->vertex_count>=3&&r->vertex_count<=384&&!(r->vertex_count%3)&&r->instance_count==1&&
    r->resources[0].metal_stage==1&&r->resources[0].metal_slot==2&&
    r->resources[1].metal_stage==1&&r->resources[1].metal_slot==3&&
    r->resources[2].metal_stage==2&&r->resources[2].metal_slot==1&&
    r->resources[3].metal_stage==2&&r->resources[3].metal_slot==2&&
    r->texture.metal_stage==2&&r->texture.metal_slot==3&&r->texture.width==194&&r->texture.height==237&&
    (r->pass.load_action==1||r->pass.load_action==2)&&r->pass.store_action==1&&
    (r->pass.load_action==2||!r->pass.clear_color[0]));
   out[0]=++fixedUiDraws;out[1]=fixedUiDraws;return 0;
  }
  if(size==sizeof(struct tegu_ui1_bind_request)) {
   assert(fixedUiPsoMode&&argc==4&&*n==2&&args[0]>=0xc0000001u);
   const struct tegu_ui1_bind_request *r=data;struct tegu_ui1_binding binding;
   assert(!tegu_ui1_bind(&fixedUiInfo,r,&binding));
   assert(args[1]==r->target.allocation.token&&args[2]==r->target.allocation.offset&&args[3]==r->pass.clear_color[0]);
   assert(r->width==16&&r->height==16&&r->target.format==80&&r->target.row_bytes==64&&
    r->vertex_slot==1&&r->vertex_stride==48&&r->vertex_count>=3&&r->vertex_count<=384&&!(r->vertex_count%3)&&r->instance_count==1&&
    r->resources[0].metal_stage==1&&r->resources[0].metal_slot==2&&
    r->resources[1].metal_stage==2&&r->resources[1].metal_slot==1&&
    (r->pass.load_action==2||(r->pass.load_action==1&&!r->pass.clear_color[0]))&&r->pass.store_action==1);
   out[0]=++fixedUiDraws;out[1]=fixedUiDraws;return 0;
  }
  assert(argc==4&&*n==2&&size==TEGU_COMPOSITOR_REQUEST_BYTES&&args[0]>=0xc0000001u);
  const struct tegu_compositor_request *r=data;
  unsigned sequence=compositorDraws-compositorMultiDrawStart;
  unsigned load=(compositorMultiDrawMode&&(sequence||compositorMultiDrawInitialLoad))?TEGU_COMPOSITOR_LOAD_LOAD:TEGU_COMPOSITOR_LOAD_CLEAR;
  U64 expectedPipeline=(compositorMultiDrawMode&&sequence==1)?compositorExpectedPipelineSecondHandle:compositorExpectedPipelineHandle;
  if(args[1]==compositorIOSurfaceHandle) {
   assert(args[1]==compositorIOSurfaceHandle&&args[2]==compositorIOSurfaceOffset&&r->width==compositorIOSurfaceWidth&&r->height==compositorIOSurfaceHeight&&r->row_stride==compositorIOSurfaceRow);
   compositorImportedTargetDraws++;
  } else assert(args[1]<4096&&memory[args[1]]&&!args[2]);
  assert(args[3]==(compositorB3PsoMode&&r->width==776?0:(load==TEGU_COMPOSITOR_LOAD_CLEAR?0x44332211:0)));
  assert(args[0]==expectedPipeline); /* each draw snapshots its own imported PSO */
  assert(r->version==(load==TEGU_COMPOSITOR_LOAD_LOAD?TEGU_COMPOSITOR_REQUEST_VERSION_LOAD:TEGU_COMPOSITOR_REQUEST_VERSION)&&((r->width==16&&r->height==16&&r->row_stride==64)||
   (compositorB3PsoMode&&r->width==776&&r->height==24&&r->row_stride==3104)||
   (r->width==compositorIOSurfaceWidth&&r->height==compositorIOSurfaceHeight&&r->row_stride==compositorIOSurfaceRow)));
  assert(r->load_action==load&&r->store_action==1&&r->attachment_mask==1&&!r->reserved);
  assert(r->vertex.version==1&&r->vertex.buffer_mask==(1u<<compositorExpectedVertexSlot));
  unsigned expectedVertexLength=(compositorMultiDrawMode&&sequence==1)?compositorExpectedVertexLength2:compositorExpectedVertexLength;
  U64 expectedVertexHandle=(compositorMultiDrawMode&&sequence==1)?compositorExpectedVertexFetch2Handle:0;
  assert(r->vertex.buffers[compositorExpectedVertexSlot].length==expectedVertexLength);
  assert(r->vertex.buffers[compositorExpectedVertexSlot].handle<4096&&memory[r->vertex.buffers[compositorExpectedVertexSlot].handle]);
  if(expectedVertexHandle)assert(r->vertex.buffers[compositorExpectedVertexSlot].handle==expectedVertexHandle);
  for(unsigned i=0;i<TEGU_VERTEX_SLOTS;i++)if(i!=compositorExpectedVertexSlot)
   assert(!r->vertex.buffers[i].handle&&!r->vertex.buffers[i].offset&&!r->vertex.buffers[i].length);
  const unsigned offset[4]={compositorB3PsoMode&&r->width==776?0u:16u,compositorB3PsoMode&&r->width==776?0u:16u,
   compositorB3PsoMode&&r->width==776?0u:24u,compositorB3PsoMode&&r->width==776?0u:30u};
  const unsigned length[4]={64,16,compositorExpectedFsUboWindow,2};
  U64 expectedVertex2=(compositorMultiDrawMode&&sequence==1)?compositorExpectedVertex2Handle:0;
  U64 expectedTexture=(compositorMultiDrawMode&&sequence==1)?compositorExpectedTexture2Handle:0;
  for(unsigned i=0;i<4;i++) {
   assert(r->ubos[i].handle<4096&&memory[r->ubos[i].handle]);
   assert(r->ubos[i].offset==offset[i]&&r->ubos[i].length==length[i]);
  }
  if(expectedVertex2)assert(r->ubos[0].handle==expectedVertex2);
  if(expectedTexture)assert(r->texture.handle==expectedTexture);
  assert(r->texture.handle<4096&&memory[r->texture.handle]&&!r->texture.offset&&
   r->texture.length==(compositorB3PsoMode&&r->texture_format==80?74496u:1024u));
  assert((r->texture_format==70&&r->texture_width==16&&r->texture_height==16&&r->texture_stride==64)||
   (compositorB3PsoMode&&r->texture_format==80&&r->texture_width==776&&r->texture_height==24&&r->texture_stride==3104));
  assert(r->sampler_class==1);
  out[0]=++compositorDraws;out[1]=compositorDraws;
  if(compositorMultiDrawMode&&compositorMultiDrawFail&&sequence==1)return -33;
  return 0;
 }
 if(selector==13) {
  assert(argc==1&&*n==1&&!size&&data==0&&args[0]>=501&&args[0]<=510);iosurfaceImports++;
  out[0]=iosurfaceReplyMode==2?0x100000001ULL:0xa0000001u;
  if(iosurfaceReplyMode==1)*n=0;
  if(iosurfaceReplyMode==3)return -1;
  return 0;
 }
 if(selector==17) {
  assert(argc==2&&*n==4&&!size&&data==0&&args[0]>=501&&args[1]==1);compositorIOSurfaceImports++;
  out[0]=compositorIOSurfaceHandle;out[1]=compositorIOSurfaceOffset;
  out[2]=compositorIOSurfaceRow;out[3]=compositorIOSurfaceAlloc;
  if(compositorIOSurfaceReplyMode==1)*n=3;
  if(compositorIOSurfaceReplyMode==2)out[0]=0x100000001ULL;
  if(compositorIOSurfaceReplyMode==3)out[1]=3;
  if(compositorIOSurfaceReplyMode==4)out[2]++;
  if(compositorIOSurfaceReplyMode==5)out[3]-=4096;
  if(compositorIOSurfaceReplyMode==6)return -44;
  return 0;
 }
 if(selector==15) {
  assert(argc==4&&*n==2&&size==TEGU_RENDER_TARGET_REQUEST_BYTES&&args[0]>=0xc0000001u);
  assert(args[1]<4096&&memory[args[1]]&&args[2]==0&&args[3]<=0xffffffffu);
  memcpy(&lastTargetRequest,data,size);
  const struct tegu_render_target_request *r=&lastTargetRequest;
  if(r->version==TEGU_RENDER_TARGET_REQUEST_VERSION_LOAD) {
   assert(r->reserved[0]==TEGU_RENDER_PASS_LOAD&&!r->reserved[1]&&!r->reserved[2]);
   assert(r->width==16&&r->height==16&&r->row_stride==64&&r->kind==TEGU_RENDER_TARGET_REQUEST_NONE);
   for(unsigned i=0;i<sizeof(r->payload);i++)assert(!r->payload[i]);
   loadDraws++;out[0]=loadDraws;out[1]=loadDraws;return 0;
  }
  assert(r->version==1&&r->reserved[0]==0&&r->reserved[1]==0&&r->reserved[2]==0);
  assert(r->width>=16&&r->width<=256&&r->height>=16&&r->height<=256);
  assert(!(r->row_stride&63)&&r->row_stride>=r->width*4&&
    (U64)r->row_stride*r->height<=1u<<20);
  assert(r->kind==TEGU_RENDER_TARGET_REQUEST_TEXTURE);
  memcpy(&lastTextureRequest,r->payload,sizeof(lastTextureRequest));
  const struct tegu_render_texture_request *t=&lastTextureRequest;
  assert((t->version==1||t->version==2)&&t->vertex.version==1&&t->vertex.buffer_mask==2);
  assert(t->vertex.buffers[1].handle<4096&&memory[t->vertex.buffers[1].handle]);
  assert(t->texture.handle<4096&&memory[t->texture.handle]&&t->texture.handle!=args[1]);
  assert(!(t->texture.offset&4095));
  if(t->version==1)assert(!t->reserved[0]&&!t->reserved[1]&&!t->reserved[2]&&t->texture.length==1024);
  else assert(t->reserved[0]==115&&t->reserved[1]==128&&t->reserved[2]==2048&&t->texture.length==2048);
  textureDraws++;out[0]=textureDraws;out[1]=textureDraws;return 0;
 }
 if(selector==12) {
  assert(argc==4&&*n==2&&size==TEGU_RENDER_TEXTURE_REQUEST_BYTES&&args[0]>=0xc0000001u);
  assert(args[1]<4096&&memory[args[1]]&&args[2]==0&&args[3]<=0xffffffffu);
  memcpy(&lastTextureRequest,data,size);
  const struct tegu_render_texture_request *r=&lastTextureRequest;
  assert((r->version==1||r->version==2));
  if(r->version==1)assert(!r->reserved[0]&&!r->reserved[1]&&!r->reserved[2]);
  else assert(r->reserved[0]==115&&r->reserved[1]==128&&r->reserved[2]==2048);
  assert(r->vertex.version==1&&r->vertex.buffer_mask==2&&!r->vertex.reserved[0]&&!r->vertex.reserved[1]);
  const struct tegu_render_vertex_binding *v=&r->vertex.buffers[1],*t=&r->texture;
  assert(v->handle<4096&&memory[v->handle]&&v->length==96);
  assert(t->handle<4096&&memory[t->handle]&&t->handle!=args[1]);
  assert(!(t->offset&4095)&&t->length==(r->version==2?2048u:1024u));
  textureDraws++;out[0]=textureDraws;out[1]=textureDraws;return 0;
 }
 if(selector==11) {
  assert(argc==4&&*n==2&&size==TEGU_RENDER_VERTEX_REQUEST_BYTES&&args[0]>=0xc0000001u);
  memcpy(&lastVertexRequest,data,size);
  assert(lastVertexRequest.version==1&&lastVertexRequest.buffer_mask==2);
  const struct tegu_render_vertex_binding *b=&lastVertexRequest.buffers[1];
  assert(b->handle<4096&&memory[b->handle]&&b->length==96);
  vertexDraws++;out[0]=vertexDraws;out[1]=vertexDraws;return 0;
 }
 assert(selector==8&&!argc&&*n==1);
 if(failImport)return failImportStatus?failImportStatus:-1;
 struct tegu_render_info info;
 if(attachmentClearPsoMode) {
  const unsigned char *p=data;assert(size==TEGU_ATTACHMENT_CLEAR_PACKAGE_BYTES&&!memcmp(p,"TAC1",4));
  assert(tegu_render_word(p+4)==1&&tegu_render_word(p+8)==2496&&tegu_render_word(p+12)==448&&
   tegu_render_word(p+16)==1792&&tegu_render_word(p+20)==2240&&tegu_render_word(p+24)==256);
  assert(tegu_render_word(p+108)==2&&tegu_render_word(p+112)==2&&!tegu_render_word(p+116)&&tegu_render_word(p+120)==15);
  assert(tegu_render_word(p+128)==1&&tegu_render_word(p+132)==1&&!tegu_render_word(p+136)&&!tegu_render_word(p+140));
  assert(tegu_render_word(p+184)==2&&tegu_render_word(p+188)==1&&!tegu_render_word(p+192)&&tegu_render_word(p+196)==1);
  char digest[65];assert(!tegu_compiler_identity_sha256(data,size,digest)&&
   !strcmp(digest,"217802f22897082c62f9017a4e279f865673b84def2fa36072d2b0b21528b18f"));
  out[0]=0xc0000001u+renderImports++;return 0;
 }
 if(compositorPsoMode) {
  if(fixedUiPsoMode) {
   const unsigned char *p=data;
   if(size>=4&&!memcmp(p,"TFU2",4))
    assert(!tegu_ui2_parse(data,size,&fixedUiTextureInfo)&&fixedUiTextureInfo.version==2&&size==6272);
   else assert(!tegu_ui1_parse(data,size,&fixedUiInfo)&&fixedUiInfo.version==1&&size==5888);
   out[0]=0xc0000001u+renderImports++;return 0;
  }
  if(compositorB3PsoMode) {
   struct tegu_native_b3_info b3;assert(!tegu_native_b3_parse(data,size,&b3)&&b3.version==TEGU_NATIVE_B3_VERSION);
   assert(b3.sample_count==4&&b3.ubo_window[2]==82&&b3.attachment_count==2&&
    b3.attachment[0].format==80&&b3.attachment[0].write_mask==15&&
    b3.attachment[1].format==80&&b3.attachment[1].write_mask==0);
  } else if(compositorV7PsoMode) {
   struct tegu_native_v7_info v7;assert(!tegu_native_v7_parse(data,size,&v7)&&v7.version==7);
   assert(v7.sample_count==16&&v7.attachment_count==2&&v7.attachment[0].format==80&&
    v7.attachment[0].write_mask==15&&v7.attachment[1].format==80&&v7.attachment[1].write_mask==0);
  } else {
   assert(!tegu_render_parse_for_import(data,size,1,&info)&&info.version==6);
   assert(info.color_count==2&&info.color_formats[0]==80&&info.color_write_masks[0]==15&&
          info.color_formats[1]==80&&info.color_write_masks[1]==0);
  }
  out[0]=0xc0000001u+renderImports++;
  if(compositorPartialImport){*n=0;return -1;}
  return 0;
 }
 assert(!tegu_render_parse(data,size,&info));
 if(info.version==1)assert(info.stages[0].code_bytes==1152&&info.stages[1].code_bytes==384);
 else if(info.version==2)assert(info.vertex_mask==1&&info.vertex_count==1);
 else if(info.version==5) {
  assert(info.vertex_mask==3&&info.vertex_count==2&&info.position_offset==384&&
   info.secondary_offset==768&&info.secondary_registers==32&&info.varying_bytes==16);
  assert(info.texture.version==1&&info.texture.stage==16&&!info.texture.texture_index&&
   !info.texture.sampler_index&&!info.texture.descriptor_set&&!info.texture.binding&&
   info.texture.normalized_coordinates==1&&!info.blend.enabled);
  assert(info.stages[0].code_offset==1408&&info.push_offset==256);
 } else {
  assert((info.version==3||info.version==4)&&info.vertex_mask==3&&info.vertex_count==2);
  assert(info.position_offset==384&&info.secondary_offset==768&&info.secondary_registers==32);
  assert(info.secondary_preload==0x3800000000000000ULL&&info.varying_bytes==16);
  assert(info.stages[0].code_bytes==1024);
  if(info.version==3) {
   assert(info.stages[1].code_bytes==1664&&info.push_span[1]==68&&tegu_render_word((const unsigned char *)data+320)==0x2222);
  } else {
   assert(info.blend.enabled&&info.blend.version==1&&info.push_span[1]==0&&info.push_offset==192);
   assert(info.stages[0].code_offset==1344&&info.vertex[1].offset==16);
  }
 }
 out[0]=0xc0000001u+renderImports++;return 0;
}
static int render_scalar(unsigned c,unsigned selector,const U64 *args,unsigned argc,U64 *out,unsigned *n) {
 if(selector==14) {assert(argc==1&&*n==0&&(args[0]==0xa0000001u||args[0]==compositorIOSurfaceHandle));iosurfaceReleases++;return 0;}
 if(selector==10) {
  assert(argc==4&&*n==2&&args[0]>=0xc0000001u);
  if(args[1]==0xa0000001u){importedTargetDraws++;importedDrawHandle=args[1];}
  else assert(args[1]<4096&&memory[args[1]]);
  assert(!args[2]&&args[3]==(renderDraws==1?0x44332212:0x44332211));renderDraws++;
  if(failDraw)return -44;out[0]=renderDraws;out[1]=renderDraws;return 0;
 }
 if(selector!=9)return call(c,selector,args,argc,out,n);
 assert(argc==1&&*n==0&&*args>=0xc0000001u);renderReleases++;return 0;
}

#include "function_descriptor_host_test.inc"
#include "native_texture_frontend_host_test.inc"
#include "iosurface_frontend_host_test.inc"
#include "compositor_snapshot_host_test.inc"
#include "home_affordance_observer_host_test.inc"
#include "fixed_ui_frontend_host_test.inc"
#include "narrow_blur_observer_host_test.inc"
#include "compositor_multidraw_host_test.inc"
#include "generic_batch_frontend_host_test.inc"
static void test_library_loaders(TeguComputeDevice *device,const char *path) {
 libraryTestBundle=[[NSBundle alloc] init];libraryTestPath=[NSString stringWithUTF8String:path];
 NSURL *url=[[NSURL alloc] init];id error=0;
 TeguLibrary *loaded[3]={[device newDefaultLibrary],
  [device newDefaultLibraryWithBundle:libraryTestBundle error:&error],
  [device newLibraryWithURL:url error:&error]};
 for(unsigned i=0;i<3;i++) {
  assert(loaded[i]&&!error&&[loaded[i] device]==device);
  TeguFunction *f=[loaded[i] newFunctionWithName:[NSString stringWithUTF8String:"test_vertex"]];
  assert(f&&tegu_function_owned_class(f)&&[f functionType]==1&&[f device]==device);
  [f release];[loaded[i] release];
 }
 libraryTestFileURL=0;assert(![device newLibraryWithURL:url error:&error]&&error);libraryTestFileURL=1;
 libraryTestPath=0;assert(![device newDefaultLibraryWithBundle:libraryTestBundle error:&error]&&error);
 assert(![device newDefaultLibrary]);assert(![device newLibraryWithURL:0 error:&error]&&error);
 [url release];[libraryTestBundle release];libraryTestBundle=0;
}
#include "compositor_pso_host_test.inc"
#include "pipeline_cache_frontend_host_test.inc"
#include "tile_fetch_frontend_host_test.inc"

static void test_compositor_8tap_native_frontend(TeguComputeDevice *device,const char *path,const char *profile) {
 FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);rewind(f);assert(n>0);
 void *bytes=malloc((size_t)n);assert(bytes&&fread(bytes,1,(size_t)n,f)==(size_t)n);fclose(f);
 id error=0;TeguLibrary *lib=[device newTeguLibraryWithBytes:bytes length:(NSUInteger)n error:&error];free(bytes);assert(lib&&!error);
 RenderDescriptor *d=[[RenderDescriptor alloc] init];
 d->vertex=[lib newFunctionWithName:[NSString stringWithUTF8String:"downsample_blur_vert_lph"]];
 d->fragment=[lib newFunctionWithName:[NSString stringWithUTF8String:"downsample_8_frag_lph"]];assert(d->vertex&&d->fragment);
 d->colors=[[RenderColors alloc] init];
 for(unsigned i=0;i<8;i++)d->colors->colors[i]=[[RenderColor alloc] init];
 d->colors->colors[0]->format=80;d->colors->colors[0]->mask=15;
 d->colors->colors[0]->blend=1;d->colors->colors[0]->rgbSrc=d->colors->colors[0]->alphaSrc=1;
 d->colors->colors[0]->rgbDst=d->colors->colors[0]->alphaDst=5;
 d->colors->colors[1]->format=80;d->colors->colors[1]->mask=0;
 for(unsigned i=2;i<8;i++){d->colors->colors[i]->format=0;d->colors->colors[i]->mask=15;}
 d->samples=1;d->raster=1;d->amplification=1;d->topology=0;
 RenderLayout *layout=[[RenderLayout alloc] init];d->layout=layout;
 for(unsigned i=0;i<4;i++)layout->entries[i]=[[RenderAttribute alloc] init];
 layout->buffers=[[RenderLayout alloc] init];
 layout->buffers->entries[0]=[[RenderAttribute alloc] init];layout->buffers->entries[1]=[[RenderAttribute alloc] init];
 const unsigned format[4]={31,29,29,27},offset[4]={0,16,24,32};
 for(unsigned i=0;i<4;i++){layout->entries[i]->value=format[i];layout->entries[i]->byteOffset=offset[i];layout->entries[i]->bufferSlot=1;}
 layout->buffers->entries[1]->layoutStride=48;layout->buffers->entries[1]->step=1;layout->buffers->entries[1]->rate=1;
 U64 old=device->capabilities;
 device->capabilities=old|TEGU_GPU_CAP_COMPOSITOR|64;
 unsigned before=renderImports;compositorV7PsoMode=1;
 /* The v7 package must refuse descriptors outside the exact real 8-tap class
  * before import; it may not fall back to v6 or silently reshape Metal slots. */
 layout->buffers->entries[1]->layoutStride=40;error=0;
 assert(![device newRenderPipelineStateWithDescriptor:d error:&error]&&error&&renderImports==before);
 layout->buffers->entries[1]->layoutStride=48;
 /* V7 is independently capability-gated; a qualified request still refuses
  * without the V7 bit even while the old compositor capability is enabled. */
 error=0;assert(![device newRenderPipelineStateWithDescriptor:d error:&error]&&error&&renderImports==before);
 device->capabilities|=TEGU_GPU_CAP_COMPOSITOR_V7;
 d->colors->colors[0]->rgbDst=4;error=0;
 assert(![device newRenderPipelineStateWithDescriptor:d error:&error]&&error&&renderImports==before);
 d->colors->colors[0]->rgbDst=5;error=0;
 TeguRenderPipeline *p=[device newRenderPipelineStateWithDescriptor:d error:&error];
 assert(p&&!error&&p->compositorSampleClass==8&&p->vertexCode.size==1152&&p->fragmentCode.size==25600);
 assert(tegu_render_v7_compiled_state_supported(p));
 p->fragmentCode.fau_words++;assert(!tegu_render_v7_compiled_state_supported(p));p->fragmentCode.fau_words--;
 p->vertexCode.preload^=1;assert(!tegu_render_v7_compiled_state_supported(p));p->vertexCode.preload^=1;
 p->fragmentCode.tls++;assert(!tegu_render_v7_compiled_state_supported(p));p->fragmentCode.tls--;
 p->push.fragment[75]=1;assert(!tegu_render_v7_compiled_state_supported(p));p->push.fragment[75]=0;
 assert(tegu_render_v7_compiled_state_supported(p));
 assert(renderImports==before+1&&p->packageBytes==TEGU_NATIVE_V7_PACKAGE_BYTES);
 struct tegu_native_v7_info v7;assert(!tegu_native_v7_parse(p->package,p->packageBytes,&v7));
 struct tegu_render_info adapted;
 assert(tegu_render_parse_for_import(p->package,p->packageBytes,1,&adapted)==TEGU_RENDER_IMPORT_UNSUPPORTED);
 assert(tegu_render_parse_for_import_classes(p->package,p->packageBytes,1,0,&adapted)==TEGU_RENDER_IMPORT_UNSUPPORTED);
 assert(!tegu_render_parse_for_import_classes(p->package,p->packageBytes,1,1,&adapted)&&adapted.version==7);
 assert(v7.sample_count==16&&v7.ubo_window[2]==32&&v7.ubo_declared_bytes[2]==48&&
        v7.ubo_window[3]==2&&v7.ubo_declared_bytes[3]==32);
 assert(v7.attachment_count==2&&v7.blend.enabled&&v7.blend.rgb_src==1&&v7.blend.rgb_dst==5&&
        v7.blend.alpha_src==1&&v7.blend.alpha_dst==5);
 compositorExpectedVertexSlot=1;compositorExpectedVertexLength=144;compositorExpectedFsUboWindow=32;
 test_compositor_draw_snapshot(device,path,profile,p);
 /* Exercise the package boundary independently of AIR translation: this is
  * the late gate reached on-device after a successful disabled-blend compile. */
 free(p->package);p->package=0;p->packageBytes=0;
 p->blend=(TeguAIRRenderBlend){1,0,0,1,0,0,1,0,15};
 assert(!tegu_render_v7_make_package(p,&p->compositorResources)&&p->package&&p->packageBytes==TEGU_NATIVE_V7_PACKAGE_BYTES);
 struct tegu_native_v7_info disabledPackageInfo;
 assert(!tegu_native_v7_parse(p->package,p->packageBytes,&disabledPackageInfo)&&
        tegu_native_v7_blend_class(&disabledPackageInfo.blend)==TEGU_NATIVE_V7_BLEND_DISABLED);
 p->blend.rgb_dst=4;assert(tegu_render_v7_make_package(p,&p->compositorResources)==-1);p->blend.rgb_dst=0;
 /* R44 captures the same 8-tap shaders with target0 blending disabled. Keep
  * that raw descriptor class in v7 metadata and permit package import; the
  * submit adapter and kernel restrict it to clear/store passes. */
 d->colors->colors[0]->blend=0;d->colors->colors[0]->rgbDst=0;d->colors->colors[0]->alphaDst=0;
 error=0;TeguRenderPipeline *disabled=[device newRenderPipelineStateWithDescriptor:d error:&error];
 assert(disabled&&!error&&renderImports==before+2&&tegu_native_v7_blend_class(&disabled->blend)==TEGU_NATIVE_V7_BLEND_DISABLED);
 struct tegu_native_v7_info disabledInfo;assert(!tegu_native_v7_parse(disabled->package,disabled->packageBytes,&disabledInfo));
 assert(tegu_native_v7_blend_class(&disabledInfo.blend)==TEGU_NATIVE_V7_BLEND_DISABLED&&!disabledInfo.blend.enabled&&disabledInfo.blend.rgb_op==0&&disabledInfo.blend.rgb_dst==0);
 [disabled release];d->colors->colors[0]->blend=1;d->colors->colors[0]->rgbDst=5;d->colors->colors[0]->alphaDst=5;
 [p release];[d release];[lib release];compositorV7PsoMode=0;compositorExpectedFsUboWindow=72;device->capabilities=old;
}

int main(int argc,char **argv) {
 test_ws_selector_compat_response();
 test_drawable_presentation();
 assert(argc>=2&&argc<=8);
#ifdef TEGU_RENDER_TRACE
 /* Exercise the bounded raw rejection record independently of a particular
  * frontend failure so its complete ABI snapshots remain regression-tested. */
 struct tegu_ui2_info traceInfo={0};struct tegu_ui2_bind_request traceRequest={0};
 struct tegu_ui2_binding traceBinding={0};int traceResult=tegu_ui2_bind(&traceInfo,&traceRequest,&traceBinding);
 assert(traceResult==TEGU_UI2_ECONTRACT);tegu_render_trace_ui2_bind(traceResult,&traceInfo,&traceRequest);
#endif
 int compositorMode=argc==5&&!strcmp(argv[2],"--compositor-snapshot");
 TeguComputeDevice *device=[TeguComputeDevice alloc];id error=0;
 device->method=render_method;device->scalar=render_scalar;device->map=map;device->unmap=unmap;
 test_heap_frontend(device);
 if(argc==4&&!strcmp(argv[2],"--attachment-clear-pso")) {
  test_attachment_clear_frontend(device,argv[3]);[device release];drain();
  assert(renderImports==1&&renderReleases==1);
  puts("PASS exact attachment-clear AIR/Mali/TAC1 pipeline, 776x24 two-target encoder snapshot and selector18 transport; no GPU execution claim");return 0;
 }
 if(argc==4&&!strcmp(argv[2],"--pipeline-pack")) {
  test_pipeline_cache_frontend(device,argv[3]);[device release];drain();return 0;
 }
 if(argc==5&&!strcmp(argv[2],"--compositor-pso")) {
  compositorPsoMode=1;
  test_compositor_native_frontend(device,argv[3]);
  test_compositor_8tap_native_frontend(device,argv[3],argv[4]);
  test_home_affordance_observer(device,argv[3]);
  test_fixed_ui_native_frontend(device,argv[3]);
  test_fixed_ui_texture_native_frontend(device,argv[3]);
  test_narrow_blur_family_observer(device,argv[3]);
  unsigned cacheImports=renderImports;test_pipeline_cache_frontend(device,argv[3]);
  [device release];drain();
  assert(cacheImports==22&&renderReleases==renderImports);
  puts("PASS actual compositor PSO to selector8 import, encoder snapshot and selector16 transport; no GPU execution claim");return 0;
 }
 test_native_sampler_state(device);
 test_generic_draw_batch_frontend(device);
#ifdef TEGU_TILE_FETCH_DEFAULT
 test_tile_fetch_frontend();
#endif
 if(compositorMode) {
  test_compositor_draw_snapshot(device,argv[3],argv[4],0);
  [device release];drain();return 0;
 }
 FILE *f=fopen(argv[1],"rb");assert(f);
 fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);void *bytes=malloc(size);
 assert(fread(bytes,1,size,f)==(size_t)size);fclose(f);
 TeguLibrary *library=[device newTeguLibraryWithBytes:bytes length:size error:&error];free(bytes);assert(library&&!error);
 test_library_loaders(device,argv[1]);
 if(argc>=3)test_function_descriptors(device,argv[2]);
 RenderDescriptor *desc=[[RenderDescriptor alloc] init];
 desc->vertex=[library newFunctionWithName:[NSString stringWithUTF8String:"test_vertex"]];
 desc->fragment=[library newFunctionWithName:[NSString stringWithUTF8String:"test_fragment"]];
 desc->colors=[[RenderColors alloc] init];desc->colors->colors[0]=[[RenderColor alloc] init];
 desc->colors->colors[0]->format=70;desc->colors->colors[0]->mask=15;
 desc->samples=1;desc->raster=1;desc->amplification=1;
 TestLabel *label=[[TestLabel alloc] init];label->value=17;desc->debugLabel=label;
 U64 initialCapabilities=device->capabilities;device->capabilities=257;
 test_compute_texture_bindings(device);
 TeguRenderPipeline *pipelines[2];
 for(unsigned i=0;i<2;i++) {
  desc->colors->colors[0]->format=i?80:70;
  pipelines[i]=[device newRenderPipelineStateWithDescriptor:desc error:&error];
  TeguRenderPipeline *p=pipelines[i];assert(p&&!error&&[p device]==device);
  assert(p->compiled&&p->colorFormat==(i?80:70));
  struct tegu_render_info parsed;assert(!tegu_render_parse(p->package,p->packageBytes,&parsed));
  assert(parsed.color_format==p->colorFormat&&parsed.stages[0].code_bytes==p->vertexCode.size);
  assert(!memcmp(p->package+parsed.stages[0].code_offset,p->vertexCode.code,p->vertexCode.size));
  assert(!memcmp(p->package+parsed.stages[1].code_offset,p->fragmentCode.code,p->fragmentCode.size));
  assert(!memcmp(p->package+256,p->push.fragment,128));
  assert(p->vertexCode.size==1152&&p->fragmentCode.size==384);
  assert(p->vertexCode.registers==32&&p->fragmentCode.registers==32);
  assert(p->vertexCode.sysval_mask==7&&p->fragmentCode.sysval_mask==8);
  assert(p->push.version==1&&p->push.fragment_span==68);
  assert(p->push.fragment[64]==0x22&&p->push.fragment[65]==0x22);
 }
 assert(renderImports==2&&[device currentAllocatedSize]==32768);
 failImport=1;error=0;
 assert(![device newRenderPipelineStateWithDescriptor:desc error:&error]&&error);
 assert([device currentAllocatedSize]==32768&&renderImports==2);failImport=0;
 device->capabilities=initialCapabilities;desc->colors->colors[0]->format=70;
 test_render_commands(device,pipelines[0]);
 test_fragment_bindings(device,pipelines[0]);
 test_native_load_frontend(device,pipelines[0]);
 test_iosurface_texture_frontend(device,pipelines[0]);
 test_texture_blits(device);
 test_mipmaps(device);
 test_ui_private_texture_allocations(device);
 label->value=99;
 assert(((TestLabel *)[pipelines[0] label])->value==17);
 #define REFUSE() do {error=0;assert(![device newRenderPipelineStateWithDescriptor:desc error:&error]&&error);}while(0)
 desc->samples=4;REFUSE();desc->samples=1;
 desc->depth=252;REFUSE();desc->depth=0;
 desc->stencil=253;REFUSE();desc->stencil=0;
 desc->coverage=1;REFUSE();desc->coverage=0;
 desc->alpha=1;REFUSE();desc->alpha=0;
 desc->raster=0;REFUSE();desc->raster=1;
 desc->topology=1;REFUSE();desc->topology=0;
 desc->amplification=2;REFUSE();desc->amplification=1;
 desc->indirect=1;REFUSE();desc->indirect=0;
 desc->colors->colors[0]->blend=1;REFUSE();desc->colors->colors[0]->blend=0;
 desc->colors->colors[0]->mask=7;REFUSE();desc->colors->colors[0]->mask=15;
 desc->colors->colors[0]->format=0;REFUSE();desc->colors->colors[0]->format=70;
 desc->colors->colors[1]=[[RenderColor alloc] init];desc->colors->colors[1]->format=70;REFUSE();
 desc->colors->colors[1]->format=0;
 TeguFunction *saved=desc->vertex;
 id foreign=[[NSObject alloc] init];desc->vertex=foreign;REFUSE();desc->vertex=saved;[foreign release];
desc->vertex=desc->fragment;REFUSE();desc->vertex=saved;
 /* Force a real translator failure after pipeline/label allocation. */
 id savedName=desc->vertex->functionName;
 desc->vertex->functionName=desc->fragment->functionName;error=0;
 assert(![device newRenderPipelineStateWithDescriptor:desc error:&error]&&error);
 assert([[error localizedDescription] isEqualToString:[NSString stringWithUTF8String:"invalid descriptor: StageMismatch { expected: Vertex, found: Fragment }"]]);
 desc->vertex->functionName=savedName;
 /* Real iOS descriptors can return an allocated but empty layout. Both nil
  * entries and materialized invalid attributes must behave like no layout. */
 RenderLayout *layout=[[RenderLayout alloc] init];desc->layout=layout;
 layout->entries[30]=[[RenderAttribute alloc] init];
 TeguRenderPipeline *empty=[device newRenderPipelineStateWithDescriptor:desc error:&error];
 assert(empty&&!error);[empty release];
 layout->entries[30]->value=30;REFUSE();
 // An unused attribute with a complete, valid descriptor has no fetch. Carry
 // its fields through the real compiler; malformed layouts must still fail.
 layout->entries[30]->bufferSlot=2;layout->entries[30]->byteOffset=16;
 layout->buffers=[[RenderLayout alloc] init];
 RenderAttribute *binding=[[RenderAttribute alloc] init];layout->buffers->entries[2]=binding;
 binding->layoutStride=32;binding->step=1;binding->rate=1;
 TeguRenderPipeline *unused=[device newRenderPipelineStateWithDescriptor:desc error:&error];
 assert(unused&&!error);[unused release];
 binding->layoutStride=0x100000000UL;REFUSE();binding->layoutStride=32;
 binding->step=999;REFUSE();binding->step=1;
 layout->entries[30]->bufferSlot=31;REFUSE();layout->entries[30]->bufferSlot=2;
 layout->entries[30]->byteOffset=0x100000000UL;REFUSE();layout->entries[30]->byteOffset=16;
 layout->entries[30]->value=0;REFUSE(); // Nonzero stride with no set attribute.
 [desc->layout release];desc->layout=0;
 TeguComputeDevice *other=[TeguComputeDevice alloc];
 assert(![other newRenderPipelineStateWithDescriptor:desc error:&error]&&error);[other release];
 assert(![device newRenderPipelineStateWithDescriptor:0 error:0]);
 if(argc>=4)test_vertex_render(device,argv[3]);
 if(argc>=5){test_varying_render(device,argv[4]);test_blend_render(device,argv[4]);}
 if(argc>=6) {
  FILE *textureFile=fopen(argv[5],"rb");assert(textureFile);
  fseek(textureFile,0,SEEK_END);long textureSize=ftell(textureFile);rewind(textureFile);
  assert(textureSize>0);void *textureBytes=malloc((NSUInteger)textureSize);assert(textureBytes);
  assert(fread(textureBytes,1,(size_t)textureSize,textureFile)==(size_t)textureSize);fclose(textureFile);
  tegu_test_native_texture_frontend(device,textureBytes,(NSUInteger)textureSize);free(textureBytes);
 }
 if(argc==7) {
  FILE *bgraFile=fopen(argv[6],"rb");assert(bgraFile);
  fseek(bgraFile,0,SEEK_END);long bgraSize=ftell(bgraFile);rewind(bgraFile);
  assert(bgraSize>0);void *bgraBytes=malloc((NSUInteger)bgraSize);assert(bgraBytes);
  assert(fread(bgraBytes,1,(size_t)bgraSize,bgraFile)==(size_t)bgraSize);fclose(bgraFile);
  test_bgra_native_frontend(device,bgraBytes,(NSUInteger)bgraSize);free(bgraBytes);
 }
 [desc release];[library release];[device release];drain();
 for(unsigned i=0;i<2;i++) {
  assert([pipelines[i] device]&&pipelines[i]->vertexCode.code&&pipelines[i]->fragmentCode.code);
  TeguAIRRenderPush reflected;char message[128];
  assert(!tegu_air_render_pipeline_push(pipelines[i]->compiled,&reflected,message,sizeof(message)));
  assert(!memcmp(&reflected,&pipelines[i]->push,sizeof(reflected)));
  [pipelines[i] release];
 }
 drain();
 assert(live==0&&!imports&&!submissions&&renderImports==(argc>=5?9u:argc==4?5u:4u)+(argc>=6?2u:0u)+(argc==7?5u:0u)&&renderReleases==renderImports);
 puts("PASS render pipeline API: real AIR/Mali compilers, RGBA/BGRA, reflected constants, descriptor refusals, ownership, simulated kernel import/release/draw, render recording and target lifetimes; no GPU pixel claim");
}
