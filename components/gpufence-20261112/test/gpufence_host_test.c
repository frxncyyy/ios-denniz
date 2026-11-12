/* 0395 host test: nord2_gpu_fence.h pure logic against a mock GPU job queue and a brute-force reference.
 * cc -std=c11 -O2 -Wall -Wextra -Werror -I../b/iokit/Kernel/arm gpufence_host_test.c -o gft && ./gft */
#define NORD2_GPU_FENCE_IMPL
#include "nord2_gpu_fence.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXJ 128u            /* N2C_MAX_JOBS */
#define JBOS 32u             /* N2C_JOB_BOS */
enum { FREE, QUEUED, RUNNING, STOPPING, DONE, FAULTED, CANCELLED, RESET };
struct mjob { uint64_t id; int state; unsigned n; uint32_t sid[JBOS]; uint32_t wmask; uint64_t done_at; };
struct mcore { struct mjob j[MAXJ]; uint64_t next_id; };
/* Reference: newest writer id of every surface id ever submitted. */
static uint64_t refmax[1024];
static unsigned nh;

static bool m_unfinished(void *ctx, unsigned i, uint64_t *id)
{
        struct mjob *j = &((struct mcore *)ctx)->j[i];
        if (j->state != QUEUED && j->state != RUNNING && j->state != STOPPING) return false;
        *id = j->id; return true;
}
static bool m_writes(void *ctx, unsigned i, uint32_t sid)
{
        struct mjob *j = &((struct mcore *)ctx)->j[i];
        for (unsigned k = 0; k < j->n; k++) if (((j->wmask >> k) & 1u) && j->sid[k] == sid) return true;
        return false;
}
static struct n2gf_jobs view(struct mcore *c) { struct n2gf_jobs v = { c, MAXJ, m_unfinished, m_writes }; return v; }

static unsigned fails;
#define CHECK(c, ...) do { if (!(c)) { if (fails++ < 20) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static uint64_t rng = 88172645463325252ull;
static uint64_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }

/* Submit like Nord2GPU: core assigns the id, then fence_record() walks the write-masked handles. sid 0 = not an IOSurface import. */
static int submit(struct mcore *c, struct n2gf_table *t, unsigned n, const uint32_t *sid, uint32_t wmask)
{
        struct mjob *j = NULL;
        for (unsigned i = 0; i < MAXJ; i++) if (c->j[i].state == FREE) { j = &c->j[i]; break; }
        if (!j) return -1;
        memset(j, 0, sizeof(*j));
        j->id = c->next_id++; j->n = n; j->wmask = wmask; memcpy(j->sid, sid, n * sizeof(*sid)); j->state = QUEUED;
        nh++; for (unsigned k = 0; k < n; k++) if (((wmask >> k) & 1u) && sid[k]) refmax[sid[k]] = j->id;
        struct n2gf_jobs v = view(c);
        for (unsigned k = 0; k < n; k++) if ((wmask >> k) & 1u) n2gf_record(t, &v, c->next_id, sid[k], j->id);
        return (int)(j - c->j);
}
/* Reference answers. */
static uint64_t ref_max_writer(uint32_t sid)
{
        return refmax[sid];
}
static bool ref_pending(struct mcore *c, uint32_t sid, uint64_t upto, uint64_t *maxid)
{
        bool any = false; uint64_t m = 0;
        for (unsigned i = 0; i < MAXJ; i++) {
                struct mjob *j = &c->j[i];
                if (j->state < QUEUED || j->state > STOPPING || j->id > upto) continue;
                for (unsigned k = 0; k < j->n; k++) if (((j->wmask >> k) & 1u) && j->sid[k] == sid) { any = true; if (j->id > m) m = j->id; }
        }
        if (maxid) *maxid = m;
        return any;
}
static uint64_t ref_low(struct mcore *c)
{
        uint64_t lo = c->next_id - 1;
        for (unsigned i = 0; i < MAXJ; i++) if (c->j[i].state >= QUEUED && c->j[i].state <= STOPPING && c->j[i].id - 1 < lo) lo = c->j[i].id - 1;
        return lo;
}

