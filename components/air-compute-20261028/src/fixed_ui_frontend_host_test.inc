struct TeguFixedUIConstantRow {const char *name;uint32_t type,size;uint8_t data[3];};
static const struct TeguFixedUIConstantRow tegu_fixed_ui_vs_constants[]={
 {"fc_generic",53,1,{0x00}},
 {"fc_texcoord_count0",49,1,{0x00}},
 {"fc_texcoord_count1",49,1,{0x00}},
 {"fc_vertex_layout",49,1,{0x00}},
};
static const struct TeguFixedUIConstantRow tegu_fixed_ui_fs_constants[]={
 {"fc_attachment_count",49,1,{0x01}},
 {"fc_blend_function",49,1,{0x36}},
 {"fc_clamp_color",53,1,{0x00}},
 {"fc_color_inputs0",49,1,{0x00}},
 {"fc_color_inputs1",49,1,{0x00}},
 {"fc_color_opaque0",53,1,{0x00}},
 {"fc_color_opaque1",53,1,{0x00}},
 {"fc_color_opcodes0",51,3,{0x00,0x00,0x00}},
 {"fc_color_opcodes1",51,3,{0x00,0x00,0x00}},
 {"fc_coord_function0",49,1,{0x00}},
 {"fc_coord_function1",49,1,{0x00}},
 {"fc_debug_xr",53,1,{0x00}},
 {"fc_dest_count",49,1,{0x00}},
 {"fc_dest_index",49,1,{0x00}},
 {"fc_destination_function",49,1,{0x00}},
 {"fc_extended_range",53,1,{0x00}},
 {"fc_filter0",49,1,{0x00}},
 {"fc_filter1",49,1,{0x00}},
 {"fc_framebuffer_fetch",53,1,{0x01}},
 {"fc_framebuffer_sample",53,1,{0x00}},
 {"fc_generic",53,1,{0x00}},
 {"fc_has_asg_77_weights",53,1,{0x00}},
 {"fc_has_attachment_0",53,1,{0x00}},
 {"fc_has_attachment_1",53,1,{0x00}},
 {"fc_has_attachment_2",53,1,{0x00}},
 {"fc_has_color_cube0",53,1,{0x00}},
 {"fc_has_color_cube1",53,1,{0x00}},
 {"fc_has_color_params",53,1,{0x00}},
 {"fc_has_color_trc0",53,1,{0x00}},
 {"fc_has_color_trc1",53,1,{0x00}},
 {"fc_has_edr_factor",53,1,{0x00}},
 {"fc_has_gamma_luts",53,1,{0x00}},
 {"fc_has_hdr_scale",53,1,{0x00}},
 {"fc_has_image_0A",53,1,{0x00}},
 {"fc_has_image_0B",53,1,{0x00}},
 {"fc_has_image_0C",53,1,{0x00}},
 {"fc_has_image_1A",53,1,{0x00}},
 {"fc_has_image_1B",53,1,{0x00}},
 {"fc_has_image_1C",53,1,{0x00}},
 {"fc_has_lod_bias",53,1,{0x00}},
 {"fc_has_lut_tex",53,1,{0x00}},
 {"fc_has_noise_scale",53,1,{0x00}},
 {"fc_has_noise_tex",53,1,{0x00}},
 {"fc_has_premultiply",53,1,{0x00}},
 {"fc_has_ycbcr_matrix",53,1,{0x00}},
 {"fc_image_count",49,1,{0x00}},
 {"fc_image_function0",49,1,{0x00}},
 {"fc_image_function1",49,1,{0x00}},
 {"fc_memoryless_index0",49,1,{0x00}},
 {"fc_memoryless_index1",49,1,{0x00}},
 {"fc_noise_width",49,1,{0x00}},
 {"fc_plane_count0",49,1,{0x00}},
 {"fc_plane_count1",49,1,{0x00}},
 {"fc_texcoord_count0",49,1,{0x00}},
 {"fc_texcoord_count1",49,1,{0x00}},
 {"fc_texture_function",49,1,{0x00}},
 {"fc_unbounded_range",53,1,{0x00}},
 {"fc_vertex_layout",49,1,{0x00}},
};

