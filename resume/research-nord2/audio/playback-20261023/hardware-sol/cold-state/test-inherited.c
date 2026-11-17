/* Actual boot snapshot plus EXPLICIT simulated assumptions for uncaptured
 * I2C controller idle/DMA off and 9874 amp POR/readback behavior. This is
 * software regression evidence, not physical hardware proof. */
#define main original_full_main
#include "test-full.c"
#undef main
#include "actual-snapshot.inc"
static unsigned forbidden,afe_accesses;
static void check_afe_power(uint32_t pa)
{
 if(pa>=NAF_AFE && pa<NAF_AFE+0x4000) {
  assert((*regp(&cm,NAF_SPM_PWR_STATUS)&(1u<<21)) &&
         (*regp(&cm,NAF_SPM_PWR_STATUS2)&(1u<<21)));
  assert((*regp(&cm,NAF_SPM_AUDIO_PWR)&0x1f)==0xd);
  afe_accesses++;
 }
}
static int audited_read(void *c,uint32_t pa,unsigned w,uint32_t *v)
{ check_afe_power(pa);return allread(c,pa,w,v); }
static int audited_write(void *c,uint32_t pa,unsigned w,uint32_t v)
{
 check_afe_power(pa);
 if(pa==0x10006358 || pa==NAF_APM_APLL2_CON0 || pa==NAF_APM_APLL2_CON1 ||
    pa==NAF_APM_APLL2_CON2 || pa==NAF_APM_APLL2_CON4) {forbidden++;assert(!"borrowed ADSP/PLL mutation");}
 if(pa==NAF_APM_AP_PLL_CON3) assert(!((v^actual_snapshot[NAU_S_AP_PLL_CON3])&(1u<<5)));
 if(pa==NAF_TOP_SET(NAF_TOP_CFG13)||pa==NAF_TOP_CLR(NAF_TOP_CFG13)) assert(!(v&~0x81u));
 if(pa==NAF_TOP_SET(NAF_TOP_CFG12)||pa==NAF_TOP_CLR(NAF_TOP_CFG12)) assert(!(v&~0x83u));
 if(pa==NAF_IFR_PROT2_SET||pa==NAF_IFR_PROT2_CLR) assert(v==(1u<<4));
 /* Borrowed shared supplies must remain live, even transiently. */
 if(pa==NAF_IFR_CG1_SET) assert(!(v&(1u<<25)));
 if(pa==NAF_IFR_CG3_SET) assert(!(v&(1u<<4)));
 if(pa==NAF_TOP_SET(NAF_TOP_CFG12)||pa==NAF_TOP_SET(NAF_TOP_CFG13)||pa==0x10000104)
  assert(!(v&(1u<<7)));
 /* Existing register model predates optional audio_h normalization. */
 if(pa==0x10000104) {*regp(&cm,0x10000100)|=v;return 0;}
 if(pa==0x10000108) {*regp(&cm,0x10000100)&=~v;return 0;}
 return allwrite(c,pa,w,v);
}
static const struct nah_io inherited_io={audited_read,audited_write,alldelay,allworker,alllease};
static void actual_prepare(void)
{
 full_prepare(0x0b74,0x0a74,0);
 for(unsigned i=0;i<NAU_SNAP_COUNT;i++) *regp(&cm,nau_snapshot_pa[i])=actual_snapshot[i];
 bm.pinmux=actual_snapshot[NAU_S_MODE_120];bm.pads=actual_snapshot[NAU_S_DIN_96];
 bm.ies=actual_snapshot[NAU_S_BM_IES122];bm.gate=actual_snapshot[NAU_S_IMPS_CG];
 bm.ch[1][0xec/2]=0x8800;bm.ch[1][0x10/2]=0x28;bm.saved[2]=0x28;bm.ch[1][0xc/2]=1;
 bm.ch[1][0x24/2]=2; /* Captured7d2ca8f9 AP RESUME readback, retained through all transfers. */
 bm.ifr1=actual_snapshot[NAU_S_IFR1_CG];bm.ifr2=actual_snapshot[NAU_S_IFR2_CG];
 bm.hclk=24960; /* captured univpll_d5_d4=124.8MHz, DT divider5 */
 /* Uncaptured state is modeled by m_init: all masters idle, DMA EN0,
  * AP completion IRQ0, I3C0, exact9874 revisions and powered-down amps. */
 forbidden=afe_accesses=0;
}
static void restored(void)
{
 for(unsigned i=0;i<NAU_SNAP_COUNT;i++) {
  uint32_t mask=i==NAU_S_SPM_POWERON?1u:0xffffffffu;
  if(((*regp(&cm,nau_snapshot_pa[i])^actual_snapshot[i])&mask)) {
   fprintf(stderr,"restore index%u pa%x got%x expected%x\n",i,nau_snapshot_pa[i],*regp(&cm,nau_snapshot_pa[i]),actual_snapshot[i]);abort();
  }
 }
 assert(!forbidden);assert(bm.ch[1][0xec/2]==0x8800 && bm.ch[1][0x24/2]==2);m_check_restored(&bm);
 assert(!bm.slave[5].accessed && !bm.slave[6].accessed);
}
static void load_profile(const char *path)
{
 unsigned char blob[4163];FILE *f=fopen(path,"rb");assert(f);
 assert(fread(blob,1,sizeof blob,f)==sizeof blob && fgetc(f)==EOF);fclose(f);
 static const unsigned char digest[32]={0x63,0x53,0x00,0xfb,0xc9,0x6f,0x25,0x8f,0xf9,0xa3,0x5f,0x82,0x43,0x5f,0x2e,0x9d,0x3f,0xf6,0xf1,0x37,0x0c,0x15,0x60,0x67,0xb4,0x01,0x46,0xe0,0x11,0x03,0x86,0x92};
 assert(!nah_board_load(blob,sizeof blob,digest));
}
int main(int argc,char **argv)
{
 assert(argc==3);load_profile(argv[1]);actual_prepare();
 if(!strcmp(argv[2],"rate")) {
  *regp(&cm,NAF_APM_APLL2_CON2)^=0x100000;
  assert(nau_hw_init(&hw,&inherited_io,0,nah_board_profile())<0);
  assert(!cm.writes && !bm.starts);puts("PASS incompatible live PLL rejected before writes");return 0;
 }
 if(!strcmp(argv[2],"status") || !strcmp(argv[2],"adsp")) {
  if(!strcmp(argv[2],"status")) *regp(&cm,NAF_SPM_PWR_STATUS2)^=1u<<22;
  else *regp(&cm,0x10006358)^=1;
  assert(nau_hw_init(&hw,&inherited_io,0,nah_board_profile())<0);
  assert(!cm.writes && !bm.starts);puts("PASS incoherent inherited domain rejected before writes");return 0;
 }
 assert(!nau_hw_init(&hw,&inherited_io,0,nah_board_profile()));
 if(!strcmp(argv[2],"drift")) {
  *regp(&cm,0x10000100)^=1;
  unsigned writes=cm.writes,trans=bm.starts;
  assert(nau_hw_start(&hw,48000,2,16,0,256,0,0)<0);
  assert(cm.writes==writes && bm.starts==trans && !afe_accesses);
  puts("PASS borrowed audio_h drift rejected before playback writes");return 0;
 }
 assert(!strcmp(argv[2],"cycles"));
 int16_t pcm[480];for(unsigned i=0;i<480;i++)pcm[i]=(i&1)?-16000:16000;
 for(unsigned cycle=0;cycle<3;cycle++) {
  uint64_t t=1000000ull*(cycle+1);cm.current=NAF_SRAM;
  assert(!nau_hw_start(&hw,48000,2,16,0,256,0,t));
  assert(!nau_hw_tick(&hw,t));
  for(unsigned j=1;j<=12;j++) {
   assert(nau_hw_feed(&hw,pcm,240)==240);
   cm.current=NAF_SRAM+4*(240*j%1024);
   assert(!nau_hw_tick(&hw,t+5000*j));
  }
  assert(!nau_hw_stop(&hw));restored();
 }
 assert(afe_accesses && bm.dma_en_reads);
 printf("PASS3 actual-snapshot playback cycles; %u simulated I2C transfers; shared fields restored; no borrowed PLL/ADSP writes\n",bm.starts);
}
