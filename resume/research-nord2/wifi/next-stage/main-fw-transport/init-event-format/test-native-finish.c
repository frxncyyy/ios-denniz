#define main inherited_fixture_main
#include "test-transport.c"
#undef main
#include "wifi_main_wfdma_native.h"
struct nwfn_native nwfn_native;
static struct mock hardware;
static int bo(void *c){assert(c==&nwfn_native);return owner(&hardware);}
static int br(void *c,uint32_t a,uint32_t*v){assert(c==&nwfn_native);return rr(&hardware,a,v);}
static int bw(void*c,uint32_t a,uint32_t v){assert(c==&nwfn_native);return rw(&hardware,a,v);}
static int bmread(void*c,uint64_t a,void*v,uint32_t n){assert(c==&nwfn_native);return mr(&hardware,a,v,n);}
static int bmwrite(void*c,uint64_t a,const void*v,uint32_t n){assert(c==&nwfn_native);return mw(&hardware,a,v,n);}
static int bs(void*c,uint64_t a,uint32_t n,unsigned w){assert(c==&nwfn_native);return syncmem(&hardware,a,n,w);}
static int bd(void*c,unsigned n){assert(c==&nwfn_native);return us(&hardware,n);}
const struct nwt_io nwfn_io={bo,br,bw,bmread,bmwrite,bs,bd};
int nwfn_owner(void *c){assert(c==&nwfn_native);return 0;}
static int nwfn_stop(struct nwfn_native *n,unsigned e){n->error=e;n->halt=n->init.transport.halt=1;return -1;}
#include "native-finish-extracted.inc"
static void init(unsigned type,unsigned cid,unsigned status){
 memset(&nwfn_native,0,sizeof(nwfn_native));nwfn_native.stage=NWFN_RETAINED;nwfn_native.init.transport=seeded();
 hardware=(struct mock){.active=1,.packet_type=type,.badcid=cid,.badstatus=status};
}
int main(void){
 for(unsigned type=7;type<=0xe000;type=0xe000){
  init(type,1,0);assert(!nwfn_finish_command(0x40)&&nwfn_native.init.transport.finish_phase==1&&nwfn_native.init.transport.rx_cid==0x41);
  init(type,1,1);assert(nwfn_finish_command(0x40)==-1&&nwfn_native.halt&&nwfn_native.error==20&&!nwfn_native.init.transport.finish_phase);
  init(type,0,0);hardware.badseq=1;assert(nwfn_finish_command(0x40)==-1&&nwfn_native.init.transport.rx_failure_reason==3);
  if(type==0xe000)break;
 }
 puts("PASS actual extracted native finish with real transport: both INIT types, source EID/seq/status, captured mismatched CID allowed, failed status rejected.");
}
