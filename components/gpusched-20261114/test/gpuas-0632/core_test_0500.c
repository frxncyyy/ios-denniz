/* 0500 (gpuas-20261114) host tests for nord2_gpu_core.c: dynamic GPU address spaces.
 *  - the 27 production scenarios + the 3 0400 scenarios in STATIC mode (the kill switch) = the pre-0500 behaviour;
 *  - the same scenarios in DYNAMIC mode, except the four that test "the AS is programmed at open" (replaced by dynamic equivalents);
 *  - targeted dynamic cases (many contexts, LRU eviction, never evicting a context with a job on the hardware, every AS busy, eviction /
 *    bind / flush failures -> reset, MMU faults routed through as_owner, orphan faults, teardown while unbound, stop/start, limits);
 *  - a randomized model run: 48 processes, random maps/releases/submits/completions/failures/faults/closes, invariants after every step.
 * The hardware model (core_test_base_0500.c) checks at every JS start that the AS holds the page tables of the job's context, and at every AS
 * configure/disable that no busy job slot runs in that AS (except the disable of a faulting context). */
#define main base_main
#include "core_test_base_0500.c"
#undef main
#include "core_test_0400_0500.c"

static void inv(void) {
 unsigned bound = 0;
 CHECK(core.as_owner[0] == N2C_AS_NONE);
 for (unsigned as = 1; as < N2C_HW_AS; as++) {
  unsigned o = core.as_owner[as];
  if (o == N2C_AS_NONE) { if (!core.reset_pending && core.as_dyn) { CHECK(m.as_root[as] == 0); for (unsigned s = 0; s < 16; s++) CHECK(m.slot_as[s] != (int)as); } continue; }
  CHECK(o < N2C_MAX_CTX && o != 0);
  const struct n2c_ctx *x = &core.ctxs[o];
  CHECK(x->state != N2C_CTX_FREE && x->as == as && (x->as_live || x->state == N2C_CTX_FAULTED));   /* a faulted context holds its disabled AS */
  if (!core.reset_pending) CHECK(x->as_live ? m.as_root[as] == x->mmu.root_pa : m.as_root[as] == 0);
  bound++;
 }
 CHECK(bound == core.as_stats.bound);
 unsigned open = 0;
 for (unsigned i = 1; i < N2C_MAX_CTX; i++) {
  const struct n2c_ctx *x = &core.ctxs[i];
  if (x->state == N2C_CTX_FREE) { CHECK(!x->bos && !x->va && !x->as_live); continue; }
  open++;
  CHECK(x->bos && x->va);
  if (!core.as_dyn) { CHECK(i < N2C_HW_AS && x->as == i); continue; }
  if (x->as_live || x->as != N2C_AS_NONE) CHECK(x->as < N2C_HW_AS && core.as_owner[x->as] == i);
  if (!x->as_live && x->as != N2C_AS_NONE) CHECK(x->state == N2C_CTX_FAULTED);
 }
 CHECK(open == core.as_stats.ctx_open);
 for (unsigned s = 0; s < 16; s++) {
  if (m.slot_as[s] < 0) continue;
  CHECK(core.running[s]);
  for (unsigned i = 0; i < N2C_MAX_JOBS; i++) {
   const struct n2c_job *j = &core.jobs[i];
   if (j->id == core.running[s] && j->state == N2C_JOB_RUNNING) CHECK(core.ctxs[j->ctx].as == (unsigned)m.slot_as[s] && core.ctxs[j->ctx].as_live);
  }
 }
}

static unsigned open_n(unsigned n, unsigned *cs) { for (unsigned i = 0; i < n; i++) cs[i] = context(); return n; }

