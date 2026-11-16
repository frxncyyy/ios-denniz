#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "genxml/v10_pack.h"
static void dump(FILE *f,const void *p,size_t n){fwrite(p,1,n,f);}
int main(int argc,char **argv){assert(argc==2);FILE *f=fopen(argv[1],"wb");assert(f);
 struct mali_resource_packed r[4]={0};
 struct MALI_RESOURCE rc={.address=0x400000,.size=256,.contains_descriptors=true};MALI_RESOURCE_pack(&r[0],&rc);
 rc=(struct MALI_RESOURCE){.address=0x510000,.size=192,.contains_descriptors=true};MALI_RESOURCE_pack(&r[1],&rc);dump(f,r,sizeof(r));
 struct mali_buffer_packed b[4];for(unsigned i=0;i<4;i++){struct MALI_BUFFER bc={.type=MALI_DESCRIPTOR_TYPE_BUFFER,.buffer_type=MALI_BUFFER_TYPE_SIMPLE,.size=(uint32_t[]){64,16,80,16}[i],.address=0x600000+0x1000*i};MALI_BUFFER_pack(&b[i],&bc);}dump(f,b,sizeof(b));
 struct mali_texture_packed t;struct MALI_TEXTURE tc={.type=MALI_DESCRIPTOR_TYPE_TEXTURE,.dimension=MALI_TEXTURE_DIMENSION_2D,.format=(187u<<12),.width=16,.height=16,.swizzle=0x688,.levels=1,.sample_count=1,.surfaces=0x520000,.array_size=1,.depth=1};MALI_TEXTURE_pack(&t,&tc);dump(f,&t,sizeof(t));
 struct mali_sampler_packed s;struct MALI_SAMPLER sc={.type=MALI_DESCRIPTOR_TYPE_SAMPLER,.reduction_mode=MALI_REDUCTION_MODE_AVERAGE,.wrap_mode_r=MALI_WRAP_MODE_CLAMP_TO_EDGE,.wrap_mode_t=MALI_WRAP_MODE_CLAMP_TO_EDGE,.wrap_mode_s=MALI_WRAP_MODE_CLAMP_TO_EDGE,.round_to_nearest_even=true,.seamless_cube_map=true,.normalized_coordinates=true,.clamp_integer_array_indices=true,.minify_nearest=true,.magnify_nearest=true,.mipmap_mode=MALI_MIPMAP_MODE_NEAREST,.maximum_anisotropy=1,.compare_function=MALI_FUNC_NEVER,.lod_algorithm=MALI_LOD_ALGORITHM_ISOTROPIC};MALI_SAMPLER_pack(&s,&sc);dump(f,&s,sizeof(s));
 struct mali_generic_plane_packed p;struct MALI_GENERIC_PLANE pc={.type=MALI_DESCRIPTOR_TYPE_PLANE,.plane_type=MALI_PLANE_TYPE_GENERIC,.clump_ordering=MALI_CLUMP_ORDERING_LINEAR,.clump_format=MALI_CLUMP_FORMAT_RAW32,.size=1024,.pointer=0x530000,.row_stride=64,.slice_stride=0,.width=16,.height=16};MALI_GENERIC_PLANE_pack(&p,&pc);dump(f,&p,sizeof(p));return fclose(f);}
