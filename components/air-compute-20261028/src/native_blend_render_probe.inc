/* Fixed-function blending through public Metal and actual donor descriptors.
 * No embedded driver, no CPU renderer: newLibrary/newPipeline/encoder/readback. */
#include "gpu_blend_oracle.h" /* AIR bytes are declared by the varying probe. */
#ifdef TEGU_IOSURFACE_RENDER_PROBE
#include "native_iosurface_render_probe.inc"
#endif
static int tegu_native_blend_render_probe_target(object device,void *objc,void *send,
 selector (*sel)(const char *),unsigned pixelFormat,int sharedSurface) {
#define BLEND_PHASE(name) (sharedSurface ? (bgra ? "TEGU_RESULT iosurface_bgra_" name : "TEGU_RESULT iosurface_rgba_" name) : (bgra ? "TEGU_RESULT bgra_" name : "TEGU_RESULT " name))
#define BLEND_LOG(text) (sharedSurface ? (bgra ? "TEGU_IOSURFACE_BGRA_BLEND_RENDER " text : "TEGU_IOSURFACE_RGBA_BLEND_RENDER " text) : (bgra ? "TEGU_BGRA_BLEND_RENDER " text : "TEGU_BLEND_RENDER " text))
 int bgra=pixelFormat==80;
 journal(BLEND_PHASE("blend_render_begin"),-1);
 object (*cls)(const char *)=dlsym(objc,"objc_getClass");
 object (*get)(object,selector)=(void *)send;
 void (*release)(object,selector)=(void *)send;
 void (*set)(object,selector,object)=(void *)send;
 void (*number)(object,selector,usize)=(void *)send;
 void *(*push)(void)=dlsym(objc,"objc_autoreleasePoolPush");
 void (*pop)(void *)=dlsym(objc,"objc_autoreleasePoolPop");
 void *system=dlopen("/usr/lib/libSystem.B.dylib",2);
 object (*dataCreate)(const void *,usize,void *,void *)=system?dlsym(system,"dispatch_data_create"):0;
 void (*dataRelease)(object)=system?dlsym(system,"dispatch_release"):0;
 if(!cls||!push||!pop||!dataCreate||!dataRelease) {
  if(bgra||sharedSurface){journal(BLEND_PHASE("blend_render_public_api"),1);journal(BLEND_PHASE("result"),1);}
  return 1;
 }
 usize baseline=((usize (*)(object,selector))send)(device,sel("currentAllocatedSize"));
 void *pool=push();object error=0;
 object data=dataCreate(tegu_varying_air,sizeof(tegu_varying_air),0,0);
 object library=data?((object (*)(object,selector,object,object *))send)
  (device,sel("newLibraryWithData:error:"),data,&error):0;
 if(data)dataRelease(data);
 object vertexName=((object (*)(object,selector,const char *))send)(cls("NSString"),sel("stringWithUTF8String:"),"varying_vertex");
 object fragmentName=((object (*)(object,selector,const char *))send)(cls("NSString"),sel("stringWithUTF8String:"),"varying_fragment");
 object vertex=((object (*)(object,selector,object))send)(library,sel("newFunctionWithName:"),vertexName);
 object fragment=((object (*)(object,selector,object))send)(library,sel("newFunctionWithName:"),fragmentName);
 unsigned cases=bgra?4u:3u;
 object pipelines[4]={0};int compileFailed=!library||!vertex||!fragment;
 for(unsigned run=0;run<cases&&!compileFailed;run++) {
 unsigned mode=bgra?run:run+1;
 object descriptor=get(cls("MTLRenderPipelineDescriptor"),sel("new"));
 set(descriptor,sel("setVertexFunction:"),vertex);set(descriptor,sel("setFragmentFunction:"),fragment);
 number(descriptor,sel("setRasterSampleCount:"),1);
 number(descriptor,sel("setMaxVertexAmplificationCount:"),1);
 number(descriptor,sel("setInputPrimitiveTopology:"),3);
 object color=((object (*)(object,selector,usize))send)(get(descriptor,sel("colorAttachments")),sel("objectAtIndexedSubscript:"),0);
 number(color,sel("setPixelFormat:"),pixelFormat);number(color,sel("setWriteMask:"),15);
 number(color,sel("setBlendingEnabled:"),mode!=0);number(color,sel("setRgbBlendOperation:"),0);
 number(color,sel("setSourceRGBBlendFactor:"),mode==2?1:4);number(color,sel("setDestinationRGBBlendFactor:"),5);
 number(color,sel("setAlphaBlendOperation:"),0);number(color,sel("setSourceAlphaBlendFactor:"),mode==1?4:1);
 number(color,sel("setDestinationAlphaBlendFactor:"),5);
 // Authored AIR: attribute7 float4 position offset0, attribute8 float4 color offset16, buffer1 stride32.
 object layout=get(cls("MTLVertexDescriptor"),sel("vertexDescriptor"));
 for(unsigned i=0;i<2;i++) {
  object attribute=((object (*)(object,selector,usize))send)(get(layout,sel("attributes")),sel("objectAtIndexedSubscript:"),7+i);
  number(attribute,sel("setFormat:"),31);number(attribute,sel("setOffset:"),16*i);number(attribute,sel("setBufferIndex:"),1);
 }
 object binding=((object (*)(object,selector,usize))send)(get(layout,sel("layouts")),sel("objectAtIndexedSubscript:"),1);
 number(binding,sel("setStride:"),32);number(binding,sel("setStepFunction:"),1);number(binding,sel("setStepRate:"),1);
 set(descriptor,sel("setVertexDescriptor:"),layout);
 journal(BLEND_PHASE("blend_render_compile_begin"),-1);
 error=0;
 pipelines[run]=((object (*)(object,selector,object,object *))send)
  (device,sel("newRenderPipelineStateWithDescriptor:error:"),descriptor,&error);
 release(descriptor,sel("release"));
 compileFailed=!pipelines[run]||error;
 journal(BLEND_PHASE("blend_render_compile"),compileFailed?1:0);
 if(compileFailed)printf(BLEND_LOG("compile mode=%u pipeline=%d error=%d\n"),mode,pipelines[run]!=0,error!=0);
 }
 release(vertex,sel("release"));release(fragment,sel("release"));release(library,sel("release"));
 if(compileFailed) {
  for(unsigned i=0;i<cases;i++)release(pipelines[i],sel("release"));
  if(bgra||sharedSurface){journal(BLEND_PHASE("blend_render_public_api"),1);journal(BLEND_PHASE("result"),1);}
  pop(pool);return 1;
 }
 object textureDescriptor=get(cls("MTLTextureDescriptor"),sel("new"));
 number(textureDescriptor,sel("setTextureType:"),2);number(textureDescriptor,sel("setPixelFormat:"),pixelFormat);
 number(textureDescriptor,sel("setWidth:"),16);number(textureDescriptor,sel("setHeight:"),16);
 number(textureDescriptor,sel("setDepth:"),1);number(textureDescriptor,sel("setMipmapLevelCount:"),1);
 number(textureDescriptor,sel("setArrayLength:"),1);number(textureDescriptor,sel("setSampleCount:"),1);
 number(textureDescriptor,sel("setResourceOptions:"),0);number(textureDescriptor,sel("setUsage:"),4);
 object texture=0;
#ifdef TEGU_IOSURFACE_RENDER_PROBE
 struct tegu_probe_surface surface={0};
 if(sharedSurface)texture=tegu_probe_surface_texture(&surface,device,textureDescriptor,pixelFormat,objc,send,sel);
 else
#endif
 texture=((object (*)(object,selector,object))send)(device,sel("newTextureWithDescriptor:"),textureDescriptor);
 release(textureDescriptor,sel("release"));
 object queue=get(device,sel("newCommandQueue"));
 object buffer=((object (*)(object,selector,usize,usize))send)(device,sel("newBufferWithLength:options:"),4096,0);
 float *mapped=(float *)get(buffer,sel("contents"));
 int failed=!texture||!queue||!buffer||!mapped;
 // Off-screen poison: a wrong offset/attribute fetch cannot cover the interior.
 if(!failed)for(unsigned i=0;i<4096/16;i++){mapped[i*4]=3;mapped[i*4+1]=3;mapped[i*4+2]=.5f;mapped[i*4+3]=1;}
 // Same vertices as gpu_varying_oracle.h and gpu_compiled_varying_image.py.
 const float positions[3][4]={{-.75f,-.75f,.5f,1},{.75f,-.75f,.5f,1},{-.75f,.75f,.5f,1}};
 const float colorValue[4]={.2f,.4f,.8f,.6f};const usize offsets[4]={0,256,512,768};
 if(!failed)for(unsigned run=0;run<cases;run++)for(unsigned i=0;i<3;i++)for(unsigned j=0;j<4;j++){
  mapped[offsets[run]/4+i*8+j]=positions[i][j];mapped[offsets[run]/4+i*8+4+j]=colorValue[j];
 }
 journal(BLEND_PHASE("blend_render_target"),failed?1:0);
 const char *rgbaPhases[3]={"TEGU_RESULT blend_render_source_alpha","TEGU_RESULT blend_render_premultiplied","TEGU_RESULT blend_render_separate"};
 const char *bgraPhases[4]={"TEGU_RESULT bgra_render_unblended","TEGU_RESULT bgra_blend_render_source_alpha","TEGU_RESULT bgra_blend_render_premultiplied","TEGU_RESULT bgra_blend_render_separate"};
 const char *surfaceRGBAPhases[3]={"TEGU_RESULT iosurface_rgba_blend_render_source_alpha","TEGU_RESULT iosurface_rgba_blend_render_premultiplied","TEGU_RESULT iosurface_rgba_blend_render_separate"};
 const char *surfaceBGRAPhases[4]={"TEGU_RESULT iosurface_bgra_render_unblended","TEGU_RESULT iosurface_bgra_blend_render_source_alpha","TEGU_RESULT iosurface_bgra_blend_render_premultiplied","TEGU_RESULT iosurface_bgra_blend_render_separate"};
 const char **phases=sharedSurface?(bgra?surfaceBGRAPhases:surfaceRGBAPhases):(bgra?bgraPhases:rgbaPhases);
 for(unsigned run=0;run<cases&&!failed;run++) {
  unsigned mode=bgra?run:run+1;
  void *passPool=push();
  object pass=get(cls("MTLRenderPassDescriptor"),sel("renderPassDescriptor"));
  object attachment=((object (*)(object,selector,usize))send)(get(pass,sel("colorAttachments")),sel("objectAtIndexedSubscript:"),0);
  set(attachment,sel("setTexture:"),texture);number(attachment,sel("setLoadAction:"),2);number(attachment,sel("setStoreAction:"),1);
  unsigned clear=0xff3366cc;struct probe_clear value={(clear&255)/255.,((clear>>8)&255)/255.,((clear>>16)&255)/255.,1.};
  ((void (*)(object,selector,struct probe_clear))send)(attachment,sel("setClearColor:"),value);
  object command=get(queue,sel("commandBuffer"));
  object encoder=((object (*)(object,selector,object))send)(command,sel("renderCommandEncoderWithDescriptor:"),pass);
  if(!command||!encoder){failed=1;journal(phases[run],1);pop(passPool);break;}
  set(encoder,sel("setRenderPipelineState:"),pipelines[run]);
  ((void (*)(object,selector,object,usize,usize))send)(encoder,sel("setVertexBuffer:offset:atIndex:"),buffer,offsets[run],1);
  ((void (*)(object,selector,usize,usize,usize))send)(encoder,sel("drawPrimitives:vertexStart:vertexCount:"),3,0,3);
  if(run==cases-1) {
   // The recorded draw must retain the buffer after unbind and caller release.
   ((void (*)(object,selector,object,usize,usize))send)(encoder,sel("setVertexBuffer:offset:atIndex:"),0,0,1);
   release(buffer,sel("release"));buffer=0;
#ifdef TEGU_IOSURFACE_RENDER_PROBE
   if(sharedSurface&&surface.retained) {
    surface.release(surface.retained);surface.retained=0;
   }
#endif
  }
  get(encoder,sel("endEncoding"));
  unsigned callbacks=0;tegu_probe_completed(send,sel("addCompletedHandler:"),command,&callbacks);
  journal(BLEND_PHASE("blend_render_submit_begin"),-1);
  get(command,sel("commit"));get(command,sel("waitUntilCompleted"));
  usize status=((usize (*)(object,selector))send)(command,sel("status"));
  if(status!=4||get(command,sel("error"))||callbacks!=2) {
   printf(BLEND_LOG("failed run=%u status=%lu callbacks=%u\n"),run,status,callbacks);
   failed=1;journal(phases[run],1);pop(passPool);break;
  }
  unsigned pixels[258];for(unsigned i=0;i<258;i++)pixels[i]=0xdeadbeef;
  struct probe_region region={{0,0,0},{16,16,1}};
  ((void (*)(object,selector,void *,usize,struct probe_region,usize))send)
   (texture,sel("getBytes:bytesPerRow:fromRegion:mipmapLevel:"),pixels+1,64,region,0);
  unsigned checked=0,interior=0,mismatches=0;
  for(unsigned i=0;i<256;i++) {
   unsigned x=i%16,y=i/16;if(x>=2&&y>=2&&x+y==15)continue;
   int inside=x>=2&&y>=2&&x+y<15;unsigned got=pixels[i+1];
   unsigned expected=inside?(mode?tegu_blend_expected(mode):0x99cc6633u):clear;
   if(bgra)expected=tegu_blend_bgra(expected);
   int ok=inside?tegu_blend_matches(got,expected):got==expected;
   checked++;interior+=inside;if(!ok) {
    if(mismatches<8)printf(BLEND_LOG("mismatch run=%u pixel=%u got=%08x expected=%08x\n"),run,i,got,expected);
    mismatches++;
   }
  }
  failed=mismatches||checked!=244||interior!=66||pixels[0]!=0xdeadbeef||pixels[257]!=0xdeadbeef;
#ifdef TEGU_IOSURFACE_RENDER_PROBE
  if(sharedSurface&&tegu_probe_surface_readback(&surface,texture,pixels+1,send,sel))failed=1;
#endif
  printf(BLEND_LOG("run=%u checked=%u interior=%u mismatches=%u callbacks=%u readback_guards=%d\n"),run,checked,interior,mismatches,callbacks,pixels[0]==0xdeadbeef&&pixels[257]==0xdeadbeef);
  journal(phases[run],failed?1:0);
  pop(passPool);
 }
#ifdef TEGU_IOSURFACE_RENDER_PROBE
 if(sharedSurface&&surface.retained)surface.release(surface.retained);
#endif
 release(buffer,sel("release"));release(queue,sel("release"));release(texture,sel("release"));
 for(unsigned i=0;i<cases;i++)release(pipelines[i],sel("release"));
 pop(pool);
 usize allocated=((usize (*)(object,selector))send)(device,sel("currentAllocatedSize"));
 if(allocated!=baseline){failed=1;printf(BLEND_LOG("allocation baseline=%lu final=%lu\n"),baseline,allocated);}
 journal(BLEND_PHASE("blend_render_public_api"),failed?1:0);
 if(bgra||sharedSurface)journal(BLEND_PHASE("result"),failed?1:0);
 return failed;
#undef BLEND_PHASE
#undef BLEND_LOG
}
static int tegu_native_blend_render_probe(object device,void *objc,void *send,selector (*sel)(const char *)) {
 return tegu_native_blend_render_probe_target(device,objc,send,sel,70,0);
}
#ifdef TEGU_BGRA_BLEND_RENDER_PROBE
static int tegu_native_bgra_blend_render_probe(object device,void *objc,void *send,selector (*sel)(const char *)) {
 return tegu_native_blend_render_probe_target(device,objc,send,sel,80,0);
}
#endif

#ifdef TEGU_IOSURFACE_RENDER_PROBE
static int tegu_native_iosurface_render_probe(object device,void *objc,void *send,selector (*sel)(const char *)) {
 int failed=tegu_native_blend_render_probe_target(device,objc,send,sel,70,1);
 if(!failed)failed=tegu_native_blend_render_probe_target(device,objc,send,sel,80,1);
 journal("TEGU_RESULT iosurface_result",failed?1:0);return failed;
}
#endif
