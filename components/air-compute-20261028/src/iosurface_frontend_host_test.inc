/* Mock IOSurface ABI around the real copied texture frontend. */
@interface IOSurfaceFixture : NSObject {@public uint32_t sid,fourcc,planes;NSUInteger w,h,row,alloc;unsigned char *bytes;}
- (unsigned)testReferences;
@end
@implementation IOSurfaceFixture
- (unsigned)testReferences{return _references;}
- (void)dealloc{free(bytes);[super dealloc];}
@end
@interface TeguComputeDevice (IOSurfaceHostRefCheck)
- (unsigned)testReferences;
@end
@implementation TeguComputeDevice (IOSurfaceHostRefCheck)
- (unsigned)testReferences{return _references;}
@end
static unsigned mockUnlockCalls,mockUnlockFailures;static int failMockUnlock;
static uint32_t mock_surface_id(void *p){return ((IOSurfaceFixture *)p)->sid;}
static size_t mock_surface_width(void *p){return ((IOSurfaceFixture *)p)->w;}
static size_t mock_surface_height(void *p){return ((IOSurfaceFixture *)p)->h;}
static size_t mock_surface_row(void *p){return ((IOSurfaceFixture *)p)->row;}
static size_t mock_surface_alloc(void *p){return ((IOSurfaceFixture *)p)->alloc;}
static uint32_t mock_surface_format(void *p){return ((IOSurfaceFixture *)p)->fourcc;}
static size_t mock_surface_planes(void *p){return ((IOSurfaceFixture *)p)->planes;}
static int mock_surface_lock(void *p,uint32_t options,uint32_t *seed){(void)p;(void)options;if(seed)*seed=1;return 0;}
static int mock_surface_unlock(void *p,uint32_t options,uint32_t *seed){(void)p;(void)options;(void)seed;mockUnlockCalls++;if(failMockUnlock){mockUnlockFailures++;return -1;}return 0;}
static void *mock_surface_base(void *p){return ((IOSurfaceFixture *)p)->bytes;}
static void *mock_cf_retain(void *p){return [(id)p retain];}
static void mock_cf_release(void *p){[(id)p release];}
void *tegu_test_iosurface_dlopen(const char *path,int mode){(void)mode;return strstr(path,"IOSurface.framework")||strstr(path,"CoreFoundation.framework")?(void *)1:0;}
void *tegu_test_iosurface_dlsym(void *handle,const char *name){assert(handle==(void *)1);
#define MAP(n,f) if(!strcmp(name,n))return (void *)(uintptr_t)&f
 MAP("IOSurfaceGetID",mock_surface_id);MAP("IOSurfaceGetWidth",mock_surface_width);MAP("IOSurfaceGetHeight",mock_surface_height);
 MAP("IOSurfaceGetBytesPerRow",mock_surface_row);MAP("IOSurfaceGetAllocSize",mock_surface_alloc);
 MAP("IOSurfaceGetPixelFormat",mock_surface_format);MAP("IOSurfaceGetPlaneCount",mock_surface_planes);
 MAP("IOSurfaceLock",mock_surface_lock);MAP("IOSurfaceUnlock",mock_surface_unlock);MAP("IOSurfaceGetBaseAddress",mock_surface_base);
 MAP("CFRetain",mock_cf_retain);MAP("CFRelease",mock_cf_release);
#undef MAP
 return 0;
}
static void *iosurface_api_thread(void *unused){(void)unused;return (void *)tegu_iosurface_api();}
static void test_iosurface_api_concurrent_once(void){pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],0,iosurface_api_thread,0));
 void *first=0;for(unsigned i=0;i<8;i++){void *p=0;assert(!pthread_join(threads[i],&p)&&p);if(!first)first=p;else assert(first==p);}}
