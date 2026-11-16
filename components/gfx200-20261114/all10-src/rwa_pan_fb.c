/*
 * y2s96 (rt1): the pieces of the framebuffer work that need Panfrost's own headers, compiled with pan_screen.c's command (build.sh)
 * and linked into libredwoodair.dylib like rwa_pressure_pan.c. Every function is called from air_metal.c under rwa_lock.
 *
 * B. MTLLoadActionDontCare / MTLStoreActionDontCare of colour attachments >= 1, honoured. Panfrost decides at SUBMIT whether a
 *    render target is preloaded ((draws & m) && valid bit of its level, pan_job.c panfrost_batch_to_fb_info) and whether it is
 *    stored (resolve & m); the valid bit is set again by the submit itself (emit_fragment_job). So:
 *    - preload: at a draw whose attachment i (>= 1) is DontCare-loaded, while nothing of this batch has touched slot i yet
 *      (draws|read|clear), the valid bit is cleared and remembered; a later draw of the SAME batch that Loads slot i puts it back
 *      (Metal would load what is in memory; so does the batch then); a batch that ends with no fragment job gets it back at once;
 *      after every flush any bit still clear is put back and counted as a VIOLATION (it must stay 0).
 *      Only one batch can write a resource at a time (panfrost_batch_write_rsrc flushes the other writer) and every reader
 *      flushes that writer first, so between the clear and our batch's submit nobody but our batch reads the bit.
 *    - store: at the LAST draw of a pass whose attachment i (>= 1) is DontCare-stored, slot i's resolve bit is cleared -- unless an
 *      earlier pass of the same batch asked for that slot to be stored (`keep`: the passes of one command buffer that share their
 *      attachments are one Panfrost batch here, and Apple's GPU would have stored that earlier pass's tile). Memory keeps what it
 *      held, which is exactly what Apple's tile GPU leaves behind for a DontCare store. A later draw of the batch that writes the
 *      slot sets resolve again (panfrost_set_batch_masks_blend runs at every draw: set_framebuffer_state dirties everything).
 *    Slots that share a resource and level with another attachment of the batch, array/3D/multisample targets and a batch whose
 *    history this file has not seen from its start (`unknown`) are left alone.
 * B+. (opt-in, deadstore) a Store of attachment >= 1 treated as DontCare for a resource that has never been sampled, blitted
 *    from, read back or Loaded in this process and is not an IOSurface/buffer-backed one; a later read of such a resource is
 *    counted (ext violations) -- the content it reads is the one before the discarded store.
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#include <string.h>
#include "pan_context.h"
#include "pan_job.h"
#include "pan_resource.h"

#define RWA_FB_B          1u   /* honour DontCare load/store of attachments >= 1 */
#define RWA_FB_LAST       2u   /* this draw is the last of its render pass */
#define RWA_FB_DEADSTORE  4u   /* (opt-in) Store of a never-read attachment >= 1 treated as DontCare */

#define RWA_FB_ZS         0x100u   /* gfx200 (internal): zs_dontcare below is meaningful */
#define RWA_G2_Z_DONTCARE 0x1000u
#define RWA_G2_S_DONTCARE 0x2000u

struct rwa_fb_pass {
   unsigned nr;                /* colour attachments */
   const unsigned *load;       /* MTLLoadAction of each (0 DontCare, 1 Load, 2 Clear), or NULL: all Load */
   const unsigned *store;      /* MTLStoreAction of each (0 DontCare, 1 Store, ...), or NULL: all Store */
   unsigned flags;             /* RWA_FB_* */
   unsigned zs_dontcare;       /* gfx200: RWA_G2_Z_DONTCARE | RWA_G2_S_DONTCARE: this pass's final depth / stencil store is DontCare */
};
void rwa_fb_zs_begin(struct pipe_context *pipe, const struct rwa_fb_pass *p);
void rwa_fb_zs_end(struct pipe_context *pipe, const struct rwa_fb_pass *p, uint64_t counts[2]);

void rwa_fb_begin(struct pipe_context *pipe, const struct rwa_fb_pass *p);
void rwa_fb_end(struct pipe_context *pipe, const struct rwa_fb_pass *p);
void rwa_fb_flushed(void);
void rwa_fb_note_read(struct pipe_resource *res);
void rwa_fb_note_shared(struct pipe_resource *res);
void rwa_fb_forget(struct pipe_resource *res, bool table);
void rwa_fb_counters(uint64_t out[16]);
void rwa_fb_count(unsigned which, unsigned n);

