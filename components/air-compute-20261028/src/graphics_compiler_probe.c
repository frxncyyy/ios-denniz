/* Host command-line wrapper for the callable graphics compiler. */
#include "mali_graphics_compiler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv) {
 if((argc!=4&&argc!=5)||(strcmp(argv[1],"vertex")&&strcmp(argv[1],"fragment"))||
    (argc==5&&strcmp(argv[4],"--vertex-inputs")))return 2;
 FILE *f=fopen(argv[2],"rb");if(!f)return 3;
 if(fseek(f,0,SEEK_END)){fclose(f);return 3;}
 long size=ftell(f);rewind(f);
 if(size<20||size>1024*1024){fclose(f);return 3;}
 void *data=malloc(size);if(!data){fclose(f);return 3;}
 size_t got=fread(data,1,size,f);fclose(f);if(got!=(size_t)size){free(data);return 3;}
 struct tegu_mali_graphics result={0};char error[512];
 unsigned stage=!strcmp(argv[1],"vertex")?TEGU_GRAPHICS_VERTEX:TEGU_GRAPHICS_FRAGMENT;
 uint32_t attributes=0;
 int status=argc==5?tegu_mali_compile_graphics_with_vertex_inputs(data,size,stage,&result,&attributes,error,sizeof(error)):
  tegu_mali_compile_graphics(data,size,stage,&result,error,sizeof(error));free(data);
 if(status){fprintf(stderr,"%s\n",error);return status;}
 f=fopen(argv[3],"wb");if(!f){tegu_mali_graphics_free(&result);return 7;}
 got=fwrite(result.code,1,result.size,f);int closed=fclose(f);
 if(got!=result.size||closed){tegu_mali_graphics_free(&result);return 7;}
 printf("stage=%s bytes=%zu registers=%u tls=%u push=%u preload=%llx GPU_EXECUTED=0\n",
  argv[1],result.size,result.registers,result.tls,result.fau_words,(unsigned long long)result.preload);
 printf("sysval_mask=%u air_push_bytes=%u\n",result.sysval_mask,result.push_bytes);
 printf("vertex_attribute_mask=%08x\n",attributes);
 tegu_mali_graphics_free(&result);return 0;
}