static IOSurfaceFixture *new_surface_fixture(uint32_t id,uint32_t format,uint32_t planes,
 NSUInteger width,NSUInteger height,NSUInteger row,NSUInteger alloc) {
 IOSurfaceFixture *s=[[IOSurfaceFixture alloc] init];s->sid=id;s->fourcc=format;s->planes=planes;
 s->w=width;s->h=height;s->row=row;s->alloc=alloc;s->bytes=calloc(alloc,1);assert(s->bytes);return s;
}
static TeguTexture *import_surface_texture(TeguComputeDevice *d,TextureDesc *desc,id surface,NSUInteger plane) {
 return [(id)d newTextureWithDescriptor:(id)desc iosurface:surface plane:plane];
}
static void test_iosurface_texture_frontend(TeguComputeDevice *device,TeguRenderPipeline *pipeline) {
 test_iosurface_api_concurrent_once();
 NSUInteger baseline=[device currentAllocatedSize];unsigned importedBase=iosurfaceImports,releasedBase=iosurfaceReleases;
 unsigned deviceRefBaseline=[device testReferences];
 TextureDesc *desc=[[TextureDesc alloc] init];desc->format=70;desc->w=desc->h=16;desc->uses=4;
 IOSurfaceFixture *surface=new_surface_fixture(501,TEGU_IOSURFACE_RGBA,0,16,16,64,16384);
 for(unsigned y=0;y<16;y++)for(unsigned x=0;x<64;x++)surface->bytes[y*64+x]=(unsigned char)(y*7+x);
 U64 savedCaps=device->capabilities;device->capabilities=511;
 assert(!import_surface_texture(device,desc,surface,0)&&iosurfaceImports==importedBase);
 device->capabilities=511|512;
 iosurfaceReplyMode=1;assert(!import_surface_texture(device,desc,surface,0)&&iosurfaceReleases==releasedBase+1);
 iosurfaceReplyMode=2;assert(!import_surface_texture(device,desc,surface,0)&&iosurfaceReleases==releasedBase+1);
 iosurfaceReplyMode=3;assert(!import_surface_texture(device,desc,surface,0)&&iosurfaceReleases==releasedBase+2);
 iosurfaceReplyMode=0;assert([surface testReferences]==1);
 TeguTexture *texture=import_surface_texture(device,desc,surface,0);
 assert(texture&&texture->surfaceImported&&!texture->buffer&&texture->importedHandle==0xa0000001u);
 assert([device testReferences]==deviceRefBaseline+1&&[surface testReferences]==2);
 assert([texture device]==device&&[texture iosurface]==surface&&[texture iosurfacePlane]==0);
 assert(iosurfaceImports==importedBase+4&&iosurfaceReleases==releasedBase+2);
 /* Direct IOSurface-backed read/write honor the surface row stride; no TeguBuffer is allocated. */
 unsigned char readback[4*4*4]={};TeguRegion region={{3,2,0},{4,4,1}};
 [texture getBytes:readback bytesPerRow:16 fromRegion:region mipmapLevel:0];
 for(unsigned y=0;y<4;y++)for(unsigned x=0;x<16;x++)assert(readback[y*16+x]==surface->bytes[(y+2)*64+12+x]);
 unsigned char patch[8]={9,8,7,6,5,4,3,2};TeguRegion writeRegion={{5,7,0},{2,1,1}};
 [texture replaceRegion:writeRegion mipmapLevel:0 withBytes:patch bytesPerRow:8];
 assert(!memcmp(surface->bytes+7*64+20,patch,8));
 unsigned unlockBaseline=mockUnlockCalls,unlockFailureBaseline=mockUnlockFailures;failMockUnlock=1;
 [texture getBytes:readback bytesPerRow:16 fromRegion:region mipmapLevel:0];
 [texture replaceRegion:writeRegion mipmapLevel:0 withBytes:patch bytesPerRow:8];failMockUnlock=0;
 assert(mockUnlockCalls==unlockBaseline+2&&mockUnlockFailures==unlockFailureBaseline+2);
 memset(readback,0xa5,sizeof(readback));surface->row=80;
 [texture getBytes:readback bytesPerRow:16 fromRegion:region mipmapLevel:0];
 for(unsigned i=0;i<sizeof(readback);i++)assert(readback[i]==0xa5);
 surface->row=64;surface->alloc=4095;
 [texture replaceRegion:writeRegion mipmapLevel:0 withBytes:patch bytesPerRow:8];
 surface->alloc=16384;surface->fourcc=TEGU_IOSURFACE_BGRA;
 [texture getBytes:readback bytesPerRow:16 fromRegion:region mipmapLevel:0];surface->fourcc=TEGU_IOSURFACE_RGBA;
 /* Descriptor mismatch, FOURCC mismatch, planes, and nonzero plane are refused. */
 assert(!import_surface_texture(device,desc,surface,1));
 IOSurfaceFixture *bgra=new_surface_fixture(502,TEGU_IOSURFACE_BGRA,0,16,16,64,16384);
 assert(!import_surface_texture(device,desc,bgra,0));
 IOSurfaceFixture *metalEnum=new_surface_fixture(503,70,0,16,16,64,16384);
 assert(!import_surface_texture(device,desc,metalEnum,0));
 IOSurfaceFixture *planar=new_surface_fixture(504,TEGU_IOSURFACE_RGBA,1,16,16,64,16384);
 assert(!import_surface_texture(device,desc,planar,0));
 IOSurfaceFixture *badStride=new_surface_fixture(505,TEGU_IOSURFACE_RGBA,0,16,16,80,16384);
 assert(!import_surface_texture(device,desc,badStride,0));
 surface->alloc=2048;assert(!import_surface_texture(device,desc,surface,0));
 surface->alloc=16385;assert(!import_surface_texture(device,desc,surface,0));
 surface->alloc=(256u<<20)+4096;assert(!import_surface_texture(device,desc,surface,0));surface->alloc=16384;
 TextureDesc *badDesc=[[TextureDesc alloc] init];badDesc->format=70;badDesc->w=32;badDesc->h=16;badDesc->uses=4;
 assert(!import_surface_texture(device,badDesc,surface,0));badDesc->w=16;badDesc->uses=1;
 assert(!import_surface_texture(device,badDesc,surface,0));badDesc->uses=4;badDesc->options=1;
 assert(!import_surface_texture(device,badDesc,surface,0));badDesc->options=0;badDesc->levels=2;
 assert(!import_surface_texture(device,badDesc,surface,0));[badDesc release];
 /* Imported targets use selector 10 with the tagged surface handle. The retained
  * command snapshot keeps both the IOSurface and import alive through commit. */
 RenderPass *pass=[[RenderPass alloc] init];pass->color=[[PassColor alloc] init];
 pass->color->target=[texture retain];pass->color->load=2;pass->color->store=1;
 TeguQueue *queue=[device newCommandQueue];TeguCommand *command=[[queue commandBuffer] retain];
 TeguTexture *regularTexture=[device newTextureWithDescriptor:(id)desc];assert(regularTexture&&regularTexture->buffer);
 TeguCommand *blitCommand=[[queue commandBuffer] retain];TeguBlitEncoder *blitter=[blitCommand blitCommandEncoder];assert(blitter);
 [blitter copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:(TeguOrigin){0,0,0}
  sourceSize:(MTLSize){16,16,1} toTexture:regularTexture destinationSlice:0 destinationLevel:0 destinationOrigin:(TeguOrigin){0,0,0}];
 [blitter copyFromTexture:regularTexture sourceSlice:0 sourceLevel:0 sourceOrigin:(TeguOrigin){0,0,0}
  sourceSize:(MTLSize){16,16,1} toTexture:texture destinationSlice:0 destinationLevel:0 destinationOrigin:(TeguOrigin){0,0,0}];
 [blitter generateMipmapsForTexture:texture];assert(blitCommand->invalid&&blitCommand->count==0);
 [blitter endEncoding];[blitCommand commit];assert([blitCommand status]==5);[blitCommand release];[regularTexture release];drain();
 /* Even a descriptor carrying a sampled-texture plan cannot turn the imported
  * target into a shader resource. It is refused before selector 12. */
 struct tegu_render_texture_plan savedPlan=pipeline->texture;
 pipeline->texture.version=1;pipeline->texture.stage=16;pipeline->texture.normalized_coordinates=1;
 SamplerTestDescriptor *samplerDesc=[[SamplerTestDescriptor alloc] init];samplerDesc->normalized=1;samplerDesc->anisotropy=1;
 TeguSampler *sampler=[device newSamplerStateWithDescriptor:(id)samplerDesc];assert(sampler);[samplerDesc release];
 unsigned textureDrawBaseline=textureDraws;TeguCommand *sampled=[[queue commandBuffer] retain];
 TeguRenderEncoder *sampleEncoder=[sampled renderCommandEncoderWithDescriptor:(id)pass];assert(sampleEncoder);
 [sampleEncoder setRenderPipelineState:pipeline];[sampleEncoder setFragmentTexture:texture atIndex:0];
 [sampleEncoder setFragmentSamplerState:sampler atIndex:0];[sampleEncoder drawPrimitives:3 vertexStart:0 vertexCount:3];
 [sampleEncoder endEncoding];[sampled commit];assert([sampled status]==5&&textureDraws==textureDrawBaseline);
 [sampled release];[sampler release];drain();pipeline->texture=savedPlan;
 TeguRenderEncoder *encoder=[command renderCommandEncoderWithDescriptor:(id)pass];assert(encoder);
 float vertices[24]={-.75f,-.75f,.5f,1,0,0,0,1,.75f,-.75f,.5f,1,1,0,0,1,-.75f,.75f,.5f,1,0,1,0,1};
 [encoder setRenderPipelineState:pipeline];[encoder setVertexBytes:vertices length:sizeof(vertices) atIndex:1];
 [encoder drawPrimitives:3 vertexStart:0 vertexCount:3];[encoder endEncoding];
 assert(TEGU_JOB(command->jobChunks,0).renderArgs[1]==0xa0000001u&&TEGU_JOB(command->jobChunks,0).renderTexture==texture);
 [texture release];assert([surface testReferences]>=2&&iosurfaceReleases==releasedBase+2);
 [pass release];[desc release];[surface release];assert([surface testReferences]==1);
 [command commit];assert([command status]==4&&importedTargetDraws==1&&importedDrawHandle==0xa0000001u);
 [command release];[queue release];drain();assert(iosurfaceReleases==releasedBase+3);
 /* Selector17 is a separate large-BGRA contract. The kernel returns the
  * validated byte offset and backing facts; userspace never invents them. */
 device->capabilities=savedCaps|TEGU_GPU_CAP_COMPOSITOR|TEGU_GPU_CAP_COMPOSITOR_IOSURFACE;
 TextureDesc *largeDesc=[[TextureDesc alloc] init];largeDesc->format=80;largeDesc->w=1080;largeDesc->h=2424;largeDesc->uses=4;
 IOSurfaceFixture *large=new_surface_fixture(511,TEGU_IOSURFACE_BGRA,0,1080,2424,4352,(NSUInteger)compositorIOSurfaceAlloc);
 unsigned importBase=compositorIOSurfaceImports,releaseBase=iosurfaceReleases;
 compositorIOSurfaceReplyMode=1;assert(!import_surface_texture(device,largeDesc,large,0));
 compositorIOSurfaceReplyMode=2;assert(!import_surface_texture(device,largeDesc,large,0));
 compositorIOSurfaceReplyMode=3;assert(!import_surface_texture(device,largeDesc,large,0));
 compositorIOSurfaceReplyMode=4;assert(!import_surface_texture(device,largeDesc,large,0));
 compositorIOSurfaceReplyMode=5;assert(!import_surface_texture(device,largeDesc,large,0));
 compositorIOSurfaceReplyMode=6;assert(!import_surface_texture(device,largeDesc,large,0));
 assert(compositorIOSurfaceImports==importBase+6&&iosurfaceReleases==releaseBase+5);
 compositorIOSurfaceReplyMode=0;
 TeguTexture *largeTarget=import_surface_texture(device,largeDesc,large,0);
 assert(largeTarget&&largeTarget->surfaceImported&&largeTarget->surfaceTargetVersion==1&&
  largeTarget->importedHandle==compositorIOSurfaceHandle&&!largeTarget->buffer&&
  largeTarget->mip[0].offset==compositorIOSurfaceOffset&&largeTarget->surfaceRowBytes==4352&&
  largeTarget->surfaceAllocSize==compositorIOSurfaceAlloc);
 assert([large testReferences]==2);
 /* BackBoard's linear display target is ShaderRead|RenderTarget (usage 5).
  * Preserve that exact public contract while retaining usage 4 support. */
 largeDesc->uses=5;TeguTexture *backboardTarget=import_surface_texture(device,largeDesc,large,0);
 assert(backboardTarget&&backboardTarget->surfaceTargetVersion==1&&
  backboardTarget->textureUsage==5&&backboardTarget->importedHandle==compositorIOSurfaceHandle);
 [backboardTarget release];
 unsigned usageImportCount=compositorIOSurfaceImports;
 for(NSUInteger refusedUsage=0;refusedUsage<=7;refusedUsage++) {
  if(refusedUsage==4||refusedUsage==5)continue;
  largeDesc->uses=refusedUsage;
  assert(!import_surface_texture(device,largeDesc,large,0));
 }
 assert(compositorIOSurfaceImports==usageImportCount);
 largeDesc->uses=4;
 unsigned importedCount=compositorIOSurfaceImports;
 large->planes=1;TeguTexture *singlePlane=import_surface_texture(device,largeDesc,large,0);
 assert(singlePlane&&singlePlane->surfaceTargetVersion==1);[singlePlane release];
 large->planes=2;assert(!import_surface_texture(device,largeDesc,large,0));
 large->planes=0;large->fourcc=TEGU_IOSURFACE_RGBA;assert(!import_surface_texture(device,largeDesc,large,0));
 large->fourcc=TEGU_IOSURFACE_BGRA;large->row=4319;assert(!import_surface_texture(device,largeDesc,large,0));
 large->row=4352;large->alloc=4096;assert(!import_surface_texture(device,largeDesc,large,0));
 assert(compositorIOSurfaceImports==importedCount+1);
 [largeTarget release];assert(iosurfaceReleases==releaseBase+8&&[large testReferences]==1);
 [largeDesc release];[large release];device->capabilities=savedCaps;
 [bgra release];[metalEnum release];[planar release];[badStride release];
 device->capabilities=savedCaps;assert([device currentAllocatedSize]==baseline);
 assert([device testReferences]==deviceRefBaseline);
 puts("PASS selector13/14 legacy IOSurface plus selector17 v1 ownership, malformed reply/plane/size refusals, retained large BGRA target metadata; mocked kernel transport only");
}