/* 1. Many contexts: 40 processes each run a job; every one gets an AS; contexts beyond 7 force LRU evictions; nothing is refused. */
static void test_many_contexts(void) {
 init(); unsigned cs[40]; uint32_t h[40]; uint64_t va[40], id[40];
 open_n(40, cs); CHECK(core.as_stats.ctx_open == 40 && core.as_stats.ctx_peak == 40 && core.as_stats.binds == 0 && m.as_cfg[1] == 0);
 for (unsigned i = 0; i < 40; i++) CHECK(map_pages(cs[i], 1, &h[i], &va[i]) == N2C_OK);
 CHECK(m.flushes == 0);                                   /* unbound contexts map without any AS command */
 for (unsigned i = 0; i < 40; i++) { id[i] = submit(cs[i], h[i], va[i], N2C_JOB_COMPUTE); inv(); complete(1); inv(); done(cs[i], id[i]); }
 CHECK(core.as_stats.binds == 40 && core.as_stats.evictions == 33 && core.as_stats.bound == 7 && core.as_stats.bind_waits == 0);
 /* LRU: the 7 bound contexts are the last 7 that ran */
 for (unsigned i = 0; i < 33; i++) CHECK(!core.ctxs[cs[i]].as_live);
 for (unsigned i = 33; i < 40; i++) CHECK(core.ctxs[cs[i]].as_live);
 /* a context that still holds its AS runs again without any AS command */
 unsigned cfg = 0; for (unsigned a = 0; a < 16; a++) cfg += m.as_cfg[a];
 uint64_t j = submit(cs[39], h[39], va[39], N2C_JOB_COMPUTE); complete(1); done(cs[39], j);
 unsigned cfg2 = 0; for (unsigned a = 0; a < 16; a++) cfg2 += m.as_cfg[a];
 CHECK(cfg2 == cfg && core.as_stats.binds == 40);
 /* the oldest gets the AS of the least recently used bound context (cs[33]) */
 unsigned as33 = core.ctxs[cs[33]].as;
 j = submit(cs[0], h[0], va[0], N2C_JOB_COMPUTE); CHECK(core.ctxs[cs[0]].as == as33 && !core.ctxs[cs[33]].as_live); complete(1); done(cs[0], j); inv();
 for (unsigned i = 0; i < 40; i++) { CHECK(n2c_bo_release(&core, cs[i], h[i]) == N2C_OK); CHECK(n2c_ctx_close(&core, cs[i]) == N2C_OK); inv(); }
 CHECK(!m.table_live && m.owners == 40 && core.as_stats.ctx_open == 0 && core.as_stats.bound == 0);
 for (unsigned a = 1; a < N2C_HW_AS; a++) CHECK(m.as_root[a] == 0);   /* closing a bound context disabled its AS */
}

/* 2. Never evict a context with a job on the hardware; a context with only queued work goes after the idle ones. */
static void test_no_evict_running(void) {
 init(); unsigned cs[9]; uint32_t h[9]; uint64_t va[9];
 open_n(9, cs); for (unsigned i = 0; i < 9; i++) CHECK(map_pages(cs[i], 1, &h[i], &va[i]) == N2C_OK);
 /* cs[0]: render job, vertex phase running on slot 1 */
 uint64_t r = submit(cs[0], h[0], va[0], N2C_JOB_RENDER); CHECK(core.running[1] == r);
 unsigned as0 = core.ctxs[cs[0]].as; uint64_t root0 = core.ctxs[cs[0]].mmu.root_pa;
 /* cs[1..6] take the other six AS with fragment jobs (slot 0) that complete one by one */
 for (unsigned i = 1; i < 7; i++) { uint64_t f = submit(cs[i], h[i], va[i], N2C_JOB_FRAGMENT); CHECK(core.running[0] == f); complete(0); done(cs[i], f); }
 CHECK(core.as_stats.bound == 7 && core.as_stats.evictions == 0 && as0 == core.ctxs[cs[0]].as);
 /* stamps: cs[1] oldest idle.  cs[7] needs an AS: cs[0] (oldest stamp, but RUNNING) must not be chosen */
 uint64_t f7 = submit(cs[7], h[7], va[7], N2C_JOB_FRAGMENT);
 CHECK(core.running[0] == f7 && core.ctxs[cs[0]].as == as0 && m.as_root[as0] == root0 && !core.ctxs[cs[1]].as_live);
 complete(0); done(cs[7], f7); inv();
 /* vertex of cs[0] finishes -> fragment phase queued; slot 0 busy with cs[8]'s fragment meanwhile? start cs[2]'s compute on slot 1 first */
 complete(1);                                              /* cs[0]'s fragment starts on slot 0 at once (same AS) */
 CHECK(core.running[0] == r && core.ctxs[cs[0]].as == as0);
 complete(0); done(cs[0], r);
 for (unsigned i = 0; i < 9; i++) { CHECK(n2c_bo_release(&core, cs[i], h[i]) == N2C_OK); CHECK(n2c_ctx_close(&core, cs[i]) == N2C_OK); }
 CHECK(!m.table_live); inv();
}

