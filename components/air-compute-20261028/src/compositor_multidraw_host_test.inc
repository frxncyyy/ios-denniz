static void compositor_multidraw_bind(TeguRenderEncoder *encoder,TeguRenderPipeline *pipeline,
 TeguBuffer *vertex,TeguBuffer *ubo2,TeguBuffer *ubo3,TeguBuffer *fs1,TeguBuffer *fs6,
 TeguTexture *texture,TeguSampler *sampler,unsigned slot) {
 [encoder setRenderPipelineState:pipeline];
 [encoder setVertexBuffer:vertex offset:0 atIndex:slot];
 [encoder setVertexBuffer:ubo2 offset:16 atIndex:2];
 [encoder setVertexBuffer:ubo3 offset:16 atIndex:3];
 [encoder setFragmentBuffer:fs1 offset:24 atIndex:1];
 [encoder setFragmentBuffer:fs6 offset:30 atIndex:6];
 [encoder setFragmentTexture:texture atIndex:3];
 [encoder setFragmentSamplerState:sampler atIndex:0];
}
static void test_compositor_multidraw_actual(TeguComputeDevice *device,TeguRenderPipeline *p1,
 TeguRenderPipeline *p2,int failSecond,int initialLoad,unsigned requestedCount) {
 assert(p1&&p2&&p1!=p2&&p1->handle&&p2->handle&&p1->blend.enabled&&p2->blend.enabled);
 NSUInteger baseline=[device currentAllocatedSize];
 TextureDesc *td=[[TextureDesc alloc] init];td->format=80;td->w=td->h=16;td->uses=4;
 TeguTexture *target=[device newTextureWithDescriptor:(id)td];assert(target);
 RenderPass *pass=[[RenderPass alloc] init];pass->color=[[PassColor alloc] init];
 pass->color->target=[target retain];pass->color->load=initialLoad?1:2;pass->color->store=1;
 device->capabilities=(device->capabilities&~(unsigned)TEGU_GPU_CAP_LOAD_PASS)|TEGU_GPU_CAP_COMPOSITOR|TEGU_GPU_CAP_COMPOSITOR_LOAD;
 TeguQueue *queue=[device newCommandQueue];assert(queue);
 TeguCommand *command=[[queue commandBuffer] retain];
 TeguRenderEncoder *encoder=[[command renderCommandEncoderWithDescriptor:(id)pass] retain];assert(encoder);
 SamplerTestDescriptor *sd=[[SamplerTestDescriptor alloc] init];sd->normalized=1;sd->anisotropy=1;
 TeguSampler *sampler=[device newSamplerStateWithDescriptor:(id)sd];assert(sampler);
 unsigned slot=p1->vertexFetch.attributes[0].buffer_index;
 assert(slot==p2->vertexFetch.attributes[0].buffer_index&&p1->vertexFetch.attributes[0].stride==32&&p2->vertexFetch.attributes[0].stride==40);
 U64 vh[16]={0},th[16]={0},u2h[16]={0};
 unsigned count=requestedCount?requestedCount:(initialLoad?1:(failSecond?3:2));
 for(unsigned i=0;i<count;i++) {
  TeguRenderPipeline *pipeline=i==1?p2:p1;
  unsigned stride=pipeline->vertexFetch.attributes[0].stride;
  TeguBuffer *vertex=[device newBufferWithLength:3u*stride options:0];assert(vertex);vh[i]=vertex->handle;
  TeguBuffer *ubo2=[device newBufferWithLength:80 options:0];assert(ubo2);u2h[i]=ubo2->handle;
  TeguBuffer *ubo3=[device newBufferWithLength:32 options:0];assert(ubo3);
  TeguBuffer *fs1=[device newBufferWithLength:96 options:0];assert(fs1);
  TeguBuffer *fs6=[device newBufferWithLength:32 options:0];assert(fs6);
  TextureDesc *sampleDesc=[[TextureDesc alloc] init];sampleDesc->format=70;sampleDesc->w=sampleDesc->h=16;sampleDesc->uses=1;
  TeguTexture *sample=[device newTextureWithDescriptor:(id)sampleDesc];assert(sample);th[i]=sample->buffer->handle;
  compositor_multidraw_bind(encoder,pipeline,vertex,ubo2,ubo3,fs1,fs6,sample,sampler,slot);
  [encoder drawPrimitives:3 vertexStart:0 vertexCount:3];
  [vertex release];[ubo2 release];[ubo3 release];[fs1 release];[fs6 release];[sample release];[sampleDesc release];
  assert(!command->invalid&&command->count==i+1);
  assert(TEGU_JOB(command->jobChunks,i).compositorLoadAction==((i||initialLoad)?TEGU_COMPOSITOR_LOAD_LOAD:TEGU_COMPOSITOR_LOAD_CLEAR));
  assert(TEGU_JOB(command->jobChunks,i).renderPipeline==pipeline);
 }
 [encoder endEncoding];[encoder release];[sampler release];[sd release];[pass release];[target release];drain();
 assert(!command->invalid&&command->count==count);
 for(unsigned i=0;i<count;i++)assert(memory[vh[i]]&&memory[th[i]]&&memory[u2h[i]]);
 device->capabilities|=TEGU_GPU_CAP_COMPOSITOR|TEGU_GPU_CAP_COMPOSITOR_LOAD;
 compositorExpectedPipelineHandle=p1->handle;compositorExpectedPipelineSecondHandle=p2->handle;
 compositorExpectedVertexSlot=slot;compositorExpectedVertexLength=3u*32;
 compositorExpectedVertexLength2=3u*40;compositorExpectedVertexFetch2Handle=vh[1];
 compositorExpectedVertex2Handle=u2h[1];compositorExpectedTexture2Handle=th[1];
 compositorMultiDrawStart=compositorDraws;compositorMultiDrawMode=1;compositorMultiDrawFail=(unsigned)failSecond;compositorMultiDrawInitialLoad=(unsigned)initialLoad;
 [command commit];[command waitUntilCompleted];
 compositorMultiDrawMode=0;compositorMultiDrawFail=0;compositorMultiDrawInitialLoad=0;
 assert([command status]==(failSecond?5:4));
 assert(compositorDraws==compositorMultiDrawStart+(failSecond?2:count)); /* failed draw 2; draw 3 is skipped */
 [command release];drain();
 for(unsigned i=0;i<count;i++)assert(!memory[vh[i]]&&!memory[th[i]]&&!memory[u2h[i]]);
 [queue release];[td release];drain();
 assert([device currentAllocatedSize]==baseline);
 puts(failSecond?"PASS actual source-over PSO multidraw: CLEAR v1, LOAD v2, second-submit error suppresses third; ASan host transport":(initialLoad?"PASS descriptor-first compositor LOAD with compositor-only gate; v2 LOAD request, no clear; ASan host transport":"PASS actual source-over PSO multidraw: distinct retained PSOs/UBOs/textures, CLEAR v1 then LOAD v2; ASan host transport"));
}
static void test_compositor_legacy_mix_refusal(TeguComputeDevice *device,TeguRenderPipeline *compositor) {
 NSUInteger baseline=[device currentAllocatedSize];unsigned drawsBefore=compositorDraws;
 TextureDesc *td=[[TextureDesc alloc] init];td->format=80;td->w=td->h=16;td->uses=4;
 TeguTexture *target=[device newTextureWithDescriptor:(id)td];assert(target);
 RenderPass *pass=[[RenderPass alloc] init];pass->color=[[PassColor alloc] init];
 pass->color->target=[target retain];pass->color->load=2;pass->color->store=1;
 device->capabilities=(device->capabilities&~(unsigned)TEGU_GPU_CAP_LOAD_PASS)|TEGU_GPU_CAP_COMPOSITOR|TEGU_GPU_CAP_COMPOSITOR_LOAD;
 TeguQueue *queue=[device newCommandQueue];TeguCommand *command=[[queue commandBuffer] retain];
 TeguRenderEncoder *encoder=[[command renderCommandEncoderWithDescriptor:(id)pass] retain];assert(encoder);
 SamplerTestDescriptor *sd=[[SamplerTestDescriptor alloc] init];sd->normalized=1;sd->anisotropy=1;
 TeguSampler *sampler=[device newSamplerStateWithDescriptor:(id)sd];assert(sampler);
 unsigned slot=compositor->vertexFetch.attributes[0].buffer_index,stride=compositor->vertexFetch.attributes[0].stride;
 TeguBuffer *v=[device newBufferWithLength:3u*stride options:0],*u2=[device newBufferWithLength:80 options:0];
 TeguBuffer *u3=[device newBufferWithLength:32 options:0],*fs1=[device newBufferWithLength:96 options:0],*fs6=[device newBufferWithLength:32 options:0];
 TextureDesc *sampleDesc=[[TextureDesc alloc] init];sampleDesc->format=70;sampleDesc->w=sampleDesc->h=16;sampleDesc->uses=1;
 TeguTexture *sample=[device newTextureWithDescriptor:(id)sampleDesc];assert(v&&u2&&u3&&fs1&&fs6&&sample);
 compositor_multidraw_bind(encoder,compositor,v,u2,u3,fs1,fs6,sample,sampler,slot);
 [encoder drawPrimitives:3 vertexStart:0 vertexCount:3];assert(!command->invalid&&command->count==1);
 TeguRenderPipeline *legacy=[[TeguRenderPipeline alloc] init];assert(legacy);
 legacy->owner=[device retain];legacy->colorFormat=80;
 [encoder setRenderPipelineState:legacy];
 [encoder drawPrimitives:3 vertexStart:0 vertexCount:3];
 assert(command->invalid&&command->count==1); /* no mixed class job is appended */
 [encoder endEncoding];[encoder release];[legacy release];
 [v release];[u2 release];[u3 release];[fs1 release];[fs6 release];[sample release];[sampleDesc release];
 [sampler release];[sd release];[pass release];[target release];
 [command commit];assert([command status]==5&&compositorDraws==drawsBefore);
 [command release];[queue release];[td release];drain();
 assert([device currentAllocatedSize]==baseline);
 puts("PASS mixed compositor/legacy encoder refusal leaves one owned snapshot and cleans it up");
}
