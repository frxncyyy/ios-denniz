/* Argument buffers with device pointers: MTLBuffer.gpuAddress and MTLArgumentEncoder over explicit
 * MTLArgumentDescriptor arrays (-newArgumentEncoderWithArguments:).
 *
 * What is implemented (host tests: api_test_argument_buffers):
 *  - gpuAddress: the BO's GPU VA from the bridge (n2mb_buffer_va), cached; 0 when unavailable.
 *  - Layout: members in ascending descriptor index, each at the natural MSL alignment of its type
 *    (scalars/vectors/matrices; float3 occupies 16 bytes), arrays contiguous, textures/samplers/pointers
 *    8 bytes aligned 8, constantBlockAlignment honoured; encodedLength is rounded to the largest alignment.
 *  - setBuffer / setBuffers write gpuAddress+offset (8 bytes); a nil buffer writes 0; constantDataAtIndex
 *    returns the member's storage inside the argument buffer.  setArgumentBuffer:offset: and the
 *    startOffset:arrayElement: form select the destination (offset alignment and extent checked).
 *  - Residency: Metal requires useResource/useHeap for pointees; those retain for the command's life
 *    (api_events.inc).  Whether the Mali kernel flushes/invalidates a BO reached only through a pointer
 *    is an open bridge contract (board: argument buffers), so this is NOT claimed to work on a device.
 * What is refused (logged, never faked): texture and sampler members (no resource-ID model on this
 * hardware path), nested/struct/array argument descriptors, pipeline-state / ICB / function-table /
 * acceleration-structure members, and encoders derived from a function (newArgumentEncoderWithBufferIndex:,
 * which needs the compiler's argument layout). */
#ifdef TEGU_GRAPHICS_BRIDGE
extern int n2mb_buffer_va(uintptr_t,uint32_t,uint64_t *,uint64_t *);
#endif
static U64 tegu_buffer_gpu_address(TeguBuffer *buffer) {
 if(!buffer)return 0;
 U64 va=__atomic_load_n(&buffer->gpuVA,__ATOMIC_ACQUIRE);
 if(va)return va;
#ifdef TEGU_GRAPHICS_BRIDGE
 uint64_t found=0;
 if(!buffer->owner||!buffer->owner->connection||!buffer->handle||buffer->handle>0xffffffffull||
    n2mb_buffer_va(buffer->owner->connection,(uint32_t)buffer->handle,&found,0)||!found)return 0;
 __atomic_store_n(&buffer->gpuVA,found,__ATOMIC_RELEASE);
 return found;
#else
 return 0;
#endif
}

enum {TEGU_ARG_CONSTANT=0,TEGU_ARG_BUFFER=1,TEGU_ARG_TEXTURE=2,TEGU_ARG_SAMPLER=3};
#define TEGU_ARG_MAX_INDEX 256u
struct TeguArgMember {unsigned char used,kind;uint32_t offset,size,stride,count,align;};
struct TeguArgInfo {uint32_t size,align;unsigned char kind;};

/* Size/alignment of an MTLDataType as an MSL struct member; 0 size = unsupported here. */
static struct TeguArgInfo tegu_arg_type_info(NSUInteger type) {
 struct TeguArgInfo r={0,0,TEGU_ARG_CONSTANT};
 if(type==58){r.size=8;r.align=8;r.kind=TEGU_ARG_TEXTURE;return r;}
 if(type==59){r.size=8;r.align=8;r.kind=TEGU_ARG_SAMPLER;return r;}
 if(type==60){r.size=8;r.align=8;r.kind=TEGU_ARG_BUFFER;return r;}
 uint32_t scalar=0,columns=1,rows=1;
 if(type>=3&&type<=6){scalar=4;rows=(uint32_t)(type-2);}                            /* float, float2..4 */
 else if(type>=7&&type<=15){scalar=4;columns=2+(uint32_t)(type-7)/3;rows=2+(uint32_t)(type-7)%3;}
 else if(type>=16&&type<=19){scalar=2;rows=(uint32_t)(type-15);}                    /* half */
 else if(type>=20&&type<=28){scalar=2;columns=2+(uint32_t)(type-20)/3;rows=2+(uint32_t)(type-20)%3;}
 else if(type>=29&&type<=32){scalar=4;rows=(uint32_t)(type-28);}                    /* int */
 else if(type>=33&&type<=36){scalar=4;rows=(uint32_t)(type-32);}                    /* uint */
 else if(type>=37&&type<=40){scalar=2;rows=(uint32_t)(type-36);}                    /* short */
 else if(type>=41&&type<=44){scalar=2;rows=(uint32_t)(type-40);}                    /* ushort */
 else if(type>=45&&type<=48){scalar=1;rows=(uint32_t)(type-44);}                    /* char */
 else if(type>=49&&type<=52){scalar=1;rows=(uint32_t)(type-48);}                    /* uchar */
 else if(type>=53&&type<=56){scalar=1;rows=(uint32_t)(type-52);}                    /* bool */
 else return r;
 uint32_t vecAlign=scalar*(rows==3?4u:rows);
 r.align=vecAlign;r.size=vecAlign*columns;
 return r;
}