/* 3. Every client AS held by a context with work on the hardware: the job waits (bind_waits), starts after a completion. */
static void test_all_busy_waits(void) {
 init(); hw.id.as_present = 0x7;                           /* AS0 + only two client AS */
 unsigned cs[3]; uint32_t h[3]; uint64_t va[3]; open_n(3, cs);
 for (unsigned i = 0; i < 3; i++) CHECK(map_pages(cs[i], 1, &h[i], &va[i]) == N2C_OK);
 uint64_t a = submit(cs[0], h[0], va[0], N2C_JOB_COMPUTE);    /* slot 1 */
 uint64_t b = submit(cs[1], h[1], va[1], N2C_JOB_FRAGMENT);   /* slot 0 */
 CHECK(core.running[1] == a && core.running[0] == b);
 uint64_t c3 = submit(cs[2], h[2], va[2], N2C_JOB_COMPUTE);   /* slot 1 busy anyway */
 complete(1); done(cs[0], a);                                  /* slot 1 free; cs[0] idle -> evicted for cs[2] */
 CHECK(core.running[1] == c3 && core.ctxs[cs[2]].as_live && !core.ctxs[cs[0]].as_live && core.as_stats.evictions == 1);
 /* now cs[0] submits a fragment job while slot 0 runs cs[1] and slot 1 runs cs[2]: both AS busy */
 complete(0); done(cs[1], b);
 uint64_t a2 = submit(cs[0], h[0], va[0], N2C_JOB_COMPUTE);   /* slot 1 busy: blocked by the slot, no bind attempt */
 uint64_t b2 = submit(cs[1], h[1], va[1], N2C_JOB_FRAGMENT);  /* cs[1] still bound: starts on slot 0 */
 CHECK(core.running[0] == b2);
 uint64_t waits = core.as_stats.bind_waits;
 complete(1); done(cs[2], c3);                                 /* slot 1 free: cs[0] needs an AS; cs[2] idle now -> evicted */
 CHECK(core.running[1] == a2 && core.as_stats.bind_waits == waits);
 complete(0); done(cs[1], b2); complete(1); done(cs[0], a2);
 /* mid-render eviction: cs[1]'s render job between its phases holds no job on the hardware, so its AS may be lent (class 1, after idle ones) */
 uint64_t x = submit(cs[0], h[0], va[0], N2C_JOB_COMPUTE), y = submit(cs[1], h[1], va[1], N2C_JOB_RENDER);
 CHECK(core.running[1] == x); complete(1); done(cs[0], x);    /* y's vertex starts on slot 1 */
 CHECK(core.running[1] == y);
 uint64_t z0 = submit(cs[0], h[0], va[0], N2C_JOB_FRAGMENT); CHECK(core.running[0] == z0);   /* cs[0] still bound */
 uint64_t z = submit(cs[2], h[2], va[2], N2C_JOB_COMPUTE);    /* slot 1 busy */
 uint64_t ev = core.as_stats.evictions;
 complete(1);                                                  /* y vertex done: y fragment waits for slot 0; z: cs[0] runs, cs[1] only queued -> cs[1] evicted */
 CHECK(core.running[1] == z && !core.ctxs[cs[1]].as_live && core.ctxs[cs[0]].as_live && core.as_stats.evictions == ev + 1);
 inv();
 complete(0); done(cs[0], z0);                                 /* slot 0 free: y's fragment rebinds cs[1] (cs[0] idle, cs[2] running) */
 CHECK(core.running[0] == y && core.ctxs[cs[1]].as_live && !core.ctxs[cs[0]].as_live && core.as_stats.evictions == ev + 2);
 complete(0); done(cs[1], y); complete(1); done(cs[2], z);
 CHECK(core.as_stats.bind_waits == waits);
 for (unsigned i = 0; i < 3; i++) { CHECK(n2c_bo_release(&core, cs[i], h[i]) == N2C_OK); CHECK(n2c_ctx_close(&core, cs[i]) == N2C_OK); }
 CHECK(!m.table_live); inv();
 /* a real wait: ONE client AS, held by a context running on slot 0; another context's job has slot 1 free but no AS until slot 0 finishes */
 init(); hw.id.as_present = 0x3; open_n(2, cs);
 for (unsigned i = 0; i < 2; i++) CHECK(map_pages(cs[i], 1, &h[i], &va[i]) == N2C_OK);
 uint64_t f = submit(cs[0], h[0], va[0], N2C_JOB_FRAGMENT), g = submit(cs[1], h[1], va[1], N2C_JOB_COMPUTE);
 CHECK(core.running[0] == f && !core.running[1] && core.as_stats.bind_waits >= 1 && !core.ctxs[cs[1]].as_live);
 m.time++; n2c_poll(&core); CHECK(!core.running[1]); inv();      /* still waiting, nothing reprogrammed under the running job */
 complete(0); done(cs[0], f);
 CHECK(core.running[1] == g && core.ctxs[cs[1]].as == 1 && !core.ctxs[cs[0]].as_live);
 complete(1); done(cs[1], g);
 for (unsigned i = 0; i < 2; i++) { CHECK(n2c_bo_release(&core, cs[i], h[i]) == N2C_OK); CHECK(n2c_ctx_close(&core, cs[i]) == N2C_OK); }
 CHECK(!m.table_live); inv();
}

