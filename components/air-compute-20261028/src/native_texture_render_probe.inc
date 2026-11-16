/* Public Metal normalized texture sampling through authored AIR. */
static int tegu_native_texture_render_probe_shape(object device,const void *air,usize air_bytes,
 void *objc,void *send,selector (*sel)(const char *),unsigned side,unsigned source_format) {
 if((side!=16&&side!=32&&side!=64&&side!=256)||(source_format!=70&&source_format!=115))return 1;
 printf("TEGU_EXTENDED_TEXTURE_BEGIN side=%u source=%u target=70\n",side,source_format);
 journal("TEGU_RESULT texture_render_begin",-1);
 object (*cls)(const char *)=dlsym(objc,"objc_getClass");
 object (*get)(object,selector)=(void *)send;
 void (*release)(object,selector)=(void *)send;
 void (*set)(object,selector,object)=(void *)send;
 void (*number)(object,selector,usize)=(void *)send;
 void (*real)(object,selector,float)=(void *)send;
 void *(*push)(void)=dlsym(objc,"objc_autoreleasePoolPush");
 void (*pop)(void *)=dlsym(objc,"objc_autoreleasePoolPop");
 void *system=dlopen("/usr/lib/libSystem.B.dylib",2);
 void *(*allocate)(usize)=system?dlsym(system,"malloc"):0;
 void (*dispose)(void *)=system?dlsym(system,"free"):0;
 object (*dataCreate)(const void *,usize,void *,void *)=system?dlsym(system,"dispatch_data_create"):0;
 void (*dataRelease)(object)=system?dlsym(system,"dispatch_release"):0;
 if(!device||!air||!air_bytes||!cls||!push||!pop||!dataCreate||!dataRelease||!allocate||!dispose)return 1;
 usize baseline=((usize (*)(object,selector))send)(device,sel("currentAllocatedSize"));
 void *pool=push();object error=0;
 object data=dataCreate(air,air_bytes,0,0);
 object library=data?((object (*)(object,selector,object,object *))send)
  (device,sel("newLibraryWithData:error:"),data,&error):0;
 if(data)dataRelease(data);
 object vertexName=((object (*)(object,selector,const char *))send)(cls("NSString"),sel("stringWithUTF8String:"),"texture_vertex");
 object fragmentName=((object (*)(object,selector,const char *))send)(cls("NSString"),sel("stringWithUTF8String:"),"texture_fragment");
 object vertex=library?((object (*)(object,selector,object))send)(library,sel("newFunctionWithName:"),vertexName):0;
 object fragment=library?((object (*)(object,selector,object))send)(library,sel("newFunctionWithName:"),fragmentName):0;
 object descriptor=get(cls("MTLRenderPipelineDescriptor"),sel("new"));
 set(descriptor,sel("setVertexFunction:"),vertex);set(descriptor,sel("setFragmentFunction:"),fragment);
 number(descriptor,sel("setRasterSampleCount:"),1);number(descriptor,sel("setMaxVertexAmplificationCount:"),1);
 number(descriptor,sel("setInputPrimitiveTopology:"),3);
 object color=((object (*)(object,selector,usize))send)(get(descriptor,sel("colorAttachments")),sel("objectAtIndexedSubscript:"),0);
 number(color,sel("setPixelFormat:"),70);number(color,sel("setWriteMask:"),15);
 object layout=get(cls("MTLVertexDescriptor"),sel("vertexDescriptor"));
 for(unsigned i=0;i<2;i++) {
  object attribute=((object (*)(object,selector,usize))send)(get(layout,sel("attributes")),sel("objectAtIndexedSubscript:"),7+i);
  number(attribute,sel("setFormat:"),31);number(attribute,sel("setOffset:"),16*i);number(attribute,sel("setBufferIndex:"),1);
 }
 object binding=((object (*)(object,selector,usize))send)(get(layout,sel("layouts")),sel("objectAtIndexedSubscript:"),1);
 number(binding,sel("setStride:"),32);number(binding,sel("setStepFunction:"),1);number(binding,sel("setStepRate:"),1);
 set(descriptor,sel("setVertexDescriptor:"),layout);
 journal("TEGU_RESULT texture_render_compile_begin",-1);
 object pipeline=vertex&&fragment?((object (*)(object,selector,object,object *))send)
  (device,sel("newRenderPipelineStateWithDescriptor:error:"),descriptor,&error):0;
 release(descriptor,sel("release"));release(vertex,sel("release"));release(fragment,sel("release"));release(library,sel("release"));
 int failed=!pipeline||error;
 journal("TEGU_RESULT texture_render_compile",failed?1:0);
 if(failed)printf("TEGU_TEXTURE_RENDER compile library=%d vertex=%d fragment=%d pipeline=%d error=%d\n",library!=0,vertex!=0,fragment!=0,pipeline!=0,error!=0);
 if(failed){release(pipeline,sel("release"));pop(pool);journal("TEGU_RESULT texture_render_public_api",1);return 1;}

 object targetDescriptor=get(cls("MTLTextureDescriptor"),sel("new"));
 number(targetDescriptor,sel("setTextureType:"),2);number(targetDescriptor,sel("setPixelFormat:"),70);
 number(targetDescriptor,sel("setWidth:"),side);number(targetDescriptor,sel("setHeight:"),side);
 number(targetDescriptor,sel("setDepth:"),1);number(targetDescriptor,sel("setMipmapLevelCount:"),1);
 number(targetDescriptor,sel("setArrayLength:"),1);number(targetDescriptor,sel("setSampleCount:"),1);
 number(targetDescriptor,sel("setResourceOptions:"),0);number(targetDescriptor,sel("setUsage:"),4);
 object target=((object (*)(object,selector,object))send)(device,sel("newTextureWithDescriptor:"),targetDescriptor);
 release(targetDescriptor,sel("release"));
 object queue=get(device,sel("newCommandQueue"));
 object buffer=((object (*)(object,selector,usize,usize))send)(device,sel("newBufferWithLength:options:"),4096,0);
 float *mapped=buffer?(float *)get(buffer,sel("contents")):0;
 failed=!target||!queue||!buffer||!mapped;
 if(!failed)for(unsigned i=0;i<4096/16;i++){mapped[i*4]=3;mapped[i*4+1]=3;mapped[i*4+2]=.5f;mapped[i*4+3]=1;}
 const float positions[3][4]={{-.75f,-.75f,.5f,1},{.75f,-.75f,.5f,1},{-.75f,.75f,.5f,1}};
 const float uv[3][4]={{.125f,.125f,0,1},{.875f,.125f,0,1},{.125f,.875f,0,1}};
 const usize offsets[2]={0,256};
 if(!failed)for(unsigned run=0;run<2;run++)for(unsigned i=0;i<3;i++)for(unsigned j=0;j<4;j++) {
  mapped[offsets[run]/4+i*8+j]=positions[i][j];mapped[offsets[run]/4+i*8+4+j]=uv[i][j];
 }
 journal("TEGU_RESULT texture_render_target",failed?1:0);
 for(unsigned run=0;run<2&&!failed;run++) {
  void *passPool=push();
  object sourceDescriptor=get(cls("MTLTextureDescriptor"),sel("new"));
  number(sourceDescriptor,sel("setTextureType:"),2);number(sourceDescriptor,sel("setPixelFormat:"),source_format);
  number(sourceDescriptor,sel("setWidth:"),16);number(sourceDescriptor,sel("setHeight:"),16);
  number(sourceDescriptor,sel("setDepth:"),1);number(sourceDescriptor,sel("setMipmapLevelCount:"),1);
  number(sourceDescriptor,sel("setArrayLength:"),1);number(sourceDescriptor,sel("setSampleCount:"),1);
  number(sourceDescriptor,sel("setResourceOptions:"),0);number(sourceDescriptor,sel("setUsage:"),1);
  object source=((object (*)(object,selector,object))send)(device,sel("newTextureWithDescriptor:"),sourceDescriptor);
  release(sourceDescriptor,sel("release"));
  union {unsigned rgba[512];unsigned short half[1024];unsigned char bytes[2048];} texels;
  const unsigned short h[16]={0x0000,0x2c00,0x3000,0x3200,0x3400,0x3500,0x3600,0x3700,
    0x3800,0x3880,0x3900,0x3980,0x3a00,0x3a80,0x3b00,0x3b80};
  for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++) {
   if(source_format==70)texels.rgba[y*16+x]=0xff000000u|(17*x)|((17*y)<<8)|((17*(x^y))<<16);
   else {unsigned n=(y*16+x)*4;texels.half[n]=h[x];texels.half[n+1]=h[y];texels.half[n+2]=h[x^y];texels.half[n+3]=0x3c00;}
  }
  struct probe_origin {usize x,y,z;};struct probe_size {usize width,height,depth;};
  struct probe_region {struct probe_origin origin;struct probe_size size;};
  struct probe_region full={{0,0,0},{side,side,1}},sourceFull={{0,0,0},{16,16,1}};
  unsigned sourceStride=source_format==70?64:128,sourceBytes=sourceStride*16;
  if(source)((void (*)(object,selector,struct probe_region,usize,const void *,usize))send)
   (source,sel("replaceRegion:mipmapLevel:withBytes:bytesPerRow:"),sourceFull,0,texels.bytes,sourceStride);
  unsigned char upload[2050];for(unsigned i=0;i<sizeof(upload);i++)upload[i]=0xa5;
  if(source)((void (*)(object,selector,void *,usize,struct probe_region,usize))send)
   (source,sel("getBytes:bytesPerRow:fromRegion:mipmapLevel:"),upload+1,sourceStride,sourceFull,0);
  unsigned uploadBad=upload[0]!=0xa5||upload[sourceBytes+1]!=0xa5;
  for(unsigned i=0;i<sourceBytes;i++)uploadBad+=upload[i+1]!=texels.bytes[i];
  if(!source||uploadBad){failed=1;printf("TEGU_EXTENDED_TEXTURE upload source=%d mismatch=%u side=%u format=%u\n",source!=0,uploadBad,side,source_format);
   release(source,sel("release"));pop(passPool);break;}

  object samplerDescriptor=get(cls("MTLSamplerDescriptor"),sel("new"));
  number(samplerDescriptor,sel("setMinFilter:"),0);number(samplerDescriptor,sel("setMagFilter:"),0);
  number(samplerDescriptor,sel("setMipFilter:"),0);
  // MTLAddressModeClampToEdge is 0: https://raw.githubusercontent.com/apple/metal-cpp/main/Metal/MTLSampler.hpp
  number(samplerDescriptor,sel("setSAddressMode:"),0);number(samplerDescriptor,sel("setTAddressMode:"),0);
  number(samplerDescriptor,sel("setRAddressMode:"),0);number(samplerDescriptor,sel("setNormalizedCoordinates:"),1);
  number(samplerDescriptor,sel("setCompareFunction:"),0);number(samplerDescriptor,sel("setMaxAnisotropy:"),1);
  real(samplerDescriptor,sel("setLodMinClamp:"),0);real(samplerDescriptor,sel("setLodMaxClamp:"),0);
  object sampler=((object (*)(object,selector,object))send)(device,sel("newSamplerStateWithDescriptor:"),samplerDescriptor);
  release(samplerDescriptor,sel("release"));
  object pass=get(cls("MTLRenderPassDescriptor"),sel("renderPassDescriptor"));
  object attachment=((object (*)(object,selector,usize))send)(get(pass,sel("colorAttachments")),sel("objectAtIndexedSubscript:"),0);
  set(attachment,sel("setTexture:"),target);number(attachment,sel("setLoadAction:"),2);number(attachment,sel("setStoreAction:"),1);
  struct probe_clear clearValue={(double)0xcc/255,(double)0x66/255,(double)0x33/255,1};
  ((void (*)(object,selector,struct probe_clear))send)(attachment,sel("setClearColor:"),clearValue);
  object command=get(queue,sel("commandBuffer"));
  object encoder=command?((object (*)(object,selector,object))send)(command,sel("renderCommandEncoderWithDescriptor:"),pass):0;
  if(!source||!sampler||!command||!encoder){failed=1;release(source,sel("release"));release(sampler,sel("release"));pop(passPool);break;}
  set(encoder,sel("setRenderPipelineState:"),pipeline);
  ((void (*)(object,selector,object,usize,usize))send)(encoder,sel("setVertexBuffer:offset:atIndex:"),buffer,offsets[run],1);
  ((void (*)(object,selector,object,usize))send)(encoder,sel("setFragmentTexture:atIndex:"),source,0);
  ((void (*)(object,selector,object,usize))send)(encoder,sel("setFragmentSamplerState:atIndex:"),sampler,0);
  ((void (*)(object,selector,usize,usize,usize))send)(encoder,sel("drawPrimitives:vertexStart:vertexCount:"),3,0,3);
  if(run==1) {
   ((void (*)(object,selector,object,usize,usize))send)(encoder,sel("setVertexBuffer:offset:atIndex:"),0,0,1);
   ((void (*)(object,selector,object,usize))send)(encoder,sel("setFragmentTexture:atIndex:"),0,0);
   ((void (*)(object,selector,object,usize))send)(encoder,sel("setFragmentSamplerState:atIndex:"),0,0);
   release(buffer,sel("release"));buffer=0;
   release(source,sel("release"));source=0;release(sampler,sel("release"));sampler=0;
  }
  get(encoder,sel("endEncoding"));unsigned callbacks=0;
  tegu_probe_completed(send,sel("addCompletedHandler:"),command,&callbacks);
  journal("TEGU_RESULT texture_render_submit_begin",-1);
  get(command,sel("commit"));get(command,sel("waitUntilCompleted"));
  usize status=((usize (*)(object,selector))send)(command,sel("status"));
  if(status!=4||get(command,sel("error"))||callbacks!=2) {
   printf("TEGU_TEXTURE_RENDER failed run=%u status=%lu callbacks=%u\n",run,status,callbacks);failed=1;
  }
  unsigned count=side*side;unsigned *pixels=allocate((count+2)*sizeof(*pixels));
  if(!pixels){failed=1;release(source,sel("release"));release(sampler,sel("release"));pop(passPool);break;}
  for(unsigned i=0;i<count+2;i++)pixels[i]=0xdeadbeef;
  ((void (*)(object,selector,void *,usize,struct probe_region,usize))send)
   (target,sel("getBytes:bytesPerRow:fromRegion:mipmapLevel:"),pixels+1,side*4,full,0);
  unsigned checked=0,interior=0,mismatches=0;
  for(unsigned i=0;i<count;i++) {
   unsigned x=i%side,y=i/side,start=side/8;if(x>=start&&y>=start&&x+y==side-1)continue;
   int inside=x>=start&&y>=start&&x+y<side-1;
   unsigned tx=((2*x+1)*8)/side,ty=((2*y+1)*8)/side;
   unsigned rr=source_format==70?17*tx:(tx*255+8)/16;
   unsigned gg=source_format==70?17*ty:(ty*255+8)/16;
   unsigned bb=source_format==70?17*(tx^ty):((tx^ty)*255+8)/16;
   unsigned expected=inside?(0xff000000u|rr|(gg<<8)|(bb<<16)):0xff3366ccu;
   checked++;interior+=inside;
   if(pixels[i+1]!=expected) {
    if(mismatches<8)printf("TEGU_TEXTURE_RENDER mismatch run=%u pixel=%u got=%08x expected=%08x\n",run,i,pixels[i+1],expected);
    mismatches++;
   }
  }
  int guards=pixels[0]==0xdeadbeef&&pixels[count+1]==0xdeadbeef;
  int runFailed=status!=4||get(command,sel("error"))||callbacks!=2||mismatches||checked!=count-3*side/4||interior!=(3*side/4-1)*(3*side/4)/2||!guards;
  printf("TEGU_TEXTURE_RENDER run=%u checked=%u interior=%u mismatches=%u callbacks=%u readback_guards=%d\n",run,checked,interior,mismatches,callbacks,guards);
  failed|=runFailed;journal(run==0?"TEGU_RESULT texture_render_first":"TEGU_RESULT texture_render_retained",runFailed?1:0);
  dispose(pixels);release(source,sel("release"));release(sampler,sel("release"));
  pop(passPool);
 }
 release(buffer,sel("release"));release(queue,sel("release"));release(target,sel("release"));release(pipeline,sel("release"));pop(pool);
 usize allocated=((usize (*)(object,selector))send)(device,sel("currentAllocatedSize"));
 if(allocated!=baseline){failed=1;printf("TEGU_TEXTURE_RENDER allocation baseline=%lu final=%lu\n",baseline,allocated);}
 printf("TEGU_EXTENDED_TEXTURE_END side=%u source=%u failed=%d\n",side,source_format,failed);
 journal("TEGU_RESULT texture_render_public_api",failed?1:0);return failed;
}

static int tegu_native_texture_render_probe(object device,const void *air,usize bytes,void *objc,void *send,selector (*sel)(const char *)) {
 if(tegu_native_texture_render_probe_shape(device,air,bytes,objc,send,sel,16,70))return 1;
#ifdef TEGU_EXTENDED_TEXTURE_RENDER_PROBE
 const unsigned shapes[4][2]={{16,115},{32,70},{64,70},{256,70}};
 for(unsigned i=0;i<4;i++)if(tegu_native_texture_render_probe_shape(device,air,bytes,objc,send,sel,shapes[i][0],shapes[i][1]))return 1;
#endif
 return 0;
}
