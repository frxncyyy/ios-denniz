#ifdef TEGU_METAL_PROTOCOLS
/* Selector presence is a diagnostic inventory, not semantic conformance:
 * inherited abstract implementations can still reject a call at runtime. */
static int tegu_protocol_inventory(void *objc, void *cls, const char *name) {
 struct method_description { void *name; const char *types; };
 void *(*getProtocol)(const char *)=dlsym(objc,"objc_getProtocol");
 struct method_description *(*methods)(void *,signed char,signed char,unsigned *)=
  dlsym(objc,"protocol_copyMethodDescriptionList");
 void *(*getMethod)(void *,void *)=dlsym(objc,"class_getInstanceMethod");
 const char *(*selectorName)(void *)=dlsym(objc,"sel_getName");
 void *system=dlopen("/usr/lib/libSystem.B.dylib",2);
 void (*freeList)(void *)=system?dlsym(system,"free"):0;
 int (*print)(const char *,...)=system?dlsym(system,"printf"):0;
 if(!getProtocol||!methods||!getMethod||!selectorName||!freeList||!print)return 0;
 void *protocol=getProtocol(name);if(!protocol)return 0;
 unsigned count=0,missing=0;
 struct method_description *list=methods(protocol,1,1,&count);
 if(count && !list)return 0;
 for(unsigned i=0;i<count;i++)if(!getMethod(cls,list[i].name)) {
  missing++;
  print("TEGU_PROTOCOL missing %s %s\n",name,selectorName(list[i].name));
 }
 print("TEGU_PROTOCOL inventory %s required_instance=%u missing=%u presence_only=1\n",name,count,missing);
 freeList(list);return count!=0;
}
#endif
/* Verify the actual framework superclass ABI before allocating our subclass.
 * No system device registration or protocol/capability conformance claim. */
