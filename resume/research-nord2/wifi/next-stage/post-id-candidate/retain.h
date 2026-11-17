#ifndef PRIVATE_NORD2_RETAIN_H
#define PRIVATE_NORD2_RETAIN_H
#include <stdint.h>
/* A private core, NOT a native adapter. Entry proof must be fresh:
 * exclusive WiFi-only owner; AP CONN power/readability20010101; WFSYS+BGFSYS
 * CPU reset and power OFF; all DMA/firmware/radios idle; 26MHz selected;
 * exact owned SRAM18050000+4000; EMI remap complete; software lock held.
 * Retain all power/common/strap state on success AND after first mutation.
 * No caller teardown after dirty/halt. check must distinguish owned A-die
 * reset deassertion from forbidden WFSYS/BGFSYS reset release. */
#define NPI_REQUIRED 0x1ffu
struct npi_ops {
 int (*check)(void *,unsigned);
 int (*read)(void *,uint32_t,uint32_t *);
 int (*write)(void *,uint32_t,uint32_t); /* completed ordered32-bit MMIO */
 int (*delay_us)(void *,unsigned); /* bounded, no allocations */
 int (*sema)(void *,unsigned,int); /*11 only; take1 bounded;release0 */
 /* TOPSPI TOP only: bounded idle-before+busy-after checks; command readb000,
  * writea000; no retries/replay after ambiguous trigger. NO internal sema. */
 int (*spi)(void *,unsigned,int,uint32_t *); /*address,write1/read0,value*/
};
struct npi_result {
 unsigned phase,mutated,halt,retained,sema_held,efuse_valid,efuse_polls;
 unsigned transfers,writes; int error;
 uint32_t chip; /* NEVER expose raw efuse values */
};
/* Implements Ddie->SPIcommon->SYSRAMclear->AdieE3->AFE->PLL. Does NOT claim
 * consys_hw_pwr_on completion: selected lowpower/status/voltage still missing.
 * Returns0 only phase7 PLL-retained; error after mutation requires manualreset.
 * buildmode0 omits compile-time optional Android buildword;1..3 reproducesit.
 * optin exactly1, no default action. NO automatic restoration whatsoever. */
int npi_run(const struct npi_ops *,void *,unsigned,unsigned,struct npi_result *);
#endif
