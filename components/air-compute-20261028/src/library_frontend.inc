/* Native Metal library/function selectors backed by the Rust AIR parser. */
#include "compiler_trace_sha256.h"
#include "pipeline_cache.c"
#ifdef TEGU_RENDER_TRACE
#include "fixed_ui_constant_trace.h"
#endif
@interface TeguLibrary : NSObject {
@public TeguComputeDevice *owner; TeguAIRLibrary *library;
 id libraryLabel;
 char sourceSHA256[65];
 /* Pipeline cache identity: SHA-256 of the source metallib, plus for a
  * specialization snapshot its entry and every constant (kind, type, index,
  * name, value).  Invalid -> pipelines from it are never cached. */
 unsigned char cacheIdentity[32];unsigned cacheIdentityValid;
 /* Specialization as text (entry|count|kind:type:index:name:hex;...), for
  * the pipeline cache's miss records; NULL for a whole metallib. */
 char *cacheReplay;
}
- (id)device;
- (id)label;
- (void)setLabel:(id)label;
- (id)functionNames;
- (id)newFunctionWithName:(id)name;
- (id)newFunctionWithName:(id)name constantValues:(id)values error:(id *)error;
- (void)newFunctionWithName:(id)name constantValues:(id)values
 completionHandler:(void (^)(id function,NSError *error))completionHandler;
- (id)newFunctionWithDescriptor:(id)descriptor error:(id *)error;
@end
@interface TeguFunction : NSObject {
@public TeguLibrary *library; id functionName,displayName,functionLabel; unsigned kind;
}
- (id)device;
- (id)name;
- (id)label;
- (void)setLabel:(id)label;
- (NSUInteger)functionType;
@end
/* Public library loaders may still return donor function objects. Establish
 * class identity before accessing our ivars; public functionType is not proof
 * that an object has TeguFunction's layout. */
#ifdef TEGU_FRONTEND_HOST
/* GNU libobjc implements object_getClass inline; host NSObject has a plain isa. */
static id tegu_host_object_class(id object){return object?*(id *)object:0;}
#endif
/* Encoder setters and draws call this ~10-15 times per draw. On the device it
 * used to dlopen libobjc and dlsym three symbols, then objc_getClass by name,
 * on every call. The runtime entry points and the classes are process
 * constants: resolve the entry points once and keep each expected class
 * after its first successful lookup (keyed by the literal's address). */
