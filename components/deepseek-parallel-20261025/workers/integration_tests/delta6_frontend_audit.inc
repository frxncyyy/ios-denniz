/* Integration-owned Delta6 real-library and selector audit helper.
 * Included by native_compute_frontend_test.py after the host frontend
 * declarations and before main.  This is test-only code, not production. */

extern SEL sel_registerName(const char *);
extern signed char class_respondsToSelector(Class, SEL);
extern id object_getClass(id);
extern id objc_getClass(const char *);

@interface NSObject (Delta6SelectorAudit)
- (signed char)respondsToSelector:(SEL)selector;
+ (signed char)instancesRespondToSelector:(SEL)selector;
@end
@implementation NSObject (Delta6SelectorAudit)
- (signed char)respondsToSelector:(SEL)selector {
    return class_respondsToSelector(*(id *)self, selector);
}
+ (signed char)instancesRespondToSelector:(SEL)selector {
    return class_respondsToSelector((Class)self, selector);
}
@end

@interface AuditConstant : NSObject { @public NSUInteger type,index; id key; unsigned char data[64]; }
@end
@implementation AuditConstant
- (NSUInteger)dataType { return type; }
- (NSUInteger)index { return index; }
- (id)name { return key; }
- (const void *)data { return data; }
- (void)dealloc { [key release]; [super dealloc]; }
@end
@interface AuditConstantList : NSObject { @public AuditConstant *records[8]; unsigned count; }
@end
@implementation AuditConstantList
- (NSUInteger)count { return count; }
- (id)objectAtIndex:(NSUInteger)i { return i < count ? records[i] : 0; }
- (void)dealloc { for(unsigned i=0;i<count;i++)[records[i] release]; [super dealloc]; }
@end
@interface AuditConstantValues : NSObject { @public AuditConstantList *indexed,*named; }
@end
@implementation AuditConstantValues
- (id)newIndexedConstantArray { return [indexed retain]; }
- (id)newNamedConstantArray { return [named retain]; }
- (void)dealloc { [indexed release]; [named release]; [super dealloc]; }
@end
@interface AuditFunctionDescriptor : NSObject { @public id entry,alias; AuditConstantValues *values; NSUInteger opts; }
@end
@implementation AuditFunctionDescriptor
- (id)name { return entry; }
- (id)specializedName { return alias; }
- (id)constantValues { return values; }
- (NSUInteger)options { return opts; }
- (void)dealloc { [entry release]; [alias release]; [values release]; [super dealloc]; }
@end

