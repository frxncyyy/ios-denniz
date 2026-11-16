static void test_home_affordance_observer(TeguComputeDevice *device,const char *path) {
 TeguAIRRenderTypedPlan exact={0};exact.version=1;exact.count=6;exact.attachment_count=2;
 exact.attachments[0]=(TeguAIRRenderColorAttachment){80,15};exact.attachments[1]=(TeguAIRRenderColorAttachment){80,0};
 exact.uses[0]=(TeguAIRRenderResourceUse){1,1,1,16,33,0,0,0};
 exact.uses[1]=(TeguAIRRenderResourceUse){1,1,2,2,5,0,0,64};
 exact.uses[2]=(TeguAIRRenderResourceUse){1,1,3,2,5,0,0,16};
 exact.uses[3]=(TeguAIRRenderResourceUse){1,2,1,2,5,0,0,12};
 exact.uses[4]=(TeguAIRRenderResourceUse){2,2,3,4,33,0,0,0};
 exact.uses[5]=(TeguAIRRenderResourceUse){2,2,4,4,33,0,0,0};
 assert(tegu_home_affordance_typed_plan_supported(&exact));
 exact.uses[5].index=5;assert(!tegu_home_affordance_typed_plan_supported(&exact));exact.uses[5].index=4;
 exact.uses[3].range_length=16;assert(!tegu_home_affordance_typed_plan_supported(&exact));exact.uses[3].range_length=12;
 exact.attachments[1].write_mask=15;assert(!tegu_home_affordance_typed_plan_supported(&exact));

 FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);rewind(f);assert(n>0);
 void *bytes=malloc((size_t)n);assert(bytes&&fread(bytes,1,(size_t)n,f)==(size_t)n);fclose(f);
 id error=0;TeguLibrary *lib=[device newTeguLibraryWithBytes:bytes length:(NSUInteger)n error:&error];free(bytes);assert(lib&&!error);
 RenderDescriptor *d=[[RenderDescriptor alloc] init];
 d->vertex=[lib newFunctionWithName:[NSString stringWithUTF8String:"std_vert1_lph"]];
 d->fragment=[lib newFunctionWithName:[NSString stringWithUTF8String:"home_affordance_base_lph"]];assert(d->vertex&&d->fragment);
 d->colors=[[RenderColors alloc] init];for(unsigned i=0;i<8;i++)d->colors->colors[i]=[[RenderColor alloc] init];
 d->colors->colors[0]->format=80;d->colors->colors[0]->mask=15;
 d->colors->colors[1]->format=80;d->colors->colors[1]->mask=0;
 d->samples=1;d->raster=1;d->amplification=1;
 RenderLayout *layout=[[RenderLayout alloc] init];d->layout=layout;layout->buffers=[[RenderLayout alloc] init];
 const unsigned formats[4]={31,29,29,27},offsets[4]={0,16,24,32};
 for(unsigned i=0;i<4;i++) {
  layout->entries[i]=[[RenderAttribute alloc] init];layout->entries[i]->value=formats[i];
  layout->entries[i]->byteOffset=offsets[i];layout->entries[i]->bufferSlot=1;
 }
 layout->buffers->entries[1]=[[RenderAttribute alloc] init];layout->buffers->entries[1]->layoutStride=48;
 layout->buffers->entries[1]->step=1;layout->buffers->entries[1]->rate=1;
 U64 oldCaps=device->capabilities;device->capabilities=oldCaps|TEGU_GPU_CAP_COMPOSITOR|256;
 unsigned importsBefore=renderImports;
 TeguRenderPipeline *pipeline=[device newRenderPipelineStateWithDescriptor:d error:&error];
 assert(pipeline&&!error&&pipeline->homeAffordanceObserverOnly&&!pipeline->handle&&!pipeline->package&&
  pipeline->compiled&&pipeline->colorFormat==80&&!pipeline->vertexFetch.version&&!pipeline->vertexFetch.count&&
  renderImports==importsBefore);
 /* Every observed descriptor field is exact; near misses remain refused before
  * any native import or observer object can escape. */
 d->colors->colors[1]->mask=15;error=0;assert(![device newRenderPipelineStateWithDescriptor:d error:&error]&&error&&renderImports==importsBefore);d->colors->colors[1]->mask=0;
 layout->buffers->entries[1]->layoutStride=40;error=0;assert(![device newRenderPipelineStateWithDescriptor:d error:&error]&&error&&renderImports==importsBefore);layout->buffers->entries[1]->layoutStride=48;
 d->colors->colors[0]->blend=1;error=0;assert(![device newRenderPipelineStateWithDescriptor:d error:&error]&&error&&renderImports==importsBefore);d->colors->colors[0]->blend=0;
 layout->entries[2]->byteOffset=28;error=0;assert(![device newRenderPipelineStateWithDescriptor:d error:&error]&&error&&renderImports==importsBefore);layout->entries[2]->byteOffset=24;

 TextureDesc *td=[[TextureDesc alloc] init];td->format=80;td->w=td->h=16;td->uses=4;
 TeguTexture *target=[device newTextureWithDescriptor:(id)td];assert(target);
 RenderPass *pass=[[RenderPass alloc] init];pass->color=[[PassColor alloc] init];pass->color->target=[target retain];pass->color->load=2;pass->color->store=1;
 TeguQueue *queue=[device newCommandQueue];TeguCommand *command=[[queue commandBuffer] retain];
 TeguRenderEncoder *encoder=[[command renderCommandEncoderWithDescriptor:(id)pass] retain];assert(encoder);
 [encoder setRenderPipelineState:pipeline];assert(!command->invalid);
 unsigned char zeros[144]={0};
 TeguBuffer *vertex=[device newBufferWithBytes:zeros length:144 options:0];
 TeguBuffer *v2=[device newBufferWithBytes:zeros length:64 options:0];
 TeguBuffer *v3=[device newBufferWithBytes:zeros length:16 options:0];
 TeguBuffer *f1=[device newBufferWithBytes:zeros length:12 options:0];assert(vertex&&v2&&v3&&f1);
 [encoder setVertexBuffer:vertex offset:0 atIndex:1];[encoder setVertexBuffer:v2 offset:0 atIndex:2];
 [encoder setVertexBuffer:v3 offset:0 atIndex:3];[encoder setFragmentBuffer:f1 offset:0 atIndex:1];
 TextureDesc *sd=[[TextureDesc alloc] init];sd->format=70;sd->w=sd->h=64;sd->uses=1;
 TeguTexture *t3=[device newTextureWithDescriptor:(id)sd],*t4=[device newTextureWithDescriptor:(id)sd];assert(t3&&t4);
 [encoder setFragmentTexture:t3 atIndex:3];[encoder setFragmentTexture:t4 atIndex:4];
 unsigned callsBefore=renderDraws,compositorBefore=compositorDraws,clearBefore=attachmentClearDraws;
 [encoder drawPrimitives:3 vertexStart:0 vertexCount:3];
 assert(!command->invalid&&!command->count);[encoder endEncoding];[command commit];[command waitUntilCompleted];
 assert([command status]==4&&![command error]&&renderDraws==callsBefore&&compositorDraws==compositorBefore&&
  attachmentClearDraws==clearBefore&&renderImports==importsBefore);
 [encoder release];[command release];

 /* A missing reflected texture cannot be mistaken for an observer success. */
 command=[[queue commandBuffer] retain];encoder=[[command renderCommandEncoderWithDescriptor:(id)pass] retain];assert(encoder);
 [encoder setRenderPipelineState:pipeline];[encoder setVertexBuffer:vertex offset:0 atIndex:1];
 [encoder setVertexBuffer:v2 offset:0 atIndex:2];[encoder setVertexBuffer:v3 offset:0 atIndex:3];
 [encoder setFragmentBuffer:f1 offset:0 atIndex:1];[encoder setFragmentTexture:t3 atIndex:3];
 [encoder drawPrimitives:3 vertexStart:0 vertexCount:3];assert(command->invalid&&!command->count);
 [encoder endEncoding];[encoder release];[command release];
 [t3 release];[t4 release];[sd release];[vertex release];[v2 release];[v3 release];[f1 release];
 [queue release];[pass release];[target release];[td release];[pipeline release];[d release];[lib release];
 device->capabilities=oldCaps;drain();
 puts("PASS home-affordance exact metadata and typed-resource observer; zero GPU submissions");
}