/* counters: 0 rt1 drawn (batch slots >= 1 drawn), 1 preload avoided, 2 store discarded, 3 restored by a later Load / no fragment
 * job, 4 VIOLATIONS (valid bit still clear after a flush), 5 ext store discarded, 6 ext violations, 7 single-target draws,
 * 8 single-target refused, 9 batches not seen from the start, 10 ext table full (ext off), 11 single-target clears */
static uint64_t fbc[16];

void
rwa_fb_count(unsigned which, unsigned n)
{
   if (which < 16)
      fbc[which] += n;
}

void
rwa_fb_counters(uint64_t out[16])
{
   memcpy(out, fbc, sizeof(fbc));
}

/* ---- per-resource flags for the opt-in extension: open addressing over the pipe_resource pointer ---- */
#define FBR_READ     1u
#define FBR_SHARED   2u
#define FBR_PENDING  4u   /* its last store was discarded by the extension and nothing has stored it since */
#define FBR_N        4096u
#define FBR_TOMB     ((struct pipe_resource *)1)
static struct { struct pipe_resource *res; unsigned flags; } fbr[FBR_N];
static unsigned fbr_used, fbr_tomb;
static bool fbr_full;   /* the table could not take an entry: the extension is off for this process */

static unsigned
fbr_hash(const struct pipe_resource *res)
{
   uintptr_t v = (uintptr_t)res >> 4;
   return (unsigned)((v ^ (v >> 13) ^ (v >> 27)) * 2654435761u) & (FBR_N - 1);
}

static int
fbr_find(const struct pipe_resource *res)
{
   for (unsigned i = fbr_hash(res), n = 0; n < FBR_N; i = (i + 1) & (FBR_N - 1), n++) {
      if (fbr[i].res == res)
         return (int)i;
      if (fbr[i].res == NULL)
         return -1;
   }
   return -1;
}

static void
fbr_rehash(void)
{
   static struct { struct pipe_resource *res; unsigned flags; } old[FBR_N];
   memcpy(old, fbr, sizeof(fbr));
   memset(fbr, 0, sizeof(fbr));
   fbr_used = fbr_tomb = 0;
   for (unsigned k = 0; k < FBR_N; k++) {
      if (old[k].res == NULL || old[k].res == FBR_TOMB)
         continue;
      unsigned i = fbr_hash(old[k].res);
      while (fbr[i].res != NULL)
         i = (i + 1) & (FBR_N - 1);
      fbr[i].res = old[k].res;
      fbr[i].flags = old[k].flags;
      fbr_used++;
   }
}

static int
fbr_get(struct pipe_resource *res)   /* find or add; -1 when full */
{
   int f = fbr_find(res);
   if (f >= 0)
      return f;
   if (fbr_tomb > FBR_N / 4)
      fbr_rehash();
   if (fbr_used + fbr_tomb + 1 > (FBR_N * 3) / 4) {
      if (!fbr_full)
         fbc[10]++;
      fbr_full = true;
      return -1;
   }
   unsigned i = fbr_hash(res);
   while (fbr[i].res != NULL && fbr[i].res != FBR_TOMB)
      i = (i + 1) & (FBR_N - 1);
   if (fbr[i].res == FBR_TOMB)
      fbr_tomb--;
   fbr[i].res = res;
   fbr[i].flags = 0;
   fbr_used++;
   return (int)i;
}

void
rwa_fb_note_read(struct pipe_resource *res)
{
   if (!res)
      return;
   int i = fbr_get(res);
   if (i < 0)
      return;
   if (fbr[i].flags & FBR_PENDING)
      fbc[6]++;
   fbr[i].flags = (fbr[i].flags | FBR_READ) & ~FBR_PENDING;
}

void
rwa_fb_note_shared(struct pipe_resource *res)
{
   int i = res ? fbr_get(res) : -1;
   if (i >= 0)
      fbr[i].flags |= FBR_SHARED;
}

