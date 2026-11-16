 /* c6o device (app switcher blur): BGRA8 704x96 pyramid levels 2..4 at
  * offsets 337920/354816/359424 (176x24 row 704, 88x12 row 384, 44x6 row
  * 192) were refused 'storage outside target span' (page alignment), and a
  * level next to a larger second attachment 'mismatched color attachments';
  * every blur command was dropped. Each level must render: a level view at its
  * own offset/stride, the larger attachment through a window of the pass
  * area, sampling level L-1 of the same texture without a CPU snapshot. */
 {
  id error=0;RenderDescriptor *pd=c6f_ca_descriptor(dsv,ds4,80,15,0,0);
  TeguRenderPipeline *p=[d newRenderPipelineStateWithDescriptor:(id)pd error:&error];assert(p&&p->genericDraw);
  TeguTexture *pyr=c6f_texture(d,80,704,96,5,5,0x20),*big=c6f_texture(d,80,704,96,1,5,0x20);
  assert(pyr&&big&&pyr->levelCount==5);
  fprintf(stderr,"C6O_PYRAMID len=%llu offs=%llu/%llu/%llu rows=%lu/%lu/%lu\n",(unsigned long long)pyr->buffer->logical,
   (unsigned long long)pyr->mip[2].offset,(unsigned long long)pyr->mip[3].offset,(unsigned long long)pyr->mip[4].offset,
   (unsigned long)pyr->mip[2].stride,(unsigned long)pyr->mip[3].stride,(unsigned long)pyr->mip[4].stride);
  assert(pyr->mip[2].offset==337920&&pyr->mip[3].offset==354816&&pyr->mip[4].offset==359424);
  assert(pyr->mip[2].stride==704&&pyr->mip[3].stride==384&&pyr->mip[4].stride==192);
  TeguBuffer *ib=[d newBufferWithLength:64 options:0];uint16_t idx[6]={0,1,2,2,1,3};memcpy([ib contents],idx,sizeof(idx));
  static const unsigned W[5]={704,352,176,88,44},H[5]={96,48,24,12,6};
  for(unsigned withSecond=0;withSecond<2;withSecond++)for(unsigned level=2;level<5;level++){
   RenderPass *pass=[[RenderPass alloc] init];
   pass->color=[[PassColor alloc] init];pass->color->target=[pyr retain];pass->color->lvl=level;pass->color->load=0;pass->color->store=1;
   if(withSecond){pass->second=[[PassColor alloc] init];pass->second->target=[big retain];pass->second->load=0;pass->second->store=0;}
   pass->passWidth=0;pass->passHeight=0;pass->arrayLength=1;pass->sampleCount=1;
   TeguCommand *c=[q commandBuffer];TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)pass];
   fprintf(stderr,"C6O_LEVEL level=%u second=%u inert=%u invalid=%u at=%s:%u\n",level,withSecond,e?e->inert:9,c->invalid,
    c->invalidFunction?c->invalidFunction:"-",c->invalidLine);
   assert(e&&!e->inert&&!c->invalid);
   TeguTexture *rt0=e->renderTargets[0];
   assert(rt0->textureWidth==W[level]&&rt0->textureHeight==H[level]&&rt0->mip[0].offset==pyr->mip[level].offset&&rt0->rowBytes==pyr->mip[level].stride);
   if(withSecond){TeguTexture *rt1=e->renderTargets[1];
    assert(rt1&&rt1!=big&&rt1->textureWidth==W[level]&&rt1->textureHeight==H[level]&&rt1->rowBytes==big->rowBytes&&
     rt1->mip[0].offset==big->mip[0].offset&&rt1->buffer==big->buffer);}
   TeguTexture *prev=[pyr newTextureViewWithPixelFormat:80 textureType:2 levels:(TeguTextureViewRange){level-1,1} slices:(TeguTextureViewRange){0,1}];
   assert(prev&&prev->textureWidth==W[level-1]);
   [e setRenderPipelineState:p];[e setVertexBuffer:vb offset:0 atIndex:1];
   for(unsigned k=0;k<4;k++){[e setFragmentTexture:prev atIndex:k];[e setFragmentSamplerState:samp atIndex:k];}
   [e drawIndexedPrimitives:3 indexCount:6 indexType:0 indexBuffer:ib indexBufferOffset:0];
   [e endEncoding];
   fprintf(stderr,"C6O_LEVEL_DRAW level=%u second=%u invalid=%u at=%s:%u count=%u\n",level,withSecond,c->invalid,
    c->invalidFunction?c->invalidFunction:"-",c->invalidLine,c->count);
   assert(!c->invalid&&c->count>=1);
   {struct Dispatch *dj=tegu_command_job(c,c->count-1);const struct tegu_generic_draw_request *rq=&dj->genericDrawRequest;
    fprintf(stderr,"C6O_LEVEL_REQ level=%u %ux%u off=%llu row=%u bindings=%u\n",level,rq->width,rq->height,
     (unsigned long long)rq->target_offset,rq->row_bytes,rq->binding_count);
    for(unsigned bi=0;bi<rq->binding_count;bi++)fprintf(stderr,"C6O_LEVEL_BIND kind=%u tok=%d off=%llu bytes=%llu %ux%u\n",
     rq->bindings[bi].kind,rq->bindings[bi].memory.token==rq->target_token,(unsigned long long)rq->bindings[bi].memory.offset,
     (unsigned long long)rq->bindings[bi].memory.bytes,rq->bindings[bi].width,rq->bindings[bi].height);
    assert(dj->genericDrawSnapshot&&rq->width==W[level]&&rq->height==H[level]&&rq->target_offset==pyr->mip[level].offset&&
     rq->row_bytes==pyr->mip[level].stride);}
   [c commit];[c waitUntilCompleted];
   fprintf(stderr,"C6O_LEVEL_DONE level=%u second=%u status=%lu error=%p\n",level,withSecond,(unsigned long)[c status],[c error]);
   assert([c status]==4&&![c error]);checks++;
   [prev release];[pass release];
  }
  [ib release];[pyr release];[big release];[p release];[pd release];
 }
