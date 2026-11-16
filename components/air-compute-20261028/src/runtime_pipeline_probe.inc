/* Authored fixture integration test of the normal library/function/pipeline and
 * dispatch selectors. Production runtime code above has no fixture dependency. */
int tegu_runtime_frontend_probe(void) {
 MARK("runtime_pipeline_begin");
 TeguComputeDevice *device=[[TeguComputeDevice alloc] init];
 id error=0;
 TeguLibrary *library=[device newTeguLibraryWithBytes:tegu_air_fixture length:sizeof(tegu_air_fixture) error:&error];
 TeguFunction *function=[library newFunctionWithName:@"scale_and_offset"];
 TeguPipeline *pipeline=[device newComputePipelineStateWithFunction:function error:&error];
 [function release];[library release];
 TeguQueue *queue=[device newCommandQueue];
 TeguBuffer *input=[device newBufferWithLength:2048 options:0];
 TeguBuffer *output=[device newBufferWithLength:2048 options:0];
 TeguBuffer *scale=[device newBufferWithLength:4 options:0];
 int ok=device&&pipeline&&!error&&queue&&input&&output&&scale&&[pipeline maxTotalThreadsPerThreadgroup]>=64;
 for(unsigned run=0;ok&&run<8;run++) {
  unsigned locals[4]={1,8,32,64},local=locals[run%4],offset=run<4?128:384,start=offset/4;
  float factor=run%2?0.5f:-3.0f;
  float *in=[input contents],*out=[output contents];
  for(unsigned i=0;i<input->mapped/4;i++) in[i]=i>=start&&i<start+256?(float)(i-start+7):-999;
  for(unsigned i=0;i<output->mapped/4;i++) out[i]=-777;
  *(float *)[scale contents]=factor;
  TeguCommand *command=[queue commandBuffer];TeguEncoder *encoder=[command computeCommandEncoder];
  if(!command||!encoder) {ok=0;break;}
  [encoder setComputePipelineState:pipeline];
  [encoder setBuffer:input offset:offset atIndex:0];
  [encoder setBuffer:output offset:offset atIndex:1];
  [encoder setBuffer:scale offset:0 atIndex:2];
  [encoder dispatchThreadgroups:(MTLSize){256/local,1,1} threadsPerThreadgroup:(MTLSize){local,1,1}];
  [encoder endEncoding];[command commit];[command waitUntilCompleted];
  ok=[command status]==4&&![command error];
  for(unsigned i=0;i<output->mapped/4;i++) {
   float expected=i>=start&&i<start+256?(float)(i-start+7)*factor+(i-start):-777;
   if(out[i]!=expected) ok=0;
  }
  for(unsigned i=0;i<input->mapped/4;i++) if(in[i]!=(i>=start&&i<start+256?(float)(i-start+7):-999)) ok=0;
  unsigned *guard=[scale contents];for(unsigned i=1;i<scale->mapped/4;i++) if(guard[i])ok=0;
  if(*(float *)[scale contents]!=factor) ok=0;
 }
 // Four compiled variants must have served eight dispatches, including changed
 // descriptor offsets: resource bindings must not be part of the code cache.
 if(pipeline&&pipeline->nextVariant!=4) ok=0;
 [scale release];[output release];[input release];[queue release];[pipeline release];[device release];
 if(ok) MARK("PASS runtime_pipeline 8_dispatches 2048_GPU_outputs local_1_8_32_64 offsets_128_384 cache_reuse full_guards");
 else MARK("FAIL runtime_pipeline_outputs_or_cache");
 return ok?0:1;
}