/* a store of `res` may be treated as dead: never read, not shared, the table had room for it */
static bool
fbr_dead(struct pipe_resource *res)
{
   if (fbr_full)
      return false;
   int i = fbr_find(res);
   return i < 0 || !(fbr[i].flags & (FBR_READ | FBR_SHARED));
}

/* ---- per-batch state, by Panfrost batch slot ---- */
struct fb_batch {
   struct panfrost_batch *batch;
   bool live, unknown;
   uint32_t seen_draws;
   unsigned keep;          /* slots an earlier pass of this batch asked to be stored */
   unsigned skipped;       /* slots whose valid bit this file cleared and has not put back */
   unsigned counted;       /* slots counted in 'preload avoided' */
   unsigned drawn;         /* slots counted in 'rt1 drawn' */
   struct panfrost_resource *skip_rsrc[8];
   unsigned skip_level[8];
};
static struct fb_batch fbb[PAN_MAX_BATCHES];

static bool
eff_dontcare(unsigned st, struct pipe_resource *res, unsigned flags)
{
   if (st == 0)
      return true;
   return (flags & RWA_FB_DEADSTORE) && st == 1 && fbr_dead(res);
}

/* slot i's surface usable here: a single-sample, single-layer 2D level whose resource and level no other attachment shares */
static bool
slot_ok(const struct pipe_framebuffer_state *key, unsigned i)
{
   struct pipe_surface *s = key->cbufs[i];
   if (!s || !s->texture)
      return false;
   struct pipe_resource *r = s->texture;
   if ((r->target != PIPE_TEXTURE_2D && r->target != PIPE_TEXTURE_RECT) || r->array_size > 1 || r->depth0 > 1 ||
       r->nr_samples > 1 || s->u.tex.first_layer != s->u.tex.last_layer || s->u.tex.level >= PAN_MAX_MIP_LEVELS)
      return false;
   for (unsigned j = 0; j < key->nr_cbufs; j++) {
      if (j != i && key->cbufs[j] && key->cbufs[j]->texture == r && key->cbufs[j]->u.tex.level == s->u.tex.level)
         return false;
   }
   if (key->zsbuf && key->zsbuf->texture == r)
      return false;
   return true;
}

static void
put_back(struct fb_batch *e, unsigned i)
{
   if (!(e->skipped & (1u << i)))
      return;
   struct panfrost_resource *rsrc = e->skip_rsrc[i];
   if (rsrc)
      BITSET_SET(rsrc->valid.data, e->skip_level[i]);
   e->skipped &= ~(1u << i);
   e->skip_rsrc[i] = NULL;
   if (e->counted & (1u << i)) {
      e->counted &= ~(1u << i);
      fbc[1]--;
   }
   fbc[3]++;
}

static struct fb_batch *
entry_for(struct panfrost_context *ctx, struct panfrost_batch *batch)
{
   unsigned idx = (unsigned)(batch - ctx->batches.slots);
   return idx < PAN_MAX_BATCHES ? &fbb[idx] : NULL;
}

void
rwa_fb_begin(struct pipe_context *pipe, const struct rwa_fb_pass *p)
{
   if (!(p->flags & RWA_FB_B))
      return;   /* no-rt1 (or a driver without the fields): nothing of Panfrost's state is touched, not even looked at */
   struct panfrost_context *ctx = pan_context(pipe);
   struct panfrost_batch *batch = panfrost_get_batch_for_fbo(ctx);   /* what the clear / draw_vbo would get */
   if (!batch)
      return;
   struct fb_batch *e = entry_for(ctx, batch);
   if (!e)
      return;
   bool pristine = batch->draw_count == 0 && !batch->clear && !batch->draws && !batch->read && !batch->resolve;
   if (pristine || !e->live || e->batch != batch || batch->draw_count < e->seen_draws) {
      /* a batch not seen from its start keeps every store and skips no preload */
      if (e->live && e->skipped) {
         for (unsigned i = 0; i < 8; i++)
            put_back(e, i);
      }
      memset(e, 0, sizeof(*e));
      e->live = true;
      e->batch = batch;
      if (!pristine) {
         e->unknown = true;
         fbc[9]++;
      }
   }
   e->seen_draws = batch->draw_count;

   const struct pipe_framebuffer_state *key = &batch->key;
   unsigned nr = MIN2(p->nr, key->nr_cbufs);
   for (unsigned i = 1; i < nr && i < 8; i++) {
      struct pipe_surface *surf = key->cbufs[i];
      if (!surf)
         continue;
      unsigned bit = 1u << i, m = PIPE_CLEAR_COLOR0 << i;
      unsigned ld = p->load ? p->load[i] : 1u, st = p->store ? p->store[i] : 1u;

      if (ld == 1u) {
         rwa_fb_note_read(surf->texture);   /* a Load is a read of what memory holds */
         put_back(e, i);
      }
      if (!eff_dontcare(st, surf->texture, p->flags))
         e->keep |= bit;
      if (e->unknown || ld != 0u || (e->skipped & bit) ||
          ((batch->draws | batch->read | batch->clear) & m) || !slot_ok(key, i))
         continue;
      struct panfrost_resource *rsrc = pan_resource(surf->texture);
      unsigned level = surf->u.tex.level;
      if (!BITSET_TEST(rsrc->valid.data, level))
         continue;   /* nothing to preload anyway */
      BITSET_CLEAR(rsrc->valid.data, level);
      e->skipped |= bit;
      e->skip_rsrc[i] = rsrc;
      e->skip_level[i] = level;
   }
}

