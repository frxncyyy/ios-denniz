/* Compiled generic float4 varying through actual donor descriptors + system device.
 * No embedded driver, no CPU renderer: newLibrary/newPipeline/encoder/readback. */
#include "tegu-varying-air.h"
#include "gpu_varying_oracle.h"
static int tegu_native_varying_render_probe(object device,void *objc,void *send,selector (*sel)(const char *)) {
 journal("TEGU_RESULT varying_render_begin",-1);
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
 if(!cls||!push||!pop||!dataCreate||!dataRelease)return 1;
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
 object descriptor=get(cls("MTLRenderPipelineDescriptor"),sel("new"));
 set(descriptor,sel("setVertexFunction:"),vertex);set(descriptor,sel("setFragmentFunction:"),fragment);
 number(descriptor,sel("setRasterSampleCount:"),1);
 number(descriptor,sel("setMaxVertexAmplificationCount:"),1);
 number(descriptor,sel("setInputPrimitiveTopology:"),3);
 object color=((object (*)(object,selector,usize))send)(get(descriptor,sel("colorAttachments")),sel("objectAtIndexedSubscript:"),0);
 number(color,sel("setPixelFormat:"),70);number(color,sel("setWriteMask:"),15);
 // Authored AIR: attribute7 float4 position offset0, attribute8 float4 color offset16, buffer1 stride32.
 object layout=get(cls("MTLVertexDescriptor"),sel("vertexDescriptor"));
 for(unsigned i=0;i<2;i++) {
  object attribute=((object (*)(object,selector,usize))send)(get(layout,sel("attributes")),sel("objectAtIndexedSubscript:"),7+i);
  number(attribute,sel("setFormat:"),31);number(attribute,sel("setOffset:"),16*i);number(attribute,sel("setBufferIndex:"),1);
 }
 object binding=((object (*)(object,selector,usize))send)(get(layout,sel("layouts")),sel("objectAtIndexedSubscript:"),1);
 number(binding,sel("setStride:"),32);number(binding,sel("setStepFunction:"),1);number(binding,sel("setStepRate:"),1);
 set(descriptor,sel("setVertexDescriptor:"),layout);
 journal("TEGU_RESULT varying_render_compile_begin",-1);
 object pipeline=vertex&&fragment?((object (*)(object,selector,object,object *))send)
  (device,sel("newRenderPipelineStateWithDescriptor:error:"),descriptor,&error):0;
 release(descriptor,sel("release"));release(vertex,sel("release"));release(fragment,sel("release"));release(library,sel("release"));
 journal("TEGU_RESULT varying_render_compile",pipeline&&!error?0:1);
 if(!pipeline||error){
  printf("TEGU_VARYING_RENDER compile library=%d vertex=%d fragment=%d pipeline=%d error=%d\n",library!=0,vertex!=0,fragment!=0,pipeline!=0,error!=0);
  release(pipeline,sel("release"));pop(pool);return 1;
 }
 object textureDescriptor=get(cls("MTLTextureDescriptor"),sel("new"));
 number(textureDescriptor,sel("setTextureType:"),2);number(textureDescriptor,sel("setPixelFormat:"),70);
 number(textureDescriptor,sel("setWidth:"),16);number(textureDescriptor,sel("setHeight:"),16);
 number(textureDescriptor,sel("setDepth:"),1);number(textureDescriptor,sel("setMipmapLevelCount:"),1);
 number(textureDescriptor,sel("setArrayLength:"),1);number(textureDescriptor,sel("setSampleCount:"),1);
 number(textureDescriptor,sel("setResourceOptions:"),0);number(textureDescriptor,sel("setUsage:"),4);
 object texture=((object (*)(object,selector,object))send)(device,sel("newTextureWithDescriptor:"),textureDescriptor);
 release(textureDescriptor,sel("release"));
 object queue=get(device,sel("newCommandQueue"));
 object buffer=((object (*)(object,selector,usize,usize))send)(device,sel("newBufferWithLength:options:"),4096,0);
 float *mapped=(float *)get(buffer,sel("contents"));
 int failed=!texture||!queue||!buffer||!mapped;
 // Off-screen poison: a wrong offset/attribute fetch cannot cover the interior.
 if(!failed)for(unsigned i=0;i<4096/16;i++){mapped[i*4]=3;mapped[i*4+1]=3;mapped[i*4+2]=.5f;mapped[i*4+3]=1;}
 // Same vertices as gpu_varying_oracle.h and gpu_compiled_varying_image.py.
 const float positions[3][4]={{-.75f,-.75f,.5f,1},{.75f,-.75f,.5f,1},{-.75f,.75f,.5f,1}};
 const float colors[2][3][4]={{{.2f,.4f,.8f,.6f},{.2f,.4f,.8f,.6f},{.2f,.4f,.8f,.6f}},
  {{1,0,0,.6f},{0,1,0,.6f},{0,0,1,.6f}}};
 const usize offsets[2]={0,256};
 if(!failed)for(unsigned run=0;run<2;run++)for(unsigned i=0;i<3;i++)for(unsigned j=0;j<4;j++){
  mapped[offsets[run]/4+i*8+j]=positions[i][j];mapped[offsets[run]/4+i*8+4+j]=colors[run][i][j];
 }
 journal("TEGU_RESULT varying_render_target",failed?1:0);
 const unsigned clears[2]={0xff112233,0xff996633};
 const char *phases[2]={"TEGU_RESULT varying_render_constant","TEGU_RESULT varying_render_gradient"};
 for(unsigned run=0;run<2&&!failed;run++) {
  void *passPool=push();
  object pass=get(cls("MTLRenderPassDescriptor"),sel("renderPassDescriptor"));
  object attachment=((object (*)(object,selector,usize))send)(get(pass,sel("colorAttachments")),sel("objectAtIndexedSubscript:"),0);
  set(attachment,sel("setTexture:"),texture);number(attachment,sel("setLoadAction:"),2);number(attachment,sel("setStoreAction:"),1);
  unsigned clear=clears[run];struct probe_clear value={(clear&255)/255.,((clear>>8)&255)/255.,((clear>>16)&255)/255.,1.};
  ((void (*)(object,selector,struct probe_clear))send)(attachment,sel("setClearColor:"),value);
  object command=get(queue,sel("commandBuffer"));
  object encoder=((object (*)(object,selector,object))send)(command,sel("renderCommandEncoderWithDescriptor:"),pass);
  if(!command||!encoder){failed=1;journal(phases[run],1);pop(passPool);break;}
  set(encoder,sel("setRenderPipelineState:"),pipeline);
  ((void (*)(object,selector,object,usize,usize))send)(encoder,sel("setVertexBuffer:offset:atIndex:"),buffer,offsets[run],1);
  ((void (*)(object,selector,usize,usize,usize))send)(encoder,sel("drawPrimitives:vertexStart:vertexCount:"),3,0,3);
  if(run==1) {
   // The recorded draw must retain the buffer after unbind and caller release.
   ((void (*)(object,selector,object,usize,usize))send)(encoder,sel("setVertexBuffer:offset:atIndex:"),0,0,1);
   release(buffer,sel("release"));buffer=0;
  }
  get(encoder,sel("endEncoding"));
  unsigned callbacks=0;tegu_probe_completed(send,sel("addCompletedHandler:"),command,&callbacks);
  journal("TEGU_RESULT varying_render_submit_begin",-1);
  get(command,sel("commit"));get(command,sel("waitUntilCompleted"));
  usize status=((usize (*)(object,selector))send)(command,sel("status"));
  if(status!=4||get(command,sel("error"))||callbacks!=2) {
   printf("TEGU_VARYING_RENDER failed run=%u status=%lu callbacks=%u\n",run,status,callbacks);
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
   // Constant is exact as on the standalone GPU test; gradient uses the shared oracle.
   unsigned expected=!inside?clear:run==0?0x99cc6633:tegu_varying_color(x,y);
   int ok=!inside||run==0?got==expected:tegu_varying_matches(got,expected);
   checked++;interior+=inside;if(!ok) {
    if(mismatches<8)printf("TEGU_VARYING_RENDER mismatch run=%u pixel=%u got=%08x expected=%08x\n",run,i,got,expected);
    mismatches++;
   }
  }
  failed=mismatches||checked!=244||interior!=66||pixels[0]!=0xdeadbeef||pixels[257]!=0xdeadbeef;
  printf("TEGU_VARYING_RENDER run=%u checked=%u interior=%u mismatches=%u callbacks=%u readback_guards=%d\n",run,checked,interior,mismatches,callbacks,pixels[0]==0xdeadbeef&&pixels[257]==0xdeadbeef);
  journal(phases[run],failed?1:0);
  pop(passPool);
 }
 release(buffer,sel("release"));release(queue,sel("release"));release(texture,sel("release"));release(pipeline,sel("release"));pop(pool);
 usize allocated=((usize (*)(object,selector))send)(device,sel("currentAllocatedSize"));
 if(allocated!=baseline){failed=1;printf("TEGU_VARYING_RENDER allocation baseline=%lu final=%lu\n",baseline,allocated);}
 journal("TEGU_RESULT varying_render_public_api",failed?1:0);
 return failed;
}