/* 4. Eviction / bind / bind-flush not acknowledged: reset, every client unbound, work resumes with fresh binds. */
static void test_bind_failures(void) {
 for (unsigned kind = 0; kind < 3; kind++) {
  init(); unsigned cs[9]; uint32_t h[9]; uint64_t va[9]; open_n(9, cs);
  for (unsigned i = 0; i < 9; i++) CHECK(map_pages(cs[i], 1, &h[i], &va[i]) == N2C_OK);
  for (unsigned i = 0; i < 7; i++) { uint64_t j = submit(cs[i], h[i], va[i], N2C_JOB_COMPUTE); complete(1); done(cs[i], j); }
  if (kind == 0) m.fail_disable = true;                   /* the eviction for cs[7] */
  if (kind == 1) m.fail_config_after = 1;                 /* the configure for cs[7] */
  if (kind == 2) m.fail_flush = true;                     /* the FLUSH_PT after the configure */
  m.js_check_off = false;
  struct n2c_submit s = {0}; s.kind = N2C_JOB_COMPUTE; s.nr_handles = 1; s.handles[0] = h[7]; s.write_mask = 1; s.heads[0] = va[7];
  uint64_t id; CHECK(n2c_submit(&core, cs[7], &s, &id) == N2C_OK);
  CHECK(core.as_stats.bind_failures == 1);
  m.fail_disable = false; m.fail_config = false; m.fail_flush = false;
  n2c_poll(&core);                                         /* reset, then the bind succeeds */
  CHECK(m.resets == 1 && core.state == N2C_CORE_READY && !core.reset_pending);
  CHECK(core.ctxs[cs[7]].as_live && core.as_stats.bound == 1 && core.running[1] == id); inv();
  complete(1); done(cs[7], id);
  for (unsigned i = 0; i < 9; i++) { CHECK(n2c_bo_release(&core, cs[i], h[i]) == N2C_OK); CHECK(n2c_ctx_close(&core, cs[i]) == N2C_OK); }
  CHECK(!m.table_live); inv();
 }
}

/* 5. MMU faults: routed to the context that holds the AS; the AS goes back to the pool; a fault on a free AS harms nobody. */
static void test_faults(void) {
 init(); unsigned cs[10]; uint32_t h[10]; uint64_t va[10]; open_n(10, cs);
 for (unsigned i = 0; i < 10; i++) CHECK(map_pages(cs[i], 1, &h[i], &va[i]) == N2C_OK);
 for (unsigned i = 0; i < 10; i++) { uint64_t j = submit(cs[i], h[i], va[i], N2C_JOB_COMPUTE); complete(1); done(cs[i], j); }
 /* cs[9] holds some AS != its index */
 uint64_t j = submit(cs[9], h[9], va[9], N2C_JOB_COMPUTE); unsigned as = core.ctxs[cs[9]].as; CHECK(as != cs[9] && as >= 1 && as < 8);
 unsigned victim_idx = 0; for (unsigned i = 0; i < 10; i++) if (cs[i] == as) victim_idx = i;   /* the context whose index equals that AS number */
 m.fault = (struct n2hw_mmu_fault){ .as = as, .status = 0xc1, .address = va[9] + 0x100000 }; m.fault_ready = true; n2c_poll(&core);
 CHECK(core.ctxs[cs[9]].state == N2C_CTX_FAULTED && core.ctxs[cs[9]].fault_status == 0xc1 && !core.ctxs[cs[9]].as_live);
 CHECK(core.ctxs[cs[victim_idx]].state == N2C_CTX_OPEN && core.ctxs[cs[victim_idx]].fault_status == 0);   /* static mapping would have blamed it */
 CHECK(core.as_owner[as] == cs[9] && m.as_root[as] == 0);      /* disabled, still held: the stopped job is not acknowledged yet */
 complete(1);                                              /* the stopped job's IRQ */
 uint32_t st, status; CHECK(n2c_job_state(&core, cs[9], j, true, &st, &status) == N2C_OK && st == N2C_JOB_FAULTED);
 CHECK(n2c_submit(&core, cs[9], &(struct n2c_submit){ .kind = N2C_JOB_COMPUTE, .nr_handles = 1, .handles = { h[9] }, .write_mask = 1, .heads = { va[9] } }, &j) == N2C_E_STATE);
 inv();
 /* orphan: a fault on the AS nobody holds now */
 uint64_t faults = core.stats.mmu_faults;
 m.fault = (struct n2hw_mmu_fault){ .as = as, .status = 0xc1 }; m.fault_ready = true; n2c_poll(&core);
 CHECK(core.as_stats.orphan_faults == 1 && core.stats.mmu_faults == faults + 1);
 for (unsigned i = 0; i < 9; i++) CHECK(core.ctxs[cs[i]].state == N2C_CTX_OPEN);
 /* the freed AS is reused at once */
 j = submit(cs[0], h[0], va[0], N2C_JOB_COMPUTE); complete(1); done(cs[0], j); inv();
 for (unsigned i = 0; i < 10; i++) { CHECK(n2c_bo_release(&core, cs[i], h[i]) == N2C_OK); CHECK(n2c_ctx_close(&core, cs[i]) == N2C_OK); }
 CHECK(!m.table_live); inv();
 /* the AS of a faulted context is not lent while its stopped job is still on the hardware (one client AS) */
 init(); hw.id.as_present = 0x3; open_n(2, cs);
 for (unsigned i = 0; i < 2; i++) CHECK(map_pages(cs[i], 1, &h[i], &va[i]) == N2C_OK);
 uint64_t fj = submit(cs[0], h[0], va[0], N2C_JOB_FRAGMENT);
 m.fault = (struct n2hw_mmu_fault){ .as = 1, .status = 0xc1 }; m.fault_ready = true; n2c_poll(&core);
 CHECK(core.jobs[0].state == N2C_JOB_STOPPING || core.jobs[1].state == N2C_JOB_STOPPING);
 uint64_t gj = submit(cs[1], h[1], va[1], N2C_JOB_COMPUTE);      /* slot 1 free, AS1 held by the stopping job's context */
 CHECK(!core.running[1] && core.as_stats.bind_waits >= 1 && m.as_root[1] == 0); inv();
 complete(0);                                                    /* stop acknowledged -> AS1 lent to cs[1] */
 CHECK(core.running[1] == gj && core.ctxs[cs[1]].as == 1 && core.as_owner[1] == cs[1]);
 CHECK(n2c_job_state(&core, cs[0], fj, true, &st, &status) == N2C_OK && st == N2C_JOB_FAULTED);
 complete(1); done(cs[1], gj); inv();
 for (unsigned i = 0; i < 2; i++) { CHECK(n2c_bo_release(&core, cs[i], h[i]) == N2C_OK); CHECK(n2c_ctx_close(&core, cs[i]) == N2C_OK); }
 CHECK(!m.table_live); inv();
}