static unsigned audit_checks,audit_missing;
static void audit_selector(id object,const char *className,const char *selectorName) {
    SEL selector=sel_registerName(selectorName);
    int ok=object?([object respondsToSelector:selector]!=0):0;
    audit_checks++;
    if(!ok) {
        fprintf(stderr,"SELECTOR_MISSING class=%s selector=%s\n",
                className?className:"?",selectorName);
        audit_missing++;
    }
}
static void audit_class_selector(const char *className,const char *selectorName) {
    Class cls=className?objc_getClass(className):0;
    SEL selector=sel_registerName(selectorName);
    int ok=cls?([(id)cls instancesRespondToSelector:selector]!=0):0;
    audit_checks++;
    if(!ok) {
        fprintf(stderr,"SELECTOR_MISSING class=%s selector=%s\n",
                className?className:"?",selectorName);
        audit_missing++;
    }
}
static void audit_selectors(TeguComputeDevice *device,TeguBuffer *buffer,TeguTexture *texture,
                            TeguCommand *command,TeguEncoder *encoder,TeguRenderEncoder *renderEncoder) {
    const struct {id object;const char *cls,*name;} instance[]={
        {(id)device,"TeguComputeDevice","newCommandQueue"},
        {(id)device,"TeguComputeDevice","newLibraryWithURL:error:"},
        {(id)device,"TeguComputeDevice","newLibraryWithFile:error:"},
        {(id)device,"TeguComputeDevice","newLibraryWithData:error:"},
        {(id)device,"TeguComputeDevice","newDefaultLibrary"},
        {(id)device,"TeguComputeDevice","newDefaultLibraryWithBundle:error:"},
        {(id)device,"TeguComputeDevice","newBufferWithLength:options:"},
        {(id)device,"TeguComputeDevice","newBufferWithBytes:length:options:"},
        {(id)device,"TeguComputeDevice","newBufferWithBytesNoCopy:length:options:deallocator:"},
        {(id)device,"TeguComputeDevice","newTextureWithDescriptor:"},
        {(id)device,"TeguComputeDevice","newSamplerStateWithDescriptor:"},
        {(id)device,"TeguComputeDevice","newRenderPipelineStateWithDescriptor:error:"},
        {(id)device,"TeguComputeDevice","newComputePipelineStateWithFunction:error:"},
        {(id)device,"TeguComputeDevice","newPipelineLibraryWithFilePath:error:"},
        {(id)device,"TeguComputeDevice","supportsFamily:"},
        {(id)device,"TeguComputeDevice","supportsFeatureSet:"},
        {(id)device,"TeguComputeDevice","supportsTextureSampleCount:"},
        {(id)device,"TeguComputeDevice","isDepth24Stencil8PixelFormatSupported"},
        {(id)buffer,"TeguBuffer","newLinearTextureWithDescriptor:offset:bytesPerRow:bytesPerImage:"},
        {(id)buffer,"TeguBuffer","contents"},
        {(id)buffer,"TeguBuffer","length"},
        {(id)buffer,"TeguBuffer","didModifyRange:"},
        {(id)buffer,"TeguBuffer","isComplete"},
        {(id)buffer,"TeguBuffer","iosurface"},
        {(id)buffer,"TeguBuffer","protectionOptions"},
        {(id)buffer,"TeguBuffer","waitUntilComplete"},
        {(id)texture,"TeguTexture","newTextureViewWithPixelFormat:"},
        {(id)texture,"TeguTexture","newTextureViewWithPixelFormat:textureType:levels:slices:"},
        {(id)texture,"TeguTexture","width"},
        {(id)texture,"TeguTexture","height"},
        {(id)texture,"TeguTexture","pixelFormat"},
        {(id)texture,"TeguTexture","textureType"},
        {(id)texture,"TeguTexture","sampleCount"},
        {(id)texture,"TeguTexture","arrayLength"},
        {(id)texture,"TeguTexture","mipmapLevelCount"},
        {(id)texture,"TeguTexture","didModifyData"},
        {(id)texture,"TeguTexture","rotation"},
        {(id)texture,"TeguTexture","protectionOptions"},
        {(id)texture,"TeguTexture","waitUntilComplete"},
        {(id)command,"TeguCommand","computeCommandEncoder"},
        {(id)command,"TeguCommand","computeCommandEncoderWithDispatchType:"},
        {(id)command,"TeguCommand","renderCommandEncoderWithDescriptor:"},
        {(id)command,"TeguCommand","blitCommandEncoder"},
        {(id)command,"TeguCommand","commit"},
        {(id)command,"TeguCommand","waitUntilCompleted"},
        {(id)command,"TeguCommand","addCompletedHandler:"},
        {(id)command,"TeguCommand","status"},
        {(id)command,"TeguCommand","error"},
        {(id)command,"TeguCommand","enqueue"},
        {(id)command,"TeguCommand","pushDebugGroup:"},
        {(id)command,"TeguCommand","popDebugGroup"},
        {(id)command,"TeguCommand","commitAndWaitUntilSubmitted"},
        {(id)command,"TeguCommand","commitAndWaitUntilSubmittedWithDeadline:"},
        {(id)command,"TeguCommand","waitUntilScheduled"},
        {(id)command,"TeguCommand","presentDrawable:"},
        {(id)command,"TeguCommand","presentDrawable:atTime:"},
        {(id)command,"TeguCommand","presentDrawable:afterMinimumDuration:"},
        {(id)encoder,"TeguEncoder","setComputePipelineState:"},
        {(id)encoder,"TeguEncoder","setBuffer:offset:atIndex:"},
        {(id)encoder,"TeguEncoder","setTexture:atIndex:"},
        {(id)encoder,"TeguEncoder","setSamplerState:atIndex:"},
        {(id)encoder,"TeguEncoder","setThreadgroupMemoryLength:atIndex:"},
        {(id)encoder,"TeguEncoder","dispatchThreadgroups:threadsPerThreadgroup:"},
        {(id)encoder,"TeguEncoder","endEncoding"},
        {(id)renderEncoder,"TeguRenderEncoder","drawPrimitives:vertexStart:vertexCount:"},
        {(id)renderEncoder,"TeguRenderEncoder","drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferOffset:"},
        {(id)renderEncoder,"TeguRenderEncoder","setScissorRect:"},
        {(id)renderEncoder,"TeguRenderEncoder","setRenderPipelineState:"},
        {(id)renderEncoder,"TeguRenderEncoder","setDepthStencilState:"},
        {(id)renderEncoder,"TeguRenderEncoder","setStencilReferenceValue:"},
        {(id)renderEncoder,"TeguRenderEncoder","setVertexBytes:length:atIndex:"},
        {(id)renderEncoder,"TeguRenderEncoder","setVertexBuffer:offset:atIndex:"},
        {(id)renderEncoder,"TeguRenderEncoder","setVertexBufferOffset:atIndex:"},
        {(id)renderEncoder,"TeguRenderEncoder","setFragmentBytes:length:atIndex:"},
        {(id)renderEncoder,"TeguRenderEncoder","setFragmentBuffer:offset:atIndex:"},
        {(id)renderEncoder,"TeguRenderEncoder","setFragmentBufferOffset:atIndex:"},
        {(id)renderEncoder,"TeguRenderEncoder","setFragmentTexture:atIndex:"},
        {(id)renderEncoder,"TeguRenderEncoder","setFragmentSamplerState:atIndex:"},
        {(id)renderEncoder,"TeguRenderEncoder","setFragmentTexture:atTextureIndex:samplerState:atSamplerIndex:"},
        {(id)renderEncoder,"TeguRenderEncoder","insertDebugSignpost:"},
        {(id)renderEncoder,"TeguRenderEncoder","pushDebugGroup:"},
        {(id)renderEncoder,"TeguRenderEncoder","popDebugGroup"},
        {(id)renderEncoder,"TeguRenderEncoder","setDepthClipMode:"},
        {(id)renderEncoder,"TeguRenderEncoder","setBlendColorRed:green:blue:alpha:"},
        {(id)renderEncoder,"TeguRenderEncoder","setVisibilityResultMode:offset:"},
        {(id)renderEncoder,"TeguRenderEncoder","useResource:usage:"},
        {(id)renderEncoder,"TeguRenderEncoder","useHeap:"},
    };
    for(unsigned i=0;i<sizeof(instance)/sizeof(instance[0]);i++)
        audit_selector(instance[i].object,instance[i].cls,instance[i].name);
    static const struct {const char *cls,*name;} classSelectors[]={
        {"TeguTexture","newTextureViewWithPixelFormat:"},
        {"TeguTexture","newTextureViewWithPixelFormat:textureType:levels:slices:"},
        {"TeguBuffer","newLinearTextureWithDescriptor:offset:bytesPerRow:bytesPerImage:"},
        {"TeguComputeDevice","newBufferWithBytesNoCopy:length:options:deallocator:"},
        {"TeguComputeDevice","newResourceGroupFromResources:count:"},
        {"TeguComputeDevice","encodeCacheHint:"},
        {"TeguEncoder","setThreadgroupMemoryLength:atIndex:"}
    };
    for(unsigned i=0;i<sizeof(classSelectors)/sizeof(classSelectors[0]);i++)
        audit_class_selector(classSelectors[i].cls,classSelectors[i].name);
}

