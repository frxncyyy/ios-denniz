#!/usr/bin/env python3
"""Generates core_test_0750.c from gpusched-20261114/test/core_test_0669.c (the JS NEXT / pass-overlap / prep / prio / power-gating model).
0750 adds: MMU-fault records (class, buffer lookup, freed ranges, job), trailing reports on the AS a FAULTED context still holds (not orphans),
revival of a FAULTED dynamic client (only with nothing of it queued / on the hardware, one clean poll, 20 ms quiet; 3 per 30 s), and random
runs with revival on, checking at every step that a revived context had no unfinished job and held no AS."""
import os, sys
H = os.path.dirname(os.path.abspath(__file__))
SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.join(H, '../../gpusched-20261114/test/core_test_0669.c')
s = open(SRC).read()
def rep(a, b, n=1):
    global s
    assert s.count(a) == n, (a[:70], s.count(a))
    s = s.replace(a, b)
rep("/* 0669 = the 0661 model test", "/* 0750 = the 0669 model test + MMU-fault records and revival (make_core_test_0750.py).  0669 = the 0661 model test")
rep("static bool g_pipe, g_prep, g_prio, g_pg;", "static bool g_pipe, g_prep, g_prio, g_pg, g_rev;\nstatic uint8_t prev_state[N2C_MAX_CTX]; static unsigned revivals_seen;")
rep("core.pg_on=g_pg;core.pg_idle_us=2000;npend=0;", "core.pg_on=g_pg;core.pg_idle_us=2000;core.revive_on=g_rev;memset(prev_state,0,sizeof(prev_state));revivals_seen=0;npend=0;")
# invariant: FAULTED -> OPEN only with nothing unfinished and no AS held; an OPEN context's AS is its own
rep(""" CHECK(m.busy_writes==0);""", """ CHECK(m.busy_writes==0);
 for(unsigned c=1;c<N2C_MAX_CTX;c++){const struct n2c_ctx*x=&core.ctxs[c];
  if(prev_state[c]==N2C_CTX_FAULTED&&x->state==N2C_CTX_OPEN){
   CHECK(core.revive_on);revivals_seen++;
   for(unsigned i=0;i<N2C_MAX_JOBS;i++){const struct n2c_job*j=&core.jobs[i];if(j->state!=N2C_JOB_FREE&&j->ctx==c)CHECK(j->state!=N2C_JOB_QUEUED&&j->state!=N2C_JOB_RUNNING&&j->state!=N2C_JOB_STOPPING);}
   CHECK(!x->as_live&&x->as==N2C_AS_NONE);for(unsigned a=0;a<N2C_HW_AS;a++)CHECK(core.as_owner[a]!=c);}
  if(core.as_dyn&&x->as_live)CHECK(x->as<N2C_HW_AS&&core.as_owner[x->as]==c);
  prev_state[c]=x->state;}
 for(unsigned a=1;a<N2C_HW_AS;a++)if(core.as_owner[a]!=N2C_AS_NONE)CHECK(core.as_owner[a]<N2C_MAX_CTX&&core.ctxs[core.as_owner[a]].as==a&&core.ctxs[core.as_owner[a]].state!=N2C_CTX_FREE);""")
# closing a context resets prev_state (generation changes on reopen)
rep("""   enum n2c_error e=n2c_ctx_close(&core,q->c);CHECK(e==N2C_OK||e==N2C_E_BUSY);if(e==N2C_OK){q->open=false;closes++;}}}""",
    """   enum n2c_error e=n2c_ctx_close(&core,q->c);CHECK(e==N2C_OK||e==N2C_E_BUSY);if(e==N2C_OK){q->open=false;closes++;prev_state[q->c]=0;}}}""")
rep("""  else if(a<90){if(rnd(6))continue;unsigned as=1+rnd(7);m.fault=(struct n2hw_mmu_fault){.as=as,.status=0xc1};m.fault_ready=true;faults++;poll();}""",
    """  else if(a<90){if(rnd(6))continue;unsigned as=1+rnd(7);m.fault=(struct n2hw_mmu_fault){.as=as,.status=0xc1,.address=0x1000000000ull+rnd(1u<<24)};m.fault_ready=true;faults++;poll();}""")
rep("""        core.lat.nx_two,m.races,m.starts);""", """        core.lat.nx_two,m.races,m.starts);
 if(g_rev)CHECK(revivals_seen==core.as_stats.revives);
 if(g_rev)printf("   0750: revive on: revived %llu (model saw %u) held %llu trailing %llu orphan %llu faulted-refused %llu fault records %llu\\n",(unsigned long long)core.as_stats.revives,revivals_seen,
        (unsigned long long)core.as_stats.revive_refused,(unsigned long long)core.as_stats.trail_faults,(unsigned long long)core.as_stats.orphan_faults,(unsigned long long)core.as_stats.faulted_refused,(unsigned long long)core.fault_seq);""")
