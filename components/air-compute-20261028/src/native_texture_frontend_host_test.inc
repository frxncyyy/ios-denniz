/* Real AIR -> native frontend -> TGR5 package with the hardware-qualified
 * texture-pair compiler. This is host import/codegen evidence, not GPU use. */
static int tegu_texture_reference_equals(const char *path,const void *bytes,size_t length) {
 FILE *f=fopen(path,"rb");if(!f)return 0;
 if(fseek(f,0,SEEK_END)){fclose(f);return 0;}
 long n=ftell(f);if(n<0||fseek(f,0,SEEK_SET)){fclose(f);return 0;}
 if((size_t)n!=length){fclose(f);return 0;}
 unsigned char *expected=malloc(length?length:1);if(!expected){fclose(f);return 0;}
 int ok=fread(expected,1,length,f)==length&&!memcmp(expected,bytes,length);
 free(expected);fclose(f);return ok;
}
/* Compare compact and pretty JSON while ignoring only JSON whitespace outside
 * strings. The metadata contents and order must otherwise match exactly. */
static int tegu_texture_json_equal(const unsigned char *a,size_t an,const unsigned char *b,size_t bn) {
 size_t i=0,j=0;int inA=0,inB=0,escapeA=0,escapeB=0;
 for(;;) {
  while(i<an&&!inA&&(a[i]==' '||a[i]=='\t'||a[i]=='\r'||a[i]=='\n'))i++;
  while(j<bn&&!inB&&(b[j]==' '||b[j]=='\t'||b[j]=='\r'||b[j]=='\n'))j++;
  if(i==an||j==bn)return i==an&&j==bn;
  unsigned char x=a[i++],y=b[j++];if(x!=y)return 0;
  if(inA) {if(escapeA)escapeA=0;else if(x=='\\')escapeA=1;else if(x=='"')inA=0;}
  else if(x=='"')inA=1;
  if(inB) {if(escapeB)escapeB=0;else if(y=='\\')escapeB=1;else if(y=='"')inB=0;}
  else if(y=='"')inB=1;
 }
}
static void tegu_test_native_texture_frontend(TeguComputeDevice *device,const void *airBytes,NSUInteger airLength) {
 assert(device&&airBytes&&airLength);
 U64 savedCapabilities=device->capabilities;
 NSUInteger allocationBaseline=[device currentAllocatedSize];unsigned importBaseline=renderImports,releaseBaseline=renderReleases;
 device->capabilities=16|32;id error=0;
 /* The existing gates still refuse sampled resources; without capability128
  * the additive texture path must refuse before kernel import. */
 TeguLibrary *library=[device newTeguLibraryWithBytes:airBytes length:airLength error:&error];
 assert(library&&!error);
 RenderDescriptor *desc=[[RenderDescriptor alloc] init];
 desc->vertex=[library newFunctionWithName:[NSString stringWithUTF8String:"texture_vertex"]];
 desc->fragment=[library newFunctionWithName:[NSString stringWithUTF8String:"texture_fragment"]];
 desc->colors=[[RenderColors alloc] init];desc->colors->colors[0]=[[RenderColor alloc] init];
 desc->colors->colors[0]->format=70;desc->colors->colors[0]->mask=15;
 desc->samples=1;desc->raster=1;desc->topology=3;desc->amplification=1;
 RenderLayout *layout=[[RenderLayout alloc] init];desc->layout=layout;
 for(unsigned i=7;i<=8;i++) {
  RenderAttribute *a=[[RenderAttribute alloc] init];a->value=31;a->bufferSlot=1;a->byteOffset=(i-7)*16;
  layout->entries[i]=a;
 }
 layout->buffers=[[RenderLayout alloc] init];
 RenderAttribute *binding=[[RenderAttribute alloc] init];binding->layoutStride=32;binding->step=1;binding->rate=1;
 layout->buffers->entries[1]=binding;
 assert(![device newRenderPipelineStateWithDescriptor:desc error:&error]&&error);
 assert(renderImports==importBaseline&&[device currentAllocatedSize]==allocationBaseline);

 device->capabilities=16|32|128|256;error=0;
 TeguRenderPipeline *pipeline=[device newRenderPipelineStateWithDescriptor:desc error:&error];
 assert(pipeline&&!error&&pipeline->compiled&&pipeline->handle);
 struct tegu_render_info parsed;assert(!tegu_render_parse(pipeline->package,pipeline->packageBytes,&parsed));
 assert(parsed.version==5&&parsed.color_format==70&&parsed.push_offset==256);
 unsigned char *textureBgra=malloc(pipeline->packageBytes);assert(textureBgra);
 memcpy(textureBgra,pipeline->package,pipeline->packageBytes);tegu_render_put(textureBgra+12,80);
 assert(!tegu_render_parse(textureBgra,pipeline->packageBytes,&parsed)&&parsed.color_format==80);free(textureBgra);
 assert(parsed.vertex_mask==3&&parsed.vertex_count==2&&parsed.position_offset==384&&
  parsed.secondary_offset==768&&parsed.secondary_registers==32&&parsed.secondary_preload==0x3800000000000000ULL&&
  parsed.varying_bytes==16);
 assert(parsed.blend.version==1&&!parsed.blend.enabled&&parsed.texture.version==1&&parsed.texture.stage==16&&
  !parsed.texture.texture_index&&!parsed.texture.sampler_index&&!parsed.texture.descriptor_set&&
  !parsed.texture.binding&&parsed.texture.normalized_coordinates==1);
 assert(pipeline->texture.version==1&&pipeline->texture.stage==16&&!pipeline->texture.texture_index&&
  !pipeline->texture.sampler_index&&!pipeline->texture.descriptor_set&&!pipeline->texture.binding&&
  pipeline->texture.normalized_coordinates==1);
 assert(pipeline->pair.version==2&&pipeline->pair.flags==1&&pipeline->pair.position_offset==384&&
  pipeline->pair.secondary_offset==768&&pipeline->pair.vs_attribute_mask==3);
 assert(pipeline->vertexCode.size==1024&&pipeline->fragmentCode.size==1664&&
  parsed.stages[0].code_offset==1408&&parsed.stages[1].code_offset==2432);
 assert(pipeline->maliTexture.version==1&&!pipeline->maliTexture.table&&pipeline->maliTexture.texture_index==1&&
  !pipeline->maliTexture.sampler_index&&pipeline->maliTexture.normalized_coordinates==1&&
  !pipeline->maliTexture.descriptor_set&&!pipeline->maliTexture.binding);
 device->capabilities|=256;desc->colors->colors[0]->format=80;error=0;
 TeguRenderPipeline *pipeline80=[device newRenderPipelineStateWithDescriptor:desc error:&error];
 desc->colors->colors[0]->format=70;
 assert(pipeline80&&!error&&pipeline80->compiled&&pipeline80->handle&&pipeline80->colorFormat==80&&pipeline80->texture.version==1);
 const struct tegu_vertex_attribute *va=pipeline->vertexFetch.attributes;
 assert(pipeline->vertexFetch.mask==3&&pipeline->vertexFetch.count==2);
 assert(va[0].location==0&&va[0].buffer_index==1&&va[0].format==109&&!va[0].offset&&va[0].stride==32&&!va[0].input_rate&&va[0].divisor==1);
 assert(va[1].location==1&&va[1].buffer_index==1&&va[1].format==109&&va[1].offset==16&&va[1].stride==32&&!va[1].input_rate&&va[1].divisor==1);
 /* These full byte comparisons also pin the known SHA-256s: VS
  * dc3c3f931ace716d... and FS ec2e816a8d999b8f.... */
 assert(tegu_texture_reference_equals("research/tegu-gpu/native-texture-pair-canonical-r1/vs_full.bin",
  pipeline->vertexCode.code,pipeline->vertexCode.size));
 assert(tegu_texture_reference_equals("research/tegu-gpu/native-texture-pair-canonical-r1/fs.bin",
  pipeline->fragmentCode.code,pipeline->fragmentCode.size));
 assert(!memcmp(pipeline->package+parsed.stages[0].code_offset,pipeline->vertexCode.code,pipeline->vertexCode.size));
 assert(!memcmp(pipeline->package+parsed.stages[1].code_offset,pipeline->fragmentCode.code,pipeline->fragmentCode.size));
 assert(parsed.push_span[1]==68&&tegu_render_word(pipeline->package+448)==0x2222);

 /* Rebuild AIR metadata directly and compare its JSON content to the
  * canonical artifact; compact bridge JSON and pretty fixture JSON differ only
  * in insignificant whitespace. */
 TeguAIRLibrary *air=tegu_air_library_open(airBytes,airLength,0,0);assert(air);
 TeguAIRVertexAttribute aa[2]={{7,31,0,1},{8,31,16,1}};
 TeguAIRVertexLayout al[1]={{1,32,1,1}};
 const uint8_t *vs=(const uint8_t *)"texture_vertex",*fs=(const uint8_t *)"texture_fragment";
 TeguAIRBlob vb={0},fb={0},mb={0};char message[512]={0};
 TeguAIRRenderPipeline *compiled=tegu_air_render_pipeline_create_with_vertex_descriptor(air,vs,14,air,fs,16,70,
  aa,2,al,1,tegu_native_profile,sizeof(tegu_native_profile),&vb,&fb,&mb,message,sizeof(message));
 assert(compiled&&vb.bytes&&fb.bytes&&mb.bytes);
 FILE *mf=fopen("research/tegu-gpu/texture-air-canonical-r1/70-metadata.json","rb");assert(mf);
 assert(!fseek(mf,0,SEEK_END));long metadataLength=ftell(mf);assert(metadataLength>=0&&!fseek(mf,0,SEEK_SET));
 unsigned char *canonicalMetadata=malloc((size_t)metadataLength);assert(canonicalMetadata);
 assert(fread(canonicalMetadata,1,(size_t)metadataLength,mf)==(size_t)metadataLength);fclose(mf);
 assert(tegu_texture_json_equal(mb.bytes,mb.length,canonicalMetadata,(size_t)metadataLength));
 free(canonicalMetadata);tegu_air_render_pipeline_close(compiled);
 tegu_air_blob_free(&vb);tegu_air_blob_free(&fb);tegu_air_blob_free(&mb);tegu_air_library_close(air);

 /* Exercise public texture/sampler setters and command-time snapshots through
  * the host selector12 transport stub; this does not execute a GPU draw. */
 NSUInteger resourceBaseline=[device currentAllocatedSize];unsigned drawsBefore=textureDraws;
 TextureDesc *targetDesc=[[TextureDesc alloc] init];targetDesc->w=targetDesc->h=16;targetDesc->format=70;targetDesc->uses=4;
 TextureDesc *sampleDesc=[[TextureDesc alloc] init];sampleDesc->w=sampleDesc->h=16;sampleDesc->format=70;sampleDesc->uses=1;
 TeguTexture *target=[device newTextureWithDescriptor:(id)targetDesc];
 TeguTexture *sample=[device newTextureWithDescriptor:(id)sampleDesc];assert(target&&sample&&target->buffer!=sample->buffer);
 SamplerTestDescriptor *samplerDesc=[[SamplerTestDescriptor alloc] init];
 samplerDesc->normalized=1;samplerDesc->anisotropy=1;samplerDesc->hasAverage=1;
 TeguSampler *sampler=[device newSamplerStateWithDescriptor:(id)samplerDesc];assert(sampler);[samplerDesc release];
 TeguQueue *queue=[device newCommandQueue];
 RenderPass *pass=[[RenderPass alloc] init];pass->color=[[PassColor alloc] init];
 pass->color->target=[target retain];pass->color->load=2;pass->color->store=1;
 TeguCommand *command=[[queue commandBuffer] retain];
 TeguRenderEncoder *encoder=[command renderCommandEncoderWithDescriptor:(id)pass];assert(encoder);
 float vertices[24]={-.75f,-.75f,.5f,1,.125f,.125f,0,1,
  .75f,-.75f,.5f,1,.875f,.125f,0,1,-.75f,.75f,.5f,1,.125f,.875f,0,1};
 [encoder setRenderPipelineState:pipeline];[encoder setVertexBytes:vertices length:sizeof(vertices) atIndex:1];
 [encoder setFragmentTexture:sample atIndex:0];[encoder setFragmentSamplerState:sampler atIndex:0];
 [encoder drawPrimitives:3 vertexStart:0 vertexCount:3];
 struct tegu_render_texture_request snapshot=TEGU_JOB(command->jobChunks,0).textureRequest;
 U64 vertexHandle=snapshot.vertex.buffers[1].handle,sampleHandle=snapshot.texture.handle;
 assert(snapshot.version==1&&snapshot.texture.length==1024&&snapshot.texture.offset==0&&
  snapshot.vertex.buffer_mask==2&&snapshot.vertex.buffers[1].length==96);
 assert(TEGU_JOB(command->jobChunks,0).sampledTexture==sample&&TEGU_JOB(command->jobChunks,0).fragmentSampler==sampler);
 /* Later API mutations cannot change the already recorded draw. */
 [encoder setFragmentTexture:0 atIndex:0];[encoder setFragmentSamplerState:0 atIndex:0];
 assert(!memcmp(&snapshot,&TEGU_JOB(command->jobChunks,0).textureRequest,sizeof(snapshot))&&
  TEGU_JOB(command->jobChunks,0).sampledTexture==sample&&TEGU_JOB(command->jobChunks,0).fragmentSampler==sampler);
 [encoder endEncoding];

 /* Missing required resources and invalid texture slot are rejected before
  * selector12; the valid snapshot above is still the sole accepted request. */
 TeguCommand *missing=[[queue commandBuffer] retain];
 TeguRenderEncoder *bad=[missing renderCommandEncoderWithDescriptor:(id)pass];assert(bad);
 [bad setRenderPipelineState:pipeline];[bad setVertexBytes:vertices length:sizeof(vertices) atIndex:1];
 [bad drawPrimitives:3 vertexStart:0 vertexCount:3];[bad endEncoding];[missing commit];
 assert([missing status]==5&&textureDraws==drawsBefore);
 [missing release];
 /* Selector 15 snapshots target layout while the existing vertex/texture
  * bindings keep their normal retained-resource lifetime. */
 TextureDesc *largeDesc=[[TextureDesc alloc] init];largeDesc->w=largeDesc->h=256;largeDesc->format=70;largeDesc->uses=4;
 TeguTexture *largeTarget=[device newTextureWithDescriptor:(id)largeDesc];assert(largeTarget&&largeTarget->rowBytes==1024);
 RenderPass *largePass=[[RenderPass alloc] init];largePass->color=[[PassColor alloc] init];
 largePass->color->target=[largeTarget retain];largePass->color->load=2;largePass->color->store=1;
 TeguCommand *largeCommand=[[queue commandBuffer] retain];
 device->capabilities&=~(U64)1024;
 TeguCommand *capOffCommand=[[queue commandBuffer] retain];
 TeguRenderEncoder *capOffEncoder=[capOffCommand renderCommandEncoderWithDescriptor:(id)largePass];assert(capOffEncoder);
 [capOffEncoder setRenderPipelineState:pipeline];[capOffEncoder setVertexBytes:vertices length:sizeof(vertices) atIndex:1];
 [capOffEncoder setFragmentTexture:sample atIndex:0];[capOffEncoder setFragmentSamplerState:sampler atIndex:0];
 unsigned capOffDraws=textureDraws;[capOffEncoder drawPrimitives:3 vertexStart:0 vertexCount:3];[capOffEncoder endEncoding];
 [capOffCommand commit];assert([capOffCommand status]==5&&textureDraws==capOffDraws&&!TEGU_JOB(capOffCommand->jobChunks,0).targetRequest.version);
 [capOffCommand release];device->capabilities|=1024;
 TeguRenderEncoder *largeEncoder=[largeCommand renderCommandEncoderWithDescriptor:(id)largePass];assert(largeEncoder);
 [largeEncoder setRenderPipelineState:pipeline];
 [largeEncoder setVertexBytes:vertices length:sizeof(vertices) atIndex:1];
 [largeEncoder setFragmentTexture:sample atIndex:0];[largeEncoder setFragmentSamplerState:sampler atIndex:0];
 [largeEncoder drawPrimitives:3 vertexStart:0 vertexCount:3];
 struct tegu_render_target_request targetSnapshot=TEGU_JOB(largeCommand->jobChunks,0).targetRequest;
 assert(targetSnapshot.version==1&&targetSnapshot.width==256&&targetSnapshot.height==256&&
  targetSnapshot.row_stride==1024&&targetSnapshot.kind==TEGU_RENDER_TARGET_REQUEST_TEXTURE);
 assert(!memcmp(targetSnapshot.payload,&TEGU_JOB(largeCommand->jobChunks,0).textureRequest,sizeof(TEGU_JOB(largeCommand->jobChunks,0).textureRequest)));
 [largeEncoder setFragmentTexture:0 atIndex:0];[largeEncoder setFragmentSamplerState:0 atIndex:0];
 largeTarget->textureWidth=16;largeTarget->textureHeight=16;largeTarget->rowBytes=64;
 assert(!memcmp(&targetSnapshot,&TEGU_JOB(largeCommand->jobChunks,0).targetRequest,sizeof(targetSnapshot))&&
  TEGU_JOB(largeCommand->jobChunks,0).sampledTexture==sample&&TEGU_JOB(largeCommand->jobChunks,0).fragmentSampler==sampler);
 [largeEncoder endEncoding];
 [largeCommand commit];assert([largeCommand status]==4&&textureDraws==drawsBefore+1);
 assert(!memcmp(&targetSnapshot,&lastTargetRequest,sizeof(targetSnapshot))&&
  !memcmp(targetSnapshot.payload,&lastTextureRequest,sizeof(lastTextureRequest)));
 [largeCommand release];[largePass release];[largeDesc release];[largeTarget release];
 TextureDesc *bgraDesc=[[TextureDesc alloc] init];bgraDesc->w=bgraDesc->h=16;bgraDesc->format=80;bgraDesc->uses=4;
 TeguTexture *bgraTarget=[device newTextureWithDescriptor:(id)bgraDesc];assert(bgraTarget);
 RenderPass *bgraPass=[[RenderPass alloc] init];bgraPass->color=[[PassColor alloc] init];
 bgraPass->color->target=[bgraTarget retain];bgraPass->color->load=2;bgraPass->color->store=1;
 TeguCommand *bgraCommand=[[queue commandBuffer] retain];
 TeguRenderEncoder *bgraEncoder=[bgraCommand renderCommandEncoderWithDescriptor:(id)bgraPass];assert(bgraEncoder);
 [bgraEncoder setRenderPipelineState:pipeline80];[bgraEncoder setVertexBytes:vertices length:sizeof(vertices) atIndex:1];
 [bgraEncoder setFragmentTexture:sample atIndex:0];[bgraEncoder setFragmentSamplerState:sampler atIndex:0];
 unsigned bgraBefore=textureDraws;[bgraEncoder drawPrimitives:3 vertexStart:0 vertexCount:3];
 assert(TEGU_JOB(bgraCommand->jobChunks,0).renderPipeline==pipeline80&&TEGU_JOB(bgraCommand->jobChunks,0).textureRequest.version==1);
 [bgraEncoder endEncoding];[bgraCommand commit];assert([bgraCommand status]==4&&textureDraws==bgraBefore+1&&lastTextureRequest.version==1);
 [bgraCommand release];[bgraPass release];[bgraTarget release];[bgraDesc release];
 const unsigned badWidths[2]={17u,257u};
 for(unsigned bi=0;bi<2;bi++) {unsigned width=badWidths[bi];
  TextureDesc *badDesc=[[TextureDesc alloc] init];badDesc->w=width;badDesc->h=16;badDesc->format=70;badDesc->uses=4;
  TeguTexture *badTarget=[device newTextureWithDescriptor:(id)badDesc];assert(badTarget);
  RenderPass *badPass=[[RenderPass alloc] init];badPass->color=[[PassColor alloc] init];
  badPass->color->target=[badTarget retain];badPass->color->load=2;badPass->color->store=1;
  TeguCommand *badCommand=[[queue commandBuffer] retain];
  assert(![badCommand renderCommandEncoderWithDescriptor:(id)badPass]);
  [badCommand commit];assert([badCommand status]==5&&textureDraws==drawsBefore+2);
  [badCommand release];[badPass release];[badDesc release];[badTarget release];
 }
 [sample release];[sampler release];[target release];[pass release];
 [targetDesc release];[sampleDesc release];[desc release];[library release];
 drain();assert(memory[vertexHandle]&&memory[sampleHandle]);
 [command commit];assert([command status]==4&&textureDraws==drawsBefore+3);
 assert(!memcmp(&snapshot,&lastTextureRequest,sizeof(snapshot)));
 [command release];[queue release];drain();
 assert(!memory[vertexHandle]&&!memory[sampleHandle]&&[device currentAllocatedSize]==resourceBaseline);

 /* Additive native source-format path: require capability2048 before creating
  * only an exact 16x16 GPU-read-only RGBA16Float source. CPU replaceRegion
  * uploads remain supported. The pass target and TGR5
  * package remain RGBA8(70); IOSurface import continues to reject format115. */
 device->capabilities=16|32|128;
 TextureDesc *halfDesc=[[TextureDesc alloc] init];halfDesc->w=halfDesc->h=16;halfDesc->format=115;halfDesc->uses=1;
 assert(![device newTextureWithDescriptor:(id)halfDesc]);
 device->capabilities|=2048;
 NSUInteger halfBaseline=[device currentAllocatedSize];unsigned halfDraws=textureDraws;
 TeguTexture *half=[device newTextureWithDescriptor:(id)halfDesc];assert(half&&half->format==115&&half->rowBytes==128&&
   half->mip[0].stride==128&&half->mip[0].offset==0&&[half->buffer length]==2048);
 unsigned char authored[2048];for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++) {
  uint16_t v[4]={(uint16_t)(x*0x0400),(uint16_t)(y*0x0400),(uint16_t)((x^y)*0x0400),0x3c00};
  memcpy(authored+y*128+x*8,v,sizeof(v));
 }
 [half replaceRegion:(TeguRegion){{0,0,0},{16,16,1}} mipmapLevel:0 withBytes:authored bytesPerRow:128];
 unsigned char copied[2048]={0};[half getBytes:copied bytesPerRow:128 fromRegion:(TeguRegion){{0,0,0},{16,16,1}} mipmapLevel:0];
 assert(!memcmp(copied,authored,sizeof(copied)));
 uint16_t onePixel[4]={0x1000,0x1400,0x1800,0x3c00};
 [half replaceRegion:(TeguRegion){{2,3,0},{1,1,1}} mipmapLevel:0 withBytes:onePixel bytesPerRow:8];
 memcpy(authored+3*128+2*8,onePixel,sizeof(onePixel));
 [half getBytes:copied bytesPerRow:128 fromRegion:(TeguRegion){{0,0,0},{16,16,1}} mipmapLevel:0];
 assert(!memcmp(copied,authored,sizeof(copied)));
 [half replaceRegion:(TeguRegion){{2,3,0},{1,1,1}} mipmapLevel:0 withBytes:onePixel bytesPerRow:7];
 [half getBytes:copied bytesPerRow:128 fromRegion:(TeguRegion){{0,0,0},{16,16,1}} mipmapLevel:0];
 assert(!memcmp(copied,authored,sizeof(copied)));
 halfDesc->uses=5;assert(![device newTextureWithDescriptor:(id)halfDesc]);halfDesc->uses=1;
 halfDesc->w=8;assert(![device newTextureWithDescriptor:(id)halfDesc]);halfDesc->w=16;
 TextureDesc *halfTargetDesc=[[TextureDesc alloc] init];halfTargetDesc->w=halfTargetDesc->h=16;halfTargetDesc->format=70;halfTargetDesc->uses=4;
 TeguTexture *halfTarget=[device newTextureWithDescriptor:(id)halfTargetDesc];assert(halfTarget&&halfTarget->format==70);
 SamplerTestDescriptor *halfSamplerDesc=[[SamplerTestDescriptor alloc] init];
 halfSamplerDesc->normalized=1;halfSamplerDesc->anisotropy=1;halfSamplerDesc->hasAverage=1;
 TeguSampler *halfSampler=[device newSamplerStateWithDescriptor:(id)halfSamplerDesc];assert(halfSampler);
 TeguQueue *halfQueue=[device newCommandQueue];RenderPass *halfPass=[[RenderPass alloc] init];
 halfPass->color=[[PassColor alloc] init];halfPass->color->target=[halfTarget retain];halfPass->color->load=2;halfPass->color->store=1;
 TeguCommand *halfCommand=[[halfQueue commandBuffer] retain];
 TeguRenderEncoder *halfEncoder=[halfCommand renderCommandEncoderWithDescriptor:(id)halfPass];assert(halfEncoder);
 [halfEncoder setRenderPipelineState:pipeline];[halfEncoder setVertexBytes:vertices length:sizeof(vertices) atIndex:1];
 [halfEncoder setFragmentTexture:half atIndex:0];[halfEncoder setFragmentSamplerState:halfSampler atIndex:0];
 TeguCommand *oldKernelCommand=[[halfQueue commandBuffer] retain];
 TeguRenderEncoder *oldKernelEncoder=[oldKernelCommand renderCommandEncoderWithDescriptor:(id)halfPass];assert(oldKernelEncoder);
 [oldKernelEncoder setRenderPipelineState:pipeline];[oldKernelEncoder setVertexBytes:vertices length:sizeof(vertices) atIndex:1];
 [oldKernelEncoder setFragmentTexture:half atIndex:0];[oldKernelEncoder setFragmentSamplerState:halfSampler atIndex:0];
 unsigned beforeOldKernel=textureDraws;device->capabilities&=~(U64)2048;
 [oldKernelEncoder drawPrimitives:3 vertexStart:0 vertexCount:3];[oldKernelEncoder endEncoding];[oldKernelCommand commit];
 assert([oldKernelCommand status]==5&&textureDraws==beforeOldKernel);
 [oldKernelCommand release];device->capabilities|=2048;
 [halfEncoder drawPrimitives:3 vertexStart:0 vertexCount:3];
 struct tegu_render_texture_request halfSnapshot=TEGU_JOB(halfCommand->jobChunks,0).textureRequest;
 U64 halfVertexHandle=halfSnapshot.vertex.buffers[1].handle,halfHandle=halfSnapshot.texture.handle;
 assert(halfSnapshot.version==2&&halfSnapshot.reserved[0]==115&&halfSnapshot.reserved[1]==128&&halfSnapshot.reserved[2]==2048&&
   halfSnapshot.texture.length==2048&&halfSnapshot.vertex.buffer_mask==2&&halfSnapshot.vertex.buffers[1].length==96);
 assert(TEGU_JOB(halfCommand->jobChunks,0).sampledTexture==half&&TEGU_JOB(halfCommand->jobChunks,0).fragmentSampler==halfSampler);
 /* Format115 sources are deliberately limited to RGBA8 target70 even though
  * the RGBA8-source pipeline above supports BGRA8 target80. */
 TextureDesc *halfBgraDesc=[[TextureDesc alloc] init];halfBgraDesc->w=halfBgraDesc->h=16;halfBgraDesc->format=80;halfBgraDesc->uses=4;
 TeguTexture *halfBgraTarget=[device newTextureWithDescriptor:(id)halfBgraDesc];assert(halfBgraTarget);
 RenderPass *halfBgraPass=[[RenderPass alloc] init];halfBgraPass->color=[[PassColor alloc] init];
 halfBgraPass->color->target=[halfBgraTarget retain];halfBgraPass->color->load=2;halfBgraPass->color->store=1;
 TeguQueue *halfBgraQueue=[device newCommandQueue];TeguCommand *halfBgraCommand=[[halfBgraQueue commandBuffer] retain];
 TeguRenderEncoder *halfBgraEncoder=[halfBgraCommand renderCommandEncoderWithDescriptor:(id)halfBgraPass];assert(halfBgraEncoder);
 [halfBgraEncoder setRenderPipelineState:pipeline80];[halfBgraEncoder setVertexBytes:vertices length:sizeof(vertices) atIndex:1];
 [halfBgraEncoder setFragmentTexture:half atIndex:0];[halfBgraEncoder setFragmentSamplerState:halfSampler atIndex:0];
 unsigned beforeHalfBgra=textureDraws;[halfBgraEncoder drawPrimitives:3 vertexStart:0 vertexCount:3];[halfBgraEncoder endEncoding];[halfBgraCommand commit];
 assert([halfBgraCommand status]==5&&textureDraws==beforeHalfBgra);
 [halfBgraCommand release];[halfBgraQueue release];[halfBgraPass release];[halfBgraTarget release];[halfBgraDesc release];
 [halfEncoder setFragmentTexture:0 atIndex:0];[halfEncoder endEncoding];
 assert(!memcmp(&halfSnapshot,&TEGU_JOB(halfCommand->jobChunks,0).textureRequest,sizeof(halfSnapshot)));
 [half release];[halfSampler release];[halfTarget release];[halfPass release];[halfDesc release];[halfTargetDesc release];[halfSamplerDesc release];
 drain();assert(memory[halfHandle]&&memory[halfVertexHandle]);
 [halfCommand commit];assert([halfCommand status]==4&&textureDraws==halfDraws+1);
 assert(lastTextureRequest.version==2&&!memcmp(&halfSnapshot,&lastTextureRequest,sizeof(halfSnapshot)));
 [halfCommand release];[halfQueue release];drain();
 assert(!memory[halfHandle]&&!memory[halfVertexHandle]&&[device currentAllocatedSize]==halfBaseline);
 device->capabilities=savedCapabilities;
 [pipeline release];[pipeline80 release];
 device->capabilities=savedCapabilities;
 assert(renderImports==importBaseline+2&&renderReleases==releaseBaseline+2&&[device currentAllocatedSize]==allocationBaseline);
 puts("PASS real AIR/Mali textured frontend: RGBA8 TGR5 byte-parity plus capability2048 RGBA16Float(115) source snapshot/stride128/2048-byte retained buffer; host transport only");
}
