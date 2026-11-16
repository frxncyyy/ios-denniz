
#include "nord2_metal_render.c"
#include <stdio.h>
int main(void){
 static const unsigned f[]={1,10,11,12,13,14,20,22,23,24,25,30,31,32,33,34,40,42,43,53,54,55,60,62,63,64,65,70,71,72,73,74,80,81,90,91,92,94,103,104,105,110,112,113,114,115,123,124,125};
 static const double colors[4][4]={{.25,.5,.75,1.0},{1.0,0.0,0.3,0.6},{0.002,0.04,0.9,0.1},{0.7,1e-5,70000.0,-1.0}};
 for(unsigned i=0;i<sizeof(f)/sizeof(f[0]);i++){
  unsigned internal=0,writeback=0,swizzle=0;int rc=n2r_rt_format(f[i],&internal,&writeback,&swizzle);
  struct tegu_blend_inputs opaque={1,0,0,1,0,0,1,0,15},over={1,1,0,1,5,0,1,5,15};
  struct tegu_blend_state o={{0}},b={{0}};int ro=tegu_blend_build_format(f[i],&opaque,&o),rb=tegu_blend_build_format(f[i],&over,&b);
  printf("{\"f\":%u,\"rc\":%d,\"internal\":%u,\"writeback\":%u,\"swizzle\":%u,\"srgb\":%u,\"tib\":%u,\"bpp\":%u,"
   "\"ro\":%d,\"o0\":%u,\"o2\":%u,\"o3\":%u,\"rb\":%d,\"b0\":%u,\"b2\":%u,\"b3\":%u,\"clear\":[",
   f[i],rc,internal,writeback,swizzle,n2r_rt_srgb(f[i]),n2r_tib_bytes(f[i]),n2r_bpp(f[i]),ro,o.words[0],o.words[2],o.words[3],rb,b.words[0],b.words[2],b.words[3]);
  for(unsigned c=0;c<4;c++){uint32_t w[4]={0};int rcc=n2r_clear(f[i],colors[c],w);printf("%s%ld",c?",":"",rcc?-1L:(long)w[0]);}
  printf("]}\n");
 }
 return 0;
}