@interface TeguArgumentEncoder : NSObject {
@public TeguComputeDevice *owner;id label;
 struct TeguArgMember *members;NSUInteger memberLimit; /* indexed by descriptor index */
 NSUInteger encodedLength,alignment;
 TeguBuffer *buffer;NSUInteger bufferOffset;
}
- (id)device;
- (id)label;
- (void)setLabel:(id)label;
- (NSUInteger)encodedLength;
- (NSUInteger)alignment;
- (void)setArgumentBuffer:(id)buffer offset:(NSUInteger)offset;
- (void)setArgumentBuffer:(id)buffer startOffset:(NSUInteger)startOffset arrayElement:(NSUInteger)arrayElement;
- (void)setBuffer:(id)buffer offset:(NSUInteger)offset atIndex:(NSUInteger)index;
- (void)setBuffers:(id const *)buffers offsets:(const NSUInteger *)offsets withRange:(TeguRange)range;
- (void)setTexture:(id)texture atIndex:(NSUInteger)index;
- (void)setTextures:(id const *)textures withRange:(TeguRange)range;
- (void)setSamplerState:(id)sampler atIndex:(NSUInteger)index;
- (void)setSamplerStates:(id const *)samplers withRange:(TeguRange)range;
- (void *)constantDataAtIndex:(NSUInteger)index;
- (id)newArgumentEncoderForBufferAtIndex:(NSUInteger)index;
- (void)setRenderPipelineState:(id)state atIndex:(NSUInteger)index;
- (void)setRenderPipelineStates:(id const *)states withRange:(TeguRange)range;
- (void)setComputePipelineState:(id)state atIndex:(NSUInteger)index;
- (void)setComputePipelineStates:(id const *)states withRange:(TeguRange)range;
- (void)setIndirectCommandBuffer:(id)buffer atIndex:(NSUInteger)index;
- (void)setIndirectCommandBuffers:(id const *)buffers withRange:(TeguRange)range;
- (void)setAccelerationStructure:(id)structure atIndex:(NSUInteger)index;
- (void)setVisibleFunctionTable:(id)table atIndex:(NSUInteger)index;
- (void)setVisibleFunctionTables:(id const *)tables withRange:(TeguRange)range;
- (void)setIntersectionFunctionTable:(id)table atIndex:(NSUInteger)index;
- (void)setIntersectionFunctionTables:(id const *)tables withRange:(TeguRange)range;
@end
@interface NSObject (TeguArgumentDescriptorInput)
- (NSUInteger)dataType;
- (NSUInteger)index;
- (NSUInteger)arrayLength;
- (NSUInteger)constantBlockAlignment;
- (NSUInteger)count;
- (id)objectAtIndex:(NSUInteger)index;
@end

