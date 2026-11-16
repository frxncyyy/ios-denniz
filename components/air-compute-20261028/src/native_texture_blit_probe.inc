/* Copy actual GPU-produced pixels into a larger shared texture. The copy is a
 * CPU blit; source rendering and cache visibility are verified independently. */
static int tegu_native_texture_blit(object device,object source,const unsigned *expected,
 void *objc,void *send,selector (*sel)(const char *)) {
 object (*get)(object,selector)=(void *)send;
 object (*cls)(const char *)=dlsym(objc,"objc_getClass");
 void (*number)(object,selector,usize)=(void *)send;
 void (*release)(object,selector)=(void *)send;
 object descriptor=get(cls("MTLTextureDescriptor"),sel("new"));
 number(descriptor,sel("setTextureType:"),2);number(descriptor,sel("setPixelFormat:"),70);
 number(descriptor,sel("setWidth:"),32);number(descriptor,sel("setHeight:"),20);
 number(descriptor,sel("setDepth:"),1);number(descriptor,sel("setMipmapLevelCount:"),6);
 number(descriptor,sel("setArrayLength:"),1);number(descriptor,sel("setSampleCount:"),1);
 number(descriptor,sel("setResourceOptions:"),0);number(descriptor,sel("setUsage:"),5);
 object target=((object (*)(object,selector,object))send)(device,sel("newTextureWithDescriptor:"),descriptor);
 release(descriptor,sel("release"));
 object queue=get(device,sel("newCommandQueue"));
 object command=get(queue,sel("commandBuffer")),encoder=get(command,sel("blitCommandEncoder"));
 int failed=!target||!command||!encoder;
 if(!failed) {
  struct blit_origin {usize x,y,z;};struct blit_size {usize width,height,depth;};
  struct blit_region {struct blit_origin origin;struct blit_size size;};
  ((void (*)(object,selector,object,usize,usize,struct blit_origin,struct blit_size,object,usize,usize,struct blit_origin))send)
   (encoder,sel("copyFromTexture:sourceSlice:sourceLevel:sourceOrigin:sourceSize:toTexture:destinationSlice:destinationLevel:destinationOrigin:"),
    source,0,0,(struct blit_origin){0,0,0},(struct blit_size){16,16,1},target,0,0,(struct blit_origin){7,2,0});
  ((void (*)(object,selector,object))send)(encoder,sel("generateMipmapsForTexture:"),target);
  get(encoder,sel("endEncoding"));
  unsigned callbacks=0;tegu_probe_completed(send,sel("addCompletedHandler:"),command,&callbacks);
  get(command,sel("commit"));get(command,sel("waitUntilCompleted"));
  if(((usize (*)(object,selector))send)(command,sel("status"))!=4||get(command,sel("error"))||callbacks!=2)failed=1;
  unsigned pixels[642];for(unsigned i=0;i<642;i++)pixels[i]=0xdeadbeef;
  ((void (*)(object,selector,void *,usize,struct blit_region,usize))send)
   (target,sel("getBytes:bytesPerRow:fromRegion:mipmapLevel:"),pixels+1,128,
    (struct blit_region){{0,0,0},{32,20,1}},0);
  for(unsigned y=0;y<20;y++)for(unsigned x=0;x<32;x++) {
   unsigned value=x>=7&&x<23&&y>=2&&y<18?expected[(y-2)*16+x-7]:0;
   if(pixels[1+y*32+x]!=value)failed=1;
  }
  if(pixels[0]!=0xdeadbeef||pixels[641]!=0xdeadbeef)failed=1;
  unsigned lower[162];for(unsigned i=0;i<162;i++)lower[i]=0xdeadbeef;
  ((void (*)(object,selector,void *,usize,struct blit_region,usize))send)
   (target,sel("getBytes:bytesPerRow:fromRegion:mipmapLevel:"),lower+1,64,
    (struct blit_region){{0,0,0},{16,10,1}},1);
  for(unsigned y=0;y<10;y++)for(unsigned x=0;x<16;x++) {
   unsigned value=0;
   for(unsigned channel=0;channel<4;channel++) {
    unsigned sum=0;
    for(unsigned dy=0;dy<2;dy++)for(unsigned dx=0;dx<2;dx++) {
     unsigned sx=x*2+dx,sy=y*2+dy;
     unsigned pixel=sx>=7&&sx<23&&sy>=2&&sy<18?expected[(sy-2)*16+sx-7]:0;
     sum+=(pixel>>(8*channel))&255;
    }
    value|=((sum+2)/4)<<(8*channel);
   }
   if(lower[1+y*16+x]!=value)failed=1;
  }
  if(lower[0]!=0xdeadbeef||lower[161]!=0xdeadbeef)failed=1;
 }
 release(target,sel("release"));release(queue,sel("release"));
 journal("TEGU_RESULT system_blit_texture",failed?1:0);
 journal("TEGU_RESULT system_mipmaps",failed?1:0);return failed;
}
