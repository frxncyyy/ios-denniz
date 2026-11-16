/* Real AIR/Mali + simulated IPC. Tests descriptor snapshots and capability
 * gates. The native device probe separately checks resulting GPU pixels. */
static void test_blend_render(TeguComputeDevice *device,const char *path) {
 _Static_assert(sizeof(TeguAIRRenderBlend)==36,"blend wire ABI");
 FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
 void *bytes=malloc(size);assert(bytes&&fread(bytes,1,size,f)==(size_t)size);fclose(f);
 id error=0;TeguLibrary *library=[device newTeguLibraryWithBytes:bytes length:size error:&error];free(bytes);assert(library&&!error);
 RenderDescriptor *desc=[[RenderDescriptor alloc] init];
 desc->vertex=[library newFunctionWithName:[NSString stringWithUTF8String:"varying_vertex"]];
 desc->fragment=[library newFunctionWithName:[NSString stringWithUTF8String:"varying_fragment"]];
 desc->colors=[[RenderColors alloc] init];RenderColor *color=[[RenderColor alloc] init];desc->colors->colors[0]=color;
 color->format=70;color->mask=15;color->blend=1;color->rgbSrc=color->alphaSrc=4;color->rgbDst=color->alphaDst=5;
 desc->samples=1;desc->raster=1;desc->amplification=1;
 RenderLayout *layout=[[RenderLayout alloc] init];desc->layout=layout;
 for(unsigned i=7;i<=8;i++) {
  layout->entries[i]=[[RenderAttribute alloc] init];layout->entries[i]->value=31;
  layout->entries[i]->bufferSlot=1;layout->entries[i]->byteOffset=(i-7)*16;
 }
 layout->buffers=[[RenderLayout alloc] init];layout->buffers->entries[1]=[[RenderAttribute alloc] init];
 layout->buffers->entries[1]->layoutStride=32;layout->buffers->entries[1]->step=1;layout->buffers->entries[1]->rate=1;
 NSUInteger baseline=[device currentAllocatedSize];unsigned importsBefore=renderImports;
 U64 saved=device->capabilities;
 const U64 missing[]={48,64,80};
 for(unsigned i=0;i<3;i++) {
  device->capabilities=missing[i];
  assert(![device newRenderPipelineStateWithDescriptor:desc error:&error]&&error);
  assert(renderImports==importsBefore&&[device currentAllocatedSize]==baseline);
 }
 device->capabilities=112;
 TeguRenderPipeline *pipelines[3];
 for(unsigned i=0;i<3;i++) {
  color->rgbSrc=i==1?1:4;color->alphaSrc=i==0?4:1;
  pipelines[i]=[device newRenderPipelineStateWithDescriptor:desc error:&error];assert(pipelines[i]&&!error);
  TeguRenderPipeline *p=pipelines[i];struct tegu_render_info parsed;
  assert(p->blend.version==1&&p->blend.enabled==1&&p->blend.rgb_src==color->rgbSrc&&p->blend.alpha_src==color->alphaSrc);
  assert(!tegu_render_parse(p->package,p->packageBytes,&parsed)&&parsed.version==4);
  assert(!memcmp(&parsed.blend,&p->blend,sizeof(p->blend))&&parsed.push_offset==192);
  assert(!p->push.fragment_span&&!p->fragmentCode.push_bytes);
  assert(parsed.stages[0].code_offset==1344&&parsed.vertex_count==2&&parsed.vertex[1].offset==16);
  assert(!memcmp(p->package+parsed.stages[1].code_offset,p->fragmentCode.code,p->fragmentCode.size));
  char message[256];assert(!tegu_air_render_pipeline_native_check_with_blend(p->compiled,70,&p->blend,message,sizeof(message)));
  assert(tegu_air_render_pipeline_native_check_with_vertex_fetch(p->compiled,70,message,sizeof(message)));
 }
 // Rejected Metal states must not allocate/import a pipeline.
 for(unsigned scenario=0;scenario<7;scenario++) {
  color->rgbSrc=4;color->alphaSrc=1;
  switch(scenario) {
   case 0:color->rgbOp=3;break;case 1:color->rgbSrc=10;break;
   case 2:color->alphaSrc=11;break;case 3:color->rgbSrc=0x100000004UL;break;
   case 4:color->mask=7;break;case 5:color->format=81;break;
   case 6:color->rgbSrc=2;color->rgbDst=8;break;
  }
  assert(![device newRenderPipelineStateWithDescriptor:desc error:&error]&&error);
  assert(renderImports==importsBefore+3);
  color->rgbOp=0;color->rgbDst=5;color->mask=15;color->format=70;
 }
 // Caller descriptors/functions/libraries can disappear before submission.
 [desc release];[library release];drain();
 TextureDesc *td=[[TextureDesc alloc] init];td->w=td->h=16;td->format=70;td->uses=4;
 TeguTexture *target=[device newTextureWithDescriptor:(id)td];assert(target);
 RenderPass *pass=[[RenderPass alloc] init];pass->color=[[PassColor alloc] init];
 pass->color->target=[target retain];pass->color->load=2;pass->color->store=1;
 TeguQueue *queue=[device newCommandQueue];unsigned draws=vertexDraws;
 float vertex[24]={-.75f,-.75f,.5f,1,.2f,.4f,.8f,.6f,.75f,-.75f,.5f,1,.2f,.4f,.8f,.6f,-.75f,.75f,.5f,1,.2f,.4f,.8f,.6f};
 for(unsigned i=0;i<3;i++) {
  TeguCommand *command=[[queue commandBuffer] retain];
  TeguRenderEncoder *encoder=[command renderCommandEncoderWithDescriptor:(id)pass];
  [encoder setRenderPipelineState:pipelines[i]];[encoder setVertexBytes:vertex length:sizeof(vertex) atIndex:1];
  [encoder drawPrimitives:3 vertexStart:0 vertexCount:3];[encoder endEncoding];
  U64 handle=TEGU_JOB(command->jobChunks,0).vertexRequest.buffers[1].handle;[pipelines[i] release];
  [command commit];assert([command status]==4&&vertexDraws==draws+i+1);[command release];drain();assert(!memory[handle]);
 }
 [queue release];[pass release];[target release];[td release];drain();
 assert([device currentAllocatedSize]==baseline&&renderImports==importsBefore+3);
 device->capabilities=saved;
 puts("PASS real blended AIR/Mali pipelines:3v4packages, exact blend metadata, capability/enum gates, immutable descriptors and retained draw lifetimes; simulated IPC only");
}
