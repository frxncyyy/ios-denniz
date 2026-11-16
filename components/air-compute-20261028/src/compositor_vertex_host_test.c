#include "compositor_vertex_state.h"
#include "compositor_request.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv) {
 _Static_assert(sizeof(struct tegu_compositor_request)==TEGU_COMPOSITOR_REQUEST_BYTES,"compositor wire ABI");
 assert(argc==2);unsigned char oracle[TEGU_COMPOSITOR_DRIVER_BYTES];FILE *f=fopen(argv[1],"rb");
 assert(f&&fread(oracle,1,sizeof(oracle),f)==sizeof(oracle)&&fgetc(f)==EOF);fclose(f);
 struct tegu_compositor_vertex_state *out=malloc(sizeof(*out)),*prior=malloc(sizeof(*prior));assert(out&&prior);
 struct tegu_vertex_attribute attrs[2]={{0,0,109,0,32,0,1},{1,0,103,16,32,0,1}};
 struct tegu_vertex_buffer buffer={0x08000000,96};struct tegu_vertex_draw draw={0,3,1,0};
 assert(!tegu_compositor_vertex_build(attrs,2,&buffer,&draw,0x06006000,out));
 assert(!memcmp(out->driver,oracle,sizeof(oracle)));
 *prior=*out;
 buffer.length=87;assert(tegu_compositor_vertex_build(attrs,2,&buffer,&draw,0x06006000,out));assert(!memcmp(prior,out,sizeof(*out)));
 buffer.length=96;attrs[1].buffer_index=1;assert(tegu_compositor_vertex_build(attrs,2,&buffer,&draw,0x06006000,out));assert(!memcmp(prior,out,sizeof(*out)));
 attrs[1].buffer_index=0;attrs[1].format=109;assert(tegu_compositor_vertex_build(attrs,2,&buffer,&draw,0x06006000,out));assert(!memcmp(prior,out,sizeof(*out)));
 attrs[1].format=103;draw.vertex_count=4;assert(tegu_compositor_vertex_build(attrs,2,&buffer,&draw,0x06006000,out));assert(!memcmp(prior,out,sizeof(*out)));
 draw.vertex_count=3;assert(tegu_compositor_vertex_build((void *)out,2,&buffer,&draw,0x06006000,out));assert(!memcmp(prior,out,sizeof(*out)));
 attrs[0].stride=attrs[1].stride=40;buffer.length=120;assert(!tegu_compositor_vertex_build(attrs,2,&buffer,&draw,0x06006000,out));
 /* BackBoard triangle lists: 12 vertices (two quads) bind; non-lists and >384 refuse. */
 draw.vertex_count=12;buffer.length=480;assert(!tegu_compositor_vertex_build(attrs,2,&buffer,&draw,0x06006000,out));
 draw.vertex_count=13;buffer.length=520;assert(tegu_compositor_vertex_build(attrs,2,&buffer,&draw,0x06006000,out));
 draw.vertex_count=387;buffer.length=387u*40u;assert(tegu_compositor_vertex_build(attrs,2,&buffer,&draw,0x06006000,out));
 draw.vertex_count=3;buffer.length=120;
 assert(out->driver[16]==40&&out->driver[48]==40);
 attrs[0]=(struct tegu_vertex_attribute){0,1,109,0,48,0,1};
 attrs[1]=(struct tegu_vertex_attribute){1,1,97,32,48,0,1};buffer.length=144;
 assert(!tegu_compositor_vertex_build(attrs,2,&buffer,&draw,0x06006000,out));
 *prior=*out;attrs[1].offset=16;assert(tegu_compositor_vertex_build(attrs,2,&buffer,&draw,0x06006000,out));
 assert(!memcmp(prior,out,sizeof(*out)));
 free(out);free(prior);puts("PASS compositor vertex driver: exact Mesa576-byte oracle, dynamic stride, logical bounds, format/shape/CPU-alias refusals; no GPU claim");
}
