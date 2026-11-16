/* Exercise actual public selectors. Transfers are shared-memory CPU operations,
 * not evidence of GPU copy execution. Render probe independently checks Mali. */
static int tegu_native_blit_probe(object device,void *objc,void *send,selector (*sel)(const char *)) {
 object (*get)(object,selector)=(void *)send;
 void (*release)(object,selector)=(void *)send;
 void *(*push)(void)=dlsym(objc,"objc_autoreleasePoolPush");
 void (*pop)(void *)=dlsym(objc,"objc_autoreleasePoolPop");
 if(!push||!pop)return 1;
 void *pool=push();
 usize baseline=((usize (*)(object,selector))send)(device,sel("currentAllocatedSize"));
 object queue=get(device,sel("newCommandQueue"));
 object a=((object (*)(object,selector,usize,usize))send)(device,sel("newBufferWithLength:options:"),64,0);
 object b=((object (*)(object,selector,usize,usize))send)(device,sel("newBufferWithLength:options:"),64,0);
 unsigned char *ap=(void *)get(a,sel("contents")),*bp=(void *)get(b,sel("contents"));
 int failed=!queue||!ap||!bp;
 if(!failed) {
  for(unsigned i=0;i<64;i++){ap[i]=0x11;bp[i]=0x22;}
  object command=get(queue,sel("commandBuffer")),encoder=get(command,sel("blitCommandEncoder"));
  if(!command||!encoder){failed=1;goto cleanup;}
  struct {usize location,length;} range={3,17};
  void (*fill)(object,selector,object,typeof(range),unsigned char)=(void *)send;
  fill(encoder,sel("fillBuffer:range:value:"),a,range,0xab);
  ((void (*)(object,selector,object,usize,object,usize,usize))send)
   (encoder,sel("copyFromBuffer:sourceOffset:toBuffer:destinationOffset:size:"),a,5,b,7,11);
  fill(encoder,sel("fillBuffer:range:value:"),a,range,0xcd);
  get(encoder,sel("endEncoding"));
  if(ap[3]!=0x11||bp[7]!=0x22||!encoder)failed=1;
  unsigned callbacks=0;tegu_probe_completed(send,sel("addCompletedHandler:"),command,&callbacks);
  release(a,sel("release"));release(b,sel("release"));a=b=0;
  get(command,sel("commit"));get(command,sel("waitUntilCompleted"));
  if(((usize (*)(object,selector))send)(command,sel("status"))!=4||get(command,sel("error"))||callbacks!=2)failed=1;
  for(unsigned i=0;i<64;i++) {
   if(ap[i]!=(i>=3&&i<20?0xcd:0x11)||bp[i]!=(i>=7&&i<18?0xab:0x22))failed=1;
  }
 }
cleanup:
 release(a,sel("release"));release(b,sel("release"));release(queue,sel("release"));pop(pool);
 if(((usize (*)(object,selector))send)(device,sel("currentAllocatedSize"))!=baseline)failed=1;
 journal("TEGU_RESULT system_blit_buffers",failed?1:0);
 return failed;
}
