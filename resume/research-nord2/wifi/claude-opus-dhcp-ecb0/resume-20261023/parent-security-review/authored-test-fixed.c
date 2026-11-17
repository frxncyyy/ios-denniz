/* OFLD RX-gate test. AUTHORED, NOT EXECUTED (no execution tool available).
 * Codex compiles/runs via tests/run.py against baseline and candidate copies.
 *
 * Seams (recorded, not hidden):
 *  - Reuses canonical tools/fixtures/nord2-wifi-station/transport-hardware.inc
 *    (synthetic MMIO/DMA model). It is a firmware MODEL, not hardware proof.
 *  - Drives the REAL static decode() through deliver()+step(), and seeds the
 *    keyed/controlled state exactly as the canonical group_tests seam does
 *    (native-regression-test.c:47-49): controlled_port + installed PTK/GTK.
 *  - Frames mirror the canonical accepted-unicast CCMP template; the only added
 *    field is DW4 OFLD (p+16), matching the immutable hardware RXERROR records
 *    (d4=6144=OFLD3, d1=d2=0). No payload bytes/identities are printed.
 *
 * Classification markers let the runner require EXACT discrimination, so a
 * crash/sanitizer/unrelated failure can never be mistaken for a pass.
 */
#define NST_TEST_NO_MAIN 1
#include "transport-hardware.inc"

/* Build a translated CCMP unicast-to-self data frame identical to the canonical
 * accepted template, with optional OFLD and optional hard ICV error. groups=9
 * (GROUP1 CCMP-PN @ +40, GROUP4 802.11 hdr @ +24); Ethernet payload at +56. */
static unsigned uc_ccmp(struct test*t,uint8_t*p,unsigned pn,unsigned ofld,unsigned icv){
 unsigned n=116;memset(p,0,n);put16(p,n);put16(p+2,2u<<11);
 put32(p+4,5u|(9u<<11)|(4u<<16)|(0u<<21)|(icv?(1u<<25):0u)); /* wlan5,sec CCMP,key0, opt ICV err (DW1 bit25) */
 put32(p+8,1u<<13);           /* translated */
 put32(p+12,6u<<8);           /* header length word */
 put32(p+16,ofld?(3u<<11):0u);/* DW4 OFLD=3 when requested */
 put16(p+24,0x4208);memcpy(p+26,t->s.peer.ap,6);p[40]=(uint8_t)pn; /* fc + A2 + CCMP PN low */
 uint8_t*m=p+56;memcpy(m,t->s.peer.self,6);m[6]=2;m[11]=7;m[12]=8;m[13]=0; /* DA=self, EtherType 0x0800 */
 return n;
}
static void ready(struct test*t){
 setup(t);t->s.controlled_port=1;t->s.ptk.installed=1;t->s.ptk.key_id=0;
 t->s.gtk.installed=1;t->s.gtk.key_id=2;t->s.gtk_keys[2]=t->s.gtk;t->s.gtk_wlan[2]=t->s.peer.group_wlan;t->s.gtk_mapped=1u<<2;
}
#ifdef CANDIDATE_BUILD
#define EXPECT_OFLD_DELIVERED 1
#else
#define EXPECT_OFLD_DELIVERED 0
#endif

int main(void){
 struct test*t=calloc(1,sizeof *t);assert(t);
 uint8_t p[128];unsigned err=0;

 /* 1. Clean translated CCMP unicast (no OFLD) is accepted on BOTH builds: shows
  *    the template and crypto path are valid and the test is not vacuous. */
 ready(t);unsigned eth=t->events[NST_ETHERNET];
 uc_ccmp(t,p,1,0,0);deliver(t,0,p,sizeof p);step(t);
 C(t->events[NST_ETHERNET]==eth+1);C(t->s.data_diag.rx_unicast>=1);cleanup(t);

 /* 2. Hard ICV error (DW1 bit25) still drops on BOTH builds (gate preserved). */
 ready(t);err=t->s.data_diag.rejected[NSDD_RX_ERROR];eth=t->events[NST_ETHERNET];
 uc_ccmp(t,p,2,0,1);deliver(t,0,p,sizeof p);step(t);
 C(t->events[NST_ETHERNET]==eth);C(t->s.data_diag.rejected[NSDD_RX_ERROR]==err+1);cleanup(t);

 /* 3. OFLD=3 unicast, otherwise clean/valid: the discriminating case.
  *    Baseline drops at the error gate; candidate routes it through the secure
  *    decode and delivers it (keys/port valid). Exactly one RXERROR breadcrumb
  *    is emitted in both builds (observational). */
 ready(t);err=t->s.data_diag.rejected[NSDD_RX_ERROR];eth=t->events[NST_ETHERNET];
 unsigned rxe=t->s.data_diag.rx_error_logs;
 uc_ccmp(t,p,3,1,0);deliver(t,0,p,sizeof p);step(t);
 C(t->s.data_diag.rx_error_logs==rxe+1); /* numeric breadcrumb retained both ways */
#if EXPECT_OFLD_DELIVERED
 C(t->events[NST_ETHERNET]==eth+1);
 C(t->s.data_diag.rejected[NSDD_RX_ERROR]==err); /* not an error drop any more */
 printf("NORD2 OFLD-RX: candidate_routed=1 delivered=1 rx_error_drop_delta=0 breadcrumb=1\n");
#else
 C(t->events[NST_ETHERNET]==eth);
 C(t->s.data_diag.rejected[NSDD_RX_ERROR]==err+1); /* baseline: dropped at error gate */
 printf("NORD2 OFLD-RX: baseline_dropped=1 delivered=0 rx_error_drop_delta=1 breadcrumb=1\n");
#endif
 cleanup(t);

 /* 4. OFLD=3 but with a bad key index must STILL be rejected on the candidate
  *    (secure decode intact): flip stored PTK key_id so the frame mismatches. */
 ready(t);t->s.ptk.key_id=1;eth=t->events[NST_ETHERNET];
 unsigned keyrej=t->s.data_diag.rejected[NSDD_RX_KEY];
 uc_ccmp(t,p,4,1,0);deliver(t,0,p,sizeof p);step(t);
 C(t->events[NST_ETHERNET]==eth);
#if EXPECT_OFLD_DELIVERED
 C(t->s.data_diag.rejected[NSDD_RX_KEY]==keyrej+1); /* reached and rejected by key gate */
#else
 (void)keyrej; /* baseline rejects earlier at the error gate */
#endif
 cleanup(t);

 free(t);
 printf("PASS ofld-rx: clean-unicast delivered, ICV-error dropped, OFLD %s, key gate preserved (synthetic model, no hardware/DHCP claim)\n",
        EXPECT_OFLD_DELIVERED?"routed-through-secure-decode":"dropped-at-error-gate");
 return 0;
}