/* 6. Teardown and mapping while unbound need no AS command; the pages tables survive eviction; the job after rebind sees new buffers. */
static void test_unbound_lifetime(void) {
 init(); unsigned cs[8]; uint32_t h[8]; uint64_t va[8]; open_n(8, cs);
 for (unsigned i = 0; i < 8; i++) CHECK(map_pages(cs[i], 1, &h[i], &va[i]) == N2C_OK);
 for (unsigned i = 0; i < 8; i++) { uint64_t j = submit(cs[i], h[i], va[i], N2C_JOB_COMPUTE); complete(1); done(cs[i], j); }
 CHECK(!core.ctxs[cs[0]].as_live);                         /* evicted by cs[7] */
 unsigned fl = m.flushes, tables = m.table_live; uint64_t root = core.ctxs[cs[0]].mmu.root_pa;
 uint32_t big; uint64_t bva; CHECK(map_pages(cs[0], 600, &big, &bva) == N2C_OK);    /* new L3 tables */
 CHECK(m.flushes == fl && m.table_live > tables);
 CHECK(n2c_bo_release(&core, cs[0], h[0]) == N2C_OK && m.flushes == fl);             /* unmapped and freed without a flush */
 uint64_t j = submit(cs[0], big, bva, N2C_JOB_RENDER);
 CHECK(core.ctxs[cs[0]].as_live && core.ctxs[cs[0]].mmu.root_pa == root && m.as_root[core.ctxs[cs[0]].as] == root);
 complete(1); complete(0); done(cs[0], j);
 CHECK(n2c_bo_release(&core, cs[0], big) == N2C_OK);
 for (unsigned i = 0; i < 8; i++) { if (i) CHECK(n2c_bo_release(&core, cs[i], h[i]) == N2C_OK); CHECK(n2c_ctx_close(&core, cs[i]) == N2C_OK); }
 CHECK(!m.table_live); inv();
}

