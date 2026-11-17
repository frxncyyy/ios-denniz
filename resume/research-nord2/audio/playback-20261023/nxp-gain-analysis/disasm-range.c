#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <capstone/capstone.h>
int main(int c,char **v){
 if(c!=5)return 2;
 FILE *f=fopen(v[1],"rb");if(!f)return 3;
 long off=strtol(v[2],0,0),len=strtol(v[3],0,0);uint64_t va=strtoull(v[4],0,0);
 if(off<0||len<0||len>1048576)return 4;
 uint8_t *buf=malloc((size_t)len);if(!buf)return 5;
 if(fseek(f,off,SEEK_SET)||fread(buf,1,(size_t)len,f)!=(size_t)len)return 6;fclose(f);
 csh h;if(cs_open(CS_ARCH_ARM64,CS_MODE_ARM,&h)!=CS_ERR_OK)return 7;
 cs_insn *ins;size_t n=cs_disasm(h,buf,(size_t)len,va,0,&ins),used=0;
 for(size_t i=0;i<n;i++){printf("%08llx: %-10s %s\n",(unsigned long long)ins[i].address,ins[i].mnemonic,ins[i].op_str);used+=ins[i].size;}
 cs_free(ins,n);cs_close(&h);free(buf);return used==(size_t)len?0:8;
}