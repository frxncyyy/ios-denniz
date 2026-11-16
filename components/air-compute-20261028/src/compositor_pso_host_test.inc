static void test_compositor_draw_snapshot(TeguComputeDevice *,const char *,const char *,TeguRenderPipeline *);
static void assert_compositor_mesa_golden(const char *name,const void *bytes,size_t length) {
 const char *dir=getenv("TEGU_COMPOSITOR_GOLDEN");assert(dir&&*dir);
 char path[1024];int n=snprintf(path,sizeof(path),"%s/%s",dir,name);assert(n>0&&(size_t)n<sizeof(path));
 FILE *f=fopen(path,"rb");assert(f);assert(fseek(f,0,SEEK_END)==0);long size=ftell(f);assert(size>=0&&(size_t)size==length);rewind(f);
 unsigned char *gold=malloc(length?length:1);assert(gold);assert(fread(gold,1,length,f)==length);fclose(f);
 assert(!memcmp(gold,bytes,length));free(gold);
}

static void test_attachment_clear_frontend(TeguComputeDevice *device,const char *path) {
 FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);assert(n>0);
 void *bytes=malloc((size_t)n);assert(bytes&&fread(bytes,1,(size_t)n,f)==(size_t)n);fclose(f);
 id error=0;TeguLibrary *lib=[device newTeguLibraryWithBytes:bytes length:(NSUInteger)n error:&error];free(bytes);assert(lib&&!error);
 RenderDescriptor *d=[[RenderDescriptor alloc] init];
 d->vertex=[lib newFunctionWithName:[NSString stringWithUTF8String:"path_blit_vert_lph"]];
 d->fragment=[lib newFunctionWithName:[NSString stringWithUTF8String:"attachment_clear_frag_lph"]];assert(d->vertex&&d->fragment);
 d->colors=[[RenderColors alloc] init];for(unsigned i=0;i<8;i++)d->colors->colors[i]=[[RenderColor alloc] init];
 d->colors->colors[0]->format=80;d->colors->colors[0]->mask=0;d->colors->colors[1]->format=80;d->colors->colors[1]->mask=15;
 d->samples=1;d->raster=1;d->amplification=1;
 RenderLayout *layout=[[RenderLayout alloc] init];d->layout=layout;layout->buffers=[[RenderLayout alloc] init];
 const unsigned format[4]={31,29,29,27},offset[4]={0,16,24,32};
 for(unsigned i=0;i<4;i++){layout->entries[i]=[[RenderAttribute alloc] init];layout->entries[i]->value=format[i];layout->entries[i]->byteOffset=offset[i];layout->entries[i]->bufferSlot=1;}
 layout->buffers->entries[1]=[[RenderAttribute alloc] init];layout->buffers->entries[1]->layoutStride=48;
 layout->buffers->entries[1]->step=1;layout->buffers->entries[1]->rate=1;
 U64 old=device->capabilities;device->capabilities=old|TEGU_GPU_CAP_COMPOSITOR|TEGU_GPU_CAP_ATTACHMENT_CLEAR;
 unsigned priorImports=renderImports,priorReleases=renderReleases;attachmentClearPsoMode=1;
 TeguRenderPipeline *pipeline=[device newRenderPipelineStateWithDescriptor:d error:&error];
 assert(pipeline&&!error&&pipeline->attachmentClearSnapshotOnly&&pipeline->handle==0xc0000001u);
 /* Structural package refusals remain distinguishable on device, before the
  * six frozen identity hashes are consulted. */
 unsigned char dummyByte=0;TeguAIRBlob dummy={&dummyByte,1};
 struct tegu_mali_graphics badVertex=pipeline->vertexCode,badFragment=pipeline->fragmentCode;
 struct tegu_mali_attachment_clear_plan_v2 badPlan=pipeline->attachmentClearPlan;
 unsigned char *badPackage=0;NSUInteger badPackageBytes=0;
 assert(tegu_attachment_clear_make_package(lib->sourceSHA256,tegu_attachment_clear_profile,
  sizeof(tegu_attachment_clear_profile),&dummy,&dummy,&pipeline->vertexCode,&pipeline->fragmentCode,
  &pipeline->attachmentClearPlan,&badPackage,0)==-5);
 badPackage=(unsigned char *)1;
 assert(tegu_attachment_clear_make_package(lib->sourceSHA256,tegu_attachment_clear_profile,
  sizeof(tegu_attachment_clear_profile),&dummy,&dummy,&pipeline->vertexCode,&pipeline->fragmentCode,
  &pipeline->attachmentClearPlan,&badPackage,&badPackageBytes)==-6);badPackage=0;badPackageBytes=1;
 assert(tegu_attachment_clear_make_package(lib->sourceSHA256,tegu_attachment_clear_profile,
  sizeof(tegu_attachment_clear_profile),&dummy,&dummy,&pipeline->vertexCode,&pipeline->fragmentCode,
  &pipeline->attachmentClearPlan,&badPackage,&badPackageBytes)==-7);badPackageBytes=0;
 assert(tegu_attachment_clear_make_package(0,tegu_attachment_clear_profile,sizeof(tegu_attachment_clear_profile),
  &dummy,&dummy,&pipeline->vertexCode,&pipeline->fragmentCode,&pipeline->attachmentClearPlan,
  &badPackage,&badPackageBytes)==-8);
 assert(tegu_attachment_clear_make_package(lib->sourceSHA256,tegu_attachment_clear_profile,
  sizeof(tegu_attachment_clear_profile),0,&dummy,&pipeline->vertexCode,&pipeline->fragmentCode,
  &pipeline->attachmentClearPlan,&badPackage,&badPackageBytes)==-9);
 assert(tegu_attachment_clear_make_package(lib->sourceSHA256,tegu_attachment_clear_profile,
  sizeof(tegu_attachment_clear_profile),&dummy,0,&pipeline->vertexCode,&pipeline->fragmentCode,
  &pipeline->attachmentClearPlan,&badPackage,&badPackageBytes)==-16);
 badVertex.size++;
 assert(tegu_attachment_clear_make_package(lib->sourceSHA256,tegu_attachment_clear_profile,
  sizeof(tegu_attachment_clear_profile),&dummy,&dummy,&badVertex,&pipeline->fragmentCode,
  &pipeline->attachmentClearPlan,&badPackage,&badPackageBytes)==-2&&!badPackage&&!badPackageBytes);
 badFragment.size++;
 assert(tegu_attachment_clear_make_package(lib->sourceSHA256,tegu_attachment_clear_profile,
  sizeof(tegu_attachment_clear_profile),&dummy,&dummy,&pipeline->vertexCode,&badFragment,
  &pipeline->attachmentClearPlan,&badPackage,&badPackageBytes)==-3&&!badPackage&&!badPackageBytes);
 badPlan.resource_count++;
 assert(tegu_attachment_clear_make_package(lib->sourceSHA256,tegu_attachment_clear_profile,
  sizeof(tegu_attachment_clear_profile),&dummy,&dummy,&pipeline->vertexCode,&pipeline->fragmentCode,
  &badPlan,&badPackage,&badPackageBytes)==-4&&!badPackage&&!badPackageBytes);
 TextureDesc *td=[[TextureDesc alloc] init];td->format=80;td->w=776;td->h=24;td->uses=4;
 TeguTexture *rt0=[device newTextureWithDescriptor:(id)td],*rt1=[device newTextureWithDescriptor:(id)td];
 assert(rt0&&rt1&&rt0->rowBytes==3136&&rt1->rowBytes==3136);
 RenderPass *pass=[[RenderPass alloc] init];pass->color=[[PassColor alloc] init];pass->second=[[PassColor alloc] init];
 pass->color->target=[rt0 retain];pass->color->load=0;pass->color->store=0;
 pass->second->target=[rt1 retain];pass->second->load=2;pass->second->store=1;
 pass->passWidth=776;pass->passHeight=24;pass->arrayLength=1;pass->sampleCount=1;
 TeguBuffer *rect=[device newBufferWithLength:64 options:0],*matrix=[device newBufferWithLength:512 options:0];assert(rect&&matrix);
 TeguQueue *queue=[device newCommandQueue];
 /* The frozen TAC1 shader writes only RT1; with RT1 StoreDontCare its result
  * is unobservable. Accept and complete this as a zero-job command. */
 pass->second->store=0;TeguCommand *bad=[queue commandBuffer];
 TeguRenderEncoder *badEncoder=[bad renderCommandEncoderWithDescriptor:(id)pass];assert(badEncoder);
 [badEncoder setRenderPipelineState:pipeline];assert(!bad->invalid);
 [badEncoder setVertexBuffer:rect offset:4 atIndex:1];[badEncoder setVertexBuffer:matrix offset:16 atIndex:2];
 [badEncoder drawPrimitives:3 vertexStart:0 vertexCount:3];assert(!bad->invalid&&!bad->count);
 [badEncoder endEncoding];assert(!bad->invalid&&!bad->count);
 for(unsigned load=0;load<=2;load+=2) {
  pass->second->load=load;TeguCommand *noop=[queue commandBuffer];
  TeguRenderEncoder *noopEncoder=[noop renderCommandEncoderWithDescriptor:(id)pass];assert(noopEncoder);
  [noopEncoder setRenderPipelineState:pipeline];assert(!noop->invalid);
  [noopEncoder setVertexBuffer:rect offset:4 atIndex:1];[noopEncoder setVertexBuffer:matrix offset:16 atIndex:2];
  [noopEncoder drawPrimitives:3 vertexStart:0 vertexCount:3];[noopEncoder endEncoding];
  assert(!noop->invalid&&!noop->count&&(!noop->jobChunks[0]||(!TEGU_JOB(noop->jobChunks,0).attachmentClearSnapshot&&!TEGU_JOB(noop->jobChunks,0).renderPipeline)));
  [noop commit];assert([noop status]==4&&![noop error]&&!attachmentClearDraws);
 }
 [bad commit];assert([bad status]==4&&![bad error]&&!attachmentClearDraws);pass->second->load=2;
 /* Loads outside the TAC1 contract and unknown store actions still refuse. */
 pass->second->load=1;bad=[queue commandBuffer];badEncoder=[bad renderCommandEncoderWithDescriptor:(id)pass];assert(badEncoder);
 [badEncoder setRenderPipelineState:pipeline];assert(bad->invalid&&!bad->count);[badEncoder endEncoding];pass->second->load=2;
 pass->second->store=2;bad=[queue commandBuffer];badEncoder=[bad renderCommandEncoderWithDescriptor:(id)pass];assert(badEncoder);
 [badEncoder setRenderPipelineState:pipeline];assert(bad->invalid&&!bad->count);[badEncoder endEncoding];pass->second->store=1;
 /* RT0's attachment actions remain an exact part of the recognized pass. */
 pass->color->load=0;pass->color->store=1;bad=[queue commandBuffer];badEncoder=[bad renderCommandEncoderWithDescriptor:(id)pass];assert(badEncoder);
 [badEncoder setRenderPipelineState:pipeline];assert(bad->invalid&&!bad->count);[badEncoder endEncoding];pass->color->store=0;
 pass->passWidth=775;bad=[queue commandBuffer];
 assert(![bad renderCommandEncoderWithDescriptor:(id)pass]&&bad->invalid&&!bad->count);pass->passWidth=776;
 U64 exactCaps=device->capabilities;device->capabilities&=~TEGU_GPU_CAP_ATTACHMENT_CLEAR;bad=[queue commandBuffer];
 badEncoder=[bad renderCommandEncoderWithDescriptor:(id)pass];assert(badEncoder);[badEncoder setRenderPipelineState:pipeline];
 assert(bad->invalid&&!bad->count);[badEncoder endEncoding];device->capabilities=exactCaps;
 bad=[queue commandBuffer];badEncoder=[bad renderCommandEncoderWithDescriptor:(id)pass];assert(badEncoder);
 [badEncoder setRenderPipelineState:pipeline];[badEncoder setVertexBuffer:rect offset:4 atIndex:1];
 [badEncoder setVertexBuffer:matrix offset:208 atIndex:2];[badEncoder drawPrimitives:3 vertexStart:0 vertexCount:3];
 assert(bad->invalid&&!bad->count);[badEncoder endEncoding];
 TeguTexture *realSecond=pass->second->target;pass->second->target=[rt0 retain];[realSecond release];
 bad=[queue commandBuffer];badEncoder=[bad renderCommandEncoderWithDescriptor:(id)pass];assert(badEncoder);
 [badEncoder setRenderPipelineState:pipeline];[badEncoder setVertexBuffer:rect offset:4 atIndex:1];
 [badEncoder setVertexBuffer:matrix offset:16 atIndex:2];[badEncoder drawPrimitives:3 vertexStart:0 vertexCount:3];
 assert(bad->invalid&&!bad->count);[badEncoder endEncoding];
 [pass->second->target release];pass->second->target=[realSecond retain];
 TeguCommand *command=[queue commandBuffer];
 TeguRenderEncoder *encoder=[command renderCommandEncoderWithDescriptor:(id)pass];assert(encoder);
 [encoder setRenderPipelineState:pipeline];[encoder setVertexBuffer:rect offset:4 atIndex:1];[encoder setVertexBuffer:matrix offset:16 atIndex:2];
 [encoder drawPrimitives:3 vertexStart:0 vertexCount:3];[encoder endEncoding];
 assert(command->count==1&&TEGU_JOB(command->jobChunks,0).attachmentClearSnapshot&&TEGU_JOB(command->jobChunks,0).attachmentClearTargets[0]==rt0&&
  TEGU_JOB(command->jobChunks,0).attachmentClearTargets[1]==rt1&&TEGU_JOB(command->jobChunks,0).attachmentClearBuffers[0]==rect&&TEGU_JOB(command->jobChunks,0).attachmentClearBuffers[1]==matrix);
 /* Snapshot ownership must survive descriptor/application releases. */
 [pass release];[rt0 release];[rt1 release];[rect release];[matrix release];[pipeline release];
 [command commit];assert([command status]==4&&![command error]&&attachmentClearDraws==1);
 [queue release];[td release];drain();attachmentClearPsoMode=0;
 assert(renderImports==priorImports+1&&renderReleases==priorReleases+1);
 device->capabilities=old;[d release];[lib release];
}

