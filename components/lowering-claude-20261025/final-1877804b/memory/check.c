
#include "mali_compute_compiler.h"
#include <stdio.h>
#include <stdlib.h>
static unsigned w(const unsigned char *p,int i){p+=4*i;return p[0]|p[1]<<8|p[2]<<16|(unsigned)p[3]<<24;}
int main(int c,char **v) {
 if(c!=2)return 90;
 FILE *f=fopen(v[1],"rb");if(!f)return 90;fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
 void *d=malloc(n);if(!d||fread(d,1,n,f)!=(size_t)n)return 90;fclose(f);
 struct tegu_mali_blob b={0};char e[300]={0};int rc=tegu_mali_compile_compute_v2(d,n,NULL,0,&b,e,sizeof e);
 printf("RESULT {\"rc\":%d,\"size\":%zu,\"tls\":%u,\"wls\":%u,\"local\":[%u,%u,%u],\"error\":\"%s\"}\n",rc,b.size,
  rc?0:w(b.data,17),rc?0:w(b.data,18),rc?0:w(b.data,9),rc?0:w(b.data,10),rc?0:w(b.data,11),e);
 if(rc&&(b.data||b.size))return 92;
 tegu_mali_free(&b);return 0;
}
