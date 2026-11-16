/* Real AIR, format-80 pipeline state, and simulated import/draw transport.
 * This host test does not submit work to a GPU. */
static int bgra_reference_equals(const char *path,const void *bytes,size_t length) {
 FILE *f=fopen(path,"rb");if(!f)return 0;
 if(fseek(f,0,SEEK_END)){fclose(f);return 0;}long n=ftell(f);
 if(n<0||(size_t)n!=length||fseek(f,0,SEEK_SET)){fclose(f);return 0;}
 unsigned char *expected=malloc(length?length:1);if(!expected){fclose(f);return 0;}
 int ok=fread(expected,1,length,f)==length&&!memcmp(expected,bytes,length);
 free(expected);fclose(f);return ok;
}
static void bgra_put32(unsigned char *p,uint32_t x) {
 p[0]=(unsigned char)x;p[1]=(unsigned char)(x>>8);p[2]=(unsigned char)(x>>16);p[3]=(unsigned char)(x>>24);
}
static void bgra_check_package_bytes(TeguRenderPipeline *p,unsigned blendMode) {
 const uint32_t codeOffset=blendMode?1344u:1280u;
 const uint32_t pushOffset=blendMode?192u:128u,attributeOffset=blendMode?448u:384u;
 uint32_t fsOffset=codeOffset+((uint32_t)p->vertexCode.size+63u&~63u);
 size_t size=(size_t)fsOffset+p->fragmentCode.size;unsigned char *expected=calloc(1,size);assert(expected);
 uint32_t words[32]={TEGU_RENDER_MAGIC,blendMode?4u:3u,0xb8a21030u,80u,
  (uint32_t)p->vertexCode.size,(uint32_t)p->fragmentCode.size,p->push.vertex_span,p->push.fragment_span};
 for(unsigned i=0;i<2;i++) {
  struct tegu_mali_graphics *c=i?&p->fragmentCode:&p->vertexCode;uint32_t *h=words+8+8*i;
  h[0]=c->registers;h[1]=c->fau_words;h[2]=c->sysval_mask;h[3]=c->tls;
  h[4]=(uint32_t)c->preload;h[5]=(uint32_t)(c->preload>>32);h[6]=c->push_bytes;
 }
 words[24]=p->vertexFetch.mask;words[25]=p->vertexFetch.count;
 words[26]=p->pair.position_offset;words[27]=p->pair.secondary_offset;
 words[28]=p->pair.secondary_registers;words[29]=(uint32_t)p->pair.secondary_preload;
 words[30]=(uint32_t)(p->pair.secondary_preload>>32);words[31]=p->pair.generic_size_B;
 for(unsigned i=0;i<32;i++)bgra_put32(expected+4*i,words[i]);
 if(blendMode) {
  const uint32_t b[9]={1,1,0,p->blend.rgb_src,p->blend.rgb_dst,0,p->blend.alpha_src,p->blend.alpha_dst,15};
  for(unsigned i=0;i<9;i++)bgra_put32(expected+128+i*4,b[i]);
 }
 memcpy(expected+pushOffset,p->push.vertex,128);memcpy(expected+pushOffset+128,p->push.fragment,128);
 for(unsigned i=0;i<p->vertexFetch.count;i++) {
  const struct tegu_vertex_attribute *a=&p->vertexFetch.attributes[i];
  const uint32_t fields[7]={a->location,a->buffer_index,a->format,a->offset,a->stride,a->input_rate,a->divisor};
  for(unsigned j=0;j<7;j++)bgra_put32(expected+attributeOffset+i*28+j*4,fields[j]);
 }
 memcpy(expected+codeOffset,p->vertexCode.code,p->vertexCode.size);
 memcpy(expected+fsOffset,p->fragmentCode.code,p->fragmentCode.size);
 assert(size==p->packageBytes&&!memcmp(expected,p->package,size));free(expected);
}
static void test_bgra_native_frontend(TeguComputeDevice *device,const void *airBytes,NSUInteger airLength) {
 assert(device&&airBytes&&airLength);
 U64 savedCapabilities=device->capabilities;device->capabilities=16|32|64;
 NSUInteger baseline=[device currentAllocatedSize];unsigned importsBefore=renderImports;
 id error=0;TeguLibrary *library=[device newTeguLibraryWithBytes:airBytes length:airLength error:&error];
 assert(library&&!error);
 RenderDescriptor *desc=[[RenderDescriptor alloc] init];
 desc->vertex=[library newFunctionWithName:[NSString stringWithUTF8String:"varying_vertex"]];
 desc->fragment=[library newFunctionWithName:[NSString stringWithUTF8String:"varying_fragment"]];
 desc->colors=[[RenderColors alloc] init];RenderColor *color=[[RenderColor alloc] init];
 desc->colors->colors[0]=color;color->format=80;color->mask=15;
 desc->samples=1;desc->raster=1;desc->amplification=1;
 RenderLayout *layout=[[RenderLayout alloc] init];desc->layout=layout;
 for(unsigned i=7;i<=8;i++) {
  layout->entries[i]=[[RenderAttribute alloc] init];layout->entries[i]->value=31;
  layout->entries[i]->bufferSlot=1;layout->entries[i]->byteOffset=(i-7)*16;
 }
 layout->buffers=[[RenderLayout alloc] init];layout->buffers->entries[1]=[[RenderAttribute alloc] init];
 layout->buffers->entries[1]->layoutStride=32;layout->buffers->entries[1]->step=1;layout->buffers->entries[1]->rate=1;

 /* BGRA is a separate opt-in capability. Refuse before AIR compilation or
  * kernel import when the driver has not advertised bit 256. */
 assert(![device newRenderPipelineStateWithDescriptor:desc error:&error]&&error);
 assert(renderImports==importsBefore&&[device currentAllocatedSize]==baseline);
 device->capabilities=16|32|64|256;

 TeguRenderPipeline *pipelines[4]={0};
 const uint32_t blendArgs[3][4]={{4,5,4,5},{1,5,1,5},{4,5,1,5}};
 const uint32_t expectedWords[3][4]={
  {0x00000a01,0xf0503503,0x0000001a,0x000ed000},
  {0x00000a01,0xf0d32d32,0x0000001a,0x000ed000},
  {0x00000a01,0xf0d32503,0x0000001a,0x000ed000},
 };
 for(unsigned mode=0;mode<4;mode++) {
  color->blend=mode!=0;color->rgbOp=color->alphaOp=0;
  color->rgbSrc=mode?blendArgs[mode-1][0]:1;color->rgbDst=mode?blendArgs[mode-1][1]:0;
  color->alphaSrc=mode?blendArgs[mode-1][2]:1;color->alphaDst=mode?blendArgs[mode-1][3]:0;
  pipelines[mode]=[device newRenderPipelineStateWithDescriptor:desc error:&error];
  assert(pipelines[mode]&&!error&&pipelines[mode]->colorFormat==80);
  struct tegu_render_info parsed;assert(!tegu_render_parse(pipelines[mode]->package,pipelines[mode]->packageBytes,&parsed));
  assert(parsed.color_format==80&&parsed.vertex_count==2&&parsed.position_offset==384&&parsed.secondary_offset==768);
  assert(parsed.version==(mode?4u:3u));
  if(!mode) assert(!parsed.blend.enabled);
  else {
   struct tegu_blend_state expected;
   assert(!tegu_blend_build_format(80,&pipelines[mode]->blend,&expected));
   assert(!memcmp(expected.words,expectedWords[mode-1],sizeof(expected.words)));
   uint32_t packageBlend[9]={1,1,0,blendArgs[mode-1][0],blendArgs[mode-1][1],0,
    blendArgs[mode-1][2],blendArgs[mode-1][3],15};
   assert(!memcmp(&parsed.blend,&pipelines[mode]->blend,sizeof(parsed.blend)));
   assert(!memcmp(pipelines[mode]->package+128,packageBlend,sizeof(packageBlend)));
   char message[256];assert(!tegu_air_render_pipeline_native_check_with_blend(
    pipelines[mode]->compiled,80,&pipelines[mode]->blend,message,sizeof(message)));
  }
  /* Every serialized word outside code payloads is independently checked by
   * the parser above; both stage payloads must exactly match the compiler result. */
  assert(!memcmp(pipelines[mode]->package+parsed.stages[0].code_offset,
   pipelines[mode]->vertexCode.code,pipelines[mode]->vertexCode.size));
  assert(!memcmp(pipelines[mode]->package+parsed.stages[1].code_offset,
   pipelines[mode]->fragmentCode.code,pipelines[mode]->fragmentCode.size));
  bgra_check_package_bytes(pipelines[mode],mode!=0);
  if(mode) {
   static const char *names[4]={0,"source-alpha","premultiplied","separate-alpha"};
   TeguAIRVertexAttribute attributes[2]={{7,31,0,1},{8,31,16,1}};
   TeguAIRVertexLayout vertexLayout[1]={{1,32,1,1}};
   TeguAIRBlob vs={0},fs={0},metadata={0};char message[512]={0};
   TeguAIRRenderPipeline *compiled=tegu_air_render_pipeline_create_with_blend(library->library,
    (const uint8_t *)"varying_vertex",14,library->library,(const uint8_t *)"varying_fragment",16,80,
    attributes,2,vertexLayout,1,&pipelines[mode]->blend,tegu_native_profile,sizeof(tegu_native_profile),
    &vs,&fs,&metadata,message,sizeof(message));
   assert(compiled&&vs.bytes&&fs.bytes&&metadata.bytes);
   char path[4096];const char *base=getenv("TEGU_BGRA_REFERENCES");assert(base&&strlen(base)<sizeof(path)-128);
   __builtin_snprintf(path,sizeof(path),"%s/%s-bgra-vertex.spv",base,names[mode]);
   assert(bgra_reference_equals(path,vs.bytes,vs.length));
   __builtin_snprintf(path,sizeof(path),"%s/%s-bgra-fragment.spv",base,names[mode]);
   assert(bgra_reference_equals(path,fs.bytes,fs.length));
   __builtin_snprintf(path,sizeof(path),"%s/%s-bgra-metadata.json",base,names[mode]);
   assert(bgra_reference_equals(path,metadata.bytes,metadata.length));
   tegu_air_render_pipeline_close(compiled);tegu_air_blob_free(&vs);tegu_air_blob_free(&fs);tegu_air_blob_free(&metadata);
  }
 }
 color->blend=0;color->format=70;
 TeguRenderPipeline *rgbaPipeline=[device newRenderPipelineStateWithDescriptor:desc error:&error];
 assert(rgbaPipeline&&!error&&rgbaPipeline->colorFormat==70);
 assert(renderImports==importsBefore+5);

 /* Format 80 and 70 pipelines cannot be interchanged at encoder binding. */
 TextureDesc *td=[[TextureDesc alloc] init];td->w=td->h=16;td->uses=4;td->format=80;
 TeguTexture *bgraTarget=[device newTextureWithDescriptor:(id)td];assert(bgraTarget);
 td->format=70;TeguTexture *rgbaTarget=[device newTextureWithDescriptor:(id)td];assert(rgbaTarget);
 TeguQueue *queue=[device newCommandQueue];RenderPass *pass=[[RenderPass alloc] init];
 pass->color=[[PassColor alloc] init];pass->color->target=[bgraTarget retain];pass->color->load=2;pass->color->store=1;
 float vertices[24]={-.75f,-.75f,.5f,1,.2f,.4f,.8f,.6f,.75f,-.75f,.5f,1,.2f,.4f,.8f,.6f,
  -.75f,.75f,.5f,1,.2f,.4f,.8f,.6f};unsigned drawsBefore=vertexDraws;
 TeguCommand *good=[[queue commandBuffer] retain];
 TeguRenderEncoder *encoder=[good renderCommandEncoderWithDescriptor:(id)pass];assert(encoder);
 [encoder setRenderPipelineState:pipelines[0]];[encoder setVertexBytes:vertices length:sizeof(vertices) atIndex:1];
 [encoder drawPrimitives:3 vertexStart:0 vertexCount:3];[encoder endEncoding];
 [good commit];assert([good status]==4&&vertexDraws==drawsBefore+1);[good release];drain();
 [pass->color->target release];pass->color->target=[rgbaTarget retain];
 TeguCommand *bad=[[queue commandBuffer] retain];
 TeguRenderEncoder *mismatch=[bad renderCommandEncoderWithDescriptor:(id)pass];assert(mismatch);
 [mismatch setRenderPipelineState:pipelines[0]];[mismatch setVertexBytes:vertices length:sizeof(vertices) atIndex:1];
 [mismatch drawPrimitives:3 vertexStart:0 vertexCount:3];[mismatch endEncoding];[bad commit];
 assert([bad status]==5&&vertexDraws==drawsBefore+1);[bad release];drain();
 [pass->color->target release];pass->color->target=[bgraTarget retain];
 TeguCommand *badReverse=[[queue commandBuffer] retain];
 TeguRenderEncoder *reverse=[badReverse renderCommandEncoderWithDescriptor:(id)pass];assert(reverse);
 [reverse setRenderPipelineState:rgbaPipeline];[reverse setVertexBytes:vertices length:sizeof(vertices) atIndex:1];
 [reverse drawPrimitives:3 vertexStart:0 vertexCount:3];[reverse endEncoding];[badReverse commit];
 assert([badReverse status]==5&&vertexDraws==drawsBefore+1);[badReverse release];drain();

 [pass release];[queue release];[bgraTarget release];[rgbaTarget release];[td release];
 [desc release];[library release];drain();
 for(unsigned i=0;i<4;i++)[pipelines[i] release];[rgbaPipeline release];drain();
 assert([device currentAllocatedSize]==baseline&&renderImports==importsBefore+5);
 device->capabilities=savedCapabilities;
 puts("PASS real BGRA frontend: format80 unblended and three bounded TGR4 blend packages; target/PSO format match; real AIR/Mali compilers, host transport only");
}
