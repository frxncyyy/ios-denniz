/* Real AIR/planner/Mali compiler with simulated GPU transport: validates API
 * wiring, imported variants, push data and lifetimes, NOT GPU shader outputs. */
#define TEGU_AIR_BRIDGE 1
#define TEGU_MALI_BRIDGE 1
#define main offline_frontend_main
#include "frontend_host_test.m"
#undef main
static id strings[1024];static char *text[1024];
@implementation NSString
+ (id)stringWithUTF8String:(const char *)bytes {
 NSString *s=[[self alloc] init];
 for(unsigned i=0;i<1024;i++) if(!strings[i]) {strings[i]=s;text[i]=strdup(bytes);return [s autorelease];}
 abort();
}
- (id)copy {return [self retain];}
- (const char *)UTF8String { for(unsigned i=0;i<1024;i++) if(strings[i]==self)return text[i];abort(); }
- (signed char)isEqualToString:(id)other {return !strcmp([self UTF8String],[other UTF8String]);}
- (void)dealloc {for(unsigned i=0;i<1024;i++)if(strings[i]==self){free(text[i]);strings[i]=0;}[super dealloc];}
@end
@implementation NXConstantString (RuntimeString)
- (const char *)UTF8String {return bytes;}
@end
@interface RuntimeComputeDescriptor : NSObject { @public id function; }
- (id)computeFunction;
@end
@implementation RuntimeComputeDescriptor
- (id)computeFunction{return function;}
- (void)dealloc{[function release];[super dealloc];}
@end
@implementation NSMutableArray
+ (id)array {abort();}
- (void)addObject:(id)o {(void)o;abort();}
@end
@implementation NSData
+ (id)dataWithContentsOfFile:(id)p {
 if(!p)return 0;FILE *file=fopen([p UTF8String],"rb");if(!file)return 0;
 fseek(file,0,SEEK_END);long size=ftell(file);rewind(file);assert(size>=0);
 NSData *data=[[self alloc] init];data->testLength=(NSUInteger)size;
 data->testBytes=malloc(data->testLength?data->testLength:1);assert(data->testBytes);
 assert(fread(data->testBytes,1,data->testLength,file)==data->testLength);fclose(file);
 return [data autorelease];
}
- (const void *)bytes {return testBytes;}
- (NSUInteger)length {return testLength;}
- (void)dealloc {free(testBytes);[super dealloc];}
@end
static id libraryTestBundle,libraryTestPath;
static signed char libraryTestFileURL=1;
@implementation NSBundle
+ (id)mainBundle{return libraryTestBundle;}
- (id)pathForResource:(id)name ofType:(id)type {
 assert(!strcmp([name UTF8String],"default")&&!strcmp([type UTF8String],"metallib"));return libraryTestPath;
}
@end
@implementation NSURL
- (signed char)isFileURL{return libraryTestFileURL;}
- (id)path{return libraryTestPath;}
@end
static unsigned imports,submissions,expectedLocal,expectedOffset,releasedPipelines;
static struct tegu_pipeline_info imported[32];
static int runtime_scalar(unsigned c,unsigned selector,const U64 *args,unsigned argc,U64 *out,unsigned *n) {
 if(selector==6) {releasedPipelines++;return 0;}
 return call(c,selector,args,argc,out,n);
}
static int runtime_method(unsigned c,unsigned selector,const U64 *args,unsigned argc,const void *data,NSUInteger size,U64 *out,unsigned *n,void *unused,NSUInteger *unusedSize) {
 (void)c;(void)unused;(void)unusedSize;
 if(selector==5) {
  assert(*n==1&&imports<31);unsigned index=++imports;
  assert(!tegu_pipeline_parse(data,size,&imported[index]));out[0]=0x80000000u|index;return 0;
 }
 assert(selector==7&&argc==1&&size==272&&*n==2);
 unsigned index=args[0]&0x7fffffff;assert(index>0&&index<=imports);
 assert(imported[index].local[0]==expectedLocal);
 const unsigned *wire=data;
 assert(wire[0]==1&&wire[1]==256/expectedLocal&&wire[2]==1&&wire[3]==1);
 for(unsigned i=0;i<3;i++)assert(memory[wire[4+4*i]]);
 unsigned base=expectedOffset&~255u;
 assert(wire[5]==base&&wire[9]==base&&wire[13]==0);
 assert(wire[6]==2048-base&&wire[10]==2048-base&&wire[14]==16);
 assert(wire[44]==expectedOffset-base&&wire[45]==expectedOffset-base&&wire[46]==0);
 submissions++;out[0]=++serial;out[1]=42;return 0;
}
#ifndef TEGU_RUNTIME_MAIN
#define TEGU_RUNTIME_MAIN main
#endif
int TEGU_RUNTIME_MAIN(int argc,char **argv) {
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
 fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);void *bytes=malloc(size);assert(fread(bytes,1,size,f)==(size_t)size);fclose(f);
 TeguComputeDevice *d=[TeguComputeDevice alloc];d->scalar=runtime_scalar;d->method=runtime_method;d->map=map;d->unmap=unmap;
 id error=0;TeguLibrary *lib=[d newTeguLibraryWithBytes:bytes length:size error:&error];free(bytes);assert(lib&&!error);
 assert(![lib label]);
 id firstLabel=[NSString stringWithUTF8String:"first-library-label"];
 id secondLabel=[NSString stringWithUTF8String:"replacement-library-label"];
 [lib setLabel:firstLabel];assert([[lib label] isEqualToString:firstLabel]);
 [lib setLabel:secondLabel];assert([[lib label] isEqualToString:secondLabel]);
 [lib setLabel:0];assert(![lib label]);
 TeguFunction *fn=[lib newFunctionWithName:[NSString stringWithUTF8String:"scale_and_offset"]];assert(fn);
 /* A missing MPS-style generated entry must stop at function lookup. It cannot
  * be mistaken for a compute translator/backend failure. */
 assert(![lib newFunctionWithName:[NSString stringWithUTF8String:"D8F18H"]]);
 TeguPipeline *p=[d newComputePipelineStateWithFunction:fn error:&error];assert(p&&!error&&[p maxTotalThreadsPerThreadgroup]>=64);
 id reflection=(id)1;
 TeguPipeline *optionPipeline=[d newComputePipelineStateWithFunction:fn options:0 reflection:&reflection error:&error];
 assert(optionPipeline&&!reflection&&!error);[optionPipeline release];
 RuntimeComputeDescriptor *descriptor=[[RuntimeComputeDescriptor alloc] init];descriptor->function=[fn retain];reflection=(id)1;
 TeguPipeline *descriptorPipeline=[d newComputePipelineStateWithDescriptor:descriptor options:0 reflection:&reflection error:&error];
 assert(descriptorPipeline&&!reflection&&!error);[descriptorPipeline release];
 reflection=(id)1;error=0;
 assert(![d newComputePipelineStateWithDescriptor:descriptor options:1 reflection:&reflection error:&error]&&!reflection&&error);
 [descriptor release];
 [fn release];[lib release];
 TeguQueue *q=[d newCommandQueue];TeguBuffer *a=[d newBufferWithLength:2048 options:0],*b=[d newBufferWithLength:2048 options:0],*scalar=[d newBufferWithLength:4 options:0];
 for(unsigned run=0;run<8;run++) {
  unsigned locals[]={1,8,32,64};expectedLocal=locals[run%4];expectedOffset=run<4?128:384;
  TeguCommand *cmd=[q commandBuffer];TeguEncoder *encoder=[cmd computeCommandEncoder];
  [encoder setComputePipelineState:p];[encoder setBuffer:a offset:expectedOffset atIndex:0];[encoder setBuffer:b offset:expectedOffset atIndex:1];[encoder setBuffer:scalar offset:0 atIndex:2];
  [encoder dispatchThreadgroups:(MTLSize){256/expectedLocal,1,1} threadsPerThreadgroup:(MTLSize){expectedLocal,1,1}];
  [encoder endEncoding];
  if(run==7) {[p release];[a release];[b release];[scalar release];[q release];[d release];}
  [cmd commit];assert([cmd status]==4&&![cmd error]);
  drain();
 }
 assert(imports==4&&submissions==8&&releasedPipelines==4&&allocated==freed&&live==0);
 puts("PASS runtime API: real compiler, specialized cache, descriptor offsets, push data, lifetimes; simulated GPU transport");
 return 0;
}

NSUInteger tegu_host_constant_data_size(NSUInteger type) {
 switch(type){case 53:case 45:case 49:return 1;case 16:case 37:case 41:return 2;
 case 3:case 29:case 33:return 4;default:return 0;}
}