struct tegu_owned_class_cache {const char *name;void *cls;};
static int tegu_owned_class(id function,const char *expectedName) {
 static id (*objectClass)(id);static id (*getClass)(const char *);static const char *(*className)(id);
 static struct tegu_owned_class_cache classes[16];static unsigned resolved;
 if(!__atomic_load_n(&resolved,__ATOMIC_ACQUIRE)) {
#ifdef TEGU_FRONTEND_HOST
  extern id objc_getClass(const char *);
  extern const char *class_getName(id);
  id (*oc)(id)=tegu_host_object_class;id (*gc)(const char *)=objc_getClass;const char *(*cn)(id)=class_getName;
#else
  void *runtime=dlopen("/usr/lib/libobjc.A.dylib",2);
  id (*oc)(id)=runtime?dlsym(runtime,"object_getClass"):0;
  id (*gc)(const char *)=runtime?dlsym(runtime,"objc_getClass"):0;
  const char *(*cn)(id)=runtime?dlsym(runtime,"class_getName"):0;
#endif
  if(!oc||!gc)return 0;
  /* Benign race: every thread stores the same values. */
  __atomic_store_n(&objectClass,oc,__ATOMIC_RELAXED);__atomic_store_n(&getClass,gc,__ATOMIC_RELAXED);
  __atomic_store_n(&className,cn,__ATOMIC_RELAXED);__atomic_store_n(&resolved,1,__ATOMIC_RELEASE);
 }
 if(!function)return 0;
 id expected=0;
 for(unsigned i=0;i<16;i++) {
  const char *n=__atomic_load_n(&classes[i].name,__ATOMIC_ACQUIRE);
  if(!n)break;
  if(n==expectedName){expected=(id)__atomic_load_n(&classes[i].cls,__ATOMIC_RELAXED);break;}
 }
 if(!expected) {
  expected=getClass(expectedName);
  if(expected) {
   /* Insert under a lock; readers match the name (release-published after
    * its class) without one. A full table only costs the lookup above. */
   static unsigned lock;
   while(__sync_lock_test_and_set(&lock,1)){}
   for(unsigned i=0;i<16;i++) {
    const char *n=__atomic_load_n(&classes[i].name,__ATOMIC_RELAXED);
    if(n==expectedName)break;
    if(!n){__atomic_store_n(&classes[i].cls,(void *)expected,__ATOMIC_RELAXED);__atomic_store_n(&classes[i].name,expectedName,__ATOMIC_RELEASE);break;}
   }
   __sync_lock_release(&lock);
  }
 }
 id actual=objectClass(function);
 if(expected&&actual==expected)return 1;
 const char *name=className?className(actual):"unknown";
 NSUInteger length=0;while(name&&name[length]&&length<128)length++;
 write(2,"TEGU_SHADER foreign_class=",26);if(name)write(2,name,length);write(2,"\n",1);
 return 0;
}
static int tegu_function_owned_class(id function){return tegu_owned_class(function,"TeguFunction");}
static int tegu_pc_library_identity(const char *sourceSHA256,unsigned char out[32]) {
 if(!sourceSHA256||!sourceSHA256[0])return -1;
 struct tegu_pc_sha256 s;tegu_pc_sha256_init(&s);tegu_pc_key_str(&s,"TPC-LIB-1");tegu_pc_key_str(&s,sourceSHA256);
 tegu_pc_sha256_final(&s,out);return 0;
}
static char *tegu_pc_specialized_replay(const char *entry,NSUInteger entryLength,const TeguAIRFunctionConstant *constants,NSUInteger count) {
 size_t cap=entryLength+32,n=0;
 for(NSUInteger i=0;i<count;i++)cap+=48+(constants[i].kind?constants[i].name_len:0)+2*sizeof(constants[i].data);
 char *out=malloc(cap);if(!out)return 0;
 static const char hex[]="0123456789abcdef";
 n+=(size_t)snprintf(out+n,cap-n,"%.*s|%lu|",(int)entryLength,entry,(unsigned long)count);
 for(NSUInteger i=0;i<count&&n<cap;i++) {
  const TeguAIRFunctionConstant *c=&constants[i];
  n+=(size_t)snprintf(out+n,cap-n,"%u:%u:%u:%.*s:",c->kind,c->type,c->kind?0:c->index,(int)(c->kind?c->name_len:0),c->kind?(const char *)c->name:"");
  for(uint32_t b=0;b<c->size&&b<sizeof(c->data)&&n+3<cap;b++){out[n++]=hex[c->data[b]>>4];out[n++]=hex[c->data[b]&15];}
  if(n+2<cap)out[n++]=';';
 }
 out[n<cap?n:cap-1]=0;return out;
}
static void tegu_pc_specialized_identity(const unsigned char parent[32],const char *entry,NSUInteger entryLength,
 const TeguAIRFunctionConstant *constants,NSUInteger count,unsigned char out[32]) {
 struct tegu_pc_sha256 s;tegu_pc_sha256_init(&s);tegu_pc_key_str(&s,"TPC-SPEC-1");
 tegu_pc_key_bytes(&s,parent,32);tegu_pc_key_bytes(&s,entry,entryLength);tegu_pc_key_u32(&s,(uint32_t)count);
 for(NSUInteger i=0;i<count;i++) {
  const TeguAIRFunctionConstant *c=&constants[i];
  tegu_pc_key_u32(&s,c->kind);tegu_pc_key_u32(&s,c->type);tegu_pc_key_u32(&s,c->kind?0:c->index);
  tegu_pc_key_bytes(&s,c->kind?c->name:0,c->kind?c->name_len:0);
  tegu_pc_key_bytes(&s,c->data,c->size<=sizeof(c->data)?c->size:sizeof(c->data));
 }
 tegu_pc_sha256_final(&s,out);
}
@interface NSBundle : NSObject
+ (id)mainBundle;
- (id)pathForResource:(id)name ofType:(id)type;
@end
@interface NSURL : NSObject
- (signed char)isFileURL;
- (id)path;
@end
@interface TeguComputeDevice (LibraryLoaders)
- (id)newDefaultLibrary;
- (id)newDefaultLibraryWithBundle:(id)bundle error:(id *)error;
- (id)newLibraryWithURL:(NSURL *)url error:(id *)error;
- (id)newLibraryWithFile:(id)path error:(id *)error;
@end
@implementation TeguComputeDevice (Libraries)
- (id)newTeguLibraryWithBytes:(const void *)bytes length:(NSUInteger)length error:(id *)error {
 if(error) *error=0;
 char message[512]={0};
 TeguAIRLibrary *handle=tegu_air_library_open(bytes,length,message,sizeof(message));
 if(!handle) {
  MARK("library_parse_refused");
  if(error) *error=[NSError errorWithDomain:@"TeguAIRLibrary" code:1 userInfo:0];
  return 0;
 }
 TeguLibrary *result=[[TeguLibrary alloc] init];
 if(!result) { tegu_air_library_close(handle); return 0; }
 result->owner=[self retain]; result->library=handle;
 /* Keep only an immutable identity, never the caller's metallib bytes.  Exact
  * native package classes can consequently bind their generated code to the
  * library which produced it without extending the library lifetime contract. */
 if(tegu_compiler_identity_sha256(bytes,length,result->sourceSHA256))result->sourceSHA256[0]=0;
 result->cacheIdentityValid=!tegu_pc_library_identity(result->sourceSHA256,result->cacheIdentity);
 return result;
}
/* Override loaders that _MTLDevice sends straight to its own libraryBuilder,
 * otherwise the returned functions belong to Apple's compiler, not our bridge. */
