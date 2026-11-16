/* Ordered transfers for the currently supported CPU-visible shared buffers.
 * No shader emulation and no GPU-copy claim. Jobs retain their backing buffers. */
#ifndef TEGU_RANGE_DEFINED
#define TEGU_RANGE_DEFINED 1
typedef struct {NSUInteger location,length;} TeguRange;
#endif
@interface TeguBlitEncoder : NSObject {
 TeguCommand *command;id debugLabel;unsigned ended;
}
- (id)initWithCommand:(TeguCommand *)value;
- (void)copyFromBuffer:(TeguBuffer *)source sourceOffset:(NSUInteger)sourceOffset
 toBuffer:(TeguBuffer *)destination destinationOffset:(NSUInteger)destinationOffset size:(NSUInteger)size;
- (void)fillBuffer:(TeguBuffer *)buffer range:(TeguRange)range value:(unsigned char)value;
- (void)copyFromBuffer:(TeguBuffer *)source sourceOffset:(NSUInteger)sourceOffset
 sourceBytesPerRow:(NSUInteger)sourceBytesPerRow sourceBytesPerImage:(NSUInteger)sourceBytesPerImage
 sourceSize:(MTLSize)size toTexture:(TeguTexture *)destination destinationSlice:(NSUInteger)destinationSlice
 destinationLevel:(NSUInteger)destinationLevel destinationOrigin:(TeguOrigin)destinationOrigin;
- (void)copyFromBuffer:(TeguBuffer *)source sourceOffset:(NSUInteger)sourceOffset
 sourceBytesPerRow:(NSUInteger)sourceBytesPerRow sourceBytesPerImage:(NSUInteger)sourceBytesPerImage
 sourceSize:(MTLSize)size toTexture:(TeguTexture *)destination destinationSlice:(NSUInteger)destinationSlice
 destinationLevel:(NSUInteger)destinationLevel destinationOrigin:(TeguOrigin)destinationOrigin options:(NSUInteger)options;
- (void)copyFromTexture:(TeguTexture *)source sourceSlice:(NSUInteger)sourceSlice sourceLevel:(NSUInteger)sourceLevel
 sourceOrigin:(TeguOrigin)sourceOrigin sourceSize:(MTLSize)size toBuffer:(TeguBuffer *)destination
 destinationOffset:(NSUInteger)destinationOffset destinationBytesPerRow:(NSUInteger)destinationBytesPerRow
 destinationBytesPerImage:(NSUInteger)destinationBytesPerImage;
- (void)copyFromTexture:(TeguTexture *)source sourceSlice:(NSUInteger)sourceSlice sourceLevel:(NSUInteger)sourceLevel
 sourceOrigin:(TeguOrigin)sourceOrigin sourceSize:(MTLSize)size toBuffer:(TeguBuffer *)destination
 destinationOffset:(NSUInteger)destinationOffset destinationBytesPerRow:(NSUInteger)destinationBytesPerRow
 destinationBytesPerImage:(NSUInteger)destinationBytesPerImage options:(NSUInteger)options;
- (void)synchronizeTexture:(TeguTexture *)texture slice:(NSUInteger)slice level:(NSUInteger)level;
- (void)optimizeContentsForGPUAccess:(id)resource;
- (void)optimizeContentsForGPUAccess:(id)resource slice:(NSUInteger)slice level:(NSUInteger)level;
- (void)updateFence:(id)fence;
- (void)waitForFence:(id)fence;
#ifdef TEGU_GRAPHICS_BRIDGE
- (void)generateMipmapsForTexture:(TeguTexture *)texture;
- (void)copyFromTexture:(TeguTexture *)source sourceSlice:(NSUInteger)sourceSlice sourceLevel:(NSUInteger)sourceLevel
 sourceOrigin:(TeguOrigin)sourceOrigin sourceSize:(MTLSize)size toTexture:(TeguTexture *)destination
 destinationSlice:(NSUInteger)destinationSlice destinationLevel:(NSUInteger)destinationLevel destinationOrigin:(TeguOrigin)destinationOrigin;