/* 7. Limits and the kill switch. */
static void test_limits_and_static(void) {
 init(); unsigned cs[N2C_MAX_CTX]; unsigned n = 0, c;
 while (n2c_ctx_open(&core, 1, 1 << 20, false, &c) == N2C_OK) cs[n++] = c;
 CHECK(n == N2C_MAX_CTX - 1 && core.as_stats.ctx_refused == 1 && core.as_stats.ctx_peak == N2C_MAX_CTX - 1);
 CHECK(!n2c_set_as_dynamic(&core, false));                 /* refused while contexts exist */
 for (unsigned i = 0; i < n; i++) CHECK(n2c_ctx_close(&core, cs[i]) == N2C_OK);
 CHECK(!m.table_live && core.as_stats.ctx_open == 0);
 g_static_as = true; init(); n = 0;
 while (n2c_ctx_open(&core, 1, 1 << 20, false, &c) == N2C_OK) { CHECK(c == n + 1 && core.ctxs[c].as == c && core.ctxs[c].as_live && m.as_root[c]); cs[n++] = c; }
 CHECK(n == N2C_HW_AS - 1 && core.as_stats.ctx_refused == 1);   /* the old limit: 7 clients, AS = index, bound at open */
 inv();
 for (unsigned i = 0; i < n; i++) CHECK(n2c_ctx_close(&core, cs[i]) == N2C_OK);
 CHECK(!m.table_live); g_static_as = false;
}

/* 8. Power cycle: stop disables and unbinds every AS, start binds nothing, the next jobs rebind. */
static void test_stop_start(void) {
 init(); unsigned cs[3]; uint32_t h[3]; uint64_t va[3]; open_n(3, cs);
 for (unsigned i = 0; i < 3; i++) { CHECK(map_pages(cs[i], 1, &h[i], &va[i]) == N2C_OK); uint64_t j = submit(cs[i], h[i], va[i], N2C_JOB_COMPUTE); complete(1); done(cs[i], j); }
 CHECK(core.as_stats.bound == 3);
 m.fail_disable = true; CHECK(n2c_stop(&core) == N2C_E_HW); m.fail_disable = false;
 CHECK(n2c_reset(&core) == N2C_OK && core.as_stats.bound == 0); inv();
 uint64_t j = submit(cs[1], h[1], va[1], N2C_JOB_COMPUTE); complete(1); done(cs[1], j);
 CHECK(n2c_stop(&core) == N2C_OK && core.as_stats.bound == 0 && !core.gpu_powered); inv();
 for (unsigned a = 1; a < 8; a++) CHECK(m.as_root[a] == 0);
 CHECK(n2c_start(&core) == N2C_OK && core.as_stats.bound == 0);
 j = submit(cs[2], h[2], va[2], N2C_JOB_COMPUTE); CHECK(core.ctxs[cs[2]].as_live); complete(1); done(cs[2], j); inv();
 for (unsigned i = 0; i < 3; i++) { CHECK(n2c_bo_release(&core, cs[i], h[i]) == N2C_OK); CHECK(n2c_ctx_close(&core, cs[i]) == N2C_OK); }
 CHECK(!m.table_live);
}

/* Dynamic equivalents of the four base scenarios that test "AS programmed at open". */
static void test_dyn_open_does_not_touch_hw(void) {
 init(); m.fail_config = m.fail_disable = true; unsigned c = 999;
 CHECK(n2c_ctx_open(&core, 1, 65536, false, &c) == N2C_OK && c == 1 && !core.ctxs[1].as_live && m.disables == 0);
 m.fail_config = m.fail_disable = false; CHECK(n2c_ctx_close(&core, c) == N2C_OK && !m.table_live && m.disables == 0);
}
static void test_dyn_fault_failed_reset_pins(void) {
 init(); unsigned c = context(); uint32_t h; uint64_t va; CHECK(map_pages(c, 1, &h, &va) == N2C_OK);
 (void)submit(c, h, va, N2C_JOB_COMPUTE); CHECK(n2c_bo_release(&core, c, h) == N2C_E_BUSY);
 m.fail_disable = m.fail_reset = true; m.fault = (struct n2hw_mmu_fault){ .as = core.ctxs[c].as, .status = 0x42, .address = va }; m.fault_ready = true;
 n2c_poll(&core); CHECK(core.state == N2C_CORE_BROKEN && core.ctxs[c].as_live);
 CHECK(!m.owners && core.stats.bos_live == 1);
 CHECK(n2c_ctx_close(&core, c) == N2C_E_BROKEN); CHECK(!m.owners);
 CHECK(n2c_stop(&core) != N2C_OK); model_destroy_after_power_cut();
}

