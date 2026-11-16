/* Actual donor descriptors + system-discovered device. No embedded driver and
 * no direct kernel draw calls: newLibrary/newPipeline/newTexture/encoder/readback. */
#include "tegu-vertex-air.h"
static int tegu_native_vertex_render_probe(object device,void *objc,void *send,selector (*sel)(const char *)) {
 journal("TEGU_RESULT vertex_render_begin",-1);
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
 object data=dataCreate(tegu_vertex_air,sizeof(tegu_vertex_air),0,0);
 object library=data?((object (*)(object,selector,object,object *))send)
  (device,sel("newLibraryWithData:error:"),data,&error):0;
 if(data)dataRelease(data);
 object vertexName=((object (*)(object,selector,const char *))send)(cls("NSString"),sel("stringWithUTF8String:"),"test_vertex");
 object fragmentName=((object (*)(object,selector,const char *))send)(cls("NSString"),sel("stringWithUTF8String:"),"test_fragment");
 object vertex=((object (*)(object,selector,object))send)(library,sel("newFunctionWithName:"),vertexName);
 object fragment=((object (*)(object,selector,object))send)(library,sel("newFunctionWithName:"),fragmentName);
 object descriptor=get(cls("MTLRenderPipelineDescriptor"),sel("new"));
 set(descriptor,sel("setVertexFunction:"),vertex);set(descriptor,sel("setFragmentFunction:"),fragment);
 number(descriptor,sel("setRasterSampleCount:"),1);
 number(descriptor,sel("setMaxVertexAmplificationCount:"),1);
 number(descriptor,sel("setInputPrimitiveTopology:"),3);
 object color=((object (*)(object,selector,usize))send)(get(descriptor,sel("colorAttachments")),sel("objectAtIndexedSubscript:"),0);
 number(color,sel("setPixelFormat:"),70);number(color,sel("setWriteMask:"),15);
 object layout=get(cls("MTLVertexDescriptor"),sel("vertexDescriptor"));
 object attribute=((object (*)(object,selector,usize))send)(get(layout,sel("attributes")),sel("objectAtIndexedSubscript:"),7);
 number(attribute,sel("setFormat:"),31);number(attribute,sel("setOffset:"),16);number(attribute,sel("setBufferIndex:"),1);
 object binding=((object (*)(object,selector,usize))send)(get(layout,sel("layouts")),sel("objectAtIndexedSubscript:"),1);
 number(binding,sel("setStride:"),32);number(binding,sel("setStepFunction:"),1);number(binding,sel("setStepRate:"),1);
 set(descriptor,sel("setVertexDescriptor:"),layout);
 journal("TEGU_RESULT vertex_render_compile_begin",-1);
 object pipeline=vertex&&fragment?((object (*)(object,selector,object,object *))send)
  (device,sel("newRenderPipelineStateWithDescriptor:error:"),descriptor,&error):0;
 release(descriptor,sel("release"));release(vertex,sel("release"));release(fragment,sel("release"));release(library,sel("release"));
 journal("TEGU_RESULT vertex_render_compile",pipeline&&!error?0:1);
 if(!pipeline||error){release(pipeline,sel("release"));pop(pool);return 1;}
 object textureDescriptor=get(cls("MTLTextureDescriptor"),sel("new"));
 number(textureDescriptor,sel("setTextureType:"),2);number(textureDescriptor,sel("setPixelFormat:"),70);
 number(textureDescriptor,sel("setWidth:"),16);number(textureDescriptor,sel("setHeight:"),16);
 number(textureDescriptor,sel("setDepth:"),1);number(textureDescriptor,sel("setMipmapLevelCount:"),1);
 number(textureDescriptor,sel("setArrayLength:"),1);number(textureDescriptor,sel("setSampleCount:"),1);
 number(textureDescriptor,sel("setResourceOptions:"),0);number(textureDescriptor,sel("setUsage:"),4);
 object texture=((object (*)(object,selector,object))send)(device,sel("newTextureWithDescriptor:"),textureDescriptor);
 release(textureDescriptor,sel("release"));
 object queue=get(device,sel("newCommandQueue"));
 object buffer=((object (*)(object,selector,usize,usize))send)(device,sel("newBufferWithLength:options:"),8192,0);
 float *mapped=(float *)get(buffer,sel("contents"));
 int failed=!texture||!queue||!buffer||!mapped;
 if(!failed)for(unsigned i=0;i<8192/16;i++){mapped[i*4]=3; mapped[i*4+1]=3;mapped[i*4+2]=.5f;mapped[i*4+3]=1;}
 const float positions[3][4]={{-.75f,-.75f,.5f,1},{.75f,-.75f,.5f,1},{-.75f,.75f,.5f,1}};
 float vertices[24];for(unsigned i=0;i<3;i++)for(unsigned j=0;j<4;j++){
  vertices[i*8+j]=(j<2?3.f:j==2?.5f:1.f);vertices[i*8+4+j]=positions[i][j];
 }
 journal("TEGU_RESULT vertex_render_target",failed?1:0);
 const unsigned clears[3]={0xff3366cc,0xff112233,0xff996633};
 const char *phases[3]={"TEGU_RESULT vertex_render_pass0","TEGU_RESULT vertex_render_pass1","TEGU_RESULT vertex_render_pass2"};
 for(unsigned run=0;run<3&&!failed;run++) {
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
  if(run==1) {
   ((void (*)(object,selector,const void *,usize,usize))send)(encoder,sel("setVertexBytes:length:atIndex:"),vertices,sizeof(vertices),1);
   // Caller memory may be changed immediately after setVertexBytes.
   for(unsigned i=0;i<24;i++)vertices[i]=3;
  } else {
   usize offset=run==0?4092:32;
   for(unsigned i=0;i<3;i++)for(unsigned j=0;j<4;j++)mapped[offset/4+i*8+4+j]=positions[i][j];
   ((void (*)(object,selector,object,usize,usize))send)(encoder,sel("setVertexBuffer:offset:atIndex:"),buffer,offset,1);
  }
  ((void (*)(object,selector,usize,usize,usize))send)(encoder,sel("drawPrimitives:vertexStart:vertexCount:"),3,0,3);
  // Offset changes after drawing must not alter the recorded draw.
  if(run==0)((void (*)(object,selector,usize,usize))send)(encoder,sel("setVertexBufferOffset:atIndex:"),32,1);
  if(run==2) {
   ((void (*)(object,selector,object,usize,usize))send)(encoder,sel("setVertexBuffer:offset:atIndex:"),0,0,1);
   release(buffer,sel("release"));buffer=0;
  }
  get(encoder,sel("endEncoding"));
  unsigned callbacks=0;tegu_probe_completed(send,sel("addCompletedHandler:"),command,&callbacks);
  journal("TEGU_RESULT vertex_render_submit_begin",-1);
  get(command,sel("commit"));get(command,sel("waitUntilCompleted"));
  usize status=((usize (*)(object,selector))send)(command,sel("status"));
  if(status!=4||get(command,sel("error"))||callbacks!=2) {
   printf("TEGU_VERTEX_RENDER failed run=%u status=%lu callbacks=%u\n",run,status,callbacks);
   failed=1;journal(phases[run],1);pop(passPool);break;
  }
  unsigned pixels[258];for(unsigned i=0;i<258;i++)pixels[i]=0xdeadbeef;
  struct probe_region region={{0,0,0},{16,16,1}};
  ((void (*)(object,selector,void *,usize,struct probe_region,usize))send)
   (texture,sel("getBytes:bytesPerRow:fromRegion:mipmapLevel:"),pixels+1,64,region,0);
  unsigned checked=0,mismatches=0;
  for(unsigned i=0;i<256;i++) {
   unsigned x=i%16,y=i/16;if(x>=2&&y>=2&&x+y==15)continue;
   unsigned expected=x>=2&&y>=2&&x+y<15?0xffbf8040:clear;
   checked++;if(pixels[i+1]!=expected) {
    if(mismatches<8)printf("TEGU_VERTEX_RENDER mismatch run=%u pixel=%u got=%08x expected=%08x\n",run,i,pixels[i+1],expected);
    mismatches++;
   }
  }
  failed=mismatches||checked!=244||pixels[0]!=0xdeadbeef||pixels[257]!=0xdeadbeef;
  printf("TEGU_VERTEX_RENDER run=%u checked=%u mismatches=%u callbacks=%u readback_guards=%d\n",run,checked,mismatches,callbacks,pixels[0]==0xdeadbeef&&pixels[257]==0xdeadbeef);
  journal(phases[run],failed?1:0);
  pop(passPool);
 }
 release(buffer,sel("release"));release(queue,sel("release"));release(texture,sel("release"));release(pipeline,sel("release"));pop(pool);
 usize allocated=((usize (*)(object,selector))send)(device,sel("currentAllocatedSize"));
 if(allocated!=baseline){failed=1;printf("TEGU_VERTEX_RENDER allocation baseline=%lu final=%lu\n",baseline,allocated);}
 journal("TEGU_RESULT vertex_render_public_api",failed?1:0);
 return failed;
}
