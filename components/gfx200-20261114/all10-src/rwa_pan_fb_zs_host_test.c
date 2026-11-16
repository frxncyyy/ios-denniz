/*
 * gfx200 (zsdc): rwa_pan_fb.c's rwa_fb_zs_begin / rwa_fb_zs_end against the same model of Panfrost's batch life as
 * rwa_pan_fb_host_test.c (draws and clears set batch->draws/clear/resolve as panfrost_set_batch_masks_zs / panfrost_clear do; the
 * model's submit writes the depth back exactly when pan_job.c panfrost_batch_to_fb_info would: resolve & PIPE_CLEAR_DEPTH).
 * Host only (rwa-pan-fb-zs-host-test.sh). Prints 'rwa_pan_fb zs: ok'.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pan_context.h"
#include "pan_job.h"
#include "pan_resource.h"

#define LAST 2u
#define ZS   0x100u
#define ZDC  0x1000u
#define SDC  0x2000u
struct rwa_fb_pass { unsigned nr; const unsigned *load, *store; unsigned flags; unsigned zs_dontcare; };
void rwa_fb_zs_begin(struct pipe_context *pipe, const struct rwa_fb_pass *p);
void rwa_fb_zs_end(struct pipe_context *pipe, const struct rwa_fb_pass *p, uint64_t counts[2]);

static struct panfrost_context *C;
static struct pipe_framebuffer_state cur;

struct panfrost_batch *
panfrost_get_batch_for_fbo(struct panfrost_context *ctx)
{
   if (ctx->batch)
      return ctx->batch;
   for (unsigned i = 0; i < PAN_MAX_BATCHES; i++) {
      struct panfrost_batch *b = &ctx->batches.slots[i];
      if (b->seqnum && !memcmp(b->key.cbufs, cur.cbufs, sizeof(cur.cbufs)) && b->key.zsbuf == cur.zsbuf)
         return ctx->batch = b;
   }
   for (unsigned i = 0; i < PAN_MAX_BATCHES; i++) {
      struct panfrost_batch *b = &ctx->batches.slots[i];
      if (!b->seqnum) {
         memset(b, 0, sizeof(*b));
         b->seqnum = 1;
         b->key = cur;
         return ctx->batch = b;
      }
   }
   abort();
}

static struct panfrost_resource R[4];
static struct pipe_surface S[4];
static int stored_z, stored_s;
static uint64_t cnt[2];

static void
setfb(int c0, int zs)
{
   memset(&cur, 0, sizeof(cur));
   cur.nr_cbufs = 1;
   cur.cbufs[0] = &S[c0];
   cur.zsbuf = zs >= 0 ? &S[zs] : NULL;
   C->batch = NULL;
}

/* one draw of a pass: clears depth (first draw) or not, writes depth/stencil or not, last or not, the pass's dontcare bits */
static void
draw(bool clear, bool writes_z, bool writes_s, bool last, unsigned dontcare, unsigned flags, bool frag)
{
   struct rwa_fb_pass p = { 1, NULL, NULL, flags | (last ? LAST : 0u), dontcare };
   rwa_fb_zs_begin(&C->base, &p);
   struct panfrost_batch *b = panfrost_get_batch_for_fbo(C);
   if (clear) {
      b->clear |= PIPE_CLEAR_DEPTH | PIPE_CLEAR_STENCIL;
      b->resolve |= PIPE_CLEAR_DEPTH | PIPE_CLEAR_STENCIL;
   }
   if (frag) {
      b->draws |= PIPE_CLEAR_COLOR0;
      b->resolve |= PIPE_CLEAR_COLOR0;
      if (writes_z) {
         b->draws |= PIPE_CLEAR_DEPTH;
         b->resolve |= PIPE_CLEAR_DEPTH;
      }
      if (writes_s) {
         b->draws |= PIPE_CLEAR_STENCIL;
         b->resolve |= PIPE_CLEAR_STENCIL;
      }
      b->draw_count++;
   }
   rwa_fb_zs_end(&C->base, &p, cnt);
}

static void
flush(void)
{
   stored_z = stored_s = -1;
   for (unsigned i = 0; i < PAN_MAX_BATCHES; i++) {
      struct panfrost_batch *b = &C->batches.slots[i];
      if (!b->seqnum)
         continue;
      if (b->draw_count || b->clear) {
         stored_z = !!(b->resolve & PIPE_CLEAR_DEPTH);
         stored_s = !!(b->resolve & PIPE_CLEAR_STENCIL);
         assert(b->resolve & PIPE_CLEAR_COLOR0);   /* colour never touched by this */
      }
      b->seqnum = 0;
   }
   C->batch = NULL;
}

