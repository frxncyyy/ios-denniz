/* gfxall7: rw_pipe_acc (EXTRACTED from pan_jm.c by gpupipe-acc-host-test.sh) + a model of the kernel's 0632 overtake rule
 * (nord2_gpu_core.c jobs_conflict/may_overtake, by buffer identity) over CoreAnimation-like pass sequences: a pass whose fragment stage
 * samples what the previous pass's fragment stage wrote must never overtake it; independent passes still may. Prints 'gpupipe_acc: ok'. */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#define PAN_BO_ACCESS_READ (1 << 0)
#define PAN_BO_ACCESS_WRITE (1 << 1)
#define PAN_BO_ACCESS_VERTEX_TILER (1 << 2)
#define PAN_BO_ACCESS_FRAGMENT (1 << 3)
#define RW_ACC_TOUCH 1u
#define RW_ACC_WRITE 2u
#include "acc_extract.inc"
#define NB 8
struct pass { uint32_t flags[NB]; };   /* per buffer id 0..NB-1: Mesa's batch access flags (0 = not in the batch) */
static void set_of(const struct pass *p, unsigned stage, uint8_t acc[NB])
{
   for (int b = 0; b < NB; b++) acc[b] = p->flags[b] ? rw_pipe_acc(p->flags[b], stage) : 0;
}
static bool conflict(const uint8_t a[NB], const uint8_t b[NB])
{
   for (int i = 0; i < NB; i++)
      if ((a[i] & RW_ACC_TOUCH) && (b[i] & RW_ACC_TOUCH) && ((a[i] | b[i]) & RW_ACC_WRITE)) return true;
   return false;
}
int main(void)
{
   const uint32_t R = PAN_BO_ACCESS_READ, W = PAN_BO_ACCESS_WRITE, V = PAN_BO_ACCESS_VERTEX_TILER, F = PAN_BO_ACCESS_FRAGMENT;
   /* truth table */
   if (rw_pipe_acc(F | R, V) != RW_ACC_TOUCH) { puts("FAIL vt chain must touch a fragment-read texture"); return 1; }
   if (rw_pipe_acc(F | W, V) != (RW_ACC_TOUCH | RW_ACC_WRITE)) { puts("FAIL vt chain must declare the fragment-written target"); return 1; }
   if (rw_pipe_acc(V | R, F) != 0) { puts("FAIL fragment chain keeps its own stage"); return 1; }
   if (rw_pipe_acc(F | R, F) != RW_ACC_TOUCH || rw_pipe_acc(R, F) != RW_ACC_TOUCH) { puts("FAIL fragment chain set"); return 1; }
   /* backdrop chain: pass k writes T (fragment), pass k+1 samples T (fragment): VT(k+1) vs FRAG(k) must conflict */
   unsigned dep_ok = 0, indep_ok = 0, trials = 0;
   srand(5);
   for (int t = 0; t < 200000; t++) {
      struct pass k = { { 0 } }, k1 = { { 0 } };
      for (int b = 0; b < NB; b++) {
         int r = rand() % 6;
         k.flags[b] = r == 0 ? 0 : r == 1 ? (F | W) : r == 2 ? (F | R) : r == 3 ? (V | R) : r == 4 ? (V | W) : (V | F | R);
         r = rand() % 6;
         k1.flags[b] = r == 0 ? 0 : r == 1 ? (F | W) : r == 2 ? (F | R) : r == 3 ? (V | R) : r == 4 ? (V | W) : (V | F | R);
      }
      uint8_t vt1[NB], fr0[NB];
      set_of(&k1, PAN_BO_ACCESS_VERTEX_TILER, vt1);
      set_of(&k, PAN_BO_ACCESS_FRAGMENT, fr0);
      /* ground truth: any buffer pass k's fragment writes that pass k+1 uses at all (any stage), or that pass k's fragment uses and pass
       * k+1 writes in any stage */
      bool dep = false;
      for (int b = 0; b < NB; b++) {
         bool k_fw = (k.flags[b] & F) && (k.flags[b] & W), k_fu = (k.flags[b] & F) || (k.flags[b] && !(k.flags[b] & (V | F)));
         if ((k_fw && k1.flags[b]) || (k_fu && (k1.flags[b] & W))) dep = true;
      }
      bool c = conflict(vt1, fr0);
      trials++;
      if (dep && !c) { printf("FAIL trial %d: a dependent pass would overtake\n", t); return 1; }
      if (dep) dep_ok++; else if (!c) indep_ok++;
   }
   printf("gpupipe_acc: ok (%u trials: %u dependent pairs all serialised, %u independent pairs still overlap)\n", trials, dep_ok, indep_ok);
   return 0;
}
