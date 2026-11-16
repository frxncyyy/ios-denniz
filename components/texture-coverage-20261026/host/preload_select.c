
#include "nord2_metal_bridge.c"
#include <stdio.h>
int main(void){
 static const unsigned f[]={1,10,11,12,13,14,20,22,23,24,25,30,31,32,33,34,40,42,43,53,54,55,60,62,63,64,65,70,71,72,73,74,80,81,90,91,92,94,103,104,105,110,112,113,114,115,123,124,125};unsigned bad=0;
 for(unsigned i=0;i<sizeof(f)/sizeof(f[0]);i++){
  unsigned raw=n2r_rt_is_raw(f[i]),twin=n2r_rt_raw_twin(f[i]);
  struct n2t_format_info a,b;
  int ok=raw?(twin&&preload_format(f[i])==twin&&preload_shader(f[i])==&n2_preload_uint_shader&&
             frame_preload_shader(f[i])==&n2_frame_preload_uint_shader&&n2r_bpp(twin)==n2r_bpp(f[i])&&
             !n2t_format_query(twin,&a)&&!n2t_format_query(f[i],&b)&&(a.flags&N2T_FMT_INTEGER)&&!(a.flags&N2T_FMT_SIGNED)&&
             a.bytes_per_block==b.bytes_per_block&&n2r_rt_is_raw(twin)&&n2r_tib_bytes(twin)==n2r_tib_bytes(f[i])):
           (!twin&&preload_format(f[i])==f[i]&&preload_shader(f[i])==&n2_preload_shader&&frame_preload_shader(f[i])==&n2_frame_preload_shader);
  if(!ok){printf("PRELOAD_BAD %u\n",f[i]);bad++;}
 }
 /* The uint programs are their own code. */
 if(sizeof(n2_preload_uint_fs)==sizeof(n2_preload_fs)&&!memcmp(n2_preload_uint_fs,n2_preload_fs,sizeof(n2_preload_fs)))bad++;
 printf("PRELOAD_SELECT bad=%u\n",bad);return bad!=0;
}
