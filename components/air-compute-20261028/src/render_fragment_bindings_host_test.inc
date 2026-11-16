/* Public binding setters and recording ownership, without shader execution.
 * Existing accepted PSOs have no fragment UBOs and only texture0/sampler0 at
 * most. Nonzero texture/sampler slots here are unused by the clear PSO. */
static void test_fragment_bindings(TeguComputeDevice *device,TeguRenderPipeline *pipeline) {
 NSUInteger baseline=[device currentAllocatedSize];
 TextureDesc *td=[[TextureDesc alloc] init];td->format=70;td->w=td->h=16;td->uses=4;
 TeguTexture *target=[device newTextureWithDescriptor:(id)td];assert(target);
 RenderPass *pass=[[RenderPass alloc] init];pass->color=[[PassColor alloc] init];
 pass->color->target=[target retain];pass->color->load=2;pass->color->store=1;
 TeguQueue *queue=[device newCommandQueue];
 TeguCommand *command=[[queue commandBuffer] retain];
 TeguRenderEncoder *encoder=[[command renderCommandEncoderWithDescriptor:(id)pass] retain];assert(encoder);
 TeguBuffer *buffer=[device newBufferWithLength:32 options:0];assert(buffer);U64 handle=buffer->handle;
 [encoder setFragmentBuffer:buffer offset:30 atIndex:6];
 [encoder setFragmentBuffer:buffer offset:16 atIndex:1];[buffer release];
 assert(memory[handle]);[encoder setFragmentBufferOffset:32 atIndex:1];
 [encoder setFragmentBuffer:0 offset:0 atIndex:6];assert(memory[handle]);
 [encoder setFragmentBuffer:0 offset:0 atIndex:1];assert(!memory[handle]);
 unsigned char input[32],original[32];for(unsigned i=0;i<32;i++)input[i]=(unsigned char)(i+11);
 memcpy(original,input,sizeof input);unsigned before=allocated;
 [encoder setFragmentBytes:input length:sizeof input atIndex:6];assert(allocated==before+1);
 U64 copied=allocated;memset(input,0xa5,sizeof input);
 assert(memory[copied]&&!memcmp(memory[copied],original,sizeof original));
 [encoder setFragmentBytes:0 length:0 atIndex:6];assert(!memory[copied]);
 [encoder setFragmentBytes:original length:sizeof original atIndex:6];copied=allocated;
 td->uses=1;TeguTexture *sample=[device newTextureWithDescriptor:(id)td];assert(sample);
 U64 sampled=sample->buffer->handle;
 SamplerTestDescriptor *sd=[[SamplerTestDescriptor alloc] init];sd->normalized=1;sd->anisotropy=1;
 TeguSampler *sampler=[device newSamplerStateWithDescriptor:(id)sd];assert(sampler);
 [encoder setFragmentTexture:sample atIndex:3];[encoder setFragmentSamplerState:sampler atIndex:15];
 [encoder setFragmentSamplerState:sampler atIndex:0];[encoder setFragmentSamplerState:0 atIndex:15];
 [sample release];[sampler release];assert(memory[sampled]&&[sampler device]==device);
 [encoder setRenderPipelineState:pipeline];[encoder drawPrimitives:3 vertexStart:0 vertexCount:3];
 [encoder endEncoding];[encoder release];drain();
 // The compiled clear PSO does not read these bindings. Encoder ownership ends
 // without leaking unused resources into the recorded draw.
 assert(!memory[copied]&&!memory[sampled]);
 unsigned draws=renderDraws;[command commit];assert([command status]==4&&renderDraws==draws+1);
 [command release];drain();

 TeguComputeDevice *foreign=[TeguComputeDevice alloc];foreign->scalar=device->scalar;
 foreign->map=device->map;foreign->unmap=device->unmap;
 TeguBuffer *otherBuffer=[foreign newBufferWithLength:32 options:0];assert(otherBuffer);
 TeguTexture *otherTexture=[foreign newTextureWithDescriptor:(id)td];assert(otherTexture);
 TeguSampler *otherSampler=[foreign newSamplerStateWithDescriptor:(id)sd];assert(otherSampler);
 buffer=[device newBufferWithLength:32 options:0];assert(buffer);id wrongClass=[[NSObject alloc] init];
 for(unsigned scenario=0;scenario<12;scenario++) {
  command=[[queue commandBuffer] retain];encoder=[command renderCommandEncoderWithDescriptor:(id)pass];assert(encoder);
  [encoder setRenderPipelineState:pipeline];draws=renderDraws;
  switch(scenario) {
   case 0:[encoder setFragmentBuffer:buffer offset:0 atIndex:31];break;
   case 1:[encoder setFragmentBuffer:otherBuffer offset:0 atIndex:6];break;
   case 2:[encoder setFragmentBuffer:(id)wrongClass offset:0 atIndex:6];break;
   case 3:[encoder setFragmentBuffer:buffer offset:33 atIndex:6];break;
   case 4:[encoder setFragmentBufferOffset:0 atIndex:6];break;
   case 5:[encoder setFragmentBytes:original length:4097 atIndex:6];break;
   case 6:[encoder setFragmentBytes:0 length:1 atIndex:6];break;
   case 7:failAlloc=1;[encoder setFragmentBytes:original length:32 atIndex:6];failAlloc=0;break;
   case 8:[encoder setFragmentTexture:otherTexture atIndex:3];break;
   case 9:[encoder setFragmentSamplerState:otherSampler atIndex:15];break;
   case 10:[encoder setFragmentTexture:0 atIndex:31];break;
   case 11:[encoder setFragmentSamplerState:0 atIndex:16];break;
  }
  [encoder drawPrimitives:3 vertexStart:0 vertexCount:3];[encoder endEncoding];[command commit];
  assert([command status]==5&&renderDraws==draws);[command release];drain();
 }
 [wrongClass release];[buffer release];[otherBuffer release];[otherTexture release];[otherSampler release];[foreign release];
 [queue release];[pass release];[target release];[td release];[sd release];drain();
 assert([device currentAllocatedSize]==baseline);
 puts("PASS fragment binding API: byte copies, shared-slot retention, unused-slot cleanup, foreign/range/class/allocation refusals; no fragment UBO GPU claim");
}
