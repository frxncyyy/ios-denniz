
#include "mali_compute_compiler.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv) {
 if(argc!=2)return 90;FILE *f=fopen(argv[1],"rb");if(!f)return 90;
 fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);void *data=malloc(n);
 if(n<20||!data||fread(data,1,n,f)!=(size_t)n)return 90;fclose(f);
 struct tegu_mali_blob output={0};char error[512]={0};
 int rc=tegu_mali_compile(data,n,&output,error,sizeof(error));free(data);
 printf("RESULT rc=%d bytes=%zu error=%s\n",rc,output.size,error);
 if(rc&&(output.data||output.size))return 91;
 if(!rc&&(!output.data||!output.size))return 92;
 tegu_mali_free(&output);return rc;
}