void
rwa_fb_end(struct pipe_context *pipe, const struct rwa_fb_pass *p)
{
   if (!(p->flags & RWA_FB_B))
      return;
   struct panfrost_context *ctx = pan_context(pipe);
   struct panfrost_batch *batch = ctx->batch;
   if (!batch)
      return;
   struct fb_batch *e = entry_for(ctx, batch);
   if (!e || !e->live || e->batch != batch)
      return;
   e->seen_draws = batch->draw_count;
   if (!panfrost_has_fragment_job(batch)) {
      /* this batch would be dropped without a fragment job, and the submit would not set the bits again */
      for (unsigned i = 0; i < 8; i++)
         put_back(e, i);
      return;
   }
   const struct pipe_framebuffer_state *key = &batch->key;
   unsigned nr = MIN2(p->nr, key->nr_cbufs);
   for (unsigned i = 1; i < nr && i < 8; i++) {
      struct pipe_surface *surf = key->cbufs[i];
      if (!surf)
         continue;
      unsigned bit = 1u << i, m = PIPE_CLEAR_COLOR0 << i;
      unsigned st = p->store ? p->store[i] : 1u;

      if ((batch->draws & m) && !(e->drawn & bit)) {
         e->drawn |= bit;
         fbc[0]++;
         if ((e->skipped & bit) && !(e->counted & bit)) {
            e->counted |= bit;
            fbc[1]++;
         }
      }
      if (!(p->flags & RWA_FB_LAST))
         continue;
      bool discard = (p->flags & RWA_FB_B) && !e->unknown && !(e->keep & bit) && (batch->resolve & m) &&
                     eff_dontcare(st, surf->texture, p->flags) && slot_ok(key, i);
      if (discard) {
         batch->resolve &= ~m;
         fbc[2]++;
         if (st != 0u) {
            fbc[5]++;
            int k = fbr_get(surf->texture);
            if (k >= 0)
               fbr[k].flags |= FBR_PENDING;
         }
      } else if (batch->resolve & m) {
         int k = fbr_find(surf->texture);
         if (k >= 0)
            fbr[k].flags &= ~FBR_PENDING;   /* stored after all */
      }
   }
}

/* after pipe->flush: every batch has been submitted, and each submit with a fragment job set its targets' valid bits again */
void
rwa_fb_flushed(void)
{
   for (unsigned b = 0; b < PAN_MAX_BATCHES; b++) {
      struct fb_batch *e = &fbb[b];
      if (!e->live)
         continue;
      for (unsigned i = 0; i < 8; i++) {
         if (!(e->skipped & (1u << i)) || !e->skip_rsrc[i])
            continue;
         if (!BITSET_TEST(e->skip_rsrc[i]->valid.data, e->skip_level[i])) {
            BITSET_SET(e->skip_rsrc[i]->valid.data, e->skip_level[i]);
            fbc[4]++;
         }
      }
      memset(e, 0, sizeof(*e));
   }
}