static void test_compositor_native_frontend(TeguComputeDevice *device,const char *path) {
 FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);assert(n>0);
 void *bytes=malloc((size_t)n);assert(bytes&&fread(bytes,1,(size_t)n,f)==(size_t)n);fclose(f);
 id error=0;TeguLibrary *lib=[device newTeguLibraryWithBytes:bytes length:(NSUInteger)n error:&error];free(bytes);assert(lib&&!error);
 NSString *vn=[NSString stringWithUTF8String:"downsample_blur_vert_lph"];
 NSString *fn=[NSString stringWithUTF8String:"downsample_4_frag_lph"];
 RenderDescriptor *d=[[RenderDescriptor alloc] init];d->vertex=[lib newFunctionWithName:vn];d->fragment=[lib newFunctionWithName:fn];assert(d->vertex&&d->fragment);
 d->colors=[[RenderColors alloc] init];d->colors->colors[0]=[[RenderColor alloc] init];d->colors->colors[1]=[[RenderColor alloc] init];
 d->colors->colors[0]->format=80;d->colors->colors[0]->mask=15;d->colors->colors[1]->format=80;d->colors->colors[1]->mask=0;
 for(unsigned i=2;i<8;i++){d->colors->colors[i]=[[RenderColor alloc] init];d->colors->colors[i]->format=0;d->colors->colors[i]->mask=15;}
 d->samples=1;d->raster=1;d->amplification=1;d->topology=0;
 RenderLayout *layout=[[RenderLayout alloc] init];d->layout=layout;
 for(unsigned i=0;i<4;i++)layout->entries[i]=[[RenderAttribute alloc] init];
 layout->buffers=[[RenderLayout alloc] init];layout->buffers->entries[0]=[[RenderAttribute alloc] init];layout->buffers->entries[1]=[[RenderAttribute alloc] init];
 const unsigned format[4]={31,29,29,27},offset[4]={0,16,24,32},strides[3]={32,40,48};
 U64 old=device->capabilities;device->capabilities=old|TEGU_GPU_CAP_COMPOSITOR;
 unsigned before=renderImports;
 /* Exercise the complete supported wire class: original Metal slots and
  * every qualified stride. The live four-row descriptor remains exact. */
 for(unsigned slot=0;slot<2;slot++)for(unsigned si=0;si<3;si++) {
  unsigned stride=strides[si];
  for(unsigned i=0;i<4;i++){layout->entries[i]->value=format[i];layout->entries[i]->byteOffset=offset[i];layout->entries[i]->bufferSlot=slot;}
  layout->buffers->entries[1u-slot]->layoutStride=0;
  layout->buffers->entries[slot]->layoutStride=stride;layout->buffers->entries[slot]->step=1;layout->buffers->entries[slot]->rate=1;
  error=0;TeguRenderPipeline *p=[device newRenderPipelineStateWithDescriptor:d error:&error];
  unsigned ordinal=slot*3+si+1;
  assert(p&&!error&&p->compositorV6&&renderImports==before+ordinal&&p->pair.version==2);
  assert_compositor_mesa_golden("vs.mali",p->vertexCode.code,p->vertexCode.size);
  assert_compositor_mesa_golden("fs.mali",p->fragmentCode.code,p->fragmentCode.size);
  struct tegu_render_info parsed;assert(tegu_render_parse_for_import(p->package,p->packageBytes,1,&parsed)==0&&parsed.version==6);
  assert(parsed.color_count==2&&parsed.vertex[0].stride==stride&&parsed.vertex[1].stride==stride&&
         parsed.vertex[0].buffer_index==slot&&parsed.vertex[1].buffer_index==slot);
  assert(parsed.vertex[0].format==109&&parsed.vertex[1].format==103&&p->compositorResources.version==1);
  test_compositor_draw_snapshot(device,path,0,p);
  [p release];
 }
 /* Inactive descriptor rows remain irrelevant when they carry valid but
  * different Metal buffer indices; both blend classes exercise this shape. */
 for(unsigned i=0;i<4;i++){layout->entries[i]->value=format[i];layout->entries[i]->byteOffset=offset[i];}
 layout->entries[0]->bufferSlot=layout->entries[1]->bufferSlot=0;
 layout->entries[2]->bufferSlot=layout->entries[3]->bufferSlot=1;
 layout->buffers->entries[0]->layoutStride=40;layout->buffers->entries[0]->step=1;layout->buffers->entries[0]->rate=1;
 layout->buffers->entries[1]->layoutStride=40;layout->buffers->entries[1]->step=1;layout->buffers->entries[1]->rate=1;
 error=0;TeguRenderPipeline *inactive=[device newRenderPipelineStateWithDescriptor:d error:&error];
 assert(inactive&&!error&&inactive->compositorV6&&renderImports==before+7);
 struct tegu_render_info inactiveInfo;assert(!tegu_render_parse_for_import(inactive->package,inactive->packageBytes,1,&inactiveInfo));
 assert(inactiveInfo.vertex_count==2&&inactiveInfo.vertex[0].buffer_index==0&&inactiveInfo.vertex[1].buffer_index==0);
 test_compositor_draw_snapshot(device,path,0,inactive);[inactive release];
 /* Preserve the previous qualified three-row descriptor with inactive attr3. */
 layout->entries[0]->value=31;layout->entries[0]->byteOffset=0;layout->entries[0]->bufferSlot=0;
 layout->entries[1]->value=29;layout->entries[1]->byteOffset=16;layout->entries[1]->bufferSlot=0;
 layout->entries[2]->value=0;layout->entries[2]->byteOffset=0;layout->entries[2]->bufferSlot=0;
 layout->entries[3]->value=30;layout->entries[3]->byteOffset=0;layout->entries[3]->bufferSlot=0;
 layout->buffers->entries[1]->layoutStride=0;
 layout->buffers->entries[0]->layoutStride=40;layout->buffers->entries[0]->step=1;layout->buffers->entries[0]->rate=1;
 error=0;TeguRenderPipeline *p=[device newRenderPipelineStateWithDescriptor:d error:&error];
 assert(p&&!error&&p->compositorV6&&renderImports==before+8);
 struct tegu_render_info parsed;assert(tegu_render_parse_for_import(p->package,p->packageBytes,1,&parsed)==0&&
   parsed.vertex_count==2&&parsed.vertex[0].buffer_index==0&&parsed.vertex[0].stride==40);
 test_compositor_draw_snapshot(device,path,0,p);[p release];
 /* The captured QuartzCore descriptor's premultiplied source-over class must
  * flow through the additive per-attachment C ABI and TGR6 import for every
  * supported live slot/stride pair. */
 RenderColor *blendColor=d->colors->colors[0];blendColor->blend=1;blendColor->rgbOp=blendColor->alphaOp=0;
 blendColor->rgbSrc=blendColor->alphaSrc=1;blendColor->rgbDst=blendColor->alphaDst=5;
 unsigned blendImportStart=renderImports;
 device->capabilities=old|TEGU_GPU_CAP_COMPOSITOR;
 error=0;assert(![device newRenderPipelineStateWithDescriptor:d error:&error]&&error&&renderImports==blendImportStart); /* missing source-over capability */
 device->capabilities=old|TEGU_GPU_CAP_COMPOSITOR|64;
 blendColor->rgbDst=blendColor->alphaDst=4;error=0;
 assert(![device newRenderPipelineStateWithDescriptor:d error:&error]&&error&&renderImports==blendImportStart); /* unsupported factor */
 assert([[error localizedDescription] isEqualToString:[NSString stringWithUTF8String:"compositor blend state outside disabled or exact premultiplied source-over class"]]);
 blendColor->rgbDst=blendColor->alphaDst=5;blendColor->rgbOp=1;error=0;
 assert(![device newRenderPipelineStateWithDescriptor:d error:&error]&&error&&renderImports==blendImportStart); /* unsupported equation */
 blendColor->rgbOp=0;RenderColor *inactiveBlend=d->colors->colors[1];inactiveBlend->blend=1;error=0;
 assert(![device newRenderPipelineStateWithDescriptor:d error:&error]&&error&&renderImports==blendImportStart); /* enabled zero-mask MRT target */
 inactiveBlend->blend=0;
 device->capabilities=old|TEGU_GPU_CAP_COMPOSITOR|64;
 TeguRenderPipeline *firstBlendPipeline=0;
 for(unsigned slot=0;slot<2;slot++)for(unsigned si=0;si<3;si++) {
  unsigned stride=strides[si];
  for(unsigned i=0;i<4;i++){layout->entries[i]->value=format[i];layout->entries[i]->byteOffset=offset[i];layout->entries[i]->bufferSlot=slot;}
  layout->buffers->entries[1u-slot]->layoutStride=0;
  layout->buffers->entries[slot]->layoutStride=stride;layout->buffers->entries[slot]->step=1;layout->buffers->entries[slot]->rate=1;
  error=0;p=[device newRenderPipelineStateWithDescriptor:d error:&error];
  assert(p&&!error&&p->compositorV6&&renderImports==before+9+slot*3+si);
  struct tegu_render_info blended;assert(!tegu_render_parse_for_import(p->package,p->packageBytes,1,&blended));
  assert(blended.version==6&&blended.blend.enabled==1&&blended.blend.rgb_src==1&&blended.blend.rgb_dst==5&&
   blended.blend.alpha_src==1&&blended.blend.alpha_dst==5&&blended.blend.write_mask==15);
  assert(tegu_render_word(p->package+128)==1&&tegu_render_word(p->package+132)==1&&
   tegu_render_word(p->package+136)==0&&tegu_render_word(p->package+140)==1&&
   tegu_render_word(p->package+144)==5&&tegu_render_word(p->package+152)==1&&tegu_render_word(p->package+156)==5);
  assert(p->vertexFetch.attributes[0].buffer_index==slot&&p->vertexFetch.attributes[0].stride==stride);
  if(slot==0&&si==0)firstBlendPipeline=[p retain];
  test_compositor_draw_snapshot(device,path,0,p);[p release];
 }
 /* Compile a second real source-over PSO with a different reflected vertex
  * stride, then exercise both through one ordered encoder. */
 assert(firstBlendPipeline);
 for(unsigned i=0;i<4;i++)layout->entries[i]->bufferSlot=0;
 layout->buffers->entries[1]->layoutStride=0;
 layout->buffers->entries[0]->layoutStride=40;
 error=0;TeguRenderPipeline *secondBlendPipeline=[device newRenderPipelineStateWithDescriptor:d error:&error];
 assert(secondBlendPipeline&&!error&&secondBlendPipeline->compositorV6&&renderImports==before+15);
 test_compositor_draw_snapshot(device,path,0,firstBlendPipeline);
 test_compositor_multidraw_actual(device,firstBlendPipeline,secondBlendPipeline,0,0,0);
 test_compositor_multidraw_actual(device,firstBlendPipeline,secondBlendPipeline,0,0,16);
 test_compositor_legacy_mix_refusal(device,firstBlendPipeline);
 test_compositor_multidraw_actual(device,firstBlendPipeline,secondBlendPipeline,0,1,0);
 test_compositor_multidraw_actual(device,firstBlendPipeline,secondBlendPipeline,1,0,0);
 [secondBlendPipeline release];[firstBlendPipeline release];
 for(unsigned i=0;i<4;i++){layout->entries[i]->value=format[i];layout->entries[i]->byteOffset=offset[i];}
 layout->entries[0]->bufferSlot=layout->entries[1]->bufferSlot=0;
 layout->entries[2]->bufferSlot=layout->entries[3]->bufferSlot=1;
 layout->buffers->entries[0]->layoutStride=40;layout->buffers->entries[0]->step=1;layout->buffers->entries[0]->rate=1;
 layout->buffers->entries[1]->layoutStride=40;layout->buffers->entries[1]->step=1;layout->buffers->entries[1]->rate=1;
 error=0;p=[device newRenderPipelineStateWithDescriptor:d error:&error];
 assert(p&&!error&&p->compositorV6&&renderImports==before+16);
 struct tegu_render_info sourceInactive;assert(!tegu_render_parse_for_import(p->package,p->packageBytes,1,&sourceInactive));
 assert(sourceInactive.blend.enabled&&sourceInactive.vertex_count==2&&sourceInactive.vertex[0].buffer_index==0&&sourceInactive.vertex[1].buffer_index==0);
 test_compositor_draw_snapshot(device,path,0,p);[p release];
 blendColor->blend=0;device->capabilities=old|TEGU_GPU_CAP_COMPOSITOR;
 /* Capability, step, attachment, and stride failures must still refuse before import. */
 layout->entries[0]->bufferSlot=layout->entries[1]->bufferSlot=0;
 layout->buffers->entries[1]->layoutStride=0;layout->buffers->entries[0]->layoutStride=40;
 device->capabilities=old;before=renderImports;error=0;assert(![device newRenderPipelineStateWithDescriptor:d error:&error]&&error&&renderImports==before);
 device->capabilities=old|TEGU_GPU_CAP_COMPOSITOR;
 layout->buffers->entries[0]->step=0;error=0;
 assert(![device newRenderPipelineStateWithDescriptor:d error:&error]&&error&&renderImports==before);
 layout->buffers->entries[0]->step=1;
 d->colors->colors[1]->mask=15;error=0;assert(![device newRenderPipelineStateWithDescriptor:d error:&error]&&error&&renderImports==before);
 d->colors->colors[1]->mask=0;layout->buffers->entries[0]->layoutStride=36;error=0;
 assert(![device newRenderPipelineStateWithDescriptor:d error:&error]&&error&&renderImports==before);
 /* Error-after-import with a valid owned reply must release that handle. */
 device->capabilities=old|TEGU_GPU_CAP_COMPOSITOR|256;layout->buffers->entries[0]->layoutStride=40;
 unsigned priorImports=renderImports,priorReleases=renderReleases;NSUInteger allocated=[device currentAllocatedSize];
 compositorPartialImport=1;error=0;
 assert(![device newRenderPipelineStateWithDescriptor:d error:&error]&&error);
 assert(strstr([[error localizedDescription] UTF8String],"status=0xffffffff outputs=0 handle=0x"));
 compositorPartialImport=0;
 assert(renderImports==priorImports+1&&renderReleases==priorReleases+1&&[device currentAllocatedSize]==allocated);
 /* Preserve the exact IOKit refusal and transfer size in the durable NSError
  * path, including failures before the kernel creates any owned handle. */
 priorImports=renderImports;priorReleases=renderReleases;
 failImport=1;failImportStatus=(int)0xe00002c2u;error=0;
 assert(![device newRenderPipelineStateWithDescriptor:d error:&error]&&error);
 assert([[error localizedDescription] isEqualToString:[NSString stringWithUTF8String:
  "kernel compositor pipeline import failed status=0xe00002c2 outputs=1 handle=0x0 bytes=10240"]]);
 assert(renderImports==priorImports&&renderReleases==priorReleases&&[device currentAllocatedSize]==allocated);
 failImport=0;failImportStatus=0;
 device->capabilities=old;[d release];[lib release];
}
