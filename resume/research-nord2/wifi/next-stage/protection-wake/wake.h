#ifndef NORD2_PRIVATE_PROTECTION_WAKE_H
#define NORD2_PRIVATE_PROTECTION_WAKE_H
#include <stdint.h>
struct npw_ops {
 /* Accessproof: freshoriginalcoldowner,restorearmed,realID20010101,pwr0d,
  * bothCONNacks1,GALS1,AP2CONNbit13clear+PROT2bit2clear. Allows onlyexisting
  * HOSTon-domain/CFGID/protection reads. NOTpermission for commonmutation. */
 int (*access)(void *);
 int (*read)(void *,uint32_t,uint32_t *);
 /* ONLY fullregister180601a0 values1/0; requires originalexact0 */
 int (*write)(void *,uint32_t,uint32_t);
 int (*barrier)(void *);
 int (*delay_us)(void *,unsigned);
 int (*strict_common)(void *); /*UNCHANGED all7 canonicalguards inclbit14zero*/
 int (*inner)(void *,int *clean); /*originalcommonobserver,notRF/CPUrelease*/
};
struct npw_result {
 uint32_t before[4],after[4]; /*request220,ack228,host184,wake1a0*/
 unsigned before_valid,after_valid,polls;
 int touched,restored,clean,strict_pass,inner_called,inner_clean,error;
};
/* optin1 only; conditional experimental source-supportedforcewake, NOTproven
 *bit14fix. Snapshotall4 beforemutation. Require requestbits6000clear,wake0,
 *ACKbit13clear. Source10polls1ms HOSTbit5zero thenID20010101. Captureafter,
 *run unchangedstrictguard; ifstillbit14high noinner. Restorewake0+verify.
 *Dirtyinner/unsafecleanup leaveswake+ownership forcallerexistingdirtyhalt.
 *NeverwriteINFRAprotect,EMI_CTL18,detectors,calibration/CPU/RF/EMImemory.
 *NativeMMIOmaystall;softwarebounds cannotinterrupta busoperation. */
int npw_try(const struct npw_ops *,void *,unsigned,struct npw_result *);
#endif