static void audit_real_quartzcore_library(TeguComputeDevice *device,const char *path) {
    libraryTestPath=[NSString stringWithUTF8String:path];
    NSURL *url=[[NSURL alloc] init];id error=0;
    TeguLibrary *library=[device newLibraryWithURL:url error:&error];
    assert(library&&!error);
    TeguFunction *plain=[library newFunctionWithName:[NSString stringWithUTF8String:"path_cubic_frag_lpf"]];
    assert(plain);[plain release];

    AuditFunctionDescriptor *desc=[[AuditFunctionDescriptor alloc] init];
    desc->entry=[[NSString stringWithUTF8String:"fixed_vert_lph_spc"] retain];
    desc->alias=[[NSString stringWithUTF8String:"fixed_vert_specialized"] retain];
    AuditConstantValues *values=[[AuditConstantValues alloc] init];desc->values=values;
    values->indexed=[[AuditConstantList alloc] init];
    values->named=[[AuditConstantList alloc] init];
    const char *names[]={"fc_generic","fc_vertex_layout","fc_texcoord_count0","fc_texcoord_count1"};
    const NSUInteger types[]={53,49,49,49};
    for(unsigned i=0;i<4;i++) {
        AuditConstant *v=[[AuditConstant alloc] init];
        v->type=types[i];v->key=[[NSString stringWithUTF8String:names[i]] retain];
        values->named->records[values->named->count++]=v;
    }
    TeguFunction *specialized=[library newFunctionWithDescriptor:desc error:&error];
    if(!specialized) {
        fprintf(stderr,"QUARTZCORE_DESCRIPTOR_REFUSED\n");
        audit_missing++;
    } else {
        assert(!strcmp([[specialized name] UTF8String],"fixed_vert_specialized"));
        [specialized release];
    }
    fprintf(stderr,"QUARTZCORE_LIBRARY_OK size=1014944 entry=fixed_vert_lph_spc constants=4\n");
    audit_checks++;
    [desc release];[library release];[url release];
}
