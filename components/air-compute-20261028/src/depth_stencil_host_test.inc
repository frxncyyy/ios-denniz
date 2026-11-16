@interface TestStencil : NSObject {
@public NSUInteger compare,fail,depthFail,pass; unsigned read,write;
}
@end
@implementation TestStencil
- (NSUInteger)stencilCompareFunction{return compare;}
- (NSUInteger)stencilFailureOperation{return fail;}
- (NSUInteger)depthFailureOperation{return depthFail;}
- (NSUInteger)depthStencilPassOperation{return pass;}
- (unsigned)readMask{return read;}
- (unsigned)writeMask{return write;}
@end
@interface TestDepth : NSObject {
@public NSUInteger compare; signed char write; id front,back,debugLabel;
}
@end
@implementation TestDepth
- (NSUInteger)depthCompareFunction{return compare;}
- (signed char)isDepthWriteEnabled{return write;}
- (id)frontFaceStencil{return front;}
- (id)backFaceStencil{return back;}
- (id)label{return debugLabel;}
@end
static void test_depth_stencil(void) {
 TeguComputeDevice *device=[TeguComputeDevice alloc];
 assert(![device newDepthStencilStateWithDescriptor:0]);
 TestDepth *desc=[TestDepth alloc];desc->compare=7;
 TeguDepthStencilState *disabled=[device newDepthStencilStateWithDescriptor:desc];
 assert(disabled&&disabled->state.compare==7&&!disabled->state.write);
 assert(!disabled->state.front.enabled&&!disabled->state.back.enabled);
 assert([disabled device]==device&&![disabled label]);[disabled release];
 TestStencil *front=[TestStencil alloc],*back=[TestStencil alloc];
 front->compare=1;front->fail=2;front->depthFail=3;front->pass=4;front->read=0x80000012;front->write=0xfedcba98;
 back->compare=6;back->fail=7;back->depthFail=5;back->pass=1;back->read=0x11223344;back->write=0x55667788;
 desc->compare=3;desc->write=1;desc->front=front;desc->back=back;
 TestLabel *label=[TestLabel alloc];label->value=42;desc->debugLabel=label;
 TeguDepthStencilState *saved=[device newDepthStencilStateWithDescriptor:desc];
 assert(saved&&[saved device]==device&&[saved label]!=label);
 assert(saved->state.compare==3&&saved->state.write);
 assert(saved->state.front.enabled&&saved->state.front.compare==1&&saved->state.front.fail==2);
 assert(saved->state.front.depthFail==3&&saved->state.front.pass==4);
 assert(saved->state.front.readMask==0x80000012&&saved->state.front.writeMask==0xfedcba98);
 assert(saved->state.back.enabled&&saved->state.back.compare==6&&saved->state.back.fail==7);
 assert(saved->state.back.depthFail==5&&saved->state.back.pass==1);
 assert(saved->state.back.readMask==0x11223344&&saved->state.back.writeMask==0x55667788);
 /* Preserve every valid operation/compare, reject out-of-range before narrowing. */
 for(NSUInteger value=0;value<8;value++) {
  desc->compare=front->compare=front->fail=front->depthFail=front->pass=value;
  TeguDepthStencilState *s=[device newDepthStencilStateWithDescriptor:desc];assert(s);
  assert(s->state.compare==value&&s->state.front.compare==value&&s->state.front.fail==value&&
   s->state.front.depthFail==value&&s->state.front.pass==value);[s release];
 }
 NSUInteger *fields[]={&desc->compare,&front->compare,&front->fail,&front->depthFail,&front->pass,
  &back->compare,&back->fail,&back->depthFail,&back->pass};
 for(unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);i++) {
  NSUInteger old=*fields[i];*fields[i]=8;assert(![device newDepthStencilStateWithDescriptor:desc]);
  *fields[i]=((NSUInteger)1<<32);assert(![device newDepthStencilStateWithDescriptor:desc]);*fields[i]=old;
 }
 desc->compare=0;desc->write=0;front->read=0;back->write=0;label->value=99;
 [front release];[back release];[label release];[desc release];[device release];
 assert(saved->state.compare==3&&saved->state.write&&saved->state.front.readMask==0x80000012);
 assert(saved->state.back.writeMask==0x55667788&&((TestLabel *)[saved label])->value==42);
 assert([saved device]);[saved release];assert(!live);
}
