/* 0400 (gpulat-20261112) host tests for nord2_gpu_core.c: late buffer release (next job first), latency accounting.
 * Reuses the production core test model (core_test_base.c = gpu/tests/nord2_core_test.c with the 0186 handle sizes). */
#define main base_main
#include "core_test_base.c"
#undef main

static unsigned starts_at_free[16], nfree;
static void free_owner_spy(void *c, void *p) { if (nfree < 16) starts_at_free[nfree++] = m.starts; free_owner(c, p); }

static uint64_t submit2(unsigned c, uint32_t h1, uint32_t h2, uint64_t va, unsigned kind) {
 struct n2c_submit s = {0}; s.kind = kind; s.nr_handles = h2 ? 2 : 1; s.handles[0] = h1; s.handles[1] = h2; s.write_mask = 1;
 s.heads[0] = kind == N2C_JOB_FRAGMENT ? 0 : va; s.heads[1] = kind == N2C_JOB_COMPUTE ? 0 : va + 128;
 uint64_t id; CHECK(n2c_submit(&core, c, &s, &id) == N2C_OK); return id;
}

/* The buffer a finished job held (released by the client meanwhile) is torn down only after the successor started. */
static void order_case(bool late, unsigned want_starts_at_free) {
 init(); core.lat_release_late = late; core.ops.free_owner = free_owner_spy; nfree = 0;
 unsigned c = context(); uint32_t h1, h2; uint64_t va1, va2;
 CHECK(map_pages(c, 1, &h1, &va1) == N2C_OK); CHECK(map_pages(c, 1, &h2, &va2) == N2C_OK);
 uint64_t a = submit2(c, h1, h2, va1, N2C_JOB_COMPUTE);
 CHECK(n2c_bo_release(&core, c, h2) == N2C_E_BUSY && m.owners == 0);   /* retained for job a (b, submitted later, cannot address it) */
 uint64_t b = submit2(c, h1, 0, va1, N2C_JOB_COMPUTE);
 CHECK(m.starts == 1);
 complete(1);
 CHECK(m.owners == 1 && nfree == 1 && starts_at_free[0] == want_starts_at_free);
 CHECK(core.running[1] == b && core.ctxs[c].jobs == 1);                  /* a released, b running */
 for (unsigned i = 0; i < N2C_MAX_JOBS; i++) CHECK(!core.jobs[i].release_due);
 done(c, a); complete(1); done(c, b);
 CHECK(n2c_bo_release(&core, c, h1) == N2C_OK && m.owners == 2);
 CHECK(n2c_ctx_close(&core, c) == N2C_OK && !m.table_live && !core.stats.bos_live);
}

static void test_0400_release_order(void) {
 order_case(true, 2);    /* 0400: successor started first */
 order_case(false, 1);   /* legacy order kept by lat_release_late=false */
}

/* A release flush that fails during the late release still resets in the same poll; the successor started meanwhile ends RESET. */
static void test_0400_release_flush_failure(void) {
 init(); unsigned c = context(); uint32_t h1, h2; uint64_t va1, va2;
 CHECK(map_pages(c, 1, &h1, &va1) == N2C_OK); CHECK(map_pages(c, 1, &h2, &va2) == N2C_OK);
 uint64_t a = submit2(c, h1, h2, va1, N2C_JOB_COMPUTE);
 CHECK(n2c_bo_release(&core, c, h2) == N2C_E_BUSY);
 uint64_t b = submit2(c, h1, 0, va1, N2C_JOB_COMPUTE);
 m.fail_flush = true; complete(1); m.fail_flush = false;
 CHECK(m.resets == 1 && !core.reset_pending && core.state == N2C_CORE_READY);
 uint32_t st, status; CHECK(n2c_job_state(&core, c, a, true, &st, &status) == N2C_OK && st == N2C_JOB_DONE);
 CHECK(n2c_job_state(&core, c, b, true, &st, &status) == N2C_OK && st == N2C_JOB_RESET);
 CHECK(m.owners == 1);                                                    /* the quarantined buffer drained by the reset */
 CHECK(n2c_bo_release(&core, c, h1) == N2C_OK && n2c_ctx_close(&core, c) == N2C_OK && !m.table_live);
}

static void test_0400_latency(void) {
 init(); unsigned c = context(); uint32_t h; uint64_t va; CHECK(map_pages(c, 1, &h, &va) == N2C_OK);
 struct n2c_lat l;
 CHECK(!n2c_lat_take(&core, &l, 0));                                      /* empty window */
 m.time = 10; uint64_t a = submit(c, h, va, N2C_JOB_RENDER);              /* vertex on slot 1 at ~10000 us */
 CHECK(core.running[1] == a);
 m.time = 11; n2c_lat_irq(&core, now_us(&m) - 100);                       /* top half 100 us before the poll */
 n2c_lat_irq(&core, now_us(&m) - 50);                                     /* a second stamp before the poll keeps the first */
 complete(1);                                                             /* fragment starts in the same poll */
 CHECK(core.running[0] == a && core.lat_irq_us == 0);
 m.time = 12; complete(0); done(c, a);
 m.time = 20; uint64_t b = submit(c, h, va, N2C_JOB_COMPUTE), d = submit(c, h, va, N2C_JOB_COMPUTE);
 CHECK(core.running[1] == b);
 m.time = 21; complete(1); CHECK(core.running[1] == d);                   /* same slot as the predecessor */
 m.time = 22; complete(1); done(c, b); done(c, d);
 CHECK(n2c_lat_take(&core, &l, 0));
 CHECK(l.submits == 3 && l.kind[N2C_JOB_RENDER] == 1 && l.kind[N2C_JOB_COMPUTE] == 2);
 CHECK(l.depth_max == 2 && l.depth_sum == 1 + 1 + 2);
 CHECK(l.irq_n == 1 && l.irq_sum == 100 && l.irq_max == 100);             /* only the poll after the stamp counts it */
 CHECK(l.phase_n == 1 && l.phase_sum >= 100 && l.phase_sum < 200);         /* vertex done (irq stamp) -> fragment start */
 CHECK(l.hand_n == 3 && l.hand_max < 1000);                                /* a: submit->start, b: submit->start, d: b done->start */
 CHECK(l.same_slot == 1);                                                  /* d after b on slot 1 (vertex 1 -> fragment 0 is not) */
 CHECK(l.busy_us >= 3000 && l.busy_us < 4500);                             /* ~2 ms render + ~2 ms computes, polled at ms steps */
 CHECK(l.rel_n == 3 && l.deferred == 3);                                   /* 3 completions (2 jobs + the render), each poll released late */
 CHECK(!n2c_lat_take(&core, &l, 0));                                       /* reset */
 CHECK(n2c_bo_release(&core, c, h) == N2C_OK && n2c_ctx_close(&core, c) == N2C_OK);
}

int main(void) {
 base_main(1, NULL);
 test_0400_release_order(); test_0400_release_flush_failure(); test_0400_latency();
 puts("core 0400: late release order (both settings), release-flush failure reset, latency accounting PASS");
 return 0;
}