/* 9. Randomized model: 48 processes over 7 AS. */
static uint64_t rs = 0x9e3779b97f4a7c15ull;
static unsigned rnd(unsigned n) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs % n); }
#define P 48
#define PB 6
struct proc { bool open; unsigned c; uint32_t h[PB]; uint64_t va[PB]; unsigned nb; uint64_t last; };
static void test_random(unsigned steps, uint64_t seed) {
 init(); rs = seed; struct proc p[P]; memset(p, 0, sizeof(p));
 uint64_t submitted = 0, closes = 0, faults = 0, fails = 0, inj = 0;
 for (unsigned step = 0; step < steps; step++) {
  unsigned a = rnd(100), k = rnd(P); struct proc *q = &p[k];
  if (a < 6) {                                             /* open */
   if (!q->open) { CHECK(n2c_ctx_open(&core, k, 64ull << 20, false, &q->c) == N2C_OK); q->open = true; q->nb = 0; q->last = 0; }
  } else if (a < 9) {                                      /* close (client death) */
   if (q->open) { enum n2c_error e = n2c_ctx_close(&core, q->c);
    CHECK(e == N2C_OK || e == N2C_E_BUSY);
    if (e == N2C_OK) { q->open = false; closes++; } }
  } else if (a < 20) {                                     /* map */
   if (q->open && q->nb < PB && core.ctxs[q->c].state == N2C_CTX_OPEN) { enum n2c_error e = map_pages(q->c, 1 + rnd(3), &q->h[q->nb], &q->va[q->nb]); if (e == N2C_OK) q->nb++; else CHECK(e == N2C_E_STATE || e == N2C_E_HW); }
  } else if (a < 26) {                                     /* release */
   if (q->open && q->nb) { unsigned i = rnd(q->nb); enum n2c_error e = n2c_bo_release(&core, q->c, q->h[i]); CHECK(e == N2C_OK || e == N2C_E_BUSY); q->h[i] = q->h[--q->nb]; q->va[i] = q->va[q->nb]; }
  } else if (a < 56) {                                     /* submit */
   if (q->open && q->nb && core.ctxs[q->c].state == N2C_CTX_OPEN && !core.reset_pending) {
    unsigned i = rnd(q->nb), kind = 1 + rnd(3);
    struct n2c_submit s = {0}; s.kind = kind; s.nr_handles = 1; s.handles[0] = q->h[i]; s.write_mask = 1;
    s.heads[0] = kind == N2C_JOB_FRAGMENT ? 0 : q->va[i]; s.heads[1] = kind == N2C_JOB_COMPUTE ? 0 : q->va[i] + 128;
    uint64_t id; enum n2c_error e = n2c_submit(&core, q->c, &s, &id);
    CHECK(e == N2C_OK || e == N2C_E_BUSY);
    if (e == N2C_OK) { q->last = id; submitted++; }
   }
  } else if (a < 86) {                                     /* a slot finishes (5 % failed) */
   unsigned s = rnd(2); if (m.slot_as[s] >= 0) { m.event = (struct n2hw_job_event){ .slot = s, .failed = rnd(20) == 0, .status = 0x40 }; if (m.event.failed) fails++; m.event_ready = true; n2c_poll(&core); }
  } else if (a < 92) {                                     /* reap */
   if (q->open && q->last) { struct n2c_seq_result r; CHECK(n2c_seq_state(&core, q->c, q->last, true, &r) == N2C_OK); }
  } else if (a < 94) {                                     /* MMU fault on a random AS (owned or not), 1 in 8 of these steps */
   if (rnd(8)) continue;
   unsigned as = 1 + rnd(7); m.fault = (struct n2hw_mmu_fault){ .as = as, .status = 0xc1 }; m.fault_ready = true; faults++; n2c_poll(&core);
  } else if (a < 95) {                                     /* one AS command not acknowledged */
   if (rnd(4) == 0) { m.fail_disable = true; inj++; n2c_poll(&core); for (unsigned t = 0; t < 4; t++) { unsigned kk = rnd(P); if (p[kk].open && p[kk].nb && core.ctxs[p[kk].c].state == N2C_CTX_OPEN && !core.reset_pending) {
      struct n2c_submit s = {0}; s.kind = N2C_JOB_COMPUTE; s.nr_handles = 1; s.handles[0] = p[kk].h[0]; s.write_mask = 1; s.heads[0] = p[kk].va[0]; uint64_t id; if (n2c_submit(&core, p[kk].c, &s, &id) == N2C_OK) { p[kk].last = id; submitted++; } } }
     m.fail_disable = false; n2c_poll(&core); }
  } else {                                                 /* time passes (2 % jumps past the job timeout) */
   m.time += rnd(50) == 0 ? N2C_JOB_TIMEOUT_MS + 1 : 1 + rnd(3); n2c_poll(&core);
  }
  CHECK(core.state == N2C_CORE_READY);
  inv();
 }
 /* drain: complete everything, close every process */
 for (unsigned round = 0; round < 100000; round++) {
  bool busy = false; for (unsigned s = 0; s < 2; s++) if (m.slot_as[s] >= 0) { m.event = (struct n2hw_job_event){ .slot = s }; m.event_ready = true; n2c_poll(&core); busy = true; }
  unsigned pend = 0; for (unsigned i = 0; i < N2C_MAX_JOBS; i++) { uint8_t st = core.jobs[i].state; if (st == N2C_JOB_QUEUED || st == N2C_JOB_RUNNING || st == N2C_JOB_STOPPING) pend++; }
  if (!busy && !pend) break;
  if (!busy) { m.time++; n2c_poll(&core); }
  inv();
 }
 for (unsigned i = 0; i < N2C_MAX_JOBS; i++) { uint8_t st = core.jobs[i].state; CHECK(st != N2C_JOB_QUEUED && st != N2C_JOB_RUNNING && st != N2C_JOB_STOPPING); }
 for (unsigned k = 0; k < P; k++) if (p[k].open) { CHECK(n2c_ctx_close(&core, p[k].c) == N2C_OK); p[k].open = false; inv(); }
 CHECK(!m.table_live && !core.stats.bos_live && core.as_stats.ctx_open == 0 && core.as_stats.bound == 0);
 printf("random seed %#llx: %u steps, %llu submitted, %llu completed, %llu faulted, %llu cancelled, %llu resets, %llu closes, %llu MMU faults (%llu orphan), %llu failed IRQs, %llu injected AS failures; "
        "AS binds %llu evictions %llu waits %llu bind failures %llu, peak %u contexts\n", (unsigned long long)seed, steps, (unsigned long long)submitted,
        (unsigned long long)core.stats.completed, (unsigned long long)core.stats.faulted, (unsigned long long)core.stats.cancelled, (unsigned long long)core.stats.resets,
        (unsigned long long)closes, (unsigned long long)faults, (unsigned long long)core.as_stats.orphan_faults, (unsigned long long)fails, (unsigned long long)inj,
        (unsigned long long)core.as_stats.binds, (unsigned long long)core.as_stats.evictions, (unsigned long long)core.as_stats.bind_waits,
        (unsigned long long)core.as_stats.bind_failures, core.as_stats.ctx_peak);
 CHECK(core.stats.completed > submitted / 4 && core.as_stats.evictions > 100 && core.as_stats.ctx_peak > 20);
}