static TeguFunction *tegu_fixed_ui_test_function(TeguLibrary *base,const char *name,
 const struct TeguFixedUIConstantRow *rows,size_t count,unsigned kind) {
 TeguAIRFunctionConstant *constants=calloc(count,sizeof(*constants));assert(constants);
 for(size_t i=0;i<count;i++) {
  constants[i].kind=1;constants[i].type=rows[i].type;constants[i].size=rows[i].size;
  constants[i].name=(const uint8_t *)rows[i].name;constants[i].name_len=strlen(rows[i].name);
  memcpy(constants[i].data,rows[i].data,rows[i].size);
 }
 char message[512]={0};TeguAIRLibrary *specialized=tegu_air_library_specialize(base->library,
  (const uint8_t *)name,strlen(name),constants,count,message,sizeof(message));free(constants);
 if(!specialized)fprintf(stderr,"fixed UI specialization %s: %s\n",name,message);assert(specialized);
 TeguLibrary *snapshot=[[TeguLibrary alloc] init];assert(snapshot);snapshot->owner=[base->owner retain];
 snapshot->library=specialized;memcpy(snapshot->sourceSHA256,base->sourceSHA256,sizeof(snapshot->sourceSHA256));
 TeguFunction *function=[[TeguFunction alloc] init];assert(function);function->library=snapshot;
 function->functionName=[[NSString stringWithUTF8String:name] retain];function->kind=kind;return function;
}
static void test_fixed_ui_native_frontend(TeguComputeDevice *device,const char *path) {
 FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);rewind(f);assert(n>0);
 void *bytes=malloc((size_t)n);assert(bytes&&fread(bytes,1,(size_t)n,f)==(size_t)n);fclose(f);
 id error=0;TeguLibrary *lib=[device newTeguLibraryWithBytes:bytes length:(NSUInteger)n error:&error];free(bytes);assert(lib&&!error);
 RenderDescriptor *d=[[RenderDescriptor alloc] init];
 d->vertex=tegu_fixed_ui_test_function(lib,"fixed_vert_lph_spc",tegu_fixed_ui_vs_constants,
  sizeof(tegu_fixed_ui_vs_constants)/sizeof(tegu_fixed_ui_vs_constants[0]),1);
 d->fragment=tegu_fixed_ui_test_function(lib,"fixed_frag_lph_cph",tegu_fixed_ui_fs_constants,
  sizeof(tegu_fixed_ui_fs_constants)/sizeof(tegu_fixed_ui_fs_constants[0]),2);
 d->colors=[[RenderColors alloc] init];for(unsigned i=0;i<8;i++)d->colors->colors[i]=[[RenderColor alloc] init];
 d->colors->colors[0]->format=80;d->colors->colors[0]->mask=15;
 d->samples=1;d->raster=1;d->amplification=1;
 RenderLayout *layout=[[RenderLayout alloc] init];d->layout=layout;layout->buffers=[[RenderLayout alloc] init];
 const unsigned formats[4]={31,29,29,27},offsets[4]={0,16,24,32};
 for(unsigned i=0;i<4;i++){layout->entries[i]=[[RenderAttribute alloc] init];layout->entries[i]->value=formats[i];layout->entries[i]->byteOffset=offsets[i];layout->entries[i]->bufferSlot=1;}
 layout->buffers->entries[1]=[[RenderAttribute alloc] init];layout->buffers->entries[1]->layoutStride=48;
 layout->buffers->entries[1]->step=1;layout->buffers->entries[1]->rate=1;
 U64 oldCaps=device->capabilities;device->capabilities=oldCaps|TEGU_GPU_CAP_COMPOSITOR|256;
 unsigned importsBefore=renderImports,drawsBefore=fixedUiDraws;fixedUiPsoMode=1;
 TeguRenderPipeline *pipeline=[device newRenderPipelineStateWithDescriptor:d error:&error];
 assert(pipeline&&!error&&pipeline->fixedUiSnapshotOnly&&pipeline->handle&&pipeline->package&&pipeline->packageBytes==5888&&
  pipeline->vertexCode.size==4096&&pipeline->fragmentCode.size==1024&&renderImports==importsBefore+1);
 struct tegu_ui1_info info;assert(!tegu_ui1_parse(pipeline->package,pipeline->packageBytes,&info)&&info.version==1&&
  info.target_format==80&&info.attributes[0].format==109&&info.attributes[1].format==97);
 TextureDesc *td=[[TextureDesc alloc] init];td->format=80;td->w=td->h=16;td->uses=4;
 TeguTexture *target=[device newTextureWithDescriptor:(id)td];assert(target);
 RenderPass *pass=[[RenderPass alloc] init];pass->color=[[PassColor alloc] init];pass->color->target=[target retain];
 pass->color->load=2;pass->color->store=1;
 TeguBuffer *vertex=[device newBufferWithLength:192 options:0];
 TeguBuffer *v2=[device newBufferWithLength:512 options:0];TeguBuffer *f1=[device newBufferWithLength:512 options:0];assert(vertex&&v2&&f1);
 TeguQueue *queue=[device newCommandQueue];TeguCommand *command=[[queue commandBuffer] retain];
 TeguRenderEncoder *encoder=[[command renderCommandEncoderWithDescriptor:(id)pass] retain];assert(encoder);
 [encoder setRenderPipelineState:pipeline];[encoder setVertexBuffer:vertex offset:0 atIndex:1];
 [encoder setVertexBuffer:v2 offset:19 atIndex:2];[encoder setFragmentBuffer:f1 offset:277 atIndex:1];
 [encoder drawPrimitives:3 vertexStart:0 vertexCount:3];assert(!command->invalid&&command->count==1);
 [encoder endEncoding];[command commit];[command waitUntilCompleted];assert([command status]==4&&![command error]&&fixedUiDraws==drawsBefore+1);
 [encoder release];[command release];
 uint16_t indices[6]={0,1,2,2,1,3};TeguBuffer *ib=[device newBufferWithBytes:indices length:sizeof(indices) options:0];
 command=[[queue commandBuffer] retain];encoder=[[command renderCommandEncoderWithDescriptor:(id)pass] retain];assert(encoder);
 [encoder setRenderPipelineState:pipeline];[encoder setVertexBuffer:vertex offset:0 atIndex:1];
 [encoder setVertexBuffer:v2 offset:19 atIndex:2];[encoder setFragmentBuffer:f1 offset:277 atIndex:1];
 [encoder drawIndexedPrimitives:3 indexCount:6 indexType:0 indexBuffer:ib indexBufferOffset:0 instanceCount:1 baseVertex:0 baseInstance:0];
 assert(!command->invalid&&command->count==1&&TEGU_JOB(command->jobChunks,0).fixedUiSnapshot&&
  TEGU_JOB(command->jobChunks,0).compositorVertexFacts[1].rangeLength==288&&TEGU_JOB(command->jobChunks,0).compositorVertexBuffers[1]!=vertex);
 struct tegu_ui1_bind_request indexedRequest;U64 indexedArgs[4];
 assert(!tegu_fixed_ui_request_for_job(&TEGU_JOB(command->jobChunks,0),device,&indexedRequest,indexedArgs)&&indexedRequest.vertex_count==6&&
  indexedRequest.pass.load_action==2&&indexedRequest.pass.scissor_width==16&&indexedRequest.pass.scissor_height==16);
 /* R104 BackBoard: a second, twelve-index draw under a partial scissor is a LOAD job. */
 uint16_t twoQuads[12]={0,1,2,2,1,3,1,2,3,3,2,0};TeguBuffer *ib12=[device newBufferWithBytes:twoQuads length:sizeof(twoQuads) options:0];
 [encoder setScissorRect:(TeguScissorRect){2,3,8,9}];
 [encoder drawIndexedPrimitives:3 indexCount:12 indexType:0 indexBuffer:ib12 indexBufferOffset:0 instanceCount:1 baseVertex:0 baseInstance:0];
 assert(!command->invalid&&command->count==2&&TEGU_JOB(command->jobChunks,1).compositorVertexFacts[1].rangeLength==12u*48u);
 assert(!tegu_fixed_ui_request_for_job(&TEGU_JOB(command->jobChunks,1),device,&indexedRequest,indexedArgs)&&indexedRequest.vertex_count==12&&
  indexedRequest.pass.load_action==1&&indexedRequest.pass.clear_color[0]==0&&indexedRequest.pass.scissor_x==2&&
  indexedRequest.pass.scissor_y==3&&indexedRequest.pass.scissor_width==8&&indexedRequest.pass.scissor_height==9);
 {struct tegu_ui1_binding twelve;assert(!tegu_ui1_bind(&info,&indexedRequest,&twelve)&&twelve.vertex_count==12&&twelve.scissor[0]==2&&twelve.scissor[3]==9);}
 [encoder drawIndexedPrimitives:3 indexCount:9 indexType:0 indexBuffer:ib12 indexBufferOffset:0 instanceCount:1 baseVertex:0 baseInstance:0];
 assert(!command->invalid&&command->count==3);
 [encoder endEncoding];[command commit];[command waitUntilCompleted];
 assert([command status]==4&&![command error]&&fixedUiDraws==drawsBefore+4);
 [encoder release];[command release];[ib release];[ib12 release];
 device->capabilities|=TEGU_GPU_CAP_GENERIC_JOB;unsigned genericBefore=genericJobs;
 command=[[queue commandBuffer] retain];encoder=[[command renderCommandEncoderWithDescriptor:(id)pass] retain];assert(encoder);
 [encoder setRenderPipelineState:pipeline];[encoder setVertexBuffer:vertex offset:0 atIndex:1];
 [encoder setVertexBuffer:v2 offset:19 atIndex:2];[encoder setFragmentBuffer:f1 offset:277 atIndex:1];
 [encoder drawPrimitives:3 vertexStart:0 vertexCount:3];[encoder drawPrimitives:3 vertexStart:0 vertexCount:3];
 [encoder endEncoding];[command commit];[command waitUntilCompleted];
 assert([command status]==4&&![command error]&&genericJobs==genericBefore+2);
 [encoder release];[command release];[queue release];[vertex release];[v2 release];[f1 release];[pass release];[target release];[td release];
 [pipeline release];[d release];[lib release];fixedUiPsoMode=0;device->capabilities=oldCaps;drain();
 puts("PASS exact specialized fixed UI PSO, UI1 import, draw snapshot and selector16 transport");
}

