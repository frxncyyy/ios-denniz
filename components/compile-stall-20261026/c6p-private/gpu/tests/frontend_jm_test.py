#!/usr/bin/env python3
"""Real AIR/frontend/JM adapter integration; only raw GPU execution is mocked.
Requires the pinned host compiler libraries and authored varying.bc. No device.
"""
from pathlib import Path
import os, subprocess
root=Path(__file__).resolve().parents[1]
os.chdir(root.parent)
r=root/'metal/tegu';o=root/'build/frontend-jm-test';o.mkdir(exist_ok=True)

s='#define TEGU_AIR_BRIDGE 1\n#define TEGU_MALI_BRIDGE 1\n#define TEGU_GRAPHICS_BRIDGE 1\n#define NORD2_JM_TRANSPORT 1\n'
s+=(r/'frontend_host_test.m').read_text().split('static unsigned allocated,freed')[0]
s+=(r/'runtime_frontend_host_test.m').read_text().split('static id strings[1024]',1)[1].split('static unsigned imports,submissions',1)[0].join(['static id strings[1024]',''])
s+=(r/'render_frontend_host_test.m').read_text().split('@interface RenderColor',1)[1].split('#include "render_encoder_host_test.inc"',1)[0].join(['@interface RenderColor',''])
s+=(r/'render_encoder_host_test.inc').read_text().split('@class IOSurfaceFixture',1)[0]
s+=Path('gpu/tests/nord2_metal_bridge_test.c').read_text().split('static uint64_t pipeline(uintptr_t',1)[0].replace('#define COUNT 300','#define COUNT 2000')
s+=r'''
/* Linux uint64_t is unsigned long; the Darwin frontend ABI uses U64. */
static int scalar_adapter(TeguConnection c,unsigned sel,const U64 *in,unsigned ni,U64 *out,unsigned *no){
 uint64_t a[16]={0},b[16]={0};assert(ni<=16&&(!no||*no<=16));
 if(ni)memcpy(a,in,ni*8);int e=n2mb_scalar(c,sel,ni?a:0,ni,out?b:0,no);
 if(out&&no){assert(*no<=16);memcpy(out,b,*no*8);}return e;
}
static int method_adapter(TeguConnection c,unsigned sel,const U64 *in,unsigned ni,const void *data,NSUInteger bytes,U64 *out,unsigned *no,void *output,NSUInteger *size){
 uint64_t a[16]={0},b[16]={0};assert(ni<=16&&(!no||*no<=16));
 if(ni)memcpy(a,in,ni*8);int e=n2mb_method(c,sel,ni?a:0,ni,data,bytes,out?b:0,no,output,size);
 if(out&&no){assert(*no<=16);memcpy(out,b,*no*8);}return e;
}
static int map_adapter(TeguConnection c,unsigned h,unsigned task,U64 *a,U64 *n,unsigned flags){uint64_t x=0,y=0;int e=n2mb_map(c,h,task,&x,&y,flags);*a=x;*n=y;return e;}
static int unmap_adapter(TeguConnection c,unsigned h,unsigned task,U64 a){return n2mb_unmap(c,h,task,a);}
static void drain(void){while(poolCount)[pool[--poolCount] release];}
int main(void){
 struct fake f={0};uintptr_t b=open_bridge(&f);
 TeguComputeDevice *d=[TeguComputeDevice alloc];d->connection=b;d->scalar=scalar_adapter;d->method=method_adapter;d->map=map_adapter;d->unmap=unmap_adapter;
 uint64_t caps[4];unsigned n=4;assert(!n2mb_scalar(b,0,0,0,caps,&n));d->capabilities=caps[1];
 NSData *data=[NSData dataWithContentsOfFile:[NSString stringWithUTF8String:"gpu/build/native/varying.bc"]];id error=0;
 TeguLibrary *lib=[d newTeguLibraryWithBytes:[data bytes] length:[data length] error:&error];assert(lib);
 RenderDescriptor *p=[[RenderDescriptor alloc] init];p->samples=p->amplification=1;p->raster=1;
 p->vertex=[lib newFunctionWithName:[NSString stringWithUTF8String:"varying_vertex"]];p->fragment=[lib newFunctionWithName:[NSString stringWithUTF8String:"varying_fragment"]];assert(p->vertex&&p->fragment);
 p->colors=[[RenderColors alloc] init];p->colors->colors[0]=[[RenderColor alloc] init];p->colors->colors[0]->mask=15;
 RenderLayout *layout=[[RenderLayout alloc] init];p->layout=layout;layout->buffers=[[RenderLayout alloc] init];
 for(unsigned k=0;k<2;k++){RenderAttribute *a=[[RenderAttribute alloc] init];layout->entries[7+k]=a;a->value=31;a->byteOffset=k*16;a->bufferSlot=1;}
 RenderAttribute *l=[[RenderAttribute alloc] init];layout->buffers->entries[1]=l;l->layoutStride=32;l->step=l->rate=1;
 const unsigned formats[]={80,70,10,115};
 for(unsigned fmt=0;fmt<4;fmt++){
 p->colors->colors[0]->format=formats[fmt];p->colors->colors[0]->blend=0;p->colors->colors[0]->mask=15;
 TeguRenderPipeline *pipeline=[d newRenderPipelineStateWithDescriptor:(id)p error:&error];assert(pipeline);
 TextureDesc *td=[[TextureDesc alloc] init];td->format=formats[fmt];td->w=td->h=64;td->uses=5;
 TeguTexture *target=[d newTextureWithDescriptor:(id)td];assert(target);
 TeguBuffer *v=[d newBufferWithLength:96 options:0];assert(v);
 float vertices[24]={-1,-1,0,1,.25,.5,.75,1,3,-1,0,1,.25,.5,.75,1,-1,3,0,1,.25,.5,.75,1};memcpy([v contents],vertices,sizeof(vertices));
 TeguQueue *q=[d newCommandQueue];assert(q);
 for(unsigned pass=0;pass<5;pass++){
 if(pass==4){p->colors->colors[0]->blend=1;p->colors->colors[0]->rgbSrc=4;p->colors->colors[0]->rgbDst=5;p->colors->colors[0]->mask=10;
 [pipeline release];pipeline=[d newRenderPipelineStateWithDescriptor:(id)p error:&error];assert(pipeline);}
 RenderPass *desc=[[RenderPass alloc] init];desc->color=[[PassColor alloc] init];desc->color->target=[target retain];desc->color->load=pass==1?1:2;desc->color->store=1;
 TeguCommand *c=[q commandBuffer];TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)desc];assert(e);
 [e setRenderPipelineState:pipeline];[e setVertexBuffer:v offset:0 atIndex:1];
 if(pass==1||pass==2)[e setScissorRect:(TeguScissorRect){16,16,32,32}];
 if(pass==3)[e setViewport:(TeguViewport){16,16,32,32,0,1}];
 if(pass!=2)[e drawPrimitives:3 vertexStart:0 vertexCount:3];[e endEncoding];
 fprintf(stderr,"BEFORE f=%u pass=%u invalid=%u first=%s:%u encoding=%u jobs=%u\n",formats[fmt],pass,c->invalid,c->invalidFunction?c->invalidFunction:"none",c->invalidLine,c->encoding,c->count);
 [c commit];[c waitUntilCompleted];
 fprintf(stderr,"AFTER f=%u pass=%u status=%lu\n",formats[fmt],pass,(unsigned long)[c status]);
 assert([c status]==4);[desc release];
 }
 [v release];[target release];[q release];[pipeline release];[td release];
 }

 [p->vertex release];p->vertex=[lib newFunctionWithName:[NSString stringWithUTF8String:"nord2_quad_vertex"]];assert(p->vertex);
 [p->layout release];p->layout=0;p->colors->colors[0]->format=80;p->colors->colors[0]->blend=0;p->colors->colors[0]->mask=15;
 TeguRenderPipeline *quad=[d newRenderPipelineStateWithDescriptor:(id)p error:&error];assert(quad);
 TextureDesc *td=[[TextureDesc alloc] init];td->format=80;td->w=td->h=64;td->uses=5;
 TeguTexture *target=[d newTextureWithDescriptor:(id)td];assert(target);TeguQueue *q=[d newCommandQueue];
 for(unsigned load=0;load<2;load++){
 RenderPass *desc=[[RenderPass alloc] init];desc->color=[[PassColor alloc] init];desc->color->target=[target retain];desc->color->load=load;desc->color->store=1;
 TeguCommand *c=[q commandBuffer];TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)desc];assert(e);[e setRenderPipelineState:quad];
 [e drawPrimitives:3 vertexStart:0 vertexCount:6];[e endEncoding];assert(!c->invalid);[c commit];[c waitUntilCompleted];assert([c status]==4);[desc release];
 }
 [q release];[target release];[td release];[quad release];
 [p release];[lib release];drain();d->connection=0;[d release];close_bridge(b,&f);puts("PASS frontend format/geometry sequences; transport model only");
}
'''
s += (r/'runtime_frontend_host_test.m').read_text().split('NSUInteger tegu_host_constant_data_size(NSUInteger type)',1)[1].join(['NSUInteger tegu_host_constant_data_size(NSUInteger type)',''])
(o/'frontend-repro.m').write_text(s)

libs=[root.parent/'research/nord2-host-air/target/release',root/'build/mesa-build/src/panfrost/clc']
cmd=['clang','-fblocks','-pthread','-O1','-g','-Wno-incompatible-pointer-types',
 '-Wno-incompatible-function-pointer-types','-fsanitize=address,undefined','-fconstant-string-class=NXConstantString']
cmd += ['-I'+str(p) for p in [r,root/'metal',root/'include',root/'build/tegu-frontend']]
cmd += [str(o/'frontend-repro.m')]+[str(root/p) for p in ['metal/nord2_metal_bridge.c','metal/nord2_metal_render.c','src/nord2_gpu_desc.c','src/nord2_gpu_jm.c']]
cmd += ['-lobjc','-ldl']+['-L'+str(p) for p in libs]+['-ltegu_air_bridge','-lnord2_mali_runtime','-o',str(o/'frontend-repro')]
subprocess.run(cmd,check=True)
subprocess.run([str(o/'frontend-repro')],check=True,env=dict(os.environ,
 LD_LIBRARY_PATH=':'.join(map(str,libs)),ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'))
