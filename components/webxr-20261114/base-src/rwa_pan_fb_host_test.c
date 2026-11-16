/*
 * y2s96: rwa_pan_fb.c against a model of Panfrost's batch life (pan_job.c): draws/clears set the masks the way
 * panfrost_set_batch_masks_blend / panfrost_clear do, and the model's submit decides preload and store exactly as
 * panfrost_batch_to_fb_info does, then sets the valid bits as emit_fragment_job does. Host only (rwa-pan-fb-host-test.sh).
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pan_context.h"
#include "pan_job.h"
#include "pan_resource.h"

#define B 1u
#define LAST 2u
#define DEAD 4u
struct rwa_fb_pass { unsigned nr; const unsigned *load, *store; unsigned flags; };
void rwa_fb_begin(struct pipe_context *pipe, const struct rwa_fb_pass *p);
void rwa_fb_end(struct pipe_context *pipe, const struct rwa_fb_pass *p);
void rwa_fb_flushed(void);
void rwa_fb_note_read(struct pipe_resource *res);
void rwa_fb_note_shared(struct pipe_resource *res);
void rwa_fb_forget(struct pipe_resource *res, bool table);
void rwa_fb_counters(uint64_t out[16]);

static struct panfrost_context *C;
static struct pipe_framebuffer_state cur;

/* the stub: one batch per framebuffer key, slot = first free / matching (pan_job.c panfrost_get_batch) */
struct panfrost_batch *
panfrost_get_batch_for_fbo(struct panfrost_context *ctx)
{
   if (ctx->batch)
      return ctx->batch;
   for (unsigned i = 0; i < PAN_MAX_BATCHES; i++) {
      struct panfrost_batch *b = &ctx->batches.slots[i];
      if (b->seqnum && !memcmp(b->key.cbufs, cur.cbufs, sizeof(cur.cbufs)) && b->key.nr_cbufs == cur.nr_cbufs)
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
static unsigned preloaded, stored;   /* the model's submit: slot 1 of the last submitted batch */

static void
setfb(int a0, int a1)
{
   memset(&cur, 0, sizeof(cur));
   cur.nr_cbufs = 2;
   cur.cbufs[0] = &S[a0];
   cur.cbufs[1] = &S[a1];
   C->batch = NULL;
}

/* one draw: writes slot 1 or not; flags; load/store of slot 1 (slot 0 Load/Store) */
static void
draw(unsigned ld1, unsigned st1, bool writes1, unsigned flags, bool frag)
{
   unsigned ld[2] = { 1, ld1 }, st[2] = { 1, st1 };
   struct rwa_fb_pass p = { 2, ld, st, flags };
   rwa_fb_begin(&C->base, &p);
   struct panfrost_batch *b = panfrost_get_batch_for_fbo(C);
   if (ld1 == 2)
      b->clear |= PIPE_CLEAR_COLOR1;
   if (frag) {
      b->draws |= PIPE_CLEAR_COLOR0;
      b->resolve |= PIPE_CLEAR_COLOR0;
      if (writes1) {
         b->draws |= PIPE_CLEAR_COLOR1;
         b->resolve |= PIPE_CLEAR_COLOR1;
      }
      b->draw_count++;
   }
   rwa_fb_end(&C->base, &p);
}

static void
flush(void)
{
   for (unsigned i = 0; i < PAN_MAX_BATCHES; i++) {
      struct panfrost_batch *b = &C->batches.slots[i];
      if (!b->seqnum)
         continue;
      if (b->draw_count || b->clear) {
         struct panfrost_resource *r = pan_resource(b->key.cbufs[1]->texture);
         unsigned m = PIPE_CLEAR_COLOR1;
         preloaded = !(b->clear & m) && ((b->read & m) || ((b->draws & m) && BITSET_TEST(r->valid.data, 0)));
         stored = !!(b->resolve & m);
         for (unsigned k = 0; k < b->key.nr_cbufs; k++)
            BITSET_SET(pan_resource(b->key.cbufs[k]->texture)->valid.data, b->key.cbufs[k]->u.tex.level);
      }
      b->seqnum = 0;
   }
   C->batch = NULL;
   rwa_fb_flushed();
}

static uint64_t
ctr(unsigned i)
{
   uint64_t c[16];
   rwa_fb_counters(c);
   return c[i];
}

#define CHECK(cond, what) do { if (!(cond)) { printf("FAIL %s\n", what); exit(1); } } while (0)

int
main(void)
{
   C = calloc(1, sizeof(*C));
   for (int i = 0; i < 4; i++) {
      R[i].base.target = PIPE_TEXTURE_2D;
      R[i].base.array_size = 1;
      R[i].base.depth0 = 1;
      R[i].base.nr_samples = 1;
      BITSET_SET(R[i].valid.data, 0);
      S[i].texture = &R[i].base;
   }
   /* S1: DontCare load, Store, slot 1 drawn: no preload, stored, valid back after the flush */
   setfb(0, 1); draw(0, 1, true, B | LAST, true); flush();
   CHECK(!preloaded && stored && BITSET_TEST(R[1].valid.data, 0), "S1 dontcare-load store");
   CHECK(ctr(1) == 1 && ctr(4) == 0, "S1 counters");
   /* S2: DontCare load and store: neither */
   setfb(0, 1); draw(0, 0, true, B | LAST, true); flush();
   CHECK(!preloaded && !stored && ctr(2) == 1, "S2 dontcare both");
   /* S3: P1 Load+Store writes slot 1, P2 (same batch) DontCare store: kept (Apple stored P1's tile) and preloaded */
   setfb(0, 1); draw(1, 1, true, B | LAST, true); setfb(0, 1); draw(0, 0, false, B | LAST, true); flush();
   CHECK(preloaded && stored && ctr(2) == 1, "S3 keep earlier store");
   /* S4: P1 DontCare (nothing of slot 1 drawn), P2 Load draws slot 1: the bit is put back, so preloaded */
   setfb(0, 1); draw(0, 1, false, B | LAST, true); setfb(0, 1); draw(1, 1, true, B | LAST, true); flush();
   CHECK(preloaded && stored && ctr(3) >= 1, "S4 later Load restores");
   /* S5: a draw that leaves no fragment job: the bit comes back at once */
   setfb(0, 1); draw(0, 1, true, B | LAST, false);
   CHECK(BITSET_TEST(R[1].valid.data, 0), "S5 no fragment job");
   flush();
   /* S6: a batch already started without this file (draws seen first): untouched */
   setfb(0, 1);
   { struct panfrost_batch *b = panfrost_get_batch_for_fbo(C); b->draws = PIPE_CLEAR_COLOR0; b->draw_count = 1; }
   uint64_t d2 = ctr(2);
   C->batch = NULL; draw(0, 0, true, B | LAST, true); flush();
   CHECK(preloaded && stored && ctr(2) == d2 && ctr(9) >= 1, "S6 unknown batch");
   /* S7: deadstore: a never-read texture's Store discarded; a later read counted */
   setfb(0, 2); draw(0, 1, true, B | DEAD | LAST, true); flush();
   CHECK(!stored && ctr(5) == 1, "S7 deadstore discard");
   rwa_fb_note_read(&R[2].base);
   CHECK(ctr(6) == 1, "S7 ext violation counted");
   setfb(0, 2); draw(0, 1, true, B | DEAD | LAST, true); flush();
   CHECK(stored && ctr(5) == 1, "S7 read texture keeps its store");
   /* shared (IOSurface) is never dead */
   rwa_fb_note_shared(&R[3].base);
   setfb(0, 3); draw(0, 1, true, B | DEAD | LAST, true); flush();
   CHECK(stored, "S7 shared keeps its store");
   /* S8: destroyed while its bit is cleared: put back */
   setfb(0, 1); draw(0, 1, true, B, true);
   CHECK(!BITSET_TEST(R[1].valid.data, 0), "S8 cleared");
   rwa_fb_forget(&R[1].base, true);
   CHECK(BITSET_TEST(R[1].valid.data, 0), "S8 forget restores");
   flush();
   /* S9: B off: Panfrost's state untouched */
   setfb(0, 1); draw(0, 0, true, LAST, true); flush();
   CHECK(preloaded && stored, "S9 off");
   /* S10: slot 1 the same resource and level as slot 0: left alone */
   setfb(0, 0); draw(0, 0, true, B | LAST, true); flush();
   CHECK(preloaded && stored, "S10 shared level");
   CHECK(ctr(4) == 0, "violations 0");
   printf("rwa_pan_fb: ok (preload avoided %llu, store discarded %llu, restored %llu, violations %llu)\n",
          (unsigned long long)ctr(1), (unsigned long long)ctr(2), (unsigned long long)ctr(3), (unsigned long long)ctr(4));
   return 0;
}
