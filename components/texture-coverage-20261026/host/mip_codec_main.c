
#include <stdio.h>
#include <stdlib.h>
#include "xr_decode_table.h"
#include "mip_codec.h"
int main(int argc,char **argv){
 FILE *f=argc>1?fopen(argv[1],"rb"):0;unsigned h[5];if(!f)return 1;
 while(fread(h,4,5,f)==5){
  struct tegu_mip_format m;if(!tegu_mip_codec(h[0],&m))return 2;
  size_t n=(size_t)h[1]*h[2]*m.bytes;unsigned char *src=malloc(n),*dst=malloc((size_t)h[3]*h[4]*m.bytes);
  if(fread(src,1,n,f)!=n)return 3;
  tegu_mip_downsample(&m,src,(size_t)h[1]*m.bytes,h[1],h[2],dst,(size_t)h[3]*m.bytes,h[3],h[4]);
  fwrite(dst,m.bytes,(size_t)h[3]*h[4],stdout);free(src);free(dst);
 }
 return 0;
}
