#define DecompressLZBITMAP_decl
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cstdarg>
#include <vector>
#include <cassert>
#define log_debug(...) ((void)0)
static void log_error(const char*f,...){va_list a;va_start(a,f);vfprintf(stderr,f,a);va_end(a);}
#include "fn.inc"
int main(int c,char**v){
  FILE*f=fopen(v[1],"rb");fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);
  std::vector<uint8_t> d(n);fread(d.data(),1,n,f);fclose(f);
  uint64_t total=strtoull(v[3],0,10);
  uint32_t first=*(uint32_t*)d.data();uint32_t nch=first/4-1;
  FILE*o=fopen(v[2],"wb");std::vector<uint8_t> out(65536);
  for(uint32_t i=0;i<nch;i++){
    uint32_t a=((uint32_t*)d.data())[i],b=((uint32_t*)d.data())[i+1];
    size_t want=(i==nch-1)?total-(uint64_t)i*65536:65536; if(want>65536)want=65536;
    size_t r;
    if(d[a]==0xff){memcpy(out.data(),&d[a+1],b-a-1);r=b-a-1;}
    else r=DecompressLZBITMAP(out.data(),want,&d[a],b-a);
    if(r!=want){fprintf(stderr,"chunk %u: got %zu want %zu\n",i,r,want);return 1;}
    fwrite(out.data(),1,r,o);
  }
  fclose(o);return 0;}
