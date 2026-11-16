#include "attachment_clear_r8_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;} } while(0)

static uint8_t *read_file(const char *path,size_t expected)
{
 FILE *f=fopen(path,"rb");if(!f)return NULL;
 uint8_t *p=(uint8_t *)malloc(expected);if(!p){fclose(f);return NULL;}
 if(fread(p,1,expected,f)!=expected||fgetc(f)!=EOF){free(p);p=NULL;}
 fclose(f);return p;
}

int main(int argc,char **argv)
{
 CHECK(argc==4);
 uint8_t *package=read_file(argv[1],TEGU_AC1_PACKAGE_BYTES);
 uint8_t *aperture=read_file(argv[2],0x77000);
 uint8_t *wire=read_file(argv[3],TEGU_AC1_R8_COMMAND_COUNT*8u);
 CHECK(package&&aperture&&wire);
 struct tegu_ac1_info info;CHECK(!tegu_ac1_parse(package,TEGU_AC1_PACKAGE_BYTES,&info));
 uint8_t *scratch=(uint8_t *)malloc(TEGU_AC1_R8_SCRATCH_BYTES);
 uint8_t *resources=(uint8_t *)malloc(TEGU_AC1_R8_RESOURCE_BYTES);
 CHECK(scratch&&resources);memset(scratch,0xa5,TEGU_AC1_R8_SCRATCH_BYTES);
 memset(resources,0x5a,TEGU_AC1_R8_RESOURCE_BYTES);
 struct tegu_ac1_r8_commands commands;memset(&commands,0xcc,sizeof(commands));
 CHECK(!tegu_ac1_r8_state_build(&info,package,TEGU_AC1_PACKAGE_BYTES,
  aperture+0x71200,TEGU_AC1_R8_RECT_BYTES,aperture+0x71300,TEGU_AC1_R8_MATRIX_BYTES,
  scratch,TEGU_AC1_R8_SCRATCH_BYTES,resources,TEGU_AC1_R8_RESOURCE_BYTES,&commands));
 CHECK(!memcmp(scratch,aperture,TEGU_AC1_R8_SCRATCH_BYTES));
 CHECK(!memcmp(resources,aperture+0x71000,TEGU_AC1_R8_RESOURCE_BYTES));
 CHECK(commands.count==TEGU_AC1_R8_COMMAND_COUNT&&!memcmp(commands.words,wire,sizeof(commands.words)));
 /* Refusals happen before either caller-owned output is changed. */
 memset(scratch,0xa5,TEGU_AC1_R8_SCRATCH_BYTES);memset(resources,0x5a,TEGU_AC1_R8_RESOURCE_BYTES);
 memset(&commands,0xcc,sizeof(commands));
 CHECK(tegu_ac1_r8_state_build(&info,package,TEGU_AC1_PACKAGE_BYTES,
  aperture+0x71200,15,aperture+0x71300,TEGU_AC1_R8_MATRIX_BYTES,
  scratch,TEGU_AC1_R8_SCRATCH_BYTES,resources,TEGU_AC1_R8_RESOURCE_BYTES,&commands)==TEGU_AC1_EINVAL);
 for(size_t i=0;i<TEGU_AC1_R8_SCRATCH_BYTES;i++)CHECK(scratch[i]==0xa5);
 for(size_t i=0;i<TEGU_AC1_R8_RESOURCE_BYTES;i++)CHECK(resources[i]==0x5a);
 const uint8_t *c=(const uint8_t *)&commands;for(size_t i=0;i<sizeof(commands);i++)CHECK(c[i]==0xcc);
 free(resources);free(scratch);free(wire);free(aperture);free(package);
 puts("AC1_R8_STATE_HOST_PASS exact 16x16 scratch/resource/59-command parity and atomic refusal");
 return 0;
}
