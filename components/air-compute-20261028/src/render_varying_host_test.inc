/* Actual AIR/Mali paired compilation and immutable v3 import, simulated IPC.
 * Pixel qualification is a separate native GPU regression. */
static void test_varying_render(TeguComputeDevice *device,const char *path) {
 FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
 void *bytes=malloc(size);assert(bytes&&fread(bytes,1,size,f)==(size_t)size);fclose(f);
 id error=0;TeguLibrary *library=[device newTeguLibraryWithBytes:bytes length:size error:&error];free(bytes);
 assert(library&&!error);
 RenderDescriptor *desc=[[RenderDescriptor alloc] init];
 desc->vertex=[library newFunctionWithName:[NSString stringWithUTF8String:"varying_vertex"]];
 desc->fragment=[library newFunctionWithName:[NSString stringWithUTF8String:"varying_fragment"]];
 desc->colors=[[RenderColors alloc] init];desc->colors->colors[0]=[[RenderColor alloc] init];
 desc->colors->colors[0]->format=70;desc->colors->colors[0]->mask=15;
 desc->samples=1;desc->raster=1;desc->amplification=1;
 RenderLayout *layout=[[RenderLayout alloc] init];desc->layout=layout;
 for(unsigned i=7;i<=8;i++) {
  layout->entries[i]=[[RenderAttribute alloc] init];layout->entries[i]->value=31;
  layout->entries[i]->bufferSlot=1;layout->entries[i]->byteOffset=(i-7)*16;
 }
 layout->buffers=[[RenderLayout alloc] init];layout->buffers->entries[1]=[[RenderAttribute alloc] init];
 layout->buffers->entries[1]->layoutStride=32;layout->buffers->entries[1]->step=1;layout->buffers->entries[1]->rate=1;
 NSUInteger original=[device currentAllocatedSize];unsigned before=renderImports;
 U64 savedCapabilities=device->capabilities;
 // Neither partial capability suffices. Refusal must precede kernel import.
 const U64 partial[]={0,16,32};
 for(unsigned i=0;i<3;i++) {
  device->capabilities=partial[i];
  assert(![device newRenderPipelineStateWithDescriptor:desc error:&error]&&error);
  assert([device currentAllocatedSize]==original&&renderImports==before);
 }
 device->capabilities=48;
 TeguRenderPipeline *pipeline=[device newRenderPipelineStateWithDescriptor:desc error:&error];assert(pipeline&&!error);
 struct tegu_render_info parsed;assert(!tegu_render_parse(pipeline->package,pipeline->packageBytes,&parsed));
 assert(parsed.version==3&&parsed.vertex_mask==3&&parsed.vertex_count==2);
 assert(pipeline->pair.version==2&&pipeline->pair.flags==1&&pipeline->pair.generic_size_B==16);
 assert(parsed.position_offset==384&&parsed.secondary_offset==768&&parsed.varying_bytes==16);
 assert(parsed.secondary_preload==pipeline->pair.secondary_preload&&parsed.secondary_registers==32);
 assert(parsed.stages[0].code_bytes==1024&&parsed.stages[1].code_bytes==1664);
 assert(pipeline->push.fragment_span==68&&pipeline->fragmentCode.push_bytes==68);
 assert(tegu_render_word(pipeline->package+320)==0x2222);
 for(unsigned i=0;i<2;i++) {
  const struct tegu_vertex_attribute *a=&parsed.vertex[i];
  assert(a->location==i&&a->buffer_index==1&&a->format==109&&a->offset==i*16&&a->stride==32);
  const struct tegu_mali_graphics *code=i?&pipeline->fragmentCode:&pipeline->vertexCode;
  assert(!memcmp(pipeline->package+parsed.stages[i].code_offset,code->code,code->size));
 }
 // Pipeline and package own their bindings, shaders and reflected push bytes.
 layout->entries[8]->byteOffset=0;layout->buffers->entries[1]->layoutStride=16;
 [desc release];[library release];drain();
 assert(pipeline->vertexFetch.attributes[1].offset==16&&pipeline->vertexFetch.attributes[1].stride==32);
 TeguAIRRenderPush push;char message[128];
 assert(!tegu_air_render_pipeline_push(pipeline->compiled,&push,message,sizeof(message)));
 assert(!memcmp(&push,&pipeline->push,sizeof(push)));
 TextureDesc *td=[[TextureDesc alloc] init];td->w=td->h=16;td->format=70;td->uses=4;
 TeguTexture *target=[device newTextureWithDescriptor:(id)td];assert(target);
 RenderPass *pass=[[RenderPass alloc] init];pass->color=[[PassColor alloc] init];
 pass->color->target=[target retain];pass->color->load=2;pass->color->store=1;
 TeguQueue *q=[device newCommandQueue];unsigned draws=vertexDraws;
 TeguCommand *command=[[q commandBuffer] retain];
 TeguRenderEncoder *encoder=[command renderCommandEncoderWithDescriptor:(id)pass];
 float vertices[24]={-.75f,-.75f,.5f,1, 1,0,0,.6f, .75f,-.75f,.5f,1, 0,1,0,.6f, -.75f,.75f,.5f,1, 0,0,1,.6f};
 [encoder setRenderPipelineState:pipeline];[encoder setVertexBytes:vertices length:sizeof(vertices) atIndex:1];
 [encoder drawPrimitives:3 vertexStart:0 vertexCount:3];[encoder endEncoding];
 U64 copied=TEGU_JOB(command->jobChunks,0).vertexRequest.buffers[1].handle;
 assert(TEGU_JOB(command->jobChunks,0).vertexRequest.buffer_mask==2&&TEGU_JOB(command->jobChunks,0).vertexRequest.buffers[1].length==96);
 memset(vertices,0,sizeof(vertices));assert(((float *)memory[copied])[23]==.6f);
 [pipeline release];[command commit];assert([command status]==4&&vertexDraws==draws+1);
 [command release];[q release];[pass release];[target release];[td release];drain();
 assert([device currentAllocatedSize]==original&&renderImports==before+1&&!memory[copied]);
 device->capabilities=savedCapabilities;
 puts("PASS real varying AIR/Mali pipeline: paired v3 package, capability gates, selected position/secondary programs, reflected push, immutable bindings, copied vertex bytes and retained lifetime; simulated IPC only");
}
