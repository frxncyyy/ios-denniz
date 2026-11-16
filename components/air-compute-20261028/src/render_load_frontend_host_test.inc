static void test_native_load_frontend(TeguComputeDevice *d,TeguRenderPipeline *pipeline) {
 NSUInteger baseline=[d currentAllocatedSize];unsigned saved=loadDraws;
 TextureDesc *td=[[TextureDesc alloc] init];td->format=70;td->w=td->h=16;td->uses=4;
 TeguTexture *target=[d newTextureWithDescriptor:(id)td];assert(target);
 unsigned char *pixels=[target->buffer contents];for(unsigned i=0;i<1024;i++)pixels[i]=(unsigned char)(i*13u+7u);
 unsigned char before[1024];memcpy(before,pixels,sizeof(before));
 RenderPass *pass=[[RenderPass alloc] init];pass->color=[[PassColor alloc] init];
 pass->color->target=[target retain];pass->color->load=1;pass->color->store=1;
 TeguQueue *q=[d newCommandQueue];
 unsigned caps=d->capabilities;
 /* LOAD is explicitly capability-gated before an encoder can be committed. */
 d->capabilities=caps&~(unsigned)TEGU_GPU_CAP_LOAD_PASS;
 TeguCommand *denied=[q commandBuffer];assert(![denied renderCommandEncoderWithDescriptor:(id)pass]);
 [denied commit];assert([denied status]==5&&loadDraws==saved);
 d->capabilities=caps|TEGU_GPU_CAP_LOAD_PASS;
 TeguCommand *command=[q commandBuffer];TeguRenderEncoder *encoder=[command renderCommandEncoderWithDescriptor:(id)pass];assert(encoder);
 [encoder setRenderPipelineState:pipeline];[encoder drawPrimitives:3 vertexStart:0 vertexCount:3];[encoder endEncoding];
 assert(command->count==1&&TEGU_JOB(command->jobChunks,0).targetRequest.version==TEGU_RENDER_TARGET_REQUEST_VERSION_LOAD);
 assert(TEGU_JOB(command->jobChunks,0).targetRequest.reserved[0]==TEGU_RENDER_PASS_LOAD);
 [command commit];assert([command status]==4&&loadDraws==saved+1);
 assert(!memcmp(before,[target->buffer contents],sizeof(before)));
 /* A larger LOAD target must refuse, not silently submit a16x16 subregion. */
 for(unsigned mode=0;mode<3;mode++) {
  td->w=td->h=mode==0?32:16;td->format=mode==1?80:70;
  TeguTexture *unsupported=[d newTextureWithDescriptor:(id)td];assert(unsupported);
  if(mode==2)unsupported->surfaceImported=1;
  [pass->color->target release];pass->color->target=[unsupported retain];
  TeguCommand *bad=[q commandBuffer];assert(![bad renderCommandEncoderWithDescriptor:(id)pass]);
  [bad commit];assert([bad status]==5&&loadDraws==saved+1);
  unsupported->surfaceImported=0;[unsupported release];
 }
 [pass->color->target release];pass->color->target=[target retain];
 /* Unsupported stores and imported/noncanonical targets are refused at pass creation. */
 pass->color->store=0;TeguCommand *badStore=[q commandBuffer];assert(![badStore renderCommandEncoderWithDescriptor:(id)pass]);[badStore commit];assert([badStore status]==5);
 pass->color->store=1;pass->color->load=2;TeguCommand *clear=[q commandBuffer];encoder=[clear renderCommandEncoderWithDescriptor:(id)pass];assert(encoder);
 [encoder setRenderPipelineState:pipeline];[encoder drawPrimitives:3 vertexStart:0 vertexCount:3];[encoder endEncoding];
 assert(TEGU_JOB(clear->jobChunks,0).targetRequest.version==0&&TEGU_JOB(clear->jobChunks,0).renderArgs[3]==0x44332211);
 [clear commit];assert([clear status]==4&&loadDraws==saved+1);
 d->capabilities=caps;
 [q release];[pass release];[target release];[td release];drain();
 assert([d currentAllocatedSize]==baseline&&allocated==freed);
 puts("PASS native LOAD pass snapshot: v2 load action, bound target/pipeline retention, store requirement and capability refusal; mock transport only");
}