/* Random workload: nsurf distinct surface ids, out-of-order completion (several job slots/contexts), reaping. */
static void random_run(unsigned nsurf, unsigned steps, bool exact_expected)
{
        static struct mcore c; static struct n2gf_table t;
        memset(&c, 0, sizeof(c)); memset(&t, 0, sizeof(t)); c.next_id = 1; nh = 0; memset(refmax, 0, sizeof(refmax));
        uint64_t last_low = 0;
        for (unsigned s = 0; s < steps; s++) {
                unsigned op = (unsigned)(rnd() % 10);
                if (op < 4) {
                        unsigned n = 1 + (unsigned)(rnd() % 6); uint32_t sid[JBOS]; uint32_t wm = 0;
                        for (unsigned k = 0; k < n; k++) {
                                uint32_t cand;
                                bool dup;
                                do { cand = (rnd() % 5 == 0) ? 0 : 1 + (uint32_t)(rnd() % nsurf); dup = false;
                                     for (unsigned q = 0; q < k; q++) if (cand && sid[q] == cand) dup = true; } while (dup);
                                sid[k] = cand; if (rnd() % 3) wm |= 1u << k;
                        }
                        (void)submit(&c, &t, n, sid, wm);
                } else if (op < 8) {            /* finish a random unfinished job (any state transition order) */
                        unsigned i = (unsigned)(rnd() % MAXJ); struct mjob *j = &c.j[i];
                        if (j->state == QUEUED) j->state = (rnd() % 4) ? RUNNING : CANCELLED;
                        else if (j->state == RUNNING) { static const int fin[4] = { DONE, DONE, FAULTED, STOPPING }; j->state = fin[rnd() % 4]; }
                        else if (j->state == STOPPING) j->state = (rnd() % 2) ? DONE : RESET;
                } else {                        /* reap acknowledged records */
                        unsigned i = (unsigned)(rnd() % MAXJ); if (c.j[i].state >= DONE) memset(&c.j[i], 0, sizeof(c.j[i]));
                }
                struct n2gf_jobs v = view(&c);
                uint32_t sid = 1 + (uint32_t)(rnd() % (nsurf + 2));   /* includes never-written ids */
                uint64_t rmax = 0; bool rp = ref_pending(&c, sid, ~0ull, &rmax);
                CHECK(n2gf_pending(&v, sid, ~0ull, NULL) == rp, "pending sid=%u", sid);
                uint64_t ws = n2gf_write_seq(&t, &v, sid), tmax = ref_max_writer(sid);
                CHECK(ws >= rmax, "write_seq %llu below pending writer %llu (sid %u)", (unsigned long long)ws, (unsigned long long)rmax, sid);
                CHECK(ws <= tmax, "write_seq %llu above true max %llu", (unsigned long long)ws, (unsigned long long)tmax);
                if (exact_expected) CHECK(ws == tmax, "write_seq %llu != %llu (sid %u)", (unsigned long long)ws, (unsigned long long)tmax, sid);
                uint64_t low = n2gf_low_water(&t, &v, c.next_id);
                CHECK(low == ref_low(&c), "low %llu ref %llu", (unsigned long long)low, (unsigned long long)ref_low(&c));
                CHECK(low >= last_low, "low-water went back");
                last_low = low;
                uint64_t upto = c.next_id > 1 ? 1 + rnd() % (c.next_id - 1) : 0;
                CHECK(n2gf_pending(&v, sid, upto, NULL) == ref_pending(&c, sid, upto, NULL), "writes_done upto");
                /* low-water implies the exact fence */
                if (ws && low >= ws) CHECK(!n2gf_pending(&v, sid, ws, NULL), "completed>=seq but writes pending");
        }
        printf("random nsurf=%u steps=%u: rec=%llu hit=%llu fill=%llu evict=%llu/%llu miss=%llu\n", nsurf, steps,
               (unsigned long long)t.st.records, (unsigned long long)t.st.hits, (unsigned long long)t.st.fills,
               (unsigned long long)t.st.evict_done, (unsigned long long)t.st.evict_pending, (unsigned long long)t.st.miss_scans);
        if (!exact_expected) CHECK(t.st.evict_done + t.st.evict_pending > 0, "eviction path not exercised");
}

/* Snapshot-at-SwapEnd scenario: an app runs ahead on the same surface. */
static void snapshot_case(void)
{
        static struct mcore c; static struct n2gf_table t; memset(&c, 0, sizeof(c)); memset(&t, 0, sizeof(t)); c.next_id = 1; nh = 0; memset(refmax, 0, sizeof(refmax));
        uint32_t s7[1] = { 7 }, s9[2] = { 9, 0 };
        int a = submit(&c, &t, 1, s7, 1);                 /* frame N renders surface 7 */
        int other = submit(&c, &t, 2, s9, 1);             /* another client's long job */
        struct n2gf_jobs v = view(&c);
        uint64_t snap = n2gf_write_seq(&t, &v, 7);
        CHECK(snap == 1, "snap %llu", (unsigned long long)snap);
        int b = submit(&c, &t, 1, s7, 1);                 /* frame N+1 on the same surface, after the swap */
        CHECK(n2gf_pending(&v, 7, snap, NULL), "frame N still pending");
        c.j[a].state = DONE;
        CHECK(!n2gf_pending(&v, 7, snap, NULL), "snapshot fence must be ready although frame N+1 is pending");
        CHECK(n2gf_pending(&v, 7, ~0ull, NULL), "write_pending must still see frame N+1");
        CHECK(n2gf_write_seq(&t, &v, 7) == 3, "newest writer");
        CHECK(n2gf_low_water(&t, &v, c.next_id) == 1, "low-water held by the other client's job (id 2)");
        c.j[other].state = FAULTED; c.j[b].state = DONE;
        CHECK(n2gf_low_water(&t, &v, c.next_id) == 3, "all done");
        uint32_t s0[1] = { 7 };
        int d = submit(&c, &t, 1, s0, 0);                 /* read-only use of surface 7: not a write */
        CHECK(!n2gf_pending(&v, 7, ~0ull, NULL) && n2gf_write_seq(&t, &v, 7) == 3, "read-only job is no write");
        (void)d;
}