- (void)copyFromTexture:(TeguTexture *)source sourceSlice:(NSUInteger)sourceSlice sourceLevel:(NSUInteger)sourceLevel
 sourceOrigin:(TeguOrigin)sourceOrigin sourceSize:(MTLSize)size toTexture:(TeguTexture *)destination
 destinationSlice:(NSUInteger)destinationSlice destinationLevel:(NSUInteger)destinationLevel
 destinationOrigin:(TeguOrigin)destinationOrigin options:(NSUInteger)options;
#endif
- (void)endEncoding;
@end
static int tegu_blit_range(TeguCommand *c,TeguBuffer *b,NSUInteger offset,NSUInteger size) {
 return b&&b->owner==c->queue->owner&&offset<=b->logical&&size<=b->logical-offset;
}
struct tegu_blit_texture_region {
 TeguBuffer *buffer;NSUInteger base,rowStride,imageStride,rowBytes,depth;
};
/* Validate one CPU-visible texture region and resolve its exact backing span.
 * The walk never relies on Metal's optional zero row/image strides. */
static int tegu_blit_texture_region(TeguTexture *t,NSUInteger slice,NSUInteger level,
 TeguOrigin origin,MTLSize size,struct tegu_blit_texture_region *out) {
 if(!t||!out||!t->buffer||t->surfaceImported||t->textureSamples!=1||
    [t storageMode]==3||level>=t->levelCount)return 0;
 NSUInteger bpp=tegu_texture_bytes_per_pixel(t->format);
 if(!bpp||!size.width||!size.height||size.width>(~(NSUInteger)0)/bpp)return 0;
 struct tegu_texture_level m=t->mip[level];
 if(origin.x>m.width||origin.y>m.height||size.width>m.width-origin.x||
    size.height>m.height-origin.y||!m.stride)return 0;
 NSUInteger layerBase=0,levelDepth=0;
 if(!tegu_texture_cpu_layer(t,level,slice,&layerBase,&levelDepth))return 0;
 if(t->textureKind==7) {
  if(origin.z>levelDepth||size.depth>levelDepth-origin.z)return 0;
 } else if(origin.z||size.depth!=1)return 0;
 NSUInteger imageStride=t->textureKind==7?m.stride*m.height:0;
 if(t->textureKind==7&&!imageStride&&size.depth>1)return 0;
 NSUInteger rowBytes=size.width*bpp;
 NSUInteger start=layerBase+origin.z*imageStride+origin.y*m.stride+origin.x*bpp;
 if(start>t->buffer->logical)return 0;
 NSUInteger tail=0;
 if(size.height>1) {if(m.stride>(~(NSUInteger)0)/(size.height-1))return 0;tail+=(size.height-1)*m.stride;}
 if(size.depth>1) {if(imageStride>(~(NSUInteger)0)/(size.depth-1))return 0;tail+=(size.depth-1)*imageStride;}
 if(tail>~(NSUInteger)0-rowBytes||start>~(NSUInteger)0-tail-rowBytes||
    start+tail+rowBytes>t->buffer->logical)return 0;
 if(out){out->buffer=t->buffer;out->base=start;out->rowStride=m.stride;out->imageStride=imageStride;
  out->rowBytes=rowBytes;out->depth=size.depth;}
 return 1;
}
static int tegu_blit_texture_compatible(TeguTexture *source,TeguTexture *destination) {
 if(!source||!destination)return 0;
 NSUInteger sb=tegu_texture_bytes_per_pixel(source->format),db=tegu_texture_bytes_per_pixel(destination->format);
 if(!sb||sb!=db)return 0;
 if(source->format==destination->format)return 1;
 struct n2t_format_info allocation,view;
 return n2t_view_format_check((uint32_t)source->format,(uint32_t)destination->format,
  &allocation,&view)==N2T_OK;
}
static int tegu_blit_buffer_span(TeguBuffer *b,NSUInteger offset,NSUInteger rowStride,
 NSUInteger imageStride,NSUInteger rowBytes,NSUInteger rows,NSUInteger depth) {
 if(!b||!rows||!depth||!rowBytes||offset>b->logical)return 0;
 NSUInteger tail=rowBytes;
 if(rows>1) {if(rowStride>(~(NSUInteger)0)/(rows-1))return 0;tail+=(rows-1)*rowStride;}
 if(depth>1) {if(imageStride>(~(NSUInteger)0)/(depth-1))return 0;tail+=(depth-1)*imageStride;}
 return tail<=b->logical-offset&&offset<=b->logical;
}
static int tegu_blit_enqueue_rows(TeguCommand *c,TeguBuffer *source,NSUInteger sourceOffset,
 NSUInteger sourceRowStride,NSUInteger sourceImageStride,TeguBuffer *destination,NSUInteger destinationOffset,
 NSUInteger destinationRowStride,NSUInteger destinationImageStride,NSUInteger rowBytes,
 NSUInteger rows,NSUInteger depth,NSUInteger bpp) {
 if(!source||!destination||!rows||!depth||!rowBytes||!bpp||
    depth>TEGU_COMMAND_MAX_JOBS-c->count)return 0;
 NSUInteger sourceSpan=rowBytes,destinationSpan=rowBytes;
 if(rows>1) {
  if(sourceRowStride>(~(NSUInteger)0)/(rows-1)||destinationRowStride>(~(NSUInteger)0)/(rows-1))return 0;
  sourceSpan+=(rows-1)*sourceRowStride;destinationSpan+=(rows-1)*destinationRowStride;
 }
 if(depth>1) {
  if(sourceImageStride>(~(NSUInteger)0)/(depth-1)||destinationImageStride>(~(NSUInteger)0)/(depth-1))return 0;
  sourceSpan+=(depth-1)*sourceImageStride;destinationSpan+=(depth-1)*destinationImageStride;
 }
 if(source==destination&&sourceOffset<destinationOffset+destinationSpan&&
    destinationOffset<sourceOffset+sourceSpan)return 0;
 for(NSUInteger z=0;z<depth;z++) {
  struct Dispatch *job=tegu_command_job(c,c->count++);
  job->blit=3;job->buffers[0]=[source retain];job->buffers[1]=[destination retain];
  job->sourceOffset=sourceOffset+z*sourceImageStride;
  job->destinationOffset=destinationOffset+z*destinationImageStride;
  job->sourceStride=sourceRowStride;job->destinationStride=destinationRowStride;
  job->byteCount=rowBytes;job->rows=rows;job->bytesPerPixel=(unsigned)bpp;
 }
 return 1;
}
@implementation TeguBlitEncoder
- (id)initWithCommand:(TeguCommand *)value {
 self=[super init];if(self)command=[value retain];return self;
}
- (id)device{return command->queue->owner;}
- (id)label{return debugLabel;}
- (void)setLabel:(id)value{id copy=[value copy];[debugLabel release];debugLabel=copy;}
- (void)copyFromBuffer:(TeguBuffer *)source sourceOffset:(NSUInteger)so
 toBuffer:(TeguBuffer *)destination destinationOffset:(NSUInteger)offset size:(NSUInteger)size {
 if(ended||[command teguCommitted]||!tegu_blit_range(command,source,so,size)||
    !tegu_blit_range(command,destination,offset,size)||
    (source==destination&&size&&so<offset+size&&offset<so+size)) {
  TEGU_INVALID(command);return;
 }
 if(!size)return;
 if(command->count>=TEGU_COMMAND_MAX_JOBS){tegu_command_capacity_marker("blit-copy",command->count);TEGU_INVALID(command);return;}
 struct Dispatch *job=tegu_command_job(command,command->count++);
 job->blit=1;job->buffers[0]=[source retain];job->buffers[1]=[destination retain];
 job->sourceOffset=so;job->destinationOffset=offset;job->byteCount=size;
}
- (void)fillBuffer:(TeguBuffer *)buffer range:(TeguRange)range value:(unsigned char)value {
 if(ended||[command teguCommitted]||!range.length||!tegu_blit_range(command,buffer,range.location,range.length)) {
  TEGU_INVALID(command);return;
 }
 if(command->count>=TEGU_COMMAND_MAX_JOBS){tegu_command_capacity_marker("blit-fill",command->count);TEGU_INVALID(command);return;}
 struct Dispatch *job=tegu_command_job(command,command->count++);
 job->blit=2;job->buffers[1]=[buffer retain];job->destinationOffset=range.location;
 job->byteCount=range.length;job->fillValue=value;
}
#ifdef TEGU_GRAPHICS_BRIDGE
- (void)copyFromBuffer:(TeguBuffer *)source sourceOffset:(NSUInteger)so
 sourceBytesPerRow:(NSUInteger)srow sourceBytesPerImage:(NSUInteger)simage sourceSize:(MTLSize)size
 toTexture:(TeguTexture *)destination destinationSlice:(NSUInteger)ds destinationLevel:(NSUInteger)dl
 destinationOrigin:(TeguOrigin)origin options:(NSUInteger)options {
 if(options||ended||[command teguCommitted]||!source||!destination||
    source->owner!=command->queue->owner||[destination device]!=command->queue->owner||
    (simage&&simage<srow)||!size.width||!size.height||!size.depth) {
  TEGU_INVALID(command);return;
 }
 struct tegu_blit_texture_region d={0};
 NSUInteger bpp=tegu_texture_bytes_per_pixel(destination->format);
 if(!bpp||!tegu_blit_texture_region(destination,ds,dl,origin,size,&d)||
    srow<size.width*bpp||(size.depth>1&&(!simage||simage<srow*size.height))||
    !tegu_blit_buffer_span(source,so,srow,simage,size.width*bpp,size.height,size.depth)) {
  TEGU_INVALID(command);return;
 }
 if(!tegu_blit_enqueue_rows(command,source,so,srow,simage,d.buffer,d.base,d.rowStride,d.imageStride,
    size.width*bpp,size.height,size.depth,bpp))TEGU_INVALID(command);
}
- (void)copyFromBuffer:(TeguBuffer *)source sourceOffset:(NSUInteger)so
 sourceBytesPerRow:(NSUInteger)srow sourceBytesPerImage:(NSUInteger)simage sourceSize:(MTLSize)size
 toTexture:(TeguTexture *)destination destinationSlice:(NSUInteger)ds destinationLevel:(NSUInteger)dl
 destinationOrigin:(TeguOrigin)origin {
 [self copyFromBuffer:source sourceOffset:so sourceBytesPerRow:srow sourceBytesPerImage:simage
  sourceSize:size toTexture:destination destinationSlice:ds destinationLevel:dl destinationOrigin:origin options:0];
}
- (void)copyFromTexture:(TeguTexture *)source sourceSlice:(NSUInteger)ss sourceLevel:(NSUInteger)sl
 sourceOrigin:(TeguOrigin)so sourceSize:(MTLSize)size toBuffer:(TeguBuffer *)destination
 destinationOffset:(NSUInteger)offset destinationBytesPerRow:(NSUInteger)drow
 destinationBytesPerImage:(NSUInteger)dimage options:(NSUInteger)options {
 if(options||ended||[command teguCommitted]||!source||!destination||
    [source device]!=command->queue->owner||destination->owner!=command->queue->owner||
    !size.width||!size.height||!size.depth) {
  TEGU_INVALID(command);return;
 }
 struct tegu_blit_texture_region s={0};NSUInteger bpp=tegu_texture_bytes_per_pixel(source->format);
 if(!bpp||!tegu_blit_texture_region(source,ss,sl,so,size,&s)||
    drow<size.width*bpp||(size.depth>1&&(!dimage||dimage<drow*size.height))||
    !tegu_blit_buffer_span(destination,offset,drow,dimage,size.width*bpp,size.height,size.depth)) {
  TEGU_INVALID(command);return;
 }
 if(!tegu_blit_enqueue_rows(command,s.buffer,s.base,s.rowStride,s.imageStride,destination,offset,drow,dimage,
    size.width*bpp,size.height,size.depth,bpp))TEGU_INVALID(command);
}
- (void)copyFromTexture:(TeguTexture *)source sourceSlice:(NSUInteger)ss sourceLevel:(NSUInteger)sl
 sourceOrigin:(TeguOrigin)so sourceSize:(MTLSize)size toBuffer:(TeguBuffer *)destination
 destinationOffset:(NSUInteger)offset destinationBytesPerRow:(NSUInteger)drow
 destinationBytesPerImage:(NSUInteger)dimage {
 [self copyFromTexture:source sourceSlice:ss sourceLevel:sl sourceOrigin:so sourceSize:size
  toBuffer:destination destinationOffset:offset destinationBytesPerRow:drow
  destinationBytesPerImage:dimage options:0];
}
- (void)synchronizeTexture:(TeguTexture *)texture slice:(NSUInteger)slice level:(NSUInteger)level {
 if(ended||[command teguCommitted]||!texture||texture->surfaceImported||!tegu_owned_class(texture,"TeguTexture")||
    [texture device]!=command->queue->owner||level>=texture->levelCount) {
  TEGU_INVALID(command);return;
 }
 NSUInteger base=0,depth=0;
 if(!tegu_texture_cpu_layer(texture,level,slice,&base,&depth))TEGU_INVALID(command);
}
- (void)optimizeContentsForGPUAccess:(id)resource {
 if(ended||[command teguCommitted]||!resource||
    !(tegu_owned_class(resource,"TeguBuffer")||tegu_owned_class(resource,"TeguTexture")))TEGU_INVALID(command);
}
- (void)optimizeContentsForGPUAccess:(id)resource slice:(NSUInteger)slice level:(NSUInteger)level {
 if(ended||[command teguCommitted]||!resource||!tegu_owned_class(resource,"TeguTexture")) {
  TEGU_INVALID(command);return;
 }
 TeguTexture *texture=(TeguTexture *)resource;
 if([texture device]!=command->queue->owner||level>=texture->levelCount) {
  TEGU_INVALID(command);return;
 }
 NSUInteger base=0,depth=0;
 if(!tegu_texture_cpu_layer(texture,level,slice,&base,&depth))TEGU_INVALID(command);
}
- (void)updateFence:(id)fence {
 if(ended||[command teguCommitted]||!fence)TEGU_INVALID(command);
}
- (void)waitForFence:(id)fence {
 if(ended||[command teguCommitted]||!fence)TEGU_INVALID(command);
}
- (void)copyFromTexture:(TeguTexture *)source sourceSlice:(NSUInteger)ss sourceLevel:(NSUInteger)sl
 sourceOrigin:(TeguOrigin)so sourceSize:(MTLSize)size toTexture:(TeguTexture *)destination
 destinationSlice:(NSUInteger)ds destinationLevel:(NSUInteger)dl destinationOrigin:(TeguOrigin)origin
 options:(NSUInteger)options {
 if(options||ended||[command teguCommitted]||!source||!destination||
    [source device]!=command->queue->owner||[destination device]!=command->queue->owner||
    !size.width||!size.height||!size.depth) {
  TEGU_INVALID(command);return;
 }
 struct tegu_blit_texture_region s={0},d={0};
 NSUInteger sbpp=tegu_texture_bytes_per_pixel(source->format),dbpp=tegu_texture_bytes_per_pixel(destination->format);
 if(!tegu_blit_texture_compatible(source,destination)||!sbpp||sbpp!=dbpp||
    !tegu_blit_texture_region(source,ss,sl,so,size,&s)||
    !tegu_blit_texture_region(destination,ds,dl,origin,size,&d)||
    !tegu_blit_enqueue_rows(command,s.buffer,s.base,s.rowStride,s.imageStride,d.buffer,d.base,d.rowStride,
     d.imageStride,size.width*sbpp,size.height,size.depth,sbpp))TEGU_INVALID(command);
}
- (void)copyFromTexture:(TeguTexture *)source sourceSlice:(NSUInteger)ss sourceLevel:(NSUInteger)sl
 sourceOrigin:(TeguOrigin)so sourceSize:(MTLSize)size toTexture:(TeguTexture *)destination
 destinationSlice:(NSUInteger)ds destinationLevel:(NSUInteger)dl destinationOrigin:(TeguOrigin)origin {
 [self copyFromTexture:source sourceSlice:ss sourceLevel:sl sourceOrigin:so sourceSize:size
  toTexture:destination destinationSlice:ds destinationLevel:dl destinationOrigin:origin options:0];
}
- (void)generateMipmapsForTexture:(TeguTexture *)texture {
 NSUInteger bytesPerPixel=texture?tegu_texture_bytes_per_pixel(texture->format):0;
 int mipFormat=texture&&(texture->format==10||texture->format==43||texture->format==70||
   texture->format==80||texture->format==115);
 if(ended||[command teguCommitted]||!texture||texture->surfaceImported||!texture->buffer||texture->textureSamples!=1||
    [texture storageMode]==3||[texture device]!=command->queue->owner||!mipFormat||!bytesPerPixel||
    texture->levelCount<1||texture->levelCount>13||
    texture->levelCount-1>TEGU_COMMAND_MAX_JOBS-command->count){
  if(texture&&texture->levelCount>1&&texture->levelCount-1>TEGU_COMMAND_MAX_JOBS-command->count)
   tegu_command_capacity_marker("blit-mipmap",command->count);
  /* Level 0 is intact, so a mipmap chain this driver cannot build (no job
   * capacity left, an unsupported format) costs only minified detail; it
   * must not fail the command and the frame drawn with it (R178: a full
   * 64-job backboardd command). Misuse still invalidates. */
  if(ended||[command teguCommitted]||!texture||[texture device]!=command->queue->owner)TEGU_INVALID(command);
  return;
 }
 for(NSUInteger level=1;level<texture->levelCount;level++) {
  struct tegu_texture_level sm=texture->mip[level-1],dm=texture->mip[level];
  NSUInteger sourceOffset=0,destinationOffset=0;
  if(!tegu_texture_level_rows(texture,level-1,0,0,sm.width,sm.height,&sourceOffset)||
     !tegu_texture_level_rows(texture,level,0,0,dm.width,dm.height,&destinationOffset)) {
   TEGU_INVALID(command);return;
  }
  struct Dispatch *job=tegu_command_job(command,command->count++);
  job->blit=4;job->buffers[0]=[texture->buffer retain];job->buffers[1]=[texture->buffer retain];
  job->sourceOffset=sourceOffset;job->destinationOffset=destinationOffset;
  job->sourceStride=sm.stride;job->destinationStride=dm.stride;
  job->sourceRows=sm.height;job->rows=dm.height;job->byteCount=dm.width*bytesPerPixel;
  job->sourceByteCount=sm.width*bytesPerPixel;job->bytesPerPixel=(unsigned)bytesPerPixel;
  job->mipComponentBytes=(texture->format==43||texture->format==115)?2:1;
 }
}
#endif
- (void)endEncoding {
 if(ended||[command teguCommitted]){TEGU_INVALID(command);return;}
 ended=1;command->encoding=0;
}
- (void)dealloc {
 if(!ended)TEGU_INVALID(command);
 [debugLabel release];[command release];[super dealloc];
}
@end
@interface TeguCommand (BlitCommands)
- (id)blitCommandEncoder;
@end
@implementation TeguCommand (BlitCommands)
- (id)blitCommandEncoder {
 if([self teguCommitted]||encoding){TEGU_INVALID(self);return 0;}
 TeguBlitEncoder *encoder=[[TeguBlitEncoder alloc] initWithCommand:self];
 if(!encoder){TEGU_INVALID(self);return 0;}
 encoding=1;return [encoder autorelease];
}
@end

#include "ws_selector_compat.inc"
