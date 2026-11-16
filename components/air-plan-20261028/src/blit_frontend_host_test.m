#define main original_frontend_test_main
#include "frontend_host_test.m"
#undef main
int main(void) {
 TeguComputeDevice *d=[TeguComputeDevice alloc];
 d->scalar=call;d->method=method;d->map=map;d->unmap=unmap;
 TeguQueue *q=[d newCommandQueue];
 TeguBuffer *a=[d newBufferWithLength:64 options:0],*b=[d newBufferWithLength:64 options:0];
 unsigned char *ap=[a contents],*bp=[b contents];
 memset(ap,0x11,64);memset(bp,0x22,64);
 TeguCommand *c=[q commandBuffer];TeguBlitEncoder *e=[c blitCommandEncoder];
 assert(e&&[e device]==d);
 [e fillBuffer:a range:(TeguRange){3,17} value:0xab];
 [e copyFromBuffer:a sourceOffset:5 toBuffer:b destinationOffset:7 size:11];
 [e fillBuffer:a range:(TeguRange){3,17} value:0xcd];
 [e endEncoding];
 assert(ap[3]==0x11&&bp[7]==0x22); /* Recorded, not eagerly executed. */
 [c commit];[c waitUntilCompleted];assert([c status]==4&&![c error]);
 for(unsigned i=0;i<64;i++){
  assert(ap[i]==(i>=3&&i<20?0xcd:0x11));
  assert(bp[i]==(i>=7&&i<18?0xab:0x22));
 }
 drain();
 for(unsigned scenario=0;scenario<7;scenario++) {
  c=[q commandBuffer];e=[c blitCommandEncoder];
  [e fillBuffer:b range:(TeguRange){0,64} value:0xef];
  switch(scenario) {
   case 0:[e copyFromBuffer:a sourceOffset:63 toBuffer:b destinationOffset:0 size:2];break;
   case 1:[e fillBuffer:b range:(TeguRange){~0UL,2} value:1];break;
   case 2:[e copyFromBuffer:b sourceOffset:0 toBuffer:b destinationOffset:1 size:4];break;
   case 3:[e fillBuffer:b range:(TeguRange){0,0} value:1];break;
   case 4:assert(![c blitCommandEncoder]);break;
   case 5:[e endEncoding];[e fillBuffer:b range:(TeguRange){0,1} value:1];break;
   case 6:for(unsigned i=0;i<TEGU_COMMAND_MAX_JOBS;i++)[e fillBuffer:b range:(TeguRange){0,1} value:1];break;
  }
  if(scenario!=5)[e endEncoding];
  [c commit];[c waitUntilCompleted];assert([c status]==5&&[c error]);
  assert(bp[0]==0x22); /* Invalid later encoding prevents partial execution. */
  drain();
 }
 c=[q commandBuffer];e=[c blitCommandEncoder];
 [e copyFromBuffer:a sourceOffset:0 toBuffer:a destinationOffset:32 size:16];
 [e endEncoding];[c commit];assert([c status]==4);
 assert(!memcmp(ap,ap+32,16));drain();
 c=[q commandBuffer];e=[c blitCommandEncoder];[e endEncoding];
 [c commit];[c waitUntilCompleted];assert([c status]==4);drain();
 c=[q commandBuffer];[c commit];assert([c status]==4);drain();
 c=[q commandBuffer];e=[c blitCommandEncoder];
 [e copyFromBuffer:a sourceOffset:0 toBuffer:b destinationOffset:0 size:64];[e endEncoding];
 [a release];[b release]; /* Command retains buffers until its own destruction. */
 [c commit];assert([c status]==4&&!memcmp(ap,bp,64));drain();
 assert([d currentAllocatedSize]==0);
 [q release];[d release];drain();assert(!live&&allocated==freed&&!dispatched);
 puts("PASS shared-buffer blit ordering, guards, bounds, overlap, empty commands and retained lifetime; CPU transfers, no GPU copy claim");
}
