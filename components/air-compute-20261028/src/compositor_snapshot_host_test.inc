/* Host Objective-C resource snapshot path for the captured QuartzCore draw. */
static unsigned char *snapshot_read_file(const char *path,size_t *length) {
 FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0);rewind(f);
 unsigned char *bytes=malloc((size_t)n);assert(bytes&&fread(bytes,1,(size_t)n,f)==(size_t)n);fclose(f);*length=(size_t)n;return bytes;
}
static void compositor_expect_bad_binding(TeguComputeDevice *device,TeguQueue *queue,
 TeguRenderPipeline *pipeline,TeguTexture *target,unsigned mode) {
 TeguCommand *command=[[queue commandBuffer] retain];
 RenderPass *pass=[[RenderPass alloc] init];pass->color=[[PassColor alloc] init];
 pass->color->target=[target retain];pass->color->load=mode==5?1:2;pass->color->store=mode==6?2:1;
 TeguRenderEncoder *encoder=[[command renderCommandEncoderWithDescriptor:(id)pass] retain];
 if(mode==6){assert(!encoder&&command->invalid);[pass release];[command release];drain();return;}
 if(!encoder){assert(mode==5&&command->invalid);[pass release];[command release];drain();return;}
 assert(encoder);
 [encoder setRenderPipelineState:pipeline];
 unsigned vertexSlot=pipeline->vertexFetch.attributes[0].buffer_index;
 unsigned vertexStride=pipeline->vertexFetch.attributes[0].stride;
 NSUInteger vertexBytes=3u*vertexStride;
 compositorExpectedVertexSlot=vertexSlot;compositorExpectedVertexLength=(unsigned)vertexBytes;
 TeguBuffer *v0=[device newBufferWithLength:vertexBytes options:0];assert(v0);[encoder setVertexBuffer:v0 offset:0 atIndex:vertexSlot];[v0 release];
 unsigned char bytes[96]={0};
 if(mode==1) {
  TeguBuffer *shortWindow=[device newBufferWithLength:63 options:0];assert(shortWindow);
  [encoder setVertexBuffer:shortWindow offset:0 atIndex:2];[shortWindow release];
 } else [encoder setVertexBytes:bytes length:80 atIndex:2];
 [encoder setVertexBytes:bytes length:32 atIndex:3];
 [encoder setFragmentBytes:bytes length:96 atIndex:1];[encoder setFragmentBytes:bytes length:32 atIndex:6];
 TextureDesc *sd=[[TextureDesc alloc] init];sd->format=70;sd->w=sd->h=64;sd->uses=1;
 TeguTexture *texture=[device newTextureWithDescriptor:(id)sd];assert(texture);
 if(mode==2)texture->mip[0].stride++;
 [encoder setFragmentTexture:texture atIndex:3];
 SamplerTestDescriptor *samplerDesc=[[SamplerTestDescriptor alloc] init];samplerDesc->normalized=1;samplerDesc->anisotropy=1;
 TeguSampler *sampler=[device newSamplerStateWithDescriptor:(id)samplerDesc];assert(sampler);
 [encoder setFragmentSamplerState:sampler atIndex:0];
 TeguComputeDevice *foreignDevice=0;
 if(mode==3) {
  foreignDevice=[TeguComputeDevice alloc];foreignDevice->method=device->method;foreignDevice->scalar=device->scalar;
  foreignDevice->map=device->map;foreignDevice->unmap=device->unmap;
  TeguBuffer *foreign=[foreignDevice newBufferWithLength:80 options:0];assert(foreign);
  [encoder->vertexBuffers[2] release];encoder->vertexBuffers[2]=foreign; // Simulate a stale cross-device encoder slot.
 } else if(mode==4) {
  [encoder->vertexBuffers[2] release];encoder->vertexBuffers[2]=(TeguBuffer *)[[NSObject alloc] init]; // Simulate wrong runtime object type.
 }
 [encoder drawPrimitives:3 vertexStart:0 vertexCount:3];
 if(!(command->invalid&&command->count==0))fprintf(stderr,"snapshot negative mode %u accepted: invalid=%d count=%u\n",mode,command->invalid,command->count);
 assert(command->invalid&&command->count==0);
 [encoder endEncoding];[encoder release];[foreignDevice release];[sampler release];[samplerDesc release];
 [texture release];[sd release];[pass release];[command release];drain();
}
static void test_compositor_draw_snapshot(TeguComputeDevice *device,const char *metallibPath,const char *profilePath,TeguRenderPipeline *productionPipeline) {
 TeguAIRRenderTypedPlan unsupported={0};unsupported.version=1;unsupported.count=1;
 unsupported.uses[0]=(TeguAIRRenderResourceUse){.kind=2,.stage=1,.class_mask=4,.flags=1};
 assert(!tegu_compositor_typed_uses_supported(&unsupported));
 unsupported.uses[0]=(TeguAIRRenderResourceUse){.kind=1,.stage=2,.class_mask=2,.flags=3};
 assert(!tegu_compositor_typed_uses_supported(&unsupported));
 TeguRenderPipeline *pipeline=0;
 if(productionPipeline) pipeline=[productionPipeline retain];
 else {
  size_t airLength,profileLength;unsigned char *airBytes=snapshot_read_file(metallibPath,&airLength),*profile=snapshot_read_file(profilePath,&profileLength);
  char error[512]={0};TeguAIRLibrary *library=tegu_air_library_open(airBytes,airLength,error,sizeof(error));assert(library);
  TeguAIRRenderColorAttachment attachments[2]={{80,15},{80,0}};
  TeguAIRVertexAttribute attrs[4]={{0,31,0,1},{1,29,16,1},{2,29,24,1},{3,27,32,1}};TeguAIRVertexLayout layouts[1]={{1,48,1,1}};
  TeguAIRBlob vs={0},fs={0},metadata={0};
  TeguAIRRenderPipeline *compiled=tegu_air_render_pipeline_create_with_attachments(library,
   (const uint8_t *)"downsample_blur_vert_lph",24,library,(const uint8_t *)"downsample_4_frag_lph",21,
   attachments,2,attrs,4,layouts,1,profile,profileLength,&vs,&fs,&metadata,error,sizeof(error));
  assert(compiled&&vs.bytes&&fs.bytes&&metadata.bytes);tegu_air_blob_free(&vs);tegu_air_blob_free(&fs);tegu_air_blob_free(&metadata);
  tegu_air_library_close(library);free(airBytes);free(profile);
  pipeline=[[TeguRenderPipeline alloc] init];assert(pipeline);pipeline->owner=[device retain];pipeline->compiled=compiled;
  pipeline->colorFormat=80;pipeline->compositorV6=1;pipeline->compositorSnapshotOnly=1;
  pipeline->blend=(TeguAIRRenderBlend){1,0,1,1,0,1,1,0,15};
  pipeline->vertexFetch.version=1;pipeline->vertexFetch.mask=3;pipeline->vertexFetch.count=2;
  pipeline->vertexFetch.attributes[0]=(struct tegu_vertex_attribute){0,1,109,0,48,0,1};
  pipeline->vertexFetch.attributes[1]=(struct tegu_vertex_attribute){1,1,103,16,48,0,1};
 }
 NSUInteger baseline=[device currentAllocatedSize];
 TextureDesc *td=[[TextureDesc alloc] init];td->format=80;td->w=td->h=16;td->uses=4;
 TeguTexture *target=[device newTextureWithDescriptor:(id)td];assert(target);
 RenderPass *pass=[[RenderPass alloc] init];pass->color=[[PassColor alloc] init];pass->color->target=[target retain];pass->color->load=2;pass->color->store=1;
 TeguQueue *queue=[device newCommandQueue];TeguCommand *command=[[queue commandBuffer] retain];
 TeguRenderEncoder *encoder=[[command renderCommandEncoderWithDescriptor:(id)pass] retain];assert(encoder);
 [encoder setRenderPipelineState:pipeline];
 unsigned vertexSlot=pipeline->vertexFetch.attributes[0].buffer_index;
 unsigned vertexStride=pipeline->vertexFetch.attributes[0].stride;
 NSUInteger vertexBytes=3u*vertexStride;
 compositorExpectedVertexSlot=vertexSlot;compositorExpectedVertexLength=(unsigned)vertexBytes;
 TeguBuffer *vertex0=[device newBufferWithLength:vertexBytes options:0];assert(vertex0);U64 vertex0Handle=vertex0->handle;
 [encoder setVertexBuffer:vertex0 offset:0 atIndex:vertexSlot];[vertex0 release];
 unsigned char source[96],saved[96];for(unsigned i=0;i<sizeof(source);i++)source[i]=(unsigned char)(i*7+3);memcpy(saved,source,sizeof(saved));
 TeguBuffer *vertex2=[device newBufferWithBytes:source length:80 options:0];assert(vertex2);
 TeguBuffer *vertex3=[device newBufferWithBytes:source length:32 options:0];assert(vertex3);
 /* A nonzero residual means the measured v7 shader window32 consumes 40
  * bytes from the aligned base; its caller buffer can therefore be56 bytes
  * total. The v6 baseline continues to use its72-byte shader window. */
 NSUInteger fragment1Bytes=compositorV7PsoMode?56u:96u;
 TeguBuffer *fragment1=[device newBufferWithBytes:source length:fragment1Bytes options:0];assert(fragment1);
 TeguBuffer *fragment6=[device newBufferWithBytes:source length:32 options:0];assert(fragment6);
 [encoder setVertexBuffer:vertex2 offset:16 atIndex:2];[encoder setVertexBuffer:vertex3 offset:16 atIndex:3];
 [encoder setFragmentBuffer:fragment1 offset:24 atIndex:1];[encoder setFragmentBuffer:fragment6 offset:30 atIndex:6];
 [vertex2 release];[vertex3 release];[fragment1 release];[fragment6 release];
 memset(source,0xee,sizeof(source));
 TeguBuffer *unusedVertex=[device newBufferWithLength:16 options:0];U64 unusedVertexHandle=unusedVertex->handle;
 TeguBuffer *unusedFragment=[device newBufferWithLength:16 options:0];U64 unusedFragmentHandle=unusedFragment->handle;
 [encoder setVertexBuffer:unusedVertex offset:0 atIndex:5];[encoder setFragmentBuffer:unusedFragment offset:0 atIndex:7];
 TextureDesc *sampleDesc=[[TextureDesc alloc] init];sampleDesc->format=70;sampleDesc->w=sampleDesc->h=16;sampleDesc->uses=1;
 TeguTexture *sample=[device newTextureWithDescriptor:(id)sampleDesc];assert(sample);U64 sampleHandle=sample->buffer->handle;
 TeguTexture *unusedTexture=[device newTextureWithDescriptor:(id)sampleDesc];assert(unusedTexture);U64 unusedTextureHandle=unusedTexture->buffer->handle;
 [encoder setFragmentTexture:sample atIndex:3];[encoder setFragmentTexture:unusedTexture atIndex:5];
 SamplerTestDescriptor *sd=[[SamplerTestDescriptor alloc] init];sd->normalized=1;sd->anisotropy=1;
 TeguSampler *sampler=[device newSamplerStateWithDescriptor:(id)sd];TeguSampler *unusedSampler=[device newSamplerStateWithDescriptor:(id)sd];assert(sampler&&unusedSampler);
 [encoder setFragmentSamplerState:sampler atIndex:0];[encoder setFragmentSamplerState:unusedSampler atIndex:4];
 [unusedVertex release];[unusedFragment release];[sample release];[unusedTexture release];[sampler release];[unusedSampler release];
 [encoder drawPrimitives:3 vertexStart:0 vertexCount:3];
 assert(!command->invalid&&command->count==1&&TEGU_JOB(command->jobChunks,0).compositorSnapshot);
 struct Dispatch *job=&TEGU_JOB(command->jobChunks,0);
 assert(job->compositorVertexBufferMask==((1u<<vertexSlot)|(1u<<2)|(1u<<3)));
 assert(job->compositorFragmentBufferMask==((1u<<1)|(1u<<6)));
 assert(job->compositorVertexFacts[vertexSlot].classMask==(1u<<4)&&
        job->compositorVertexFacts[2].classMask==(1u<<1)&&job->compositorVertexFacts[3].classMask==(1u<<1)&&
        job->compositorFragmentFacts[1].classMask==(1u<<1)&&job->compositorFragmentFacts[6].classMask==(1u<<1));
 assert(job->compositorTextureMask==(1u<<3)&&job->compositorSamplerMask==1);
 assert(job->compositorSamplers[0]==sampler&&job->compositorSamplers[4]==0);
 assert(job->compositorAttachmentCount==2&&job->compositorAttachments[0].format==80&&
        job->compositorAttachments[0].writeMask==15&&job->compositorAttachments[1].format==80&&
        job->compositorAttachments[1].writeMask==0);
 assert(job->compositorLoadAction==2&&job->compositorStoreAction==1);
 assert(job->compositorVertexFacts[vertexSlot].handle==vertex0Handle&&job->compositorVertexFacts[vertexSlot].available==vertexBytes);
 assert(job->compositorVertexFacts[2].offset==16&&job->compositorVertexFacts[2].available==64&&
        job->compositorVertexFacts[2].bindingResidual==0&&job->compositorVertexFacts[2].rangeOffset==0&&job->compositorVertexFacts[2].rangeLength==64);
 assert(job->compositorVertexFacts[3].offset==16&&job->compositorVertexFacts[3].available==16&&
        job->compositorVertexFacts[3].bindingResidual==0&&job->compositorVertexFacts[3].rangeOffset==0&&job->compositorVertexFacts[3].rangeLength==16);
 assert(job->compositorFragmentFacts[1].offset==16&&job->compositorFragmentFacts[1].available==(compositorV7PsoMode?40u:80u)&&
        job->compositorFragmentFacts[1].bindingResidual==8&&job->compositorFragmentFacts[1].rangeOffset==0&&
        job->compositorFragmentFacts[1].rangeLength==(compositorV7PsoMode?32u:72u));
 assert(job->compositorFragmentFacts[6].offset==16&&job->compositorFragmentFacts[6].available==16&&
        job->compositorFragmentFacts[6].bindingResidual==14&&job->compositorFragmentFacts[6].rangeOffset==0&&job->compositorFragmentFacts[6].rangeLength==2);
 assert(job->compositorTextures[3]==sample&&job->compositorTextureFacts[3].handle==sampleHandle&&
        job->compositorTextureFacts[3].span==1024&&job->compositorTextureFacts[3].stride==64);
 assert(job->compositorPlan.bytes&&job->compositorPlan.length);
 assert(!memcmp([(TeguBuffer *)job->compositorVertexBuffers[2] contents],saved,80));
 assert(!memcmp([(TeguBuffer *)job->compositorVertexBuffers[3] contents],saved,32));
 assert(!memcmp([(TeguBuffer *)job->compositorFragmentBuffers[1] contents],saved,fragment1Bytes));
 assert(!memcmp([(TeguBuffer *)job->compositorFragmentBuffers[6] contents],saved,32));
 [encoder endEncoding];[encoder release];drain();
 // Encoder/caller references are gone: only the reflected job snapshot remains.
 assert(memory[vertex0Handle]&&memory[sampleHandle]);
 assert(!memory[unusedVertexHandle]&&!memory[unusedFragmentHandle]&&!memory[unusedTextureHandle]);
 unsigned beforeCalls=renderDraws,beforeCompositor=compositorDraws,caps=device->capabilities;
 struct tegu_compositor_request wire;U64 wireArgs[4];
 memset(&wire,0x5a,sizeof(wire));struct tegu_compositor_request wireBefore=wire;
 U64 argsBefore[4]={11,22,33,44};memcpy(wireArgs,argsBefore,sizeof(wireArgs));
 U64 productionHandle=pipeline->handle;pipeline->handle=0;
 assert(tegu_compositor_request_for_job(job,device,&wire,wireArgs)<0);
 assert(!memcmp(&wire,&wireBefore,sizeof(wire))&&!memcmp(wireArgs,argsBefore,sizeof(wireArgs)));
 /* The production PSO must provide an imported handle and explicit capability. */
 pipeline->handle=productionPipeline?productionHandle:0xc0000001u;device->capabilities=caps|TEGU_GPU_CAP_COMPOSITOR;
 compositorExpectedPipelineHandle=pipeline->handle;
 assert(!tegu_compositor_request_for_job(job,device,&wire,wireArgs));
 /* Metal Constant(0) uses pulled Storage, unlike PerVertex(1). Never feed
  * a pulled pipeline to this fixed attribute-fetch backend. */
 job->compositorVertexFacts[vertexSlot].classMask=1;
 assert(tegu_compositor_request_for_job(job,device,&wire,wireArgs)<0);
 job->compositorVertexFacts[vertexSlot].classMask=16;
 assert(wire.vertex.buffer_mask==(1u<<vertexSlot)&&wire.vertex.buffers[vertexSlot].handle==vertex0Handle&&
        wire.vertex.buffers[vertexSlot].length==vertexBytes);
 for(unsigned i=0;i<TEGU_VERTEX_SLOTS;i++)if(i!=vertexSlot)
  assert(!wire.vertex.buffers[i].handle&&!wire.vertex.buffers[i].offset&&!wire.vertex.buffers[i].length);
 unsigned mask=job->compositorFragmentBufferMask;job->compositorFragmentBufferMask|=1u<<7;
 assert(tegu_compositor_request_for_job(job,device,&wire,wireArgs)<0);job->compositorFragmentBufferMask=mask;
 U64 residual=job->compositorFragmentFacts[1].bindingResidual;
 job->compositorFragmentFacts[1].bindingResidual=16;
 assert(tegu_compositor_request_for_job(job,device,&wire,wireArgs)<0);
 job->compositorFragmentFacts[1].bindingResidual=residual;
 [command commit];[command waitUntilCompleted];
 assert([command status]==4&&renderDraws==beforeCalls&&compositorDraws==beforeCompositor+1);
 /* Record a screen-sized owned target through the public encoder too. Pixel
  * rows are padded to the kernel's64-byte contract; no GPU output is simulated. */
 td->w=1080;td->h=2424;td->uses=0;
 TeguTexture *screen=[device newTextureWithDescriptor:(id)td];assert(screen);
 assert(screen->rowBytes==4352&&screen->mip[0].stride==4352&&screen->buffer->logical==4352u*2424u);
 RenderPass *screenPass=[[RenderPass alloc] init];screenPass->color=[[PassColor alloc] init];
 screenPass->color->target=[screen retain];screenPass->color->load=2;screenPass->color->store=1;
 TeguCommand *screenCommand=[[queue commandBuffer] retain];
 TeguRenderEncoder *screenEncoder=[[screenCommand renderCommandEncoderWithDescriptor:(id)screenPass] retain];assert(screenEncoder);
 [screenEncoder setRenderPipelineState:pipeline];
 for(unsigned i=0;i<31;i++) {
  if(job->compositorVertexBufferMask&(1u<<i))
   [screenEncoder setVertexBuffer:job->compositorVertexBuffers[i] offset:job->compositorVertexFacts[i].offset+job->compositorVertexFacts[i].bindingResidual atIndex:i];
  if(job->compositorFragmentBufferMask&(1u<<i))
   [screenEncoder setFragmentBuffer:job->compositorFragmentBuffers[i] offset:job->compositorFragmentFacts[i].offset+job->compositorFragmentFacts[i].bindingResidual atIndex:i];
 }
 [screenEncoder setFragmentTexture:job->compositorTextures[3] atIndex:3];
 [screenEncoder setFragmentSamplerState:job->compositorSamplers[0] atIndex:0];
 [screenEncoder drawPrimitives:3 vertexStart:0 vertexCount:3];[screenEncoder endEncoding];
 [screenEncoder release];[screenPass release];[screen release];
 assert(!screenCommand->invalid&&screenCommand->count==1);
 [screenCommand commit];[screenCommand waitUntilCompleted];
 assert([screenCommand status]==4&&compositorDraws==beforeCompositor+2&&renderDraws==beforeCalls);
 [screenCommand release];drain();td->w=td->h=16;td->uses=4;
 /* Exercise the real typed-snapshot -> selector16 adapter with an imported
  * backing and nonzero IOSurface byte offset. */
 device->capabilities|=TEGU_GPU_CAP_COMPOSITOR_IOSURFACE;
 /* Exercise the public imported target path at the actual device geometry. The
  * sampled source remains the compositor's fixed 16x16 texture. */
 compositorIOSurfaceWidth=1125;compositorIOSurfaceHeight=2436;compositorIOSurfaceRow=4608;
 compositorIOSurfaceAlloc=(compositorIOSurfaceOffset+compositorIOSurfaceRow*compositorIOSurfaceHeight+4095)&~4095ull;
 TextureDesc *surfaceDesc=[[TextureDesc alloc] init];surfaceDesc->format=80;surfaceDesc->w=compositorIOSurfaceWidth;surfaceDesc->h=compositorIOSurfaceHeight;surfaceDesc->uses=4;
 NSUInteger surfaceAlloc=(NSUInteger)compositorIOSurfaceAlloc;
 IOSurfaceFixture *largeSurface=new_surface_fixture(512,TEGU_IOSURFACE_BGRA,0,compositorIOSurfaceWidth,compositorIOSurfaceHeight,compositorIOSurfaceRow,surfaceAlloc);
 unsigned importCount=compositorIOSurfaceImports,releaseCount=iosurfaceReleases,targetDrawCount=compositorImportedTargetDraws;
 TeguTexture *surfaceTarget=import_surface_texture(device,surfaceDesc,largeSurface,0);assert(surfaceTarget);
 compositorExpectedPipelineHandle=pipeline->handle;
 RenderPass *surfacePass=[[RenderPass alloc] init];surfacePass->color=[[PassColor alloc] init];
 surfacePass->color->target=[surfaceTarget retain];surfacePass->color->load=2;surfacePass->color->store=1;
 TeguCommand *surfaceCommand=[[queue commandBuffer] retain];
 TeguRenderEncoder *surfaceEncoder=[[surfaceCommand renderCommandEncoderWithDescriptor:(id)surfacePass] retain];
 if(!surfaceEncoder)fprintf(stderr,"IOSurface target encoder refused: caps=%llx imported=%u ver=%u fmt=%u usage=%u size=%ux%u stride=%u alloc=%llu offset=%llu targetdev=%p owner=%p load=%llu store=%llu\n",
  (unsigned long long)device->capabilities,(unsigned)surfaceTarget->surfaceImported,(unsigned)surfaceTarget->surfaceTargetVersion,(unsigned)surfaceTarget->format,(unsigned)surfaceTarget->textureUsage,
  (unsigned)surfaceTarget->textureWidth,(unsigned)surfaceTarget->textureHeight,(unsigned)surfaceTarget->rowBytes,(unsigned long long)surfaceTarget->surfaceAllocSize,
  (unsigned long long)surfaceTarget->mip[0].offset,[surfaceTarget device],device,(unsigned long long)surfacePass->color->load,(unsigned long long)surfacePass->color->store);
 assert(surfaceEncoder);
 [surfaceEncoder setRenderPipelineState:pipeline];
 for(unsigned i=0;i<31;i++) {
  if(job->compositorVertexBufferMask&(1u<<i))
   [surfaceEncoder setVertexBuffer:job->compositorVertexBuffers[i] offset:job->compositorVertexFacts[i].offset+job->compositorVertexFacts[i].bindingResidual atIndex:i];
  if(job->compositorFragmentBufferMask&(1u<<i))
   [surfaceEncoder setFragmentBuffer:job->compositorFragmentBuffers[i] offset:job->compositorFragmentFacts[i].offset+job->compositorFragmentFacts[i].bindingResidual atIndex:i];
 }
 [surfaceEncoder setFragmentTexture:job->compositorTextures[3] atIndex:3];
 [surfaceEncoder setFragmentSamplerState:job->compositorSamplers[0] atIndex:0];
 [surfaceEncoder drawPrimitives:3 vertexStart:0 vertexCount:3];[surfaceEncoder endEncoding];
 assert(!surfaceCommand->invalid&&surfaceCommand->count==1);
 [surfaceTarget release];[surfaceEncoder release];[surfacePass release];
 assert([largeSurface testReferences]>=2&&iosurfaceReleases==releaseCount);
 [surfaceCommand commit];[surfaceCommand waitUntilCompleted];
 assert([surfaceCommand status]==4&&compositorImportedTargetDraws==targetDrawCount+1&&
  compositorIOSurfaceImports==importCount+1&&iosurfaceReleases==releaseCount);
 [surfaceCommand release];drain();assert(iosurfaceReleases==releaseCount+1&&[largeSurface testReferences]==1);
 [largeSurface release];[surfaceDesc release];compositorIOSurfaceWidth=1080;compositorIOSurfaceHeight=2424;compositorIOSurfaceRow=4352;compositorIOSurfaceAlloc=(4096u+4352u*2424u+4095u)&~4095u;device->capabilities&=~TEGU_GPU_CAP_COMPOSITOR_IOSURFACE;
 if(!productionPipeline)pipeline->handle=0;device->capabilities=caps;
 [command release];drain();
 // Normal command dealloc released snapshot-only buffer/backing references.
 assert(!memory[vertex0Handle]&&!memory[sampleHandle]);
 NSUInteger postSnapshotBaseline=[device currentAllocatedSize];
 compositor_expect_bad_binding(device,queue,pipeline,target,1); // reflected UBO window is short after offset
 assert([device currentAllocatedSize]==postSnapshotBaseline);
 compositor_expect_bad_binding(device,queue,pipeline,target,2); // texture backing span exceeds allocation
 assert([device currentAllocatedSize]==postSnapshotBaseline);
 compositor_expect_bad_binding(device,queue,pipeline,target,3); // defensive snapshot owner recheck
 assert([device currentAllocatedSize]==postSnapshotBaseline);
 compositor_expect_bad_binding(device,queue,pipeline,target,4); // defensive runtime class recheck
 assert([device currentAllocatedSize]==postSnapshotBaseline);
 unsigned loadCaps=device->capabilities;
 device->capabilities&=~(unsigned)(TEGU_GPU_CAP_LOAD_PASS|TEGU_GPU_CAP_COMPOSITOR_LOAD);
 compositor_expect_bad_binding(device,queue,pipeline,target,5); // LOAD without either explicit gate is rejected
 device->capabilities=loadCaps;
 compositor_expect_bad_binding(device,queue,pipeline,target,6); // unsupported compositor STORE is not erased
 assert([device currentAllocatedSize]==postSnapshotBaseline);
 // A two-target descriptor is retained until its pipeline class is known.  The
 // existing compositor PSO still refuses it before any job is recorded.
 // The full plan used above is retained through the job and destroyed on command cleanup.
 td->format=80;TeguTexture *second=[device newTextureWithDescriptor:(id)td];assert(second);
 RenderPass *badPass=[[RenderPass alloc] init];badPass->color=[[PassColor alloc] init];badPass->color->target=[target retain];badPass->color->load=2;badPass->color->store=1;
 badPass->second=[[PassColor alloc] init];badPass->second->target=[second retain];badPass->second->load=1;badPass->second->store=1;
 command=[[queue commandBuffer] retain];TeguRenderEncoder *badEncoder=[[command renderCommandEncoderWithDescriptor:(id)badPass] retain];
 assert(badEncoder&&!command->invalid);[badEncoder setRenderPipelineState:pipeline];assert(command->invalid&&command->count==0);[badEncoder release];
 [command release];[badPass release];[second release];[target release];[td release];[sampleDesc release];[pass release];[queue release];[pipeline release];drain();
 assert([device currentAllocatedSize]==baseline);
 puts("PASS compositor snapshots: typed bindings, size-offset owned target and selector17 imported IOSurface target reach selector16, retains through completion, refusals; mocked kernel transport only");
}