/* a texture over `res` is being destroyed: nothing here may keep its pointer; a bit cleared on it is put back first */
void
rwa_fb_forget(struct pipe_resource *res, bool table)
{
   for (unsigned b = 0; b < PAN_MAX_BATCHES; b++) {
      struct fb_batch *e = &fbb[b];
      if (!e->live)
         continue;
      for (unsigned i = 0; i < 8; i++) {
         if ((e->skipped & (1u << i)) && e->skip_rsrc[i] == pan_resource(res))
            put_back(e, i);
      }
   }
   int k = table ? fbr_find(res) : -1;   /* a view's destruction leaves the resource's flags to its other owners */
   if (k >= 0) {
      fbr[k].res = FBR_TOMB;
      fbr[k].flags = 0;
      fbr_used--;
      fbr_tomb++;
   }
}

/*
 * gfx200: MTLStoreActionDontCare of the DEPTH / STENCIL attachment, honoured. Panfrost writes the depth (and the separate stencil)
 * back at the end of a fragment job whenever a clear or a draw touched it (batch->resolve, pan_job.c zs.discard), which for
 * Metal's memoryless / DontCare depth of a full-screen multisample pass is tens of MiB of writes a frame that nothing reads.
 * At the LAST draw of a pass whose final depth (stencil) store action is DontCare, the batch's resolve bit is cleared -- unless
 * an earlier pass of the same batch asked for that aspect to be stored (keep), or the batch was not seen from its start
 * (unknown: something else drew or cleared into it first). A later draw of the batch that writes the aspect sets the bit again
 * (panfrost_set_batch_masks_zs runs at every draw here), which only ever keeps more. counts[0]: discarded, counts[1]: refused.
 */
struct zs_batch {
   struct panfrost_batch *batch;
   bool live, unknown;
   uint32_t seen;
   unsigned keep;   /* RWA_G2_*_DONTCARE bits of aspects an earlier pass of this batch stored */
};
static struct zs_batch zsb[PAN_MAX_BATCHES];

void
rwa_fb_zs_begin(struct pipe_context *pipe, const struct rwa_fb_pass *p)
{
   if (!(p->flags & RWA_FB_ZS))
      return;
   struct panfrost_context *ctx = pan_context(pipe);
   struct panfrost_batch *batch = panfrost_get_batch_for_fbo(ctx);
   if (!batch || !batch->key.zsbuf)
      return;
   unsigned idx = (unsigned)(batch - ctx->batches.slots);
   if (idx >= PAN_MAX_BATCHES)
      return;
   struct zs_batch *e = &zsb[idx];
   bool pristine = batch->draw_count == 0 && !batch->clear && !batch->draws && !batch->read && !batch->resolve;
   if (pristine || !e->live || e->batch != batch || batch->draw_count < e->seen) {
      memset(e, 0, sizeof(*e));
      e->live = true;
      e->batch = batch;
      e->unknown = !pristine;
   }
   e->seen = batch->draw_count;
}

void
rwa_fb_zs_end(struct pipe_context *pipe, const struct rwa_fb_pass *p, uint64_t counts[2])
{
   if (!(p->flags & RWA_FB_ZS) || !(p->flags & RWA_FB_LAST))
      return;
   struct panfrost_context *ctx = pan_context(pipe);
   struct panfrost_batch *batch = ctx->batch;
   if (!batch || !batch->key.zsbuf)
      return;
   unsigned idx = (unsigned)(batch - ctx->batches.slots);
   if (idx >= PAN_MAX_BATCHES)
      return;
   struct zs_batch *e = &zsb[idx];
   if (!e->live || e->batch != batch)
      return;
   e->seen = batch->draw_count;
   const unsigned both = RWA_G2_Z_DONTCARE | RWA_G2_S_DONTCARE;
   e->keep |= ~p->zs_dontcare & both;   /* this pass's own stores count for the passes after it */
   unsigned want = p->zs_dontcare & both & ~e->keep, m = 0;
   if (want & RWA_G2_Z_DONTCARE)
      m |= PIPE_CLEAR_DEPTH;
   if (want & RWA_G2_S_DONTCARE)
      m |= PIPE_CLEAR_STENCIL;
   if (!(batch->resolve & m))
      return;
   if (e->unknown || !panfrost_has_fragment_job(batch)) {
      counts[1]++;
      return;
   }
   batch->resolve &= ~m;
   counts[0]++;
}
