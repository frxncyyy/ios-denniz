#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "audio_afe.h"
#include "actual-snapshot.inc"
int main(void)
{
 struct nau_state s={0};s.snap_read=NAU_SNAP_COUNT;
 memcpy(s.snap,actual_snapshot,sizeof actual_snapshot);
 struct naf_amp_evidence a[2]={{1,1,1,0,0},{1,1,1,0,0}};
 unsigned gate=naf_gate(&s,a);
 assert(gate==0xf8);
 assert(!(gate&(NAF_G_AUDIO_ON|NAF_G_PINS)));
 assert((s.snap[NAU_S_PWR_STATUS]&(1u<<22)) && (s.snap[NAU_S_PWR_STATUS_2ND]&(1u<<22)));
 assert((s.snap[NAU_S_ADSP_PWR_CON]&0x227f)==0x224d);
 assert(naf_pll_rate(s.snap[NAU_S_APLL2_CON2],s.snap[NAU_S_APLL2_CON1]>>24)==NAF_APLL2_HZ);
 assert(s.snap[NAU_S_APLL2_TUNER]==s.snap[NAU_S_APLL2_CON2]+1);
 assert((s.snap[NAU_S_CLK_CFG_7]>>24&0x83)==1);
 assert((s.snap[NAU_S_CLK_CFG_8]&0x83)==1);
 assert((s.snap[NAU_S_CLK_CFG_12]&0x83)==2);
 assert((s.snap[NAU_S_CLK_CFG_13]&0x81)==1);
 assert((s.snap[NAU_S_CLK_CFG_15]&0x83)==3);
 /* Snapshot supports existing I2C8 mux, pads, CAM-off preconditions.
  * DMA and controller idle are intentionally NOT inferred here. */
 assert((s.snap[NAU_S_MODE_120]&0xff00)==0x1100);
 assert((s.snap[NAU_S_BM_IES122]&0x2008)==0x2008);
 assert((s.snap[NAU_S_DIN_96]&0x0c000000)==0x0c000000);
 assert(!((s.snap[NAU_S_PWR_STATUS]|s.snap[NAU_S_PWR_STATUS_2ND])&(1u<<23)));
 assert(!(s.snap[NAU_S_IMPS_CG]&16));
 assert(!(s.snap[NAU_S_CLK_CFG_10]&32768));
 puts("PASS captured8e6697a2 gatef8, stable ADSP on, live196.608MHz APLL2, inherited audio muxes, known I2C preconditions");
}
