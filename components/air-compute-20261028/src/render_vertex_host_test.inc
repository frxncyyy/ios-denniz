/* Real AIR/Mali pipeline plus simulated IPC: immutable binding snapshots,
 * copied bytes, ownership and refusal before submission. No GPU pixel claim. */
static unsigned vertexDraws;
static struct tegu_render_vertex_request lastVertexRequest;
static void test_vertex_render(TeguComputeDevice *device,const char *path) {
 FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
 void *bytes=malloc(size);assert(bytes&&fread(bytes,1,size,f)==(size_t)size);fclose(f);
 id error=0;TeguLibrary *library=[device newTeguLibraryWithBytes:bytes length:size error:&error];free(bytes);
 assert(library&&!error);
 RenderDescriptor *desc=[[RenderDescriptor alloc] init];
 desc->vertex=[library newFunctionWithName:[NSString stringWithUTF8String:"test_vertex"]];
 desc->fragment=[library newFunctionWithName:[NSString stringWithUTF8String:"test_fragment"]];
 desc->colors=[[RenderColors alloc] init];desc->colors->colors[0]=[[RenderColor alloc] init];
 desc->colors->colors[0]->format=70;desc->colors->colors[0]->mask=15;
 desc->samples=1;desc->raster=1;desc->amplification=1;
 RenderLayout *layout=[[RenderLayout alloc] init];desc->layout=layout;
 layout->entries[7]=[[RenderAttribute alloc] init];layout->entries[7]->value=31;
 layout->entries[7]->bufferSlot=1;layout->entries[7]->byteOffset=16;
 layout->buffers=[[RenderLayout alloc] init];layout->buffers->entries[1]=[[RenderAttribute alloc] init];
 layout->buffers->entries[1]->layoutStride=32;layout->buffers->entries[1]->step=1;layout->buffers->entries[1]->rate=1;
 NSUInteger original=[device currentAllocatedSize];unsigned beforeImport=renderImports;
 assert(!(device->capabilities&16));
 assert(![device newRenderPipelineStateWithDescriptor:desc error:&error]&&error);
 assert([device currentAllocatedSize]==original&&renderImports==beforeImport);
 device->capabilities|=16;
 TeguRenderPipeline *pipeline=[device newRenderPipelineStateWithDescriptor:desc error:&error];assert(pipeline&&!error);
 assert(pipeline->vertexFetch.count==1&&pipeline->vertexFetch.mask==1);
 const struct tegu_vertex_attribute *attr=&pipeline->vertexFetch.attributes[0];
 assert(attr->location==0&&attr->buffer_index==1&&attr->format==109&&attr->offset==16&&attr->stride==32);
 struct tegu_render_info parsed;assert(!tegu_render_parse(pipeline->package,pipeline->packageBytes,&parsed));
 assert(parsed.version==2&&parsed.vertex_mask==1&&parsed.vertex_count==1);
 assert(!memcmp(parsed.vertex,pipeline->vertexFetch.attributes,sizeof(*attr)));
 assert(!memcmp(pipeline->package+parsed.stages[0].code_offset,pipeline->vertexCode.code,pipeline->vertexCode.size));
 // Application descriptors can change after creation without mutating the plan.
 layout->entries[7]->byteOffset=0;layout->buffers->entries[1]->layoutStride=16;
 assert(pipeline->vertexFetch.attributes[0].offset==16&&pipeline->vertexFetch.attributes[0].stride==32);
 NSUInteger baseline=[device currentAllocatedSize];
 TextureDesc *td=[[TextureDesc alloc] init];td->w=td->h=16;td->format=70;td->uses=4;
 TeguTexture *target=[device newTextureWithDescriptor:(id)td];assert(target);
 RenderPass *pass=[[RenderPass alloc] init];pass->color=[[PassColor alloc] init];
 pass->color->target=[target retain];pass->color->load=2;pass->color->store=1;
 TeguQueue *q=[device newCommandQueue];
 TeguBuffer *a=[device newBufferWithLength:192 options:0],*b=[device newBufferWithLength:192 options:0];assert(a&&b);
 U64 ah=a->handle,bh=b->handle;
 TeguCommand *command=[[q commandBuffer] retain];
 TeguRenderEncoder *encoder=[command renderCommandEncoderWithDescriptor:(id)pass];
 [encoder setRenderPipelineState:pipeline];[encoder setVertexBuffer:a offset:64 atIndex:1];
 [encoder drawPrimitives:3 vertexStart:0 vertexCount:3];
 [encoder setVertexBufferOffset:0 atIndex:1];[encoder setVertexBuffer:b offset:0 atIndex:1];[encoder endEncoding];
 encoder=[command renderCommandEncoderWithDescriptor:(id)pass];
 [encoder setRenderPipelineState:pipeline];[encoder setVertexBuffer:b offset:32 atIndex:1];
 [encoder drawPrimitives:3 vertexStart:0 vertexCount:3];[encoder setVertexBufferOffset:0 atIndex:1];[encoder endEncoding];
 assert(TEGU_JOB(command->jobChunks,0).vertexRequest.buffers[1].handle==ah&&TEGU_JOB(command->jobChunks,0).vertexRequest.buffers[1].offset==64);
 assert(TEGU_JOB(command->jobChunks,1).vertexRequest.buffers[1].handle==bh&&TEGU_JOB(command->jobChunks,1).vertexRequest.buffers[1].offset==32);
 assert(TEGU_JOB(command->jobChunks,0).vertexRequest.buffers[1].length==96&&TEGU_JOB(command->jobChunks,1).vertexRequest.buffers[1].length==96);
 [a release];[b release];drain();assert(memory[ah]&&memory[bh]);
 [command commit];assert([command status]==4&&vertexDraws==2&&lastVertexRequest.buffers[1].offset==32);
 [command release];assert(!memory[ah]&&!memory[bh]);
 // setVertexBytes copies immediately and its generated buffer survives commit.
 unsigned values[24];for(unsigned i=0;i<24;i++)values[i]=0x12340000+i;
 command=[q commandBuffer];encoder=[command renderCommandEncoderWithDescriptor:(id)pass];
 [encoder setRenderPipelineState:pipeline];[encoder setVertexBytes:values length:sizeof(values) atIndex:1];
 [encoder drawPrimitives:3 vertexStart:0 vertexCount:3];[encoder setVertexBuffer:0 offset:999 atIndex:1];[encoder endEncoding];
 U64 copied=TEGU_JOB(command->jobChunks,0).vertexRequest.buffers[1].handle;memset(values,0,sizeof(values));
 assert(((unsigned *)memory[copied])[23]==0x12340017);
 [command commit];assert([command status]==4&&vertexDraws==3);drain();assert(!memory[copied]);
 // Invalid bindings must never reach the transport, even with a valid shader.
 a=[device newBufferWithLength:192 options:0];b=[device newBufferWithLength:64 options:0];
 TeguComputeDevice *foreign=[TeguComputeDevice alloc];foreign->scalar=render_scalar;foreign->map=map;foreign->unmap=unmap;
 TeguBuffer *foreignBuffer=[foreign newBufferWithLength:192 options:0];id wrongClass=[[NSObject alloc] init];
 for(unsigned scenario=0;scenario<9;scenario++) {
  unsigned draws=vertexDraws;
  command=[q commandBuffer];encoder=[command renderCommandEncoderWithDescriptor:(id)pass];
  [encoder setRenderPipelineState:pipeline];
  switch(scenario) {
   case 0:break;
   case 1:[encoder setVertexBuffer:b offset:0 atIndex:1];break;
   case 2:[encoder setVertexBuffer:a offset:1 atIndex:1];break;
   case 3:[encoder setVertexBuffer:foreignBuffer offset:0 atIndex:1];break;
   case 4:[encoder setVertexBuffer:(id)wrongClass offset:0 atIndex:1];break;
   case 5:[encoder setVertexBuffer:a offset:0 atIndex:31];break;
   case 6:[encoder setVertexBytes:values length:4097 atIndex:1];break;
   case 7:[encoder setVertexBufferOffset:0 atIndex:1];break;
   case 8:[encoder setVertexBuffer:a offset:193 atIndex:1];break;
  }
  [encoder drawPrimitives:3 vertexStart:0 vertexCount:3];[encoder endEncoding];
  [command commit];assert([command status]==5&&[command error]&&vertexDraws==draws);drain();
 }
 [wrongClass release];[foreignBuffer release];[foreign release];[a release];[b release];
 [q release];[pass release];[target release];[td release];drain();assert([device currentAllocatedSize]==baseline);
 [pipeline release];[desc release];[library release];device->capabilities&=~16ULL;
 assert([device currentAllocatedSize]==original&&renderImports==beforeImport+1);
 puts("PASS real vertex AIR/Mali pipeline: v2 package, kernel capability gate, linked locations, owned immutable bindings, copied vertex bytes, range/device/class refusals and retained lifetime; simulated IPC only");
}
