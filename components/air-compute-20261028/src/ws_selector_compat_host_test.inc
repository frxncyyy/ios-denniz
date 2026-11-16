extern id objc_getClass(const char *name);
extern void *class_getInstanceMethod(id klass,SEL selector);

/* The production superclass is Apple's NSObject, which implements runtime
 * introspection.  The freestanding host fixture supplies only ownership
 * methods.  Add the query narrowly to the encoder exercised below so descriptor
 * probes in other tests retain their deliberately explicit selector surfaces. */
@interface TeguRenderEncoder (TeguWSHostIntrospection)
- (signed char)respondsToSelector:(SEL)selector;
@end
@implementation TeguRenderEncoder (TeguWSHostIntrospection)
- (signed char)respondsToSelector:(SEL)selector {
 return class_getInstanceMethod(objc_getClass("TeguRenderEncoder"),selector)!=0;
}
@end

static void test_ws_selector_compat_response(void) {
#define TEGU_WS_RESPONDS(c,s) assert(class_getInstanceMethod(objc_getClass(c),@selector(s)))
 TEGU_WS_RESPONDS("TeguCommand",computeCommandEncoderWithDispatchType:);
 TEGU_WS_RESPONDS("TeguCommand",pushDebugGroup:);
 TEGU_WS_RESPONDS("TeguCommand",popDebugGroup);
 TEGU_WS_RESPONDS("TeguEncoder",device);
 TEGU_WS_RESPONDS("TeguEncoder",label);
 TEGU_WS_RESPONDS("TeguEncoder",setLabel:);
 TEGU_WS_RESPONDS("TeguEncoder",insertDebugSignpost:);
 TEGU_WS_RESPONDS("TeguEncoder",pushDebugGroup:);
 TEGU_WS_RESPONDS("TeguEncoder",popDebugGroup);
 TEGU_WS_RESPONDS("TeguRenderEncoder",setFragmentTexture:atTextureIndex:samplerState:atSamplerIndex:);
 TEGU_WS_RESPONDS("TeguRenderEncoder",insertDebugSignpost:);
 TEGU_WS_RESPONDS("TeguRenderEncoder",pushDebugGroup:);
 TEGU_WS_RESPONDS("TeguRenderEncoder",popDebugGroup);
 TEGU_WS_RESPONDS("TeguBlitEncoder",copyFromTexture:toTexture:);
 TEGU_WS_RESPONDS("TeguBlitEncoder",synchronizeResource:);
 TEGU_WS_RESPONDS("TeguBlitEncoder",insertDebugSignpost:);
 TEGU_WS_RESPONDS("TeguBlitEncoder",pushDebugGroup:);
 TEGU_WS_RESPONDS("TeguBlitEncoder",popDebugGroup);
 TEGU_WS_RESPONDS("TeguBuffer",didModifyRange:);
 TEGU_WS_RESPONDS("TeguBuffer",iosurface);
 TEGU_WS_RESPONDS("TeguBuffer",isComplete);
 TEGU_WS_RESPONDS("TeguBuffer",protectionOptions);
 TEGU_WS_RESPONDS("TeguBuffer",waitUntilComplete);
 TEGU_WS_RESPONDS("TeguTexture",didModifyData);
 TEGU_WS_RESPONDS("TeguTexture",isComplete);
 TEGU_WS_RESPONDS("TeguTexture",protectionOptions);
 TEGU_WS_RESPONDS("TeguTexture",rotation);
 TEGU_WS_RESPONDS("TeguTexture",waitUntilComplete);
 TEGU_WS_RESPONDS("TeguPipeline",allocatedSize);
 TEGU_WS_RESPONDS("TeguPipeline",label);
 TEGU_WS_RESPONDS("TeguRenderPipeline",allocatedSize);
#undef TEGU_WS_RESPONDS
}
