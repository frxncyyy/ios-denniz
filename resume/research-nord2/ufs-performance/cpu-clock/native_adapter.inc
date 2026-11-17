/* PRIVATE integration candidate: explicit nord2cpuclock=1+nord2ufsprofile=1.
 * Read-only AP configured-clock snapshot; no automatic future activation. */
#include "configured_clock.h"
#include "configured_clock.c"
static bool nord2_clock_enabled;
static vm_offset_t nord2_clock_apmixed, nord2_clock_mcucfg;
static struct n2clk_capture nord2_clock_capture;
static int nord2_clock_read(void *unused,uint64_t physical,uint32_t *value)
{
 (void)unused;
 if(!value || !nord2_clock_enabled)return -1;
 unsigned index;
 for(index=0;index<N2CLK_WORDS;index++)if(physical==n2clk_read_allowlist[index])break;
 if(index==N2CLK_WORDS)return -2;
 vm_offset_t base=index<5?nord2_clock_apmixed:nord2_clock_mcucfg;
 if(!base)return -3;
 uint64_t offset=physical-(index<5?UINT64_C(0x1000c000):UINT64_C(0x0c530000));
 /* Allowlist proves aligned offset and bounds; no generic arbitrary read. */
 *value=*(volatile uint32_t *)(base+(vm_offset_t)offset);
 return 0;
}
static void nord2_clock_sample(const char *stage)
{
 if(!nord2_clock_enabled)return;
 struct n2clk_capture *c=&nord2_clock_capture;
 n2clk_collect(nord2_clock_read,NULL,c);
 int stable=c->status==N2CLK_READ_ERROR?-1:c->status==N2CLK_UNSTABLE?0:1;
 printf("NORD2 CPU CLOCK: stage=%s configured_only=1 status=%u stable=%d attempts=%u freq=%llu\n",
  stage,(unsigned)c->status,stable,c->attempted,(unsigned long long)__builtin_arm_rsr64("CNTFRQ_EL0"));
 printf("NORD2 CPU CLOCK VALID: stage=%s p0=%#x p1=%#x p2=%#x\n",stage,c->valid[0],c->valid[1],c->valid[2]);
 for(unsigned p=0;p<3;p++)for(unsigned start=0;start<10;start+=5){
  printf("NORD2 CPU CLOCK RAW: stage=%s pass=%u start=%u v0=%#x v1=%#x v2=%#x v3=%#x v4=%#x\n",
   stage,p,start,c->words[p][start],c->words[p][start+1],c->words[p][start+2],c->words[p][start+3],c->words[p][start+4]);
 }
 for(unsigned i=0;i<5;i++){
  const struct n2clk_decoded *d=&c->pairs[i];
  printf("NORD2 CPU CLOCK PAIR: stage=%s index=%u status=%u mux=%u pcw=%#x post=%u div=%u configured_khz=%llu\n",
   stage,i,(unsigned)d->status,d->mux,d->pcw,d->postdiv_code,d->divider_code,(unsigned long long)d->configured_khz);
 }
 if(c->status==N2CLK_READ_ERROR){
  unsigned position=c->attempted-1;
  printf("NORD2 CPU CLOCK ERROR: stage=%s pass=%u word=%u result=%d\n",
   stage,position/10,position%10,c->read_result[position/10][position%10]);
 }
}
static void nord2_clock_init(void)
{
 uint32_t optin=0;
 PE_parse_boot_argn("nord2cpuclock",&optin,sizeof(optin));
 if(optin!=1 || !nord2_ufs_profile_enabled)return;
 nord2_clock_enabled=true;
 /* Source-backed controller ranges; AP mapping/access still requires separate
  * native review before integration. No access/MPU/PLL state is modified. */
 nord2_clock_apmixed=ml_io_map(0x1000c000,0xe00);
 if(nord2_clock_apmixed)nord2_clock_mcucfg=ml_io_map(0x0c530000,0x10000);
 nord2_clock_sample("pre-root"); /* no UFS mutex held */
}