int tegu_framework_device_probe(void) {
 void *objc=dlopen("/usr/lib/libobjc.A.dylib",2);
 void *(*getClass)(const char *)=objc?dlsym(objc,"objc_getClass"):0;
 void *(*superclass)(void *)=objc?dlsym(objc,"class_getSuperclass"):0;
 NSUInteger (*instanceSize)(void *)=objc?dlsym(objc,"class_getInstanceSize"):0;
 void *(*getIvar)(void *,const char *)=objc?dlsym(objc,"class_getInstanceVariable"):0;
 long (*ivarOffset)(void *)=objc?dlsym(objc,"ivar_getOffset"):0;
 if(!getClass||!superclass||!instanceSize||!getIvar||!ivarOffset) {MARK("FAIL framework_runtime_symbols");return 1;}
 void *base=getClass("_MTLDevice"),*derived=getClass("TeguComputeDevice");
 void *ivar=derived?getIvar(derived,"connection"):0;
 if(!base||!derived||superclass(derived)!=base||!ivar||ivarOffset(ivar)<0||
    (NSUInteger)ivarOffset(ivar)<instanceSize(base)||instanceSize(derived)<=instanceSize(base)) {
  MARK("FAIL framework_superclass_layout");return 1;
 }
 MARK("framework_superclass_layout_verified");
 TeguComputeDevice *device=[[TeguComputeDevice alloc] init];
 if(!device) {MARK("FAIL framework_device_init");return 1;}
 int ok=[device name]&&[device registryID]&&[device acceleratorPort]&&device->connection;
#ifdef TEGU_METAL_FEATURE_INIT
 MARK("feature_init_begin unqualified_profile");
 [device initLimits];
 if(!device->limits_ready) {MARK("FAIL framework_limits_initializer");[device release];return 1;}
 const struct tegu_device_limits *limits=[device limits];
#ifdef TEGU_GRAPHICS_BRIDGE
 if(!limits||limits->maxComputeBuffers!=8||limits->maxTotalComputeThreadsPerThreadgroup!=256||
    limits->maxBufferLength!=(16ULL<<20)||limits->maxColorAttachments!=4||
    limits->maxVertexAttributes!=4||limits->maxVertexBuffers!=4||
    limits->maxFragmentBuffers!=7||limits->maxFragmentTextures!=4||
    limits->maxFragmentSamplers!=1||limits->maxTextureWidth1D!=4096||
    limits->maxTextureWidth2D!=4096||limits->maxTextureHeight2D!=4096||
    limits->maxTextureWidth3D!=64||limits->maxTextureHeight3D!=64||limits->maxTextureDepth3D!=64||
    limits->maxIndirectBuffers||limits->maxComputeThreadgroupMemory) {
  MARK("FAIL framework_graphics_limits_readback");[device release];return 1;
 }
 MARK("feature_limits_initialized compositor_2d_bounded_readback");
#else
 if(!limits||limits->maxComputeBuffers!=8||limits->maxTotalComputeThreadsPerThreadgroup!=256||
    limits->maxBufferLength!=(16ULL<<20)||limits->maxColorAttachments||
    limits->maxIndirectBuffers||limits->maxTextureWidth2D||limits->maxComputeThreadgroupMemory) {
  MARK("FAIL framework_limits_readback");[device release];return 1;
 }
 MARK("feature_limits_initialized compute_only_readback");
#endif
 [device initFeatureQueries];
 const long long families[]={1001,1002,1003,1004,1005,1006,1007,1008,1009,
                             2001,2002,3001,3002,3003,4001,5001};
 for(unsigned i=0;i<sizeof(families)/sizeof(families[0]);i++) {
#ifdef TEGU_GRAPHICS_BRIDGE
  signed char expected=families[i]==1001||families[i]==1002;
#else
  signed char expected=0;
#endif
  if([device supportsFamily:families[i]]!=expected)ok=0;
 }
 if([device maxBufferLength]!=(16UL<<20)||[device indirectArgumentBufferCapabilities])ok=0;
#ifdef TEGU_METAL_FEATURE_INIT
 if([device featureProfile]!=TEGU_METAL_FEATURE_PROFILE_UNQUALIFIED)ok=0;
#endif
 if([device supportsTextureSampleCount:1] !=
#ifdef TEGU_GRAPHICS_BRIDGE
    1
#else
    0
#endif
    ||[device supportsTextureSampleCount:4]
#ifdef TEGU_GRAPHICS_BRIDGE
    ||![device supportsFamily:1001]||![device supportsFamily:1002]||[device supportsFamily:1003]
#else
    ||[device supportsFamily:1001]||[device supportsFamily:1002]
#endif
    )ok=0;
#ifdef TEGU_GRAPHICS_BRIDGE
 if(ok) MARK("PASS framework_feature_initialization Apple1_Apple2_compatibility NOT_system_registered");
#else
 if(ok) MARK("PASS framework_feature_initialization no_qualified_GPU_family NOT_system_registered");
#endif
 else MARK("FAIL framework_feature_defaults");
 void *(*getProtocol)(const char *)=dlsym(objc,"objc_getProtocol");
 signed char (*conforms)(void *,void *)=dlsym(objc,"class_conformsToProtocol");
 if(getProtocol&&conforms) {
  void *publicProtocol=getProtocol("MTLDevice"),*privateProtocol=getProtocol("MTLDeviceSPI");
  if(publicProtocol&&conforms(derived,publicProtocol))MARK("framework_MTLDevice_protocol_present");
  else MARK("framework_MTLDevice_protocol_absent");
  if(privateProtocol&&conforms(derived,privateProtocol))MARK("framework_MTLDeviceSPI_protocol_present");
  else MARK("framework_MTLDeviceSPI_protocol_absent");
#ifdef TEGU_METAL_PROTOCOLS
  if(!publicProtocol||!privateProtocol||!conforms(derived,publicProtocol)||!conforms(derived,privateProtocol))ok=0;
  if(!tegu_protocol_inventory(objc,derived,"MTLDevice")||
     !tegu_protocol_inventory(objc,derived,"MTLDeviceSPI"))ok=0;
#endif
 }
#ifdef TEGU_METAL_PROTOCOLS
 else ok=0;
#endif
#endif
 // Exercise the initializer the Metal plugin loader calls, with the real port.
 TeguComputeDevice *second=[[TeguComputeDevice alloc] initWithAcceleratorPort:[device acceleratorPort]];
 if(!second||[second registryID]!=[device registryID])ok=0;
 [device release];
 if(second) {
  unsigned source[16];for(unsigned i=0;i<16;i++)source[i]=0x54454755u^i;
  if([second currentAllocatedSize])ok=0;
  TeguBuffer *buffer=[second newBufferWithBytes:source length:sizeof(source) options:0];
  if(!buffer||[buffer device]!=second||![buffer contents]||[second currentAllocatedSize]!=16384)ok=0;
  if(buffer&&[buffer contents]) {
   const unsigned *copy=[buffer contents];
   for(unsigned i=0;i<16;i++)if(copy[i]!=source[i])ok=0;
   source[0]=0;if(copy[0]!=0x54454755u)ok=0;
  }
  [buffer release];
  if([second currentAllocatedSize]||![second hasUnifiedMemory]||[second isRemovable]||
     [second maxThreadgroupMemoryLength]||
#ifdef TEGU_GRAPHICS_BRIDGE
     ![second supportsSampleCount:1]
#else
     [second supportsSampleCount:1]
#endif
     )ok=0;
  MTLSize maximum=[second maxThreadsPerThreadgroup];
  if(maximum.width!=256||maximum.height!=256||maximum.depth!=256)ok=0;
  if(ok)MARK("PASS device_buffer_copy allocation_accounting compute_properties");
  [second release];
 }
 if(ok) MARK("PASS framework_base_device relocated_ivars accelerator_initializer registry_identity storage NOT_system_registered");
 else MARK("FAIL framework_device_identity_or_storage");
 return ok?0:1;
}
