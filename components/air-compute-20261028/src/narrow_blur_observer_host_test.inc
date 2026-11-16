static void test_narrow_blur_family_observer(TeguComputeDevice *device,const char *path) {
 TeguAIRRenderTypedPlan exact={0};exact.version=1;exact.count=7;exact.attachment_count=2;
 exact.attachments[0]=(TeguAIRRenderColorAttachment){80,15};exact.attachments[1]=(TeguAIRRenderColorAttachment){80,0};
 exact.uses[0]=(TeguAIRRenderResourceUse){1,1,1,16,33,0,0,0};
 exact.uses[1]=(TeguAIRRenderResourceUse){1,1,2,2,5,0,0,64};
 exact.uses[2]=(TeguAIRRenderResourceUse){1,1,3,2,5,0,0,16};
 exact.uses[3]=(TeguAIRRenderResourceUse){1,2,1,2,5,0,0,82};
 exact.uses[4]=(TeguAIRRenderResourceUse){1,2,6,2,5,0,0,2};
 exact.uses[5]=(TeguAIRRenderResourceUse){2,2,3,4,33,0,0,0};
 exact.uses[6]=(TeguAIRRenderResourceUse){3,2,0,0,24,0,0,0};
 assert(tegu_narrow_blur_11_typed_plan_supported(&exact));
 exact.uses[4].range_length=4;assert(!tegu_narrow_blur_11_typed_plan_supported(&exact));exact.uses[4].range_length=2;
 exact.uses[6].flags=25;assert(!tegu_narrow_blur_11_typed_plan_supported(&exact));exact.uses[6].flags=24;
 exact.attachments[1].write_mask=15;assert(!tegu_narrow_blur_11_typed_plan_supported(&exact));
 exact.attachments[1].write_mask=0;exact.attachments[0].format=115;exact.attachments[1].format=115;
 assert(tegu_narrow_blur_11_typed_plan_supported(&exact));
 exact.attachments[1].format=80;assert(!tegu_narrow_blur_11_typed_plan_supported(&exact));

 FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);rewind(f);assert(n>0);
 void *bytes=malloc((size_t)n);assert(bytes&&fread(bytes,1,(size_t)n,f)==(size_t)n);fclose(f);
 id error=0;TeguLibrary *lib=[device newTeguLibraryWithBytes:bytes length:(NSUInteger)n error:&error];free(bytes);assert(lib&&!error);
 NSString *knownName=[NSString stringWithUTF8String:"narrow_blur_23_frag_lph"];
 NSString *missingName=[NSString stringWithUTF8String:"tegu_missing_blur_function"];
 for(unsigned i=0;i<64;i++) {
  TeguFunction *known=[lib newFunctionWithName:knownName];
  assert(known);[known release];
  assert(![lib newFunctionWithName:missingName]);
 }
 RenderDescriptor *d=[[RenderDescriptor alloc] init];
 d->vertex=[lib newFunctionWithName:[NSString stringWithUTF8String:"downsample_blur_vert_lph"]];
 d->fragment=[lib newFunctionWithName:[NSString stringWithUTF8String:"narrow_blur_11_frag_lph"]];assert(d->vertex&&d->fragment);
 d->colors=[[RenderColors alloc] init];for(unsigned i=0;i<8;i++){
  d->colors->colors[i]=[[RenderColor alloc] init];d->colors->colors[i]->mask=15;
 }
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
 U64 oldCaps=device->capabilities;device->capabilities=oldCaps|TEGU_GPU_CAP_COMPOSITOR|
  TEGU_GPU_CAP_COMPOSITOR_LOAD|256;
 unsigned importsBefore=renderImports;
 TeguRenderPipeline *pipeline=[device newRenderPipelineStateWithDescriptor:d error:&error];
 assert(pipeline&&!error&&pipeline->homeAffordanceObserverOnly==2&&!pipeline->handle&&!pipeline->package&&
  pipeline->compiled&&pipeline->colorFormat==80&&renderImports==importsBefore);
 char savedDonor=lib->sourceSHA256[0];lib->sourceSHA256[0]=savedDonor=='0'?'1':'0';error=0;
 assert(![device newRenderPipelineStateWithDescriptor:d error:&error]&&error&&renderImports==importsBefore);
 lib->sourceSHA256[0]=savedDonor;
 unsigned char *mutatedProfile=malloc(sizeof(tegu_compositor_profile));assert(mutatedProfile);
 memcpy(mutatedProfile,tegu_compositor_profile,sizeof(tegu_compositor_profile));mutatedProfile[0]^=1;
 assert(tegu_render_catalog_profile_exact(tegu_compositor_profile,sizeof(tegu_compositor_profile))&&
  !tegu_render_catalog_profile_exact(mutatedProfile,sizeof(tegu_compositor_profile)));free(mutatedProfile);
 char staleIdentity[65];memset(staleIdentity,'0',64);staleIdentity[64]=0;char catalogMessage[128]={0};
 assert(tegu_air_render_pipeline_catalog_metadata_check(pipeline->compiled,staleIdentity,64,
  catalogMessage,sizeof(catalogMessage))!=0);
 d->colors->colors[1]->mask=15;error=0;assert(![device newRenderPipelineStateWithDescriptor:d error:&error]&&error&&renderImports==importsBefore);d->colors->colors[1]->mask=0;
 layout->buffers->entries[1]->layoutStride=40;error=0;assert(![device newRenderPipelineStateWithDescriptor:d error:&error]&&error&&renderImports==importsBefore);layout->buffers->entries[1]->layoutStride=48;
 d->colors->colors[0]->blend=1;error=0;assert(![device newRenderPipelineStateWithDescriptor:d error:&error]&&error&&renderImports==importsBefore);d->colors->colors[0]->blend=0;

 /* All six admitted functions translate to distinct fragment programs and
  * the same seven-resource plan. Radius 7 has its own byte-exact precision
  * metadata contract; the other five share one byte-exact contract. */
 const char *const family[]={"narrow_blur_15_frag_lph","narrow_blur_19_frag_lph",
  "narrow_blur_23_frag_lph","narrow_blur_27_frag_lph"};
 for(unsigned i=0;i<sizeof(family)/sizeof(family[0]);i++) {
  TeguFunction *candidate=[lib newFunctionWithName:[NSString stringWithUTF8String:family[i]]];assert(candidate);
  [d->fragment release];d->fragment=candidate;error=0;
  TeguRenderPipeline *member=[device newRenderPipelineStateWithDescriptor:d error:&error];
  assert(member&&!error&&member->homeAffordanceObserverOnly==2&&!member->handle&&!member->package&&
   member->compiled&&member->colorFormat==80&&renderImports==importsBefore);
  [member release];
 }
 TeguFunction *radius7=[lib newFunctionWithName:[NSString stringWithUTF8String:"narrow_blur_7_frag_lph"]];assert(radius7);
 [d->fragment release];d->fragment=radius7;error=0;
 TeguRenderPipeline *seven=[device newRenderPipelineStateWithDescriptor:d error:&error];
 assert(seven&&!error&&seven->homeAffordanceObserverOnly==2&&!seven->handle&&!seven->package&&
  seven->compiled&&seven->colorFormat==80&&renderImports==importsBefore);
 [seven release];
 TeguFunction *single3=[lib newFunctionWithName:[NSString stringWithUTF8String:"single_pass_blur_3_lph"]];assert(single3);
 [d->fragment release];d->fragment=single3;error=0;compositorB3PsoMode=1;
 TeguRenderPipeline *single=[device newRenderPipelineStateWithDescriptor:d error:&error];
 assert(single&&!error&&!single->homeAffordanceObserverOnly&&single->compositorSnapshotOnly&&
  single->compositorB3&&!single->compositorV6&&!single->compositorV7&&single->handle&&single->package&&
  single->packageBytes==TEGU_NATIVE_B3_PACKAGE_BYTES&&single->compiled&&single->colorFormat==80&&
  renderImports==importsBefore+1);
 struct tegu_native_b3_info b3;assert(!tegu_native_b3_parse(single->package,single->packageBytes,&b3));
 assert(b3.ubo_window[2]==82&&b3.sample_count==4&&b3.ubo_declared_bytes[2]==112);
 struct tegu_qc_catalog_descriptor halfCatalog=tegu_qc_catalog_descriptors[1];
 assert(tegu_qc_catalog_find("downsample_blur_vert_lph","narrow_blur_23_frag_lph",&halfCatalog));
 halfCatalog.attachments[1].write_mask=15;
 assert(!tegu_qc_catalog_find("downsample_blur_vert_lph","narrow_blur_23_frag_lph",&halfCatalog));
 compositorExpectedFsUboWindow=82;compositorExpectedPipelineHandle=single->handle;

 TextureDesc *td=[[TextureDesc alloc] init];td->format=80;td->w=776;td->h=24;td->uses=5;td->options=0x20;td->levels=10;
 TeguTexture *target=[device newTextureWithDescriptor:(id)td];
 TeguTexture *secondTarget=[device newTextureWithDescriptor:(id)td];
 TeguTexture *sample=[device newTextureWithDescriptor:(id)td];assert(target&&secondTarget&&sample);
 RenderPass *pass=[[RenderPass alloc] init];pass->color=[[PassColor alloc] init];pass->color->target=[target retain];pass->color->load=0;pass->color->store=1;
 RenderPass *b3Pass=[[RenderPass alloc] init];b3Pass->color=[[PassColor alloc] init];b3Pass->second=[[PassColor alloc] init];
 b3Pass->color->target=[target retain];b3Pass->color->load=0;b3Pass->color->store=1;
 b3Pass->second->target=[secondTarget retain];b3Pass->second->load=0;b3Pass->second->store=0;
 TeguQueue *queue=[device newCommandQueue];TeguCommand *command=[[queue commandBuffer] retain];
 TeguRenderEncoder *encoder=[[command renderCommandEncoderWithDescriptor:(id)pass] retain];assert(encoder&&encoder->observerPassOnly);
 [encoder setRenderPipelineState:pipeline];assert(!command->invalid);
 unsigned char zeros[144]={0};
 TeguBuffer *vertex=[device newBufferWithBytes:zeros length:144 options:0];
 TeguBuffer *v2=[device newBufferWithBytes:zeros length:64 options:0];
 TeguBuffer *v3=[device newBufferWithBytes:zeros length:16 options:0];
 TeguBuffer *f1=[device newBufferWithBytes:zeros length:82 options:0];
 TeguBuffer *f6=[device newBufferWithBytes:zeros length:2 options:0];assert(vertex&&v2&&v3&&f1&&f6);
 [encoder setVertexBuffer:vertex offset:0 atIndex:1];[encoder setVertexBuffer:v2 offset:0 atIndex:2];
 [encoder setVertexBuffer:v3 offset:0 atIndex:3];[encoder setFragmentBuffer:f1 offset:0 atIndex:1];
 [encoder setFragmentBuffer:f6 offset:0 atIndex:6];[encoder setFragmentTexture:sample atIndex:3];
 SamplerTestDescriptor *sd=[[SamplerTestDescriptor alloc] init];sd->normalized=1;sd->anisotropy=1;
 TeguSampler *sampler=[device newSamplerStateWithDescriptor:(id)sd];assert(sampler);[encoder setFragmentSamplerState:sampler atIndex:0];
 unsigned callsBefore=renderDraws,compositorBefore=compositorDraws,clearBefore=attachmentClearDraws;
 [encoder drawPrimitives:3 vertexStart:0 vertexCount:3];
 assert(!command->invalid&&!command->count);[encoder endEncoding];[command commit];[command waitUntilCompleted];
 assert([command status]==4&&![command error]&&renderDraws==callsBefore&&compositorDraws==compositorBefore&&
  attachmentClearDraws==clearBefore&&renderImports==importsBefore+1);
 [encoder release];[command release];

 /* The captured half-float PSO follows the native B3 PSO. It consumes exact
  * typed metadata for all three load actions and never queues a kernel job. */
 TeguFunction *halfFragment=[lib newFunctionWithName:[NSString stringWithUTF8String:"narrow_blur_23_frag_lph"]];assert(halfFragment);
 [d->fragment release];d->fragment=halfFragment;d->colors->colors[0]->format=115;d->colors->colors[1]->format=115;error=0;
 TeguRenderPipeline *half=[device newRenderPipelineStateWithDescriptor:d error:&error];
 assert(half&&!error&&half->homeAffordanceObserverOnly==2&&!half->handle&&!half->package&&
  half->compiled&&half->colorFormat==115&&renderImports==importsBefore+1);
 TextureDesc *halfTd=[[TextureDesc alloc] init];halfTd->format=115;halfTd->w=776;halfTd->h=24;
 halfTd->uses=5;halfTd->options=0x20;halfTd->levels=10;
 TeguTexture *halfTarget=[device newTextureWithDescriptor:(id)halfTd];
 assert(halfTarget&&tegu_rgba16float_private_observer_texture(halfTarget,device));
 RenderPass *halfPass=[[RenderPass alloc] init];halfPass->color=[[PassColor alloc] init];
 halfPass->color->target=[halfTarget retain];halfPass->color->store=1;
 for(unsigned halfLoad=0;halfLoad<=2;halfLoad++) {
  halfPass->color->load=halfLoad;command=[[queue commandBuffer] retain];
  encoder=[[command renderCommandEncoderWithDescriptor:(id)halfPass] retain];assert(encoder&&encoder->observerPassOnly);
  [encoder setRenderPipelineState:half];assert(!command->invalid);
  [encoder setVertexBuffer:vertex offset:0 atIndex:1];[encoder setVertexBuffer:v2 offset:0 atIndex:2];
  [encoder setVertexBuffer:v3 offset:0 atIndex:3];[encoder setFragmentBuffer:f1 offset:0 atIndex:1];
  [encoder setFragmentBuffer:f6 offset:0 atIndex:6];[encoder setFragmentTexture:sample atIndex:3];
  [encoder setFragmentSamplerState:sampler atIndex:0];[encoder drawPrimitives:3 vertexStart:0 vertexCount:3];
  assert(!command->invalid&&!command->count);[encoder endEncoding];[command commit];[command waitUntilCompleted];
  assert([command status]==4&&![command error]&&renderDraws==callsBefore&&compositorDraws==compositorBefore&&
   attachmentClearDraws==clearBefore&&renderImports==importsBefore+1);
  [encoder release];[command release];
 }
 halfTarget->mip[1].offset++;halfPass->color->load=0;command=[[queue commandBuffer] retain];
 assert(![command renderCommandEncoderWithDescriptor:(id)halfPass]&&command->invalid);halfTarget->mip[1].offset--;
 [command release];
 [halfPass release];[halfTarget release];[halfTd release];[half release];
 d->colors->colors[0]->format=80;d->colors->colors[1]->format=80;

 /* A near-miss resource is refused by the encoder before it can become a
  * retained compositor job. */
 command=[[queue commandBuffer] retain];encoder=[[command renderCommandEncoderWithDescriptor:(id)pass] retain];
 assert(encoder&&encoder->observerPassOnly);[encoder setRenderPipelineState:single];assert(command->invalid);
 [encoder endEncoding];[encoder release];[command release];
 b3Pass->second->load=1;command=[[queue commandBuffer] retain];
 encoder=[[command renderCommandEncoderWithDescriptor:(id)b3Pass] retain];assert(encoder&&!encoder->observerPassOnly);
 [encoder setRenderPipelineState:single];assert(command->invalid);[encoder endEncoding];[encoder release];[command release];
 b3Pass->second->load=0;
 b3Pass->second->store=1;command=[[queue commandBuffer] retain];
 encoder=[[command renderCommandEncoderWithDescriptor:(id)b3Pass] retain];assert(encoder&&!encoder->observerPassOnly);
 [encoder setRenderPipelineState:single];assert(command->invalid);[encoder endEncoding];[encoder release];[command release];
 b3Pass->second->store=0;secondTarget->mip[1].offset++;command=[[queue commandBuffer] retain];
 encoder=[[command renderCommandEncoderWithDescriptor:(id)b3Pass] retain];assert(encoder&&!encoder->observerPassOnly);
 [encoder setRenderPipelineState:single];assert(command->invalid);[encoder endEncoding];[encoder release];[command release];
 secondTarget->mip[1].offset--;
 command=[[queue commandBuffer] retain];encoder=[[command renderCommandEncoderWithDescriptor:(id)b3Pass] retain];
 assert(encoder&&encoder->observerPassOnly);[encoder setRenderPipelineState:single];assert(!command->invalid);
 [encoder setVertexBuffer:vertex offset:0 atIndex:1];[encoder setVertexBuffer:v2 offset:0 atIndex:2];
 [encoder setVertexBuffer:v3 offset:0 atIndex:3];[encoder setFragmentBuffer:f1 offset:0 atIndex:1];
 [encoder setFragmentBuffer:f6 offset:0 atIndex:6];[encoder setFragmentTexture:sample atIndex:3];
 [encoder setFragmentSamplerState:sampler atIndex:0];sample->mip[1].offset++;
 [encoder drawPrimitives:3 vertexStart:0 vertexCount:3];sample->mip[1].offset--;
 assert(command->invalid&&command->count==0);
 [encoder endEncoding];[encoder release];[command release];

 /* LOD is pinned to zero by the admitted sampler, so the exact ten-mip object
  * is represented by its fully bounded mip0 view. Every mip layout remains
  * checked before the selector16 snapshot is built. */
 for(unsigned capturedLoad=1;capturedLoad<=2;capturedLoad++) {
  b3Pass->color->load=capturedLoad;command=[[queue commandBuffer] retain];
  encoder=[[command renderCommandEncoderWithDescriptor:(id)b3Pass] retain];assert(encoder&&encoder->observerPassOnly);
  [encoder setRenderPipelineState:single];assert(!command->invalid);
  [encoder setVertexBuffer:vertex offset:0 atIndex:1];[encoder setVertexBuffer:v2 offset:0 atIndex:2];
  [encoder setVertexBuffer:v3 offset:0 atIndex:3];[encoder setFragmentBuffer:f1 offset:0 atIndex:1];
  [encoder setFragmentBuffer:f6 offset:0 atIndex:6];[encoder setFragmentTexture:sample atIndex:3];
  [encoder setFragmentSamplerState:sampler atIndex:0];[encoder drawPrimitives:3 vertexStart:0 vertexCount:3];
  assert(!command->invalid&&command->count==1&&TEGU_JOB(command->jobChunks,0).compositorSnapshot&&
   TEGU_JOB(command->jobChunks,0).attachmentClearTargets[1]==secondTarget&&!TEGU_JOB(command->jobChunks,0).attachmentClearSnapshot);
  struct tegu_compositor_request capturedRequest;U64 capturedArgs[4];
  assert(!tegu_compositor_request_for_job(&TEGU_JOB(command->jobChunks,0),device,&capturedRequest,capturedArgs)&&
   capturedRequest.load_action==capturedLoad&&capturedArgs[1]==target->buffer->handle&&
   capturedArgs[1]!=secondTarget->buffer->handle);
  [encoder endEncoding];[encoder release];[command release];
 }
 b3Pass->color->load=0;command=[[queue commandBuffer] retain];encoder=[[command renderCommandEncoderWithDescriptor:(id)b3Pass] retain];
 assert(encoder&&encoder->observerPassOnly);[encoder setRenderPipelineState:single];assert(!command->invalid);
 [encoder setVertexBuffer:vertex offset:0 atIndex:1];[encoder setVertexBuffer:v2 offset:0 atIndex:2];
 [encoder setVertexBuffer:v3 offset:0 atIndex:3];[encoder setFragmentBuffer:f1 offset:0 atIndex:1];
 [encoder setFragmentBuffer:f6 offset:0 atIndex:6];[encoder setFragmentTexture:sample atIndex:3];
 [encoder setFragmentSamplerState:sampler atIndex:0];[encoder drawPrimitives:3 vertexStart:0 vertexCount:3];
 assert(!command->invalid&&command->count==1&&TEGU_JOB(command->jobChunks,0).compositorSnapshot);
 assert(TEGU_JOB(command->jobChunks,0).attachmentClearTargets[1]==secondTarget&&!TEGU_JOB(command->jobChunks,0).attachmentClearSnapshot);
 assert(TEGU_JOB(command->jobChunks,0).compositorTextureFacts[3].format==80&&
  TEGU_JOB(command->jobChunks,0).compositorTextureFacts[3].width==776&&TEGU_JOB(command->jobChunks,0).compositorTextureFacts[3].height==24&&
  TEGU_JOB(command->jobChunks,0).compositorTextureFacts[3].stride==3104&&TEGU_JOB(command->jobChunks,0).compositorTextureFacts[3].span==74496);
 struct tegu_compositor_request exactRequest;U64 exactArgs[4];
 assert(!tegu_compositor_request_for_job(&TEGU_JOB(command->jobChunks,0),device,&exactRequest,exactArgs));
 assert(exactRequest.texture_format==80&&exactRequest.texture_width==776&&exactRequest.texture_height==24&&
  exactRequest.texture_stride==3104&&exactRequest.texture.length==74496&&exactRequest.load_action==2&&
  exactArgs[1]==target->buffer->handle&&exactArgs[1]!=secondTarget->buffer->handle);
 sample->mip[1].offset++;assert(tegu_compositor_request_for_job(&TEGU_JOB(command->jobChunks,0),device,&exactRequest,exactArgs)<0);sample->mip[1].offset--;
 sample->textureUsage=4;assert(tegu_compositor_request_for_job(&TEGU_JOB(command->jobChunks,0),device,&exactRequest,exactArgs)<0);sample->textureUsage=5;
 target->mip[9].stride=8;assert(tegu_compositor_request_for_job(&TEGU_JOB(command->jobChunks,0),device,&exactRequest,exactArgs)<0);target->mip[9].stride=4;
 [encoder endEncoding];[command commit];[command waitUntilCompleted];
 assert([command status]==4&&![command error]&&compositorDraws==compositorBefore+1);
 [encoder release];[command release];compositorB3PsoMode=0;compositorExpectedFsUboWindow=72;

 command=[[queue commandBuffer] retain];encoder=[[command renderCommandEncoderWithDescriptor:(id)pass] retain];assert(encoder);
 [encoder setRenderPipelineState:pipeline];[encoder setVertexBuffer:vertex offset:0 atIndex:1];
 [encoder setVertexBuffer:v2 offset:0 atIndex:2];[encoder setVertexBuffer:v3 offset:0 atIndex:3];
 [encoder setFragmentBuffer:f1 offset:0 atIndex:1];[encoder setFragmentBuffer:f6 offset:0 atIndex:6];
 [encoder setFragmentTexture:sample atIndex:3];
 [encoder drawPrimitives:3 vertexStart:0 vertexCount:3];assert(command->invalid&&!command->count);
 [encoder endEncoding];[encoder release];[command release];

 [sampler release];[sd release];[vertex release];[v2 release];[v3 release];[f1 release];[f6 release];
 [queue release];[b3Pass release];[pass release];[sample release];[secondTarget release];[target release];[td release];[single release];[pipeline release];[d release];[lib release];
 device->capabilities=oldCaps;drain();
 puts("PASS narrow-blur observers plus imported single-pass-blur-3 B3 pipeline and exact window82 compositor snapshot");
}