- (id)newDefaultLibrary {return [self newDefaultLibraryWithBundle:[NSBundle mainBundle] error:0];}
- (id)newDefaultLibraryWithBundle:(id)bundle error:(id *)error {
 if(error)*error=0;
 id path=[bundle pathForResource:@"default" ofType:@"metallib"];
 if(!path) {
  if(error)*error=[NSError errorWithDomain:@"TeguAIRLibrary" code:2 userInfo:0];
  return 0;
 }
 return [self newLibraryWithFile:path error:error];
}
- (id)newLibraryWithURL:(NSURL *)url error:(id *)error {
 if(error)*error=0;
 if(!url||![url isFileURL]) {
  if(error)*error=[NSError errorWithDomain:@"TeguAIRLibrary" code:3 userInfo:0];
  return 0;
 }
 return [self newLibraryWithFile:[url path] error:error];
}
- (id)newLibraryWithFile:(id)path error:(id *)error {
 NSData *data=path?[NSData dataWithContentsOfFile:path]:0;
 return [self newTeguLibraryWithBytes:[data bytes] length:[data length] error:error];
}
- (id)newLibraryWithData:(id)data error:(id *)error {
 /* Metal takes dispatch_data_t, NOT NSData. Mapping may allocate a contiguous
  * temporary; library_open copies what it needs before releasing the mapping. */
 void *sys=dlopen("/usr/lib/libSystem.B.dylib",2);
 id (*mapData)(id,const void **,NSUInteger *)=sys?dlsym(sys,"dispatch_data_create_map"):0;
 void (*releaseData)(id)=sys?dlsym(sys,"dispatch_release"):0;
 const void *bytes=0; NSUInteger length=0;
 id mapped=data&&mapData&&releaseData?mapData(data,&bytes,&length):0;
 id result=[self newTeguLibraryWithBytes:bytes length:length error:error];
 if(mapped) releaseData(mapped);
 return result;
}
@end
@interface NSObject (TeguFunctionInputs)
- (id)constantValues;
- (id)specializedName;
- (NSUInteger)options;
- (id)newNamedConstantArray;
- (id)newIndexedConstantArray;
- (NSUInteger)count;
- (id)objectAtIndex:(NSUInteger)index;
- (NSUInteger)dataType;
- (NSUInteger)index;
- (const void *)data;
@end
static void tegu_function_error(id *error,const char *message) {
 NSUInteger n=0;while(message[n]&&n<511)n++;
 write(2,"TEGU_FUNCTION ",14);write(2,message,n);write(2,"\n",1);
 if(error)*error=[NSError errorWithDomain:@"TeguAIRFunction" code:1 userInfo:0];
}
static void tegu_function_lookup_missing(const char *name,NSUInteger nameLength,TeguLibrary *library) {
 /* MPS asserts after a nil function lookup and otherwise hides whether the
  * metallib inventory or compute translation failed. Emit the lookup boundary
  * once per request, before any compute-pipeline method can run. */
 static unsigned emitted;
 if(!name||!library||__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED)>=8)return;
 char line[320];NSUInteger shown=nameLength<96?nameLength:96;
 int n=snprintf(line,sizeof(line),
  "TEGU_FUNCTION_LOOKUP result=missing name=%.*s name_bytes=%lu entries=%lu source_sha16=%.16s\n",
  (int)shown,name,(unsigned long)nameLength,
  (unsigned long)tegu_air_function_count(library->library),library->sourceSHA256);
 if(n>0)write(2,line,(NSUInteger)n<sizeof(line)?(NSUInteger)n:sizeof(line)-1);
}
@implementation TeguLibrary
- (id)device { return owner; }
- (id)label { return libraryLabel; }
- (void)setLabel:(id)label { id value=[label copy];[libraryLabel release];libraryLabel=value; }
- (id)functionNames {
 id names=[NSMutableArray array];
 for(NSUInteger i=0;i<tegu_air_function_count(library);i++) {
  char name[4096]; unsigned kind;
  NSUInteger required=tegu_air_function_info(library,i,name,sizeof(name),&kind);
  if(required>0&&required<=sizeof(name)&&kind) [names addObject:[NSString stringWithUTF8String:name]];
 }
 return names;
}
- (id)newFunctionWithName:(id)requested {
 if(!requested) return 0;
 const char *requestedName=[requested UTF8String];
 if(!requestedName) return 0;
 NSUInteger requestedLength=0;
 while(requestedLength<4096&&requestedName[requestedLength])requestedLength++;
 if(requestedLength==4096) return 0;
 for(NSUInteger i=0;i<tegu_air_function_count(library);i++) {
  char name[4096]; unsigned kind;
  NSUInteger required=tegu_air_function_info(library,i,name,sizeof(name),&kind);
  if(!required||required>sizeof(name)||!kind) continue;
  /* The inventory can contain hundreds of entries. Compare its bounded C
   * spelling before constructing an Objective-C string so one lookup does
   * not allocate a temporary NSString for every preceding function. */
  if(required!=requestedLength+1||memcmp(name,requestedName,requestedLength+1)) continue;
  id entry=[NSString stringWithUTF8String:name];if(!entry)return 0;
  TeguFunction *function=[[TeguFunction alloc] init];
  if(function) { function->library=[self retain];function->functionName=[entry retain];function->kind=kind; }
  return function;
 }
 tegu_function_lookup_missing(requestedName,requestedLength,self);
 return 0;
}
- (id)newFunctionWithName:(id)name constantValues:(id)values error:(id *)error {
 if(error)*error=0;
 TeguFunction *function=[self newFunctionWithName:name];
 if(!function){tegu_function_error(error,"function name not found");return 0;}
 id indexed=values?[values newIndexedConstantArray]:0,named=values?[values newNamedConstantArray]:0;
 NSUInteger counts[2]={[indexed count],[named count]};
 TeguAIRFunctionConstant *constants=0;TeguAIRLibrary *specialized=0;char message[512]={0};
 if(counts[0]>256||counts[1]>256-counts[0])goto fail;
 NSUInteger count=counts[0]+counts[1];
 constants=count?malloc(count*sizeof(*constants)):0;if(count&&!constants)goto fail;
 if(count) {
#ifdef TEGU_FRONTEND_HOST
  extern NSUInteger tegu_host_constant_data_size(NSUInteger);
  NSUInteger (*sizeOf)(NSUInteger)=tegu_host_constant_data_size;
#else
  void *metal=dlopen("/System/Library/Frameworks/Metal.framework/Metal",2);
  NSUInteger (*sizeOf)(NSUInteger)=metal?dlsym(metal,"_MTLConstantDataSize"):0;
#endif
  if(!sizeOf)goto fail;
  for(unsigned kind=0;kind<2;kind++)for(NSUInteger i=0;i<counts[kind];i++) {
   id value=[(kind?named:indexed) objectAtIndex:i];
   NSUInteger type=[value dataType],size=sizeOf(type);
   if(type>0xffffffffUL||!size||size>64||![value data])goto fail;
   TeguAIRFunctionConstant *c=&constants[(kind?counts[0]:0)+i];
   __builtin_memset(c,0,sizeof(*c));c->kind=kind;c->type=(uint32_t)type;c->size=(uint32_t)size;
   if(kind) {
    c->name=(const uint8_t *)[[value name] UTF8String];if(!c->name)goto fail;
    while(c->name_len<4096&&c->name[c->name_len])c->name_len++;
    if(!c->name_len||c->name_len==4096)goto fail;
   } else {NSUInteger index=[value index];if(index>0xffffffffUL)goto fail;c->index=(uint32_t)index;}
   memcpy(c->data,[value data],size);
  }
 }
 const char *entry=[function->functionName UTF8String];NSUInteger length=0;
 while(entry&&entry[length]&&length<4096)length++;
 /* Preserve the exact QuartzCore specialization values needed to reproduce
  * R80's fixed UI pair off-device. Keep this bounded per process and emit each
  * constant as one parseable record so concurrent specializations can be
  * grouped by serial. Render-trace builds only: 1400 records filled R171's
  * whole USB log ring before SpringBoard unlocked. */
#ifdef TEGU_RENDER_TRACE
 unsigned fixedIndex=entry&&!__builtin_strcmp(entry,"fixed_frag_lph_cph")?1:
  (entry&&!__builtin_strcmp(entry,"fixed_vert_lph_spc")?0:2);
 if(fixedIndex<2) {
  static unsigned emitted[2],serials;
  unsigned serial=__atomic_fetch_add(&serials,1,__ATOMIC_RELAXED);
  /* Lock-screen variants arrive after the first eight fragment specializations. */
  if(__atomic_fetch_add(&emitted[fixedIndex],1,__ATOMIC_RELAXED)<(fixedIndex?24u:8u)) {
   char header[192];int hn=snprintf(header,sizeof(header),
    "TEGU_FIXED_UI_FC_BEGIN entry=%s serial=%u defined_count=%lu\n",entry,serial,(unsigned long)count);
   if(hn>0)write(2,header,(NSUInteger)hn<sizeof(header)?(NSUInteger)hn:sizeof(header)-1);
   for(NSUInteger i=0;i<count;i++) {
    char line[384];TeguAIRFunctionConstant *c=&constants[i];
    size_t bytes=tegu_fixed_ui_constant_trace_format(line,sizeof(line),entry,serial,i,
     c->kind,c->index,c->name,c->name_len,c->type,c->data,c->size);
    if(bytes)write(2,line,bytes);
   }
   int en=snprintf(header,sizeof(header),"TEGU_FIXED_UI_FC_END entry=%s serial=%u\n",entry,serial);
   if(en>0)write(2,header,(NSUInteger)en<sizeof(header)?(NSUInteger)en:sizeof(header)-1);
  }
 }
#endif
 specialized=tegu_air_library_specialize(library,(const uint8_t *)entry,length,constants,count,message,sizeof(message));
 if(!specialized)goto fail;
 TeguLibrary *snapshot=[[TeguLibrary alloc] init];if(!snapshot)goto fail;
 snapshot->owner=[owner retain];snapshot->library=specialized;specialized=0;
 /* Specialization changes the parsed AIR library, but its immutable source
  * provenance remains the metallib supplied to the public loader.  Preserve
  * that identity so exact native packages can bind the specialized SPIR-V and
  * generated code to the original metallib instead of seeing an empty hash. */
 __builtin_memcpy(snapshot->sourceSHA256,sourceSHA256,sizeof(sourceSHA256));
 if(cacheIdentityValid) {
  tegu_pc_specialized_identity(cacheIdentity,entry,length,constants,count,snapshot->cacheIdentity);
  snapshot->cacheIdentityValid=1;snapshot->cacheReplay=tegu_pc_specialized_replay(entry,length,constants,count);
 }
 [function->library release];function->library=snapshot;
 free(constants);[indexed release];[named release];return function;
fail:
 tegu_air_library_close(specialized);free(constants);[indexed release];[named release];[function release];
 tegu_function_error(error,message[0]?message:"invalid or unsupported function constants");return 0;
}
- (id)newFunctionWithDescriptor:(id)descriptor error:(id *)error {
 if(error)*error=0;
 if(!descriptor||[descriptor options]){tegu_function_error(error,"unsupported function descriptor options");return 0;}
 TeguFunction *function=[self newFunctionWithName:[descriptor name] constantValues:[descriptor constantValues] error:error];
 if(function)function->displayName=[[descriptor specializedName] copy];
 return function;
}
- (void)newFunctionWithName:(id)name constantValues:(id)values
 completionHandler:(void (^)(id,NSError *))completionHandler {
 NSError *error=0;
 id function=[self newFunctionWithName:name constantValues:values error:&error];
 /* Completion is exactly-once. This bridge has no asynchronous compiler
  * queue, so the handler runs after the synchronous specialization result is
  * known; it still receives the same nil-or-function and NSError contract. */
 if(completionHandler)completionHandler(function,error);
 /* The handler receives a borrowed reference (Metal async contract); drop the
  * +1 from the synchronous new... call so each async creation does not leak. */
 [function release];
}
- (void)dealloc { tegu_air_library_close(library); free(cacheReplay); [libraryLabel release]; [owner release]; [super dealloc]; }
@end
@implementation TeguFunction
- (id)device { return library->owner; }
- (id)name { return displayName?displayName:functionName; }
- (id)label { return functionLabel; }
- (void)setLabel:(id)label { id value=[label copy];[functionLabel release];functionLabel=value; }
- (NSUInteger)functionType { return kind; }
- (void)dealloc { [functionLabel release];[displayName release];[functionName release]; [library release]; [super dealloc]; }
@end

