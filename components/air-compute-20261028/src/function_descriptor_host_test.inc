@interface ConstantRecord : NSObject {@public NSUInteger type,index;id key;unsigned char bytes[64];}
@end
@implementation ConstantRecord
- (NSUInteger)dataType{return type;}
- (NSUInteger)index{return index;}
- (id)name{return key;}
- (const void *)data{return bytes;}
- (void)dealloc{[key release];[super dealloc];}
@end
@interface ConstantList : NSObject {@public ConstantRecord *record;}
@end
@implementation ConstantList
- (NSUInteger)count{return record?1:0;}
- (id)objectAtIndex:(NSUInteger)i{assert(i==0&&record);return record;}
- (void)dealloc{[record release];[super dealloc];}
@end
@interface ConstantValues : NSObject {@public ConstantList *indexed,*named;}
@end
@implementation ConstantValues
- (id)newIndexedConstantArray{return [indexed retain];}
- (id)newNamedConstantArray{return [named retain];}
- (void)dealloc{[indexed release];[named release];[super dealloc];}
@end
@interface FunctionDesc : NSObject {@public id entry,alias,values;NSUInteger opts;}
@end
@implementation FunctionDesc
- (id)name{return entry;}
- (id)specializedName{return alias;}
- (id)constantValues{return values;}
- (NSUInteger)options{return opts;}
- (void)dealloc{[entry release];[alias release];[values release];[super dealloc];}
@end
static void test_function_descriptors(TeguComputeDevice *device,const char *path) {
 id error=0;
 TeguLibrary *library=[device newLibraryWithFile:[NSString stringWithUTF8String:path] error:&error];assert(library&&!error);
 FunctionDesc *desc=[[FunctionDesc alloc] init];
 desc->entry=[[NSString stringWithUTF8String:"test_fragment"] retain];
 desc->alias=[[NSString stringWithUTF8String:"specialized_fragment"] retain];
 ConstantValues *values=[[ConstantValues alloc] init];desc->values=values;
 values->indexed=[[ConstantList alloc] init];values->named=[[ConstantList alloc] init];
 ConstantRecord *value=[[ConstantRecord alloc] init];value->type=53;value->index=0;
 values->indexed->record=value;value->bytes[0]=0;
 TeguFunction *fragment[2];fragment[0]=[library newFunctionWithDescriptor:desc error:&error];assert(fragment[0]&&!error);
 assert(library->sourceSHA256[0]&&!memcmp(fragment[0]->library->sourceSHA256,
  library->sourceSHA256,sizeof(library->sourceSHA256)));
 assert(!strcmp([[fragment[0] name] UTF8String],"specialized_fragment"));
 assert(![fragment[0] label]);
 id label=[[NSString stringWithUTF8String:"fragment label"] copy];
 [fragment[0] setLabel:label];[label release];
 assert(!strcmp([[fragment[0] label] UTF8String],"fragment label"));
 [fragment[0] setLabel:[fragment[0] label]];
 assert(!strcmp([[fragment[0] name] UTF8String],"specialized_fragment"));
 [fragment[0] setLabel:0];assert(![fragment[0] label]);
 // Same value by name, with opposite data; prior snapshot must remain false.
 value->key=[[NSString stringWithUTF8String:"red"] retain];value->bytes[0]=1;
 values->named->record=[value retain];[values->indexed->record release];values->indexed->record=0;
 fragment[1]=[library newFunctionWithName:desc->entry constantValues:values error:&error];assert(fragment[1]&&!error);
 assert(!memcmp(fragment[1]->library->sourceSHA256,library->sourceSHA256,
  sizeof(library->sourceSHA256)));
 // Known constant type mismatch, duplicate selector aliases, and descriptor options fail.
 value->type=33;assert(![library newFunctionWithDescriptor:desc error:&error]&&error);value->type=53;
 values->indexed->record=[value retain];assert(![library newFunctionWithDescriptor:desc error:&error]&&error);
 [values->indexed->record release];values->indexed->record=0;
 desc->opts=1;assert(![library newFunctionWithDescriptor:desc error:&error]&&error);desc->opts=0;
 assert(![library newFunctionWithDescriptor:0 error:&error]&&error);
 assert(![library newFunctionWithName:desc->entry constantValues:0 error:&error]&&error);
 TeguFunction *vertex=[library newFunctionWithName:[NSString stringWithUTF8String:"test_vertex"]];assert(vertex);
 [desc release];[library release];drain(); // Functions and snapshots outlive all caller inputs.
 TeguAIRBlob code[2]={{0}},metadata={0};unsigned char *first=0;NSUInteger firstSize=0;
 for(unsigned i=0;i<2;i++) {
  const char *vn=[vertex->functionName UTF8String],*fn=[fragment[i]->functionName UTF8String];char message[512]={0};
  TeguAIRRenderPipeline *p=tegu_air_render_pipeline_create(vertex->library->library,(const uint8_t *)vn,strlen(vn),
   fragment[i]->library->library,(const uint8_t *)fn,strlen(fn),70,tegu_native_profile,sizeof(tegu_native_profile),
   &code[0],&code[1],&metadata,message,sizeof(message));
  if(!p)fprintf(stderr,"descriptor compile: %s\n",message);assert(p);
  assert(!tegu_air_render_pipeline_native_check(p,70,message,sizeof(message)));
  struct tegu_mali_graphics native={0};assert(!tegu_mali_compile_graphics(code[1].bytes,code[1].length,TEGU_GRAPHICS_FRAGMENT,&native,message,sizeof(message)));
  tegu_mali_graphics_free(&native);
  if(!i){firstSize=code[1].length;first=malloc(firstSize);memcpy(first,code[1].bytes,firstSize);}
  else assert(firstSize!=code[1].length||memcmp(first,code[1].bytes,firstSize));
  tegu_air_render_pipeline_close(p);tegu_air_blob_free(&code[0]);tegu_air_blob_free(&code[1]);tegu_air_blob_free(&metadata);
 }
 free(first);[vertex release];[fragment[0] release];[fragment[1] release];drain();
}
