
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xr_decode_table.h"
#include "ycbcr_decode.h"
int main(int argc,char **argv){
 FILE *f=argc<2?0:fopen(argv[1],"rb");unsigned hdr[10];if(!f)return 1;
 while(fread(hdr,4,10,f)==10){
  unsigned fmt=hdr[0],full=hdr[1],matrix=hdr[2],w=hdr[3],h=hdr[4],lrow=hdr[5],crow=hdr[6],lbytes=hdr[7],cbytes=hdr[8];
  unsigned char *luma=malloc(lbytes),*chroma=malloc(cbytes);
  if(fread(luma,1,lbytes,f)!=lbytes||fread(chroma,1,cbytes,f)!=cbytes)return 2;
  struct tegu_ycbcr_layout l;if(!tegu_ycbcr_format(fmt,&l))return 3;l.full_range=full;l.matrix=matrix;
  unsigned cw=l.sub==TEGU_YCBCR_SUB_444?w:(w+1)/2,ch=l.sub==TEGU_YCBCR_SUB_420?(h+1)/2:h;
  size_t texel=l.output==TEGU_YCBCR_OUT_CODES?4:8;unsigned char *row=malloc(w*texel),*fastrow=malloc(w*texel);
  struct tegu_ycbcr_fast fast;uint16_t blend[2*64];int has_fast=tegu_ycbcr_fast_init(&l,&fast);
  for(unsigned y=0;y<h;y++){tegu_ycbcr_decode_row(&l,luma,lrow,chroma,crow,w,cw,ch,y,row,tegu_srgb_knots_8192);
   if(has_fast){tegu_ycbcr_fast_row(&l,&fast,luma,lrow,chroma,crow,w,cw,ch,y,(uint16_t *)(void *)fastrow,blend);
    if(memcmp(row,fastrow,w*texel)){fprintf(stderr,"fast path differs fmt=%u y=%u\n",fmt,y);return 4;}}
   fwrite(row,texel,w,stdout);}
  free(row);free(fastrow);free(luma);free(chroma);
 }
 /* RGB10A2 packing of converted halves: round(v*1023/2048) for every v. */
 for(unsigned v=0;v<=2048;v++){uint16_t h=tegu_ycbcr_half(v,11);
  if(tegu_ycbcr_code10(h)!=(v*1023u+1024u)/2048u){fprintf(stderr,"code10 v=%u\n",v);return 5;}}
 return 0;
}