#ifdef TEGU_MALI_BRIDGE
#include "runtime_pipeline.inc"
#ifdef TEGU_GRAPHICS_BRIDGE
#include "sampler_state.inc"
#include "render_pipeline.inc"
#include "render_encoder.inc"
#endif
#endif

#ifdef TEGU_AIR_FIXTURE
#include "tegu-air-fixture.h"
#ifdef TEGU_MALI_BRIDGE
#include "runtime_pipeline_probe.inc"
#endif
int tegu_library_frontend_probe(void) {
 MARK("library_begin dispatch_data");
 TeguComputeDevice *device=[[TeguComputeDevice alloc] init];
 void *sys=dlopen("/usr/lib/libSystem.B.dylib",2);
 id (*create)(const void *,NSUInteger,void *,void *)=sys?dlsym(sys,"dispatch_data_create"):0;
 void (*dispose)(id)=sys?dlsym(sys,"dispatch_release"):0;
 if(!device||!create||!dispose) { [device release]; MARK("FAIL library_device_or_dispatch"); return 1; }
 id data=create(tegu_air_fixture,sizeof(tegu_air_fixture),0,0),error=0;
 TeguLibrary *library=[device newLibraryWithData:data error:&error];
 if(data) dispose(data);
 TeguFunction *function=[library newFunctionWithName:@"scale_and_offset"];
 int ok=library&&function&&!error&&[function functionType]==3&&[function device]==device;
 id missing=[library newFunctionWithName:@"tegu_missing_function"];
 if(missing) { ok=0; [missing release]; }
 [library release]; // function must keep library alive
 if(function&&![[function name] isEqualToString:@"scale_and_offset"]) ok=0;
 [function release];
 error=0;
 id invalid=[device newLibraryWithData:0 error:&error];
 if(invalid||!error) ok=0;
 [invalid release];[device release];
 if(ok) MARK("PASS library_dispatch_data function_lookup retained_lifetime invalid_input");
 else MARK("FAIL library_dispatch_data_or_function");
 return ok?0:1;
}
#endif
