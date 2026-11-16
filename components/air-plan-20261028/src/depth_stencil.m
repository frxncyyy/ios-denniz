/* Immutable Metal descriptor snapshot. These values are Metal enums, NOT Mali
 * hardware encodings. A graphics encoder must explicitly lower them at draw.
 * Creating state alone does not implement depth/stencil attachments or drawing.
 */
@interface NSObject (TeguDepthStencilDescriptor)
- (NSUInteger)depthCompareFunction;
- (signed char)isDepthWriteEnabled;
- (id)frontFaceStencil;
- (id)backFaceStencil;
- (id)label;
- (NSUInteger)stencilCompareFunction;
- (NSUInteger)stencilFailureOperation;
- (NSUInteger)depthFailureOperation;
- (NSUInteger)depthStencilPassOperation;
- (unsigned)readMask;
- (unsigned)writeMask;
@end
struct TeguStencilFace {
 unsigned enabled, compare, fail, depthFail, pass, readMask, writeMask;
};
struct TeguDepthStencilDescription {
 unsigned compare, write;
 struct TeguStencilFace front, back;
};
@interface TeguDepthStencilState : NSObject {
@public TeguComputeDevice *owner; id debugLabel;
 struct TeguDepthStencilDescription state;
}
- (id)device;
- (id)label;
@end
static int tegu_stencil_snapshot(id descriptor,struct TeguStencilFace *out) {
 *out=(struct TeguStencilFace){0,7,0,0,0,~0u,~0u};
 if(!descriptor)return 1; /* nil face disables its stencil test */
 NSUInteger compare=[descriptor stencilCompareFunction],fail=[descriptor stencilFailureOperation];
 NSUInteger depthFail=[descriptor depthFailureOperation],pass=[descriptor depthStencilPassOperation];
 if(compare>7||fail>7||depthFail>7||pass>7)return 0;
 *out=(struct TeguStencilFace){1,(unsigned)compare,(unsigned)fail,(unsigned)depthFail,
  (unsigned)pass,[descriptor readMask],[descriptor writeMask]};
 return 1;
}
#ifdef TEGU_METAL_PROTOCOLS
static int tegu_depth_protocol_ready;
#endif
@implementation TeguDepthStencilState
#ifdef TEGU_METAL_PROTOCOLS
+ (void)initialize {
 void *objc=dlopen("/usr/lib/libobjc.A.dylib",2);
 void *(*protocol)(const char *)=objc?dlsym(objc,"objc_getProtocol"):0;
 signed char (*add)(void *,void *)=objc?dlsym(objc,"class_addProtocol"):0;
 signed char (*conforms)(void *,void *)=objc?dlsym(objc,"class_conformsToProtocol"):0;
 void *p=protocol?protocol("MTLDepthStencilState"):0;
 if(p&&add&&conforms)tegu_depth_protocol_ready=conforms(self,p)||add(self,p);
}
#endif
- (id)device {return owner;}
- (id)label {return debugLabel;}
- (void)dealloc {[debugLabel release];[owner release];[super dealloc];}
@end
@implementation TeguComputeDevice (DepthStencil)
- (id)newDepthStencilStateWithDescriptor:(id)descriptor {
 if(!descriptor)return 0;
 NSUInteger compare=[descriptor depthCompareFunction];
 struct TeguDepthStencilDescription snapshot={0};
 if(compare>7||!tegu_stencil_snapshot([descriptor frontFaceStencil],&snapshot.front)||
    !tegu_stencil_snapshot([descriptor backFaceStencil],&snapshot.back)) {
  MARK("FAIL depth_stencil_descriptor");return 0;
 }
 snapshot.compare=(unsigned)compare;snapshot.write=!![descriptor isDepthWriteEnabled];
 TeguDepthStencilState *result=[[TeguDepthStencilState alloc] init];if(!result)return 0;
#ifdef TEGU_METAL_PROTOCOLS
 if(!tegu_depth_protocol_ready){[result release];MARK("FAIL depth_stencil_protocol");return 0;}
#endif
 result->owner=[self retain];result->debugLabel=[[descriptor label] copy];result->state=snapshot;
 return result;
}
@end