@implementation TeguArgumentEncoder
- (id)device { return owner; }
- (id)label { return label; }
- (void)setLabel:(id)value { id copy=[value copy];[label release];label=copy; }
- (NSUInteger)encodedLength { return encodedLength; }
- (NSUInteger)alignment { return alignment; }
- (struct TeguArgMember *)memberFor:(NSUInteger)index element:(NSUInteger *)element {
 /* an array member answers for index .. index+count-1 */
 if(!memberLimit)return 0;
 NSUInteger i=index<memberLimit?index:memberLimit-1;
 for(;;) {
  struct TeguArgMember *m=&members[i];
  if(m->used){if(index-i<m->count){*element=index-i;return m;}return 0;}
  if(!i)return 0;
  i--;
 }
}
- (unsigned char *)memberBytes:(struct TeguArgMember *)m element:(NSUInteger)element {
 if(!buffer)return 0;
 unsigned char *bytes=(unsigned char *)tegu_buffer_bytes(buffer);
 return bytes?bytes+bufferOffset+m->offset+element*m->stride:0;
}
- (void)setArgumentBuffer:(id)value offset:(NSUInteger)offset {
 if(value&&(!tegu_api_class_is(value,"TeguBuffer",0)||((TeguBuffer *)value)->owner!=owner||offset%alignment||
    offset>((TeguBuffer *)value)->logical||encodedLength>((TeguBuffer *)value)->logical-offset)) {
  tegu_api_unsupported("argument_buffer_extent");return;
 }
 [value retain];[buffer release];buffer=(TeguBuffer *)value;bufferOffset=value?offset:0;
}
- (void)setArgumentBuffer:(id)value startOffset:(NSUInteger)startOffset arrayElement:(NSUInteger)arrayElement {
 NSUInteger stride=(encodedLength+alignment-1)/alignment*alignment;
 if(arrayElement&&stride>(~(NSUInteger)0)/arrayElement){tegu_api_unsupported("argument_buffer_extent");return;}
 [self setArgumentBuffer:value offset:startOffset+arrayElement*stride];
}
- (void)setBuffer:(id)value offset:(NSUInteger)offset atIndex:(NSUInteger)index {
 NSUInteger element=0;struct TeguArgMember *m=[self memberFor:index element:&element];
 if(!m||m->kind!=TEGU_ARG_BUFFER){tegu_api_unsupported("argument_encoder_index");return;}
 unsigned char *slot=[self memberBytes:m element:element];
 if(!slot){tegu_api_unsupported("argument_buffer_unset");return;}
 uint64_t address=0;
 if(value) {
  if(!tegu_api_class_is(value,"TeguBuffer",0)||((TeguBuffer *)value)->owner!=owner||offset>((TeguBuffer *)value)->logical){
   tegu_api_unsupported("argument_buffer_pointee");return;
  }
  U64 base=tegu_buffer_gpu_address((TeguBuffer *)value);
  if(!base){tegu_api_unsupported("argument_buffer_gpu_address");return;}
  address=base+offset;
 }
 __builtin_memcpy(slot,&address,8);
}
- (void)setBuffers:(id const *)buffers offsets:(const NSUInteger *)offsets withRange:(TeguRange)range {
 if(range.length&&(!buffers||!offsets)){tegu_api_unsupported("argument_encoder_range");return;}
 for(NSUInteger i=0;i<range.length;i++)[self setBuffer:buffers[i] offset:offsets[i] atIndex:range.location+i];
}
- (void)setTexture:(id)texture atIndex:(NSUInteger)index { (void)texture;(void)index;tegu_api_unsupported("argument_buffer_texture"); }
- (void)setTextures:(id const *)textures withRange:(TeguRange)range { (void)textures;(void)range;tegu_api_unsupported("argument_buffer_texture"); }
- (void)setSamplerState:(id)sampler atIndex:(NSUInteger)index { (void)sampler;(void)index;tegu_api_unsupported("argument_buffer_sampler"); }
- (void)setSamplerStates:(id const *)samplers withRange:(TeguRange)range { (void)samplers;(void)range;tegu_api_unsupported("argument_buffer_sampler"); }
- (void *)constantDataAtIndex:(NSUInteger)index {
 NSUInteger element=0;struct TeguArgMember *m=[self memberFor:index element:&element];
 if(!m||m->kind!=TEGU_ARG_CONSTANT){tegu_api_unsupported("argument_encoder_index");return 0;}
 return [self memberBytes:m element:element];
}
- (id)newArgumentEncoderForBufferAtIndex:(NSUInteger)index { (void)index;tegu_api_unsupported("argument_encoder_nested");return 0; }
- (void)setRenderPipelineState:(id)state atIndex:(NSUInteger)index { (void)state;(void)index;tegu_api_unsupported("argument_buffer_pipeline_state"); }
- (void)setRenderPipelineStates:(id const *)states withRange:(TeguRange)range { (void)states;(void)range;tegu_api_unsupported("argument_buffer_pipeline_state"); }
- (void)setComputePipelineState:(id)state atIndex:(NSUInteger)index { (void)state;(void)index;tegu_api_unsupported("argument_buffer_pipeline_state"); }
- (void)setComputePipelineStates:(id const *)states withRange:(TeguRange)range { (void)states;(void)range;tegu_api_unsupported("argument_buffer_pipeline_state"); }
- (void)setIndirectCommandBuffer:(id)value atIndex:(NSUInteger)index { (void)value;(void)index;tegu_api_unsupported("argument_buffer_icb"); }
- (void)setIndirectCommandBuffers:(id const *)buffers withRange:(TeguRange)range { (void)buffers;(void)range;tegu_api_unsupported("argument_buffer_icb"); }
- (void)setAccelerationStructure:(id)structure atIndex:(NSUInteger)index { (void)structure;(void)index;tegu_api_unsupported("argument_buffer_acceleration_structure"); }
- (void)setVisibleFunctionTable:(id)table atIndex:(NSUInteger)index { (void)table;(void)index;tegu_api_unsupported("argument_buffer_function_table"); }
- (void)setVisibleFunctionTables:(id const *)tables withRange:(TeguRange)range { (void)tables;(void)range;tegu_api_unsupported("argument_buffer_function_table"); }
- (void)setIntersectionFunctionTable:(id)table atIndex:(NSUInteger)index { (void)table;(void)index;tegu_api_unsupported("argument_buffer_function_table"); }
- (void)setIntersectionFunctionTables:(id const *)tables withRange:(TeguRange)range { (void)tables;(void)range;tegu_api_unsupported("argument_buffer_function_table"); }
- (void)dealloc { [buffer release];[label release];free(members);[owner release];[super dealloc]; }
@end

