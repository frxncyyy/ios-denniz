/* gfxall6: the tiler-heap bookkeeping of pan_jm.c (gpupipe1 rotation) -- the functions are EXTRACTED from pan_jm.c by
 * gpupipe-heap-host-test.sh (between the markers), compiled against stub types, and driven like backboardd drives them: batch slots
 * reused, each slot's descriptor pool handing out the SAME GPU address every time (pool reset), heaps rotating A/B, several batches open
 * at once. Checks: the heap returned at submit is the one the batch's tiler context was built with (never another), else NULL.
 * Also runs the gfxall5 ring (old) on the same sequence and reports how often it returned a wrong heap. */
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PAN_MAX_BATCHES 32
struct panfrost_bo { int id; };
struct panfrost_device { int x; };
struct panfrost_batch;
struct panfrost_context { struct { struct panfrost_batch *slots; } batches; };
struct panfrost_batch { struct panfrost_context *ctx; };
static struct panfrost_bo base_heap = { 0 };
static struct panfrost_bo *panfrost_device_tiler_heap(struct panfrost_device *d) { (void)d; return &base_heap; }
#define RW_PIPE_HEAPS 2u
#define RW_PIPE_DEVS 4u
static pthread_mutex_t rw_pipe_lock = PTHREAD_MUTEX_INITIALIZER;
static struct { struct panfrost_device *dev; struct panfrost_bo *heap[RW_PIPE_HEAPS]; unsigned next; } rw_pipe_dev[RW_PIPE_DEVS];
#define RW_PIPE_WARMUP 240u
#define PAN_BO_ACCESS_WRITE (1 << 1)
#define PAN_BO_ACCESS_VERTEX_TILER (1 << 2)
#define PAN_BO_ACCESS_FRAGMENT (1 << 3)
#define RW_ACC_TOUCH 1u
#define RW_ACC_WRITE 2u
#define PAN_BO_INVISIBLE 1
#define PAN_BO_GROWABLE 2
static int redwood_pipe_enabled(void) { return 1; }
static size_t panfrost_bo_size(struct panfrost_bo *b) { (void)b; return 1; }
static struct panfrost_bo *panfrost_bo_create(struct panfrost_device *d, size_t s, int f, const char *l) { (void)d; (void)s; (void)f; (void)l; return NULL; }
#include "heap_extract.inc"

/* gfxall5's ring, verbatim logic */
static struct { uint64_t desc; struct panfrost_bo *heap; } old_ring[64];
static unsigned old_pos;
static void old_note(uint64_t d, struct panfrost_bo *h) { unsigned k = old_pos++ % 64; old_ring[k].desc = d; old_ring[k].heap = h; }
static struct panfrost_bo *old_heap_of(uint64_t d) { struct panfrost_bo *h = NULL; for (unsigned i = 0; i < 64 && d; i++) if (old_ring[i].desc == d) h = old_ring[i].heap; return h; }

int main(void)
{
   (void)rw_pipe_pick_heap; (void)rw_pipe_acc;
   static struct panfrost_batch slots[2][PAN_MAX_BATCHES];
   static struct panfrost_context ctx[2];
   struct panfrost_bo heapA = { 1 }, heapB = { 2 };
   for (int c = 0; c < 2; c++) { ctx[c].batches.slots = slots[c]; for (int b = 0; b < PAN_MAX_BATCHES; b++) slots[c][b].ctx = &ctx[c]; }
   srand(11);
   unsigned n = 0, old_wrong = 0, old_null = 0, new_wrong = 0, new_null = 0;
   for (unsigned frame = 0; frame < 20000; frame++) {
      /* a commit: 1..40 passes over random batch slots of a random context, all open, then submitted in random order */
      int c = rand() % 2, np = 1 + rand() % 40;
      int bs[40]; struct panfrost_bo *made[40]; uint64_t desc[40];
      for (int i = 0; i < np; i++) {
         bs[i] = rand() % PAN_MAX_BATCHES;
         for (int j = 0; j < i; j++) if (bs[j] == bs[i]) { bs[i] = -1; break; }   /* a slot holds one batch */
         if (bs[i] < 0) continue;
         made[i] = (n++ & 1) ? &heapB : &heapA;                                      /* rotation */
         desc[i] = 0x100000000ull + (uint64_t)c * 0x10000000ull + (uint64_t)bs[i] * 0x10000ull;   /* the slot pool's same address */
         rw_pipe_note_desc(&slots[c][bs[i]], desc[i], made[i]);
         old_note(desc[i], made[i]);
      }
      for (int i = np - 1; i >= 0; i--) {
         if (bs[i] < 0) continue;
         struct panfrost_bo *h = rw_pipe_heap_of(&slots[c][bs[i]], desc[i]);
         struct panfrost_bo *o = old_heap_of(desc[i]);
         if (!h) new_null++; else if (h != made[i]) new_wrong++;
         if (!o) old_null++; else if (o != made[i]) old_wrong++;
      }
   }
   printf("passes %u: gfxall6 wrong %u unknown %u | gfxall5 ring wrong %u unknown %u\n", n, new_wrong, new_null, old_wrong, old_null);
   if (new_wrong || new_null) { printf("FAIL\n"); return 1; }
   if (!old_wrong) { printf("FAIL: the model does not reproduce the gfxall5 bug\n"); return 1; }
   printf("gpupipe_heap: ok\n");
   return 0;
}