rep("""\n#define P 12\n#define PB 4""", r"""
/* 0750 targeted */
static void fault_on(unsigned as,uint64_t addr){m.fault=(struct n2hw_mmu_fault){.as=as,.status=0x2c3,.address=addr};m.fault_ready=true;poll();}
static void ack_stops(void){for(unsigned k=0;k<8;k++){for(unsigned s=0;s<16;s++)if(m.sl[s].cur.v&&m.sl[s].cur.stop_pending)chain_end(s,true,0x03);poll();}}
static struct n2c_fault_rec last_rec(void){struct n2c_fault_rec r;CHECK(core.fault_seq&&n2c_fault_get(&core,core.fault_seq-1,&r)&&r.seq==core.fault_seq);return r;}
static void t_revive(void){g_where=__func__;
 g_rev=true;init(true);unsigned a=context(),b=context();uint32_t ha,hb,h2;uint64_t va,vb,v2;
 CHECK(map_pages(a,2,&ha,&va)==N2C_OK&&map_pages(b,1,&hb,&vb)==N2C_OK&&map_pages(a,1,&h2,&v2)==N2C_OK);
 uint64_t x=submit(a,ha,va,N2C_JOB_COMPUTE),y=submit(b,hb,vb,N2C_JOB_FRAGMENT);unsigned as=core.ctxs[a].as;CHECK(as<N2C_HW_AS&&core.running[1]==x);
 /* 1. the fault: context killed, record says INSIDE buffer ha, compute job on slot 1 */
 fault_on(as,va+0x1010);CHECK(core.ctxs[a].state==N2C_CTX_FAULTED&&!core.ctxs[a].as_live&&core.as_owner[as]==a);
 struct n2c_fault_rec r=last_rec();CHECK(r.cls==N2C_FC_CTX&&r.ctx==a&&r.as==as&&r.where==N2C_FW_INSIDE&&r.bo_va==va&&r.bo_size==0x2000&&r.kind==N2C_JOB_COMPUTE&&r.slot==1&&r.head==va&&r.status==0x2c3&&r.ctx_bos==2&&r.ctx_jobs==1);inv();
 /* 2. the second report of the same stalled access: trailing, not orphan, nobody else blamed */
 fault_on(as,va+0x1010);r=last_rec();CHECK(r.cls==N2C_FC_TRAIL&&r.ctx==a&&core.as_stats.trail_faults==1&&core.as_stats.orphan_faults==0&&core.ctxs[b].state==N2C_CTX_OPEN);
 struct n2c_submit sub={0};sub.kind=N2C_JOB_COMPUTE;sub.nr_handles=1;sub.handles[0]=h2;sub.write_mask=1;sub.heads[0]=v2;uint64_t id;
 CHECK(n2c_submit(&core,a,&sub,&id)==N2C_E_STATE&&core.as_stats.faulted_refused==1);
 /* 3. not revived while its stopped chain is unacknowledged, nor before the quiet time */
 m.time+=1;poll();poll();
 if(core.ctxs[a].state==N2C_CTX_FAULTED)CHECK(st(x)==N2C_JOB_STOPPING||(st(x)==N2C_JOB_FAULTED&&m.time-core.ctxs[a].fault_ms<N2C_REVIVE_QUIET_MS));   /* stop not acknowledged yet, or not quiet long enough */
 else CHECK(st(x)==N2C_JOB_FAULTED&&core.as_stats.revives==1);
 ack_stops();CHECK(st(x)==N2C_JOB_FAULTED);
 if(core.ctxs[a].state!=N2C_CTX_OPEN){m.time+=N2C_REVIVE_QUIET_MS;poll();poll();}
 CHECK(core.ctxs[a].state==N2C_CTX_OPEN&&core.as_stats.revives==1&&core.ctxs[a].as==N2C_AS_NONE&&core.as_owner[as]!=a);inv();
 /* 4. it works again: gets an AS (UPDATE + flush), its job completes; the other client never noticed */
 x=submit(a,h2,v2,N2C_JOB_COMPUTE);CHECK(core.ctxs[a].as_live);gpu_step(1,false);poll();CHECK(st(x)==N2C_JOB_DONE);
 gpu_step(0,false);poll();CHECK(st(y)==N2C_JOB_DONE);inv();
 /* 5. guard page / freed range / no buffer / orphan records */
 x=submit(a,h2,v2,N2C_JOB_COMPUTE);as=core.ctxs[a].as;fault_on(as,v2+0x1000+0x20);r=last_rec();CHECK(r.cls==N2C_FC_CTX&&r.where==N2C_FW_GUARD&&r.bo_va==v2);
 ack_stops();CHECK(core.ctxs[a].state==N2C_CTX_FAULTED);   /* acknowledged, polls drained, but not 20 ms quiet yet */
 m.time+=N2C_REVIVE_QUIET_MS-1;poll();poll();CHECK(core.ctxs[a].state==N2C_CTX_FAULTED);
 m.time+=1;poll();CHECK(core.ctxs[a].state==N2C_CTX_OPEN&&core.as_stats.revives==2);
 CHECK(n2c_bo_release(&core,a,ha)==N2C_OK);m.time+=7;
 x=submit(a,h2,v2,N2C_JOB_COMPUTE);as=core.ctxs[a].as;fault_on(as,va+0x10);r=last_rec();CHECK(r.where==N2C_FW_FREED&&r.bo_va==va&&r.freed_ago_ms==7&&r.bo_handle==ha);
 ack_stops();m.time+=N2C_REVIVE_QUIET_MS;poll();poll();CHECK(core.ctxs[a].state==N2C_CTX_OPEN&&core.as_stats.revives==3);
 /* 6. budget: the 4th fault inside 30 s stays FAULTED (counted once), revived when the window ends */
 x=submit(a,h2,v2,N2C_JOB_COMPUTE);as=core.ctxs[a].as;fault_on(as,0x7000000000ull);r=last_rec();CHECK(r.where==N2C_FW_NONE&&r.bo_va==v2);   /* nearest below */
 ack_stops();for(unsigned k=0;k<5;k++){m.time+=100;poll();}CHECK(core.ctxs[a].state==N2C_CTX_FAULTED&&core.as_stats.revive_refused==1);
 CHECK(n2c_submit(&core,a,&sub,&id)==N2C_E_STATE);
 m.time+=N2C_REVIVE_WINDOW_MS;poll();CHECK(core.ctxs[a].state==N2C_CTX_OPEN&&core.as_stats.revives==4&&core.as_stats.revive_refused==1);inv();
 /* 7. orphan: an AS nobody holds */
 unsigned freeas=N2C_AS_NONE;for(unsigned k=1;k<N2C_HW_AS&&freeas==N2C_AS_NONE;k++)if(core.as_owner[k]==N2C_AS_NONE)freeas=k;CHECK(freeas!=N2C_AS_NONE);
 fault_on(freeas,0x1234000);r=last_rec();CHECK(r.cls==N2C_FC_ORPHAN&&r.ctx==N2C_AS_NONE&&core.as_stats.orphan_faults==1);inv();
 /* 8. a job in prep at the fault: no revival before its commit cancels it */
 core.preclean=true;CHECK(n2c_submit(&core,a,&sub,&id)==N2C_OK);const struct n2c_job*pj=job(id);
 if(pj&&pj->prep){gpu_step(1,false);poll();as=core.ctxs[a].as;
  if(as<N2C_HW_AS&&core.ctxs[a].as_live){fault_on(as,v2);m.time+=1000;poll();poll();CHECK(core.ctxs[a].state==N2C_CTX_FAULTED&&st(id)==N2C_JOB_QUEUED);
   commit_one(a,id);CHECK(st(id)==N2C_JOB_CANCELLED);m.time+=N2C_REVIVE_QUIET_MS;poll();poll();CHECK(core.ctxs[a].state==N2C_CTX_OPEN);}
  else commit_one(a,id);}
 else{gpu_step(1,false);poll();}
 core.preclean=false;ack_stops();inv();
 /* 9. ring overrun: the oldest kept record is handed out */
 uint64_t base=core.fault_seq;for(unsigned k=0;k<N2C_FAULT_RING+3;k++)fault_on(freeas,0x2000000+k);
 CHECK(n2c_fault_get(&core,base,&r)&&r.seq==base+4&&r.address==0x2000000+3);CHECK(!n2c_fault_get(&core,core.fault_seq,&r));
 /* 10. kill switch: revive off = the old behaviour (FAULTED until close) */
 core.revive_on=false;x=submit(b,hb,vb,N2C_JOB_COMPUTE);as=core.ctxs[b].as;fault_on(as,vb);ack_stops();for(unsigned k=0;k<5;k++){m.time+=N2C_REVIVE_WINDOW_MS;poll();}
 CHECK(core.ctxs[b].state==N2C_CTX_FAULTED);core.revive_on=true;m.time+=1;poll();poll();CHECK(core.ctxs[b].state==N2C_CTX_OPEN);inv();
 g_rev=false;
}

#define P 12
#define PB 4""")
rep(""" t_pg();""", """ t_pg();
 t_revive();
 printf("0750 targeted: fault record (inside / guard / freed N ms ago / nearest below / orphan, job kind+slot+head), trailing report != orphan, no revival before the stop is acknowledged or the 20 ms quiet time or a pending prep commit, revived client rebinds and runs, 3 per 30 s budget then wait, ring overrun, kill switch PASS\\n");""")
rep(""" g_pg=false;
 t_random(steps,0x0661066106610661ull,false,false);""", """ g_pg=false;
 g_rev=true;
 for(unsigned i=0;i<3;i++)t_random(steps,seeds[i]^0x750,true,true);
 t_random(steps,0x0750075007500750ull,false,false);
 g_pg=true;t_random(steps,0x07500669ull,true,true);g_pg=false;
 g_rev=false;
 t_random(steps,0x0661066106610661ull,false,false);""")
rep("CORE_0669_HOST_TESTS_OK", "CORE_0750_HOST_TESTS_OK")
open(os.path.join(H, 'core_test_0750.c'), 'w').write(s)
print('core_test_0750.c written')