static void run_base(bool dynamic) {
 g_static_as = !dynamic;
 test_reuse(); test_render_order(); if (!dynamic) { test_quarantine_close(); test_partial_map_failure(); } test_close_running();
 if (!dynamic) test_open_failure();
 test_allocation_failure_matrix(); if (!dynamic) { test_full_map_flush_failure(); test_quarantine_overflow(); } test_wide_reclaim(); test_unlisted_buffer();
 if (!dynamic) { test_failed_open_pins(); test_stop_ack(); test_fault_failed_reset_pins(); }
 /* flush-failure scenarios need a context that holds an AS (an unbound one maps and unmaps without any AS command): context() binds it first */
 if (dynamic) { g_bind_on_open = true; test_quarantine_close(); test_partial_map_failure(); test_full_map_flush_failure(); test_quarantine_overflow(); g_bind_on_open = false; }
 test_imported_accounting(); test_scoped_cache_maintenance(); test_full_list_is_not_trusted(); test_context_limit_384(); test_job_timing(); test_async_chain();
 test_cache_tracking(); test_full_list_follows_tracking(); test_handle_limit_1024(); test_write_mask_invalidate(); test_resv_basic(); test_resv_time_and_jobs();
 test_resv_failure_paths();
 main_0400();
 printf("base: %s mode: %s + 3 0400 scenarios PASS\n", dynamic ? "DYNAMIC" : "STATIC (kill switch)", dynamic ? "23 production scenarios (4 flush-failure ones on a bound context; the 4 open-time-AS ones replaced below)" : "27 production scenarios");
 g_static_as = false;
}

int main(int argc, char **argv) {
 unsigned steps = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 0) : 200000u;
 run_base(false);
 run_base(true);
 test_dyn_open_does_not_touch_hw(); test_dyn_fault_failed_reset_pins();
 test_many_contexts(); test_no_evict_running(); test_all_busy_waits(); test_bind_failures(); test_faults(); test_unbound_lifetime();
 test_limits_and_static(); test_stop_start();
 puts("0500 targeted: open without AS, fault with failed reset pins, 40 contexts / LRU, never evict running, every AS busy waits, eviction/bind/flush failure resets, fault routing + orphan, unbound teardown, limits + static kill switch, stop/start PASS");
 test_random(steps, 0x9e3779b97f4a7c15ull);
 test_random(steps, 0x0123456789abcdefull);
 test_random(steps, 0xfeedfacecafebeefull);
 puts("CORE_0500_HOST_TESTS_OK");
 return 0;
}