#define CHECK(cond, what) do { if (!(cond)) { printf("FAIL %s (z %d s %d, discarded %llu refused %llu)\n", what, stored_z, stored_s, \
   (unsigned long long)cnt[0], (unsigned long long)cnt[1]); exit(1); } } while (0)

int
main(void)
{
   C = calloc(1, sizeof(*C));
   for (int i = 0; i < 4; i++) {
      R[i].base.target = PIPE_TEXTURE_2D;
      R[i].base.array_size = 1;
      R[i].base.depth0 = 1;
      R[i].base.nr_samples = 4;
      S[i].texture = &R[i].base;
   }
   /* Z1: memoryless-style pass: clear, draws, DontCare both: nothing written back */
   setfb(0, 1); draw(true, true, true, false, ZDC | SDC, ZS, true); draw(false, true, false, true, ZDC | SDC, ZS, true); flush();
   CHECK(stored_z == 0 && stored_s == 0 && cnt[0] == 1, "Z1 dontcare both");
   /* Z2: Store: kept */
   setfb(0, 1); draw(true, true, true, true, 0, ZS, true); flush();
   CHECK(stored_z == 1 && stored_s == 1 && cnt[0] == 1, "Z2 store");
   /* Z3: P1 Store, P2 of the same batch DontCare: kept (Apple stored P1's tile) */
   setfb(0, 1); draw(true, true, false, true, 0, ZS, true); setfb(0, 1); draw(false, true, false, true, ZDC | SDC, ZS, true); flush();
   CHECK(stored_z == 1 && stored_s == 1 && cnt[0] == 1, "Z3 earlier store kept");
   /* Z4: P1 DontCare, P2 of the same batch writes depth and Stores: stored (the draw set the bit again) */
   setfb(0, 1); draw(true, true, false, true, ZDC | SDC, ZS, true); setfb(0, 1); draw(false, true, true, true, 0, ZS, true); flush();
   CHECK(stored_z == 1 && stored_s == 1 && cnt[0] == 2, "Z4 later store wins");
   /* Z5: a batch begun without this file (a clear-only pass first): untouched, refusal counted */
   setfb(0, 1);
   { struct panfrost_batch *b = panfrost_get_batch_for_fbo(C); b->clear = PIPE_CLEAR_DEPTH; b->resolve = PIPE_CLEAR_DEPTH; }
   C->batch = NULL; draw(false, true, false, true, ZDC | SDC, ZS, true); flush();
   CHECK(stored_z == 1 && cnt[1] == 1, "Z5 unknown batch");
   /* Z6: without the flag (driver without gfx200, or no-gfx200-zsdc): untouched */
   setfb(0, 1); draw(true, true, true, true, ZDC | SDC, 0, true); flush();
   CHECK(stored_z == 1 && stored_s == 1, "Z6 off");
   /* Z7: not the last draw: untouched until the last */
   setfb(0, 1); draw(true, true, false, false, ZDC, ZS, true);
   CHECK(C->batch->resolve & PIPE_CLEAR_DEPTH, "Z7 before last");
   flush();
   CHECK(stored_z == 1, "Z7 no last draw: stored");
   /* Z8: depth DontCare, stencil Store: only depth dropped */
   setfb(0, 2); draw(true, true, true, true, ZDC, ZS, true); flush();
   CHECK(stored_z == 0 && stored_s == 1, "Z8 split aspects");
   /* Z9: no fragment job (a pass with nothing drawn and no clear): nothing to drop */
   uint64_t d0 = cnt[0];
   setfb(0, 3); draw(false, false, false, true, ZDC | SDC, ZS, false); flush();
   CHECK(cnt[0] == d0, "Z9 no fragment job");
   /* Z10: no depth attachment: nothing touched, nothing counted */
   setfb(0, -1); draw(false, false, false, true, ZDC | SDC, ZS, true); flush();
   CHECK(cnt[0] == d0, "Z10 no zsbuf");
   /* Z11: two different batches (other colour target) sharing the depth: each judged on its own */
   setfb(0, 1); draw(true, true, false, true, 0, ZS, true); setfb(2, 1); draw(true, true, false, true, ZDC | SDC, ZS, true);
   {
      int z[2] = { -1, -1 }, k = 0;
      for (unsigned i = 0; i < PAN_MAX_BATCHES; i++) {
         struct panfrost_batch *b = &C->batches.slots[i];
         if (b->seqnum && k < 2)
            z[k++] = !!(b->resolve & PIPE_CLEAR_DEPTH);
         b->seqnum = 0;
      }
      C->batch = NULL;
      CHECK(k == 2 && z[0] == 1 && z[1] == 0, "Z11 per batch");
   }
   printf("rwa_pan_fb zs: ok (discarded %llu, refused %llu)\n", (unsigned long long)cnt[0], (unsigned long long)cnt[1]);
   return 0;
}