/* Bounded wait with a mock clock: jobs complete at scheduled times while the waiter sleeps. */
static struct mcore wc; static uint64_t wnow; static uint32_t wsid; static unsigned wsleeps, wbigstep;
static uint64_t w_now(void *ctx) { (void)ctx; return wnow; }
static void w_sleep(void *ctx, uint32_t us)
{
        (void)ctx; wsleeps++; if (us > N2GF_WAIT_STEP_US) wbigstep++;
        wnow += us + 7;                                   /* oversleep a little, like a real timer */
        for (unsigned i = 0; i < MAXJ; i++) if (wc.j[i].state == RUNNING && wc.j[i].done_at && wc.j[i].done_at <= wnow) wc.j[i].state = DONE;
}
static bool w_pending(void *ctx) { (void)ctx; struct n2gf_jobs v = view(&wc); return n2gf_pending(&v, wsid, ~0ull, NULL); }
static void wait_case(uint64_t done_after, uint32_t timeout, bool expect_idle)
{
        static struct n2gf_table t; memset(&wc, 0, sizeof(wc)); memset(&t, 0, sizeof(t)); wc.next_id = 1; nh = 0; memset(refmax, 0, sizeof(refmax));
        wnow = 1000000; wsid = 5; wsleeps = wbigstep = 0;
        uint32_t s[1] = { 5 };
        int i = submit(&wc, &t, 1, s, 1); wc.j[i].state = RUNNING; wc.j[i].done_at = done_after ? wnow + done_after : 0;
        struct n2gf_wait_ops o = { NULL, w_now, w_sleep, w_pending };
        uint32_t el = 0; bool idle = n2gf_wait(&o, timeout, &el); n2gf_wait_account(&t, idle, el);
        uint32_t eff = timeout > N2GF_WAIT_MAX_US ? N2GF_WAIT_MAX_US : timeout;
        CHECK(idle == expect_idle, "wait done_after=%llu timeout=%u idle=%d", (unsigned long long)done_after, timeout, idle);
        CHECK(el <= eff + 7 + 1, "elapsed %u beyond timeout %u", el, eff);
        if (idle && done_after) CHECK(el >= done_after && el <= done_after + N2GF_WAIT_STEP_US + 7, "late wake %u for %llu", el, (unsigned long long)done_after);
        if (!idle) CHECK(el >= eff, "gave up early %u < %u", el, eff);
        CHECK(!wbigstep, "sleep step above %u us", N2GF_WAIT_STEP_US);
        CHECK(t.st.waits == 1 && t.st.wait_ok == (idle ? 1u : 0u), "wait accounting");
}

static void pool_case(void)
{
        CHECK(n2gf_pool_page(0xC0000000ull), "pool base");
        CHECK(n2gf_pool_page(0xCFFFF000ull), "pool last page");
        CHECK(!n2gf_pool_page(0xD0000000ull), "pool end (CAMSV) excluded");
        CHECK(!n2gf_pool_page(0xBFFFF000ull), "below pool");
        CHECK(!n2gf_pool_page(0xC0000800ull), "unaligned");
        CHECK(!n2gf_pool_page(0x142000000ull), "XNU RAM is not pool");
        CHECK(!n2gf_pool_page(0x76e10000ull), "panel carve-out is not pool");
        CHECK(!n2gf_pool_page(0xC0000000ull - 0x100000000ull + 0x10000000000ull), "40-bit alias");
        CHECK(!n2gf_pool_page(~0ull & ~4095ull), "top of address space");
        CHECK(N2GF_POOL_BASE + N2GF_POOL_SIZE <= (1ull << 40), "pool below NORD2_GPU_PA_BITS");
}

int main(void)
{
        pool_case();
        snapshot_case();
        random_run(8, 200000, true);       /* typical: few display surfaces */
        random_run(60, 200000, true);      /* up to the table size: exact */
        random_run(300, 120000, false);    /* more surfaces than slots: evictions, safety must hold */
        wait_case(250, 10000, true);
        wait_case(5, 10000, true);
        wait_case(0, 3000, false);         /* never completes */
        wait_case(20000, 10000, false);    /* completes after the timeout */
        wait_case(0, 500000, false);       /* clamp to 100 ms */
        wait_case(99000, 500000, true);
        printf("%s (%u failures)\n", fails ? "FAILED" : "ALL OK", fails);
        return fails ? 1 : 0;
}