/* NSArray<MTLArgumentDescriptor *> -> encoder.  nil on any unsupported or inconsistent descriptor. */
static id tegu_make_argument_encoder(TeguComputeDevice *device,id arguments) {
 NSUInteger count=arguments?[arguments count]:0;
 if(!count||count>TEGU_ARG_MAX_INDEX){tegu_api_unsupported("argument_encoder");return 0;}
 struct TeguArgMember *members=calloc(TEGU_ARG_MAX_INDEX,sizeof(*members));
 if(!members)return 0;
 NSUInteger limit=0;
 for(NSUInteger i=0;i<count;i++) {
  id d=[arguments objectAtIndex:i];
  NSUInteger type=d?[d dataType]:0,index=d?[d index]:~(NSUInteger)0,length=d?[d arrayLength]:0;
  NSUInteger block=d?[d constantBlockAlignment]:0;
  if(!length)length=1;
  struct TeguArgInfo info=tegu_arg_type_info(type);
  if(!info.size||index>=TEGU_ARG_MAX_INDEX||length>4096||members[index].used||(block&(block-1))||block>65536){
   free(members);tegu_api_unsupported("argument_descriptor");return 0;
  }
  uint32_t align=info.align;
  if(info.kind==TEGU_ARG_CONSTANT&&block>align)align=(uint32_t)block;
  /* constantBlockAlignment moves the start of the block; elements keep the type's natural stride */
  members[index]=(struct TeguArgMember){1,info.kind,0,info.size,(info.size+info.align-1)/info.align*info.align,(uint32_t)length,align};
  if(index>=limit)limit=index+1;
 }
 NSUInteger cursor=0,maxAlign=4;
 for(NSUInteger i=0;i<limit;i++) {
  struct TeguArgMember *m=&members[i];
  if(!m->used)continue;
  cursor=(cursor+m->align-1)/m->align*m->align;
  if(m->align>maxAlign)maxAlign=m->align;
  m->offset=(uint32_t)cursor;
  cursor+=(NSUInteger)m->stride*m->count;
  if(cursor>(1u<<24)){free(members);tegu_api_unsupported("argument_descriptor");return 0;}
 }
 TeguArgumentEncoder *e=[[TeguArgumentEncoder alloc] init];
 if(!e){free(members);return 0;}
 e->owner=[device retain];e->members=members;e->memberLimit=limit;e->alignment=maxAlign;
 e->encodedLength=(cursor+maxAlign-1)/maxAlign*maxAlign;
 return e;
}