static void test_fixed_ui_texture_native_frontend(TeguComputeDevice *device,const char *path) {
 FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);rewind(f);assert(n>0);
 void *bytes=malloc((size_t)n);assert(bytes&&fread(bytes,1,(size_t)n,f)==(size_t)n);fclose(f);
 id error=0;TeguLibrary *lib=[device newTeguLibraryWithBytes:bytes length:(NSUInteger)n error:&error];free(bytes);assert(lib&&!error);
 struct TeguFixedUIConstantRow fs[sizeof(tegu_fixed_ui_fs_constants)/sizeof(tegu_fixed_ui_fs_constants[0])];
 struct TeguFixedUIConstantRow vs[sizeof(tegu_fixed_ui_vs_constants)/sizeof(tegu_fixed_ui_vs_constants[0])];
 memcpy(fs,tegu_fixed_ui_fs_constants,sizeof(fs));memcpy(vs,tegu_fixed_ui_vs_constants,sizeof(vs));
 const char *enabled[]={"fc_has_image_0A","fc_has_lod_bias","fc_image_count","fc_plane_count0","fc_texcoord_count0","fc_texture_function"};
 for(unsigned i=0;i<sizeof(fs)/sizeof(fs[0]);i++)for(unsigned j=0;j<sizeof(enabled)/sizeof(enabled[0]);j++)
  if(!strcmp(fs[i].name,enabled[j]))fs[i].data[0]=1;
 for(unsigned i=0;i<sizeof(vs)/sizeof(vs[0]);i++)if(!strcmp(vs[i].name,"fc_texcoord_count0"))vs[i].data[0]=1;
 RenderDescriptor *d=[[RenderDescriptor alloc] init];
 d->vertex=tegu_fixed_ui_test_function(lib,"fixed_vert_lph_spc",vs,sizeof(vs)/sizeof(vs[0]),1);
 d->fragment=tegu_fixed_ui_test_function(lib,"fixed_frag_lph_cph",fs,sizeof(fs)/sizeof(fs[0]),2);
 d->colors=[[RenderColors alloc] init];for(unsigned i=0;i<8;i++)d->colors->colors[i]=[[RenderColor alloc] init];
 d->colors->colors[0]->format=80;d->colors->colors[0]->mask=15;
 d->colors->colors[0]->blend=1;d->colors->colors[0]->rgbSrc=1;d->colors->colors[0]->rgbDst=5;
 d->colors->colors[0]->alphaSrc=1;d->colors->colors[0]->alphaDst=5;
 d->samples=1;d->raster=1;d->amplification=1;
 RenderLayout *layout=[[RenderLayout alloc] init];d->layout=layout;layout->buffers=[[RenderLayout alloc] init];
 const unsigned formats[4]={31,29,29,27},offsets[4]={0,16,24,32};
 for(unsigned i=0;i<4;i++){layout->entries[i]=[[RenderAttribute alloc] init];layout->entries[i]->value=formats[i];layout->entries[i]->byteOffset=offsets[i];layout->entries[i]->bufferSlot=1;}
 layout->buffers->entries[1]=[[RenderAttribute alloc] init];layout->buffers->entries[1]->layoutStride=48;
 layout->buffers->entries[1]->step=1;layout->buffers->entries[1]->rate=1;
 U64 oldCaps=device->capabilities;device->capabilities=oldCaps|TEGU_GPU_CAP_COMPOSITOR|256;
 unsigned importsBefore=renderImports;fixedUiPsoMode=1;
 TeguRenderPipeline *pipeline=[device newRenderPipelineStateWithDescriptor:d error:&error];
 assert(pipeline&&!error&&pipeline->fixedUiTextureSnapshotOnly&&pipeline->handle&&pipeline->package&&
  pipeline->packageBytes==6272&&pipeline->vertexCode.size==4096&&pipeline->fragmentCode.size==1152&&renderImports==importsBefore+1);
 struct tegu_ui2_info info;assert(!tegu_ui2_parse(pipeline->package,pipeline->packageBytes,&info)&&
  info.version==2&&info.texture.texture_slot==3&&info.texture.sampler_slot==0);
 TextureDesc *td=[[TextureDesc alloc] init];td->format=80;td->w=td->h=16;td->uses=4;
 TextureDesc *sd=[[TextureDesc alloc] init];sd->format=80;sd->w=194;sd->h=237;sd->uses=1;
 TeguTexture *target=[device newTextureWithDescriptor:(id)td],*sample=[device newTextureWithDescriptor:(id)sd];assert(target&&sample&&sample->rowBytes==832&&sample->mip[0].stride==832&&sample->buffer->logical>=UINT64_C(832)*237);
 SamplerTestDescriptor *samplerDesc=[[SamplerTestDescriptor alloc] init];samplerDesc->normalized=1;samplerDesc->anisotropy=1;
 TeguSampler *sampler=[device newSamplerStateWithDescriptor:(id)samplerDesc];assert(sampler);
 RenderPass *pass=[[RenderPass alloc] init];pass->color=[[PassColor alloc] init];pass->color->target=[target retain];pass->color->load=2;pass->color->store=1;
 TeguBuffer *vertex=[device newBufferWithLength:192 options:0],*v2=[device newBufferWithLength:512 options:0],*v3=[device newBufferWithLength:64 options:0];
 TeguBuffer *f1=[device newBufferWithLength:512 options:0],*f2=[device newBufferWithLength:64 options:0];assert(vertex&&v2&&v3&&f1&&f2);
 TeguQueue *queue=[device newCommandQueue];TeguCommand *command=[[queue commandBuffer] retain];
 TeguRenderEncoder *encoder=[[command renderCommandEncoderWithDescriptor:(id)pass] retain];assert(encoder);
 [encoder setRenderPipelineState:pipeline];[encoder setVertexBuffer:vertex offset:0 atIndex:1];
 [encoder setVertexBuffer:v2 offset:19 atIndex:2];[encoder setVertexBuffer:v3 offset:0 atIndex:3];
 [encoder setFragmentBuffer:f1 offset:277 atIndex:1];[encoder setFragmentBuffer:f2 offset:0 atIndex:2];
 [encoder setFragmentTexture:sample atIndex:3];[encoder setFragmentSamplerState:sampler atIndex:0];
 [encoder drawPrimitives:3 vertexStart:0 vertexCount:3];assert(!command->invalid&&command->count==1);
 struct Dispatch *job=&TEGU_JOB(command->jobChunks,0);assert(job->fixedUiTextureSnapshot&&job->fixedUiTexturePlan.version==1&&
  job->fixedUiTextureRequest.version==2&&job->fixedUiTextureRequest.vertex_count==3&&
  job->compositorVertexBufferMask==14&&job->compositorFragmentBufferMask==6&&
  job->compositorTextureMask==8&&job->compositorSamplerMask==1);
 struct tegu_ui2_binding bound;assert(!tegu_ui2_bind(&pipeline->fixedUiTextureInfo,&job->fixedUiTextureRequest,&bound));
 [encoder drawPrimitives:3 vertexStart:0 vertexCount:3];
 assert(!command->invalid&&command->count==2&&TEGU_JOB(command->jobChunks,1).fixedUiTextureSnapshot&&
  TEGU_JOB(command->jobChunks,0).fixedUiTextureRequest.pass.load_action==2&&
  TEGU_JOB(command->jobChunks,1).fixedUiTextureRequest.pass.load_action==1&&
  TEGU_JOB(command->jobChunks,1).fixedUiTextureRequest.pass.clear_color[0]==0);
 unsigned drawsBefore=fixedUiDraws;[encoder endEncoding];[command commit];[command waitUntilCompleted];
 assert([command status]==4&&![command error]&&fixedUiDraws==drawsBefore+2);[encoder release];[command release];
 uint16_t indices[6]={0,1,2,2,1,3};TeguBuffer *ib=[device newBufferWithBytes:indices length:sizeof(indices) options:0];
 command=[[queue commandBuffer] retain];encoder=[[command renderCommandEncoderWithDescriptor:(id)pass] retain];assert(encoder);
 [encoder setRenderPipelineState:pipeline];[encoder setVertexBuffer:vertex offset:0 atIndex:1];
 [encoder setVertexBuffer:v2 offset:19 atIndex:2];[encoder setVertexBuffer:v3 offset:0 atIndex:3];
 [encoder setFragmentBuffer:f1 offset:277 atIndex:1];[encoder setFragmentBuffer:f2 offset:0 atIndex:2];
 [encoder setFragmentTexture:sample atIndex:3];[encoder setFragmentSamplerState:sampler atIndex:0];
 [encoder drawIndexedPrimitives:3 indexCount:6 indexType:0 indexBuffer:ib indexBufferOffset:0 instanceCount:1 baseVertex:0 baseInstance:0];
 assert(!command->invalid&&command->count==1&&TEGU_JOB(command->jobChunks,0).fixedUiTextureSnapshot&&
  TEGU_JOB(command->jobChunks,0).fixedUiTextureRequest.vertex_count==6&&TEGU_JOB(command->jobChunks,0).compositorVertexBuffers[1]!=vertex);
 uint16_t twoQuads[12]={0,1,2,2,1,3,1,2,3,3,2,0};TeguBuffer *ib12=[device newBufferWithBytes:twoQuads length:sizeof(twoQuads) options:0];
 [encoder setScissorRect:(TeguScissorRect){1,4,10,7}];
 [encoder drawIndexedPrimitives:3 indexCount:12 indexType:0 indexBuffer:ib12 indexBufferOffset:0 instanceCount:1 baseVertex:0 baseInstance:0];
 {const struct tegu_ui2_bind_request *q=&TEGU_JOB(command->jobChunks,1).fixedUiTextureRequest;
  assert(!command->invalid&&command->count==2&&q->vertex_count==12&&q->pass.load_action==1&&q->pass.scissor_x==1&&
   q->pass.scissor_y==4&&q->pass.scissor_width==10&&q->pass.scissor_height==7);
  assert(!tegu_ui2_bind(&pipeline->fixedUiTextureInfo,q,&bound)&&bound.vertex_count==12&&bound.scissor[1]==4);}
 [encoder endEncoding];[encoder release];[command release];[ib release];[ib12 release];
 /* The same draws through TGJ1 when the kernel advertises it. */
 device->capabilities|=TEGU_GPU_CAP_GENERIC_JOB;unsigned genericBefore=genericJobs;
 command=[[queue commandBuffer] retain];encoder=[[command renderCommandEncoderWithDescriptor:(id)pass] retain];assert(encoder);
 [encoder setRenderPipelineState:pipeline];[encoder setVertexBuffer:vertex offset:0 atIndex:1];
 [encoder setVertexBuffer:v2 offset:19 atIndex:2];[encoder setVertexBuffer:v3 offset:0 atIndex:3];
 [encoder setFragmentBuffer:f1 offset:277 atIndex:1];[encoder setFragmentBuffer:f2 offset:0 atIndex:2];
 [encoder setFragmentTexture:sample atIndex:3];[encoder setFragmentSamplerState:sampler atIndex:0];
 [encoder drawPrimitives:3 vertexStart:0 vertexCount:3];[encoder setScissorRect:(TeguScissorRect){1,4,10,7}];
 {uint16_t q[12]={0,1,2,2,1,3,1,2,3,3,2,0};TeguBuffer *ibg=[device newBufferWithBytes:q length:sizeof(q) options:0];
  [encoder drawIndexedPrimitives:3 indexCount:12 indexType:0 indexBuffer:ibg indexBufferOffset:0 instanceCount:1 baseVertex:0 baseInstance:0];
  [encoder endEncoding];[command commit];[command waitUntilCompleted];[ibg release];}
 assert([command status]==4&&![command error]&&genericJobs==genericBefore+2&&(genericLastSampler&(3u<<27))==(3u<<27));
 /* R113 lock screen: the same draw with BackBoard's linear-filtered sampler. */
 {SamplerTestDescriptor *linearDesc=[[SamplerTestDescriptor alloc] init];linearDesc->normalized=1;linearDesc->anisotropy=1;
  linearDesc->min=linearDesc->mag=1;linearDesc->lodMax=__FLT_MAX__;
  TeguSampler *linear=[device newSamplerStateWithDescriptor:(id)linearDesc];assert(linear);
  [encoder release];[command release];genericBefore=genericJobs;
  command=[[queue commandBuffer] retain];encoder=[[command renderCommandEncoderWithDescriptor:(id)pass] retain];assert(encoder);
  [encoder setRenderPipelineState:pipeline];[encoder setVertexBuffer:vertex offset:0 atIndex:1];
  [encoder setVertexBuffer:v2 offset:19 atIndex:2];[encoder setVertexBuffer:v3 offset:0 atIndex:3];
  [encoder setFragmentBuffer:f1 offset:277 atIndex:1];[encoder setFragmentBuffer:f2 offset:0 atIndex:2];
  [encoder setFragmentTexture:sample atIndex:3];[encoder setFragmentSamplerState:linear atIndex:0];
  [encoder drawPrimitives:3 vertexStart:0 vertexCount:3];[encoder endEncoding];[command commit];[command waitUntilCompleted];
  assert([command status]==4&&![command error]&&genericJobs==genericBefore+1&&!(genericLastSampler&(3u<<27)));
  assert(genericLastPlaneStride==832&&genericLastTextureSize==(193u|(236u<<16)));
  /* Lock-screen textures are not the 194x237 logo: any single-level BGRA8 shape. */
  TextureDesc *wd=[[TextureDesc alloc] init];wd->format=80;wd->w=300;wd->h=200;wd->uses=1;
  TeguTexture *wall=[device newTextureWithDescriptor:(id)wd];assert(wall&&wall->rowBytes==1216);
  [encoder release];[command release];genericBefore=genericJobs;
  command=[[queue commandBuffer] retain];encoder=[[command renderCommandEncoderWithDescriptor:(id)pass] retain];assert(encoder);
  [encoder setRenderPipelineState:pipeline];[encoder setVertexBuffer:vertex offset:0 atIndex:1];
  [encoder setVertexBuffer:v2 offset:19 atIndex:2];[encoder setVertexBuffer:v3 offset:0 atIndex:3];
  [encoder setFragmentBuffer:f1 offset:277 atIndex:1];[encoder setFragmentBuffer:f2 offset:0 atIndex:2];
  [encoder setFragmentTexture:wall atIndex:3];[encoder setFragmentSamplerState:linear atIndex:0];
  [encoder drawPrimitives:3 vertexStart:0 vertexCount:3];[encoder endEncoding];[command commit];[command waitUntilCompleted];
  assert([command status]==4&&![command error]&&genericJobs==genericBefore+1&&
   genericLastPlaneStride==1216&&genericLastTextureSize==(299u|(199u<<16)));
  [wall release];[wd release];[linear release];[linearDesc release];}
 [encoder release];[command release];
 /* Opaque descriptor of the same R85 shader (wallpaper-style): older kernels
  * refuse it; with TGJ1 it needs no kernel import and draws opaque. */
 d->colors->colors[0]->blend=0;d->colors->colors[0]->rgbDst=0;d->colors->colors[0]->alphaDst=0;
 device->capabilities&=~TEGU_GPU_CAP_GENERIC_JOB;error=0;
 assert(![device newRenderPipelineStateWithDescriptor:d error:&error]&&error);
 device->capabilities|=TEGU_GPU_CAP_GENERIC_JOB;error=0;unsigned opaqueImports=renderImports;
 TeguRenderPipeline *opaque=[device newRenderPipelineStateWithDescriptor:d error:&error];
 assert(opaque&&!error&&opaque->fixedUiTextureSnapshotOnly&&!opaque->handle&&renderImports==opaqueImports&&
  opaque->fixedUiTextureInfo.target_blend_enabled==0);
 genericBefore=genericJobs;command=[[queue commandBuffer] retain];
 encoder=[[command renderCommandEncoderWithDescriptor:(id)pass] retain];assert(encoder);
 [encoder setRenderPipelineState:opaque];[encoder setVertexBuffer:vertex offset:0 atIndex:1];
 [encoder setVertexBuffer:v2 offset:19 atIndex:2];[encoder setVertexBuffer:v3 offset:0 atIndex:3];
 [encoder setFragmentBuffer:f1 offset:277 atIndex:1];[encoder setFragmentBuffer:f2 offset:0 atIndex:2];
 [encoder setFragmentTexture:sample atIndex:3];[encoder setFragmentSamplerState:sampler atIndex:0];
 [encoder drawPrimitives:3 vertexStart:0 vertexCount:3];[encoder endEncoding];[command commit];[command waitUntilCompleted];
 assert([command status]==4&&![command error]&&genericJobs==genericBefore+1&&genericLastDcd0==3);
 [encoder release];[command release];[opaque release];[queue release];
 [vertex release];[v2 release];[v3 release];[f1 release];[f2 release];[pass release];[sampler release];[samplerDesc release];
 [sample release];[target release];[sd release];[td release];[pipeline release];[d release];[lib release];
 fixedUiPsoMode=0;device->capabilities=oldCaps;drain();puts("PASS exact sampled fixed UI UI2 import, direct transport and indexed snapshot");
}
