#include "mali_compute_compiler.h"
#include <stdio.h>
#include <stdlib.h>
int main(int c,char**v){FILE*f=fopen(v[1],"rb");if(!f)return 90;fseek(f,0,2);long n=ftell(f);rewind(f);void*d=malloc(n);if(fread(d,1,n,f)!=(size_t)n)return 90;
struct tegu_mali_blob b={0};char e[300]={0};int rc=tegu_mali_compile_compute_v2(d,n,NULL,0,&b,e,sizeof e);printf("%d\t%zu\t%s\n",rc,b.size,e);return 0;}
