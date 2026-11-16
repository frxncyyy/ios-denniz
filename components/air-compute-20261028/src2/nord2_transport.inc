/* Runtime IOKit binding: no SDK dependency and no global connection table. */
#include "../nord2_metal_bridge.h"
#include "../nord2_metal_render.h"
/* Proposed IOConnectTrap fast path (ledger #12, kernel side: KA). The same
 * request as IOConnectCallMethod, without the MIG message:
 *   IOConnectTrap6(conn, NORD2_GPU_TRAP_CALL, (uintptr_t)&call, sizeof(call), 0,0,0,0)
 * The kernel copies `call` and struct_in in, runs the selector's handler on
 * the caller's thread exactly like externalMethod, copies struct_out and the
 * scalars back, stores the method's IOReturn in `result` and sets
 * done=NORD2_GPU_TRAP_DONE. XNU answers success WITHOUT calling anything when
 * a user client has no trap at the index, so support is probed once at open
 * with the side-effect-free STATUS selector and `done` decides; afterwards
 * every call uses the trap. NORD2_METAL_TRAP=0 forces IOConnectCallMethod. */
#define NORD2_GPU_TRAP_CALL 0u
#define NORD2_GPU_TRAP_MAGIC 0x4e325443u /* 'N2TC' */
#define NORD2_GPU_TRAP_DONE 0x4e325444u  /* 'N2TD' */
struct nord2_gpu_trap_call {
 uint32_t magic,selector,scalar_in_count,scalar_out_count;
 uint64_t scalar_in[4],scalar_out[4];
 uint64_t struct_in,struct_in_size,struct_out,struct_out_size;
 int32_t result;uint32_t done;
};
struct nord2_raw_transport {
 unsigned port,task;void *semaphore;
 int (*trap6)(unsigned,unsigned,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
 unsigned trap; /* 1 trap path in use, 0 MIG */
 int (*call)(unsigned,unsigned,const uint64_t *,unsigned,const void *,size_t,uint64_t *,unsigned *,void *,size_t *);
 int (*map)(unsigned,unsigned,unsigned,uint64_t *,uint64_t *,unsigned);
 int (*unmap)(unsigned,unsigned,unsigned,uint64_t);
 int (*close)(unsigned);
 long (*wait)(void *,uint64_t);
 long (*signal)(void *);
 void (*release)(void *);
};
/* One trap call; returns 1 when the kernel did not execute it (no trap). */
static int nord2_trap_call(struct nord2_raw_transport *r,unsigned s,const uint64_t *in,unsigned ni,const void *bytes,size_t size,
 uint64_t *out,unsigned *no,void *outb,size_t *outsz,int *result) {
 struct nord2_gpu_trap_call c;memset(&c,0,sizeof(c));
 if(ni>4||(no&&*no>4))return 2;
 c.magic=NORD2_GPU_TRAP_MAGIC;c.selector=s;c.scalar_in_count=ni;c.scalar_out_count=no?*no:0;
 for(unsigned i=0;i<ni;i++)c.scalar_in[i]=in[i];
 c.struct_in=(uint64_t)(uintptr_t)bytes;c.struct_in_size=bytes?size:0;
 c.struct_out=(uint64_t)(uintptr_t)outb;c.struct_out_size=outb&&outsz?*outsz:0;
 int kr=r->trap6(r->port,NORD2_GPU_TRAP_CALL,(uintptr_t)&c,sizeof(c),0,0,0,0);
 if(c.done!=NORD2_GPU_TRAP_DONE)return 1; /* not executed: no trap (or refused before running) */
 if(kr){*result=kr;return 0;}
 if(no){if(c.scalar_out_count>*no){*result=(int)0xe00002c2u;return 0;}*no=c.scalar_out_count;
  for(unsigned i=0;i<c.scalar_out_count;i++)out[i]=c.scalar_out[i];}
 if(outsz)*outsz=outb?(size_t)c.struct_out_size:0;
 *result=c.result;return 0;
}
/* TEGU_PERF_KCALL: kernel calls by class (0 SUBMIT_JM, 1 WAIT/WAIT_SEQ,
 * 2 SYNC, 3 ALLOC/RELEASE/IMPORT, 4 map/unmap, 5 other): count and time
 * inside the call, so a device log splits the submit CPU into kernel time and
 * userspace building. Counting only. */
#define TEGU_KCALL_CLASSES 6u
static U64 tegu_kcall_n[TEGU_KCALL_CLASSES],tegu_kcall_ns[TEGU_KCALL_CLASSES];
static double tegu_host_seconds(void);
static unsigned tegu_kcall_class(unsigned s) {
 return s==9u?0u:(s==4u||s==12u)?1u:s==10u?2u:(s==1u||s==2u||s==8u)?3u:5u;
}
/* TEGU_PERF_KCALL_SITE: mem (3) and map (4) calls by call site = bridge site
 * (n2mb_kcall_site) x frontend path of a client buffer (tegu_kcall_fsite). */
#define TEGU_KSITE_SLOTS (N2MB_SITES*8u)
static U64 tegu_ksite_n[TEGU_KSITE_SLOTS][2],tegu_ksite_ns[TEGU_KSITE_SLOTS][2];
static void tegu_kcall_note(unsigned cls,double t0) {
 double t=tegu_host_seconds()-t0;
 __atomic_fetch_add(&tegu_kcall_n[cls],1,__ATOMIC_RELAXED);
 if(t0>0&&t>0)__atomic_fetch_add(&tegu_kcall_ns[cls],(U64)(t*1e9),__ATOMIC_RELAXED);
 if(cls==3||cls==4) {
  unsigned site=n2mb_kcall_site();if(site>=N2MB_SITES)site=0;
  unsigned slot=site*8u+((site==N2MB_SITE_CLIENT||site==N2MB_SITE_CLIENT_MAP)?(tegu_kcall_fsite&7u):0u);
  __atomic_fetch_add(&tegu_ksite_n[slot][cls-3],1,__ATOMIC_RELAXED);
  if(t0>0&&t>0)__atomic_fetch_add(&tegu_ksite_ns[slot][cls-3],(U64)(t*1e9),__ATOMIC_RELAXED);
 }
}
static int nord2_raw_call_timed(void *p,unsigned s,const uint64_t *in,unsigned ni,const void *bytes,size_t size,uint64_t *out,unsigned *no,void *outb,size_t *outsz);
static int nord2_raw_call(void *p,unsigned s,const uint64_t *in,unsigned ni,const void *bytes,size_t size,uint64_t *out,unsigned *no,void *outb,size_t *outsz) {
 double t0=tegu_host_seconds();
 int rc=nord2_raw_call_timed(p,s,in,ni,bytes,size,out,no,outb,outsz);
 tegu_kcall_note(tegu_kcall_class(s),t0);return rc;
}
static int nord2_raw_call_timed(void *p,unsigned s,const uint64_t *in,unsigned ni,const void *bytes,size_t size,uint64_t *out,unsigned *no,void *outb,size_t *outsz) {
 struct nord2_raw_transport *r=p;
 if(r->trap){int result=0;int t=nord2_trap_call(r,s,in,ni,bytes,size,out,no,outb,outsz,&result);if(!t)return result;
  if(t==1)r->trap=0; /* the kernel stopped executing traps: MIG from now on */}
 return r->call(r->port,s,in,ni,bytes,size,out,no,outb,outsz);
}
static int nord2_raw_map(void *p,unsigned h,uint64_t *a,uint64_t *size) {
 struct nord2_raw_transport *r=p;double t0=tegu_host_seconds();
 int rc=r->map(r->port,h,r->task,a,size,1);tegu_kcall_note(4,t0);return rc;
}
static int nord2_raw_unmap(void *p,unsigned h,uint64_t a) {
 struct nord2_raw_transport *r=p;double t0=tegu_host_seconds();
 int rc=r->unmap(r->port,h,r->task,a);tegu_kcall_note(4,t0);return rc;
}
static int nord2_raw_close(void *p) {struct nord2_raw_transport *r=p;return r->close(r->port);}
static void nord2_raw_lock(void *p) {struct nord2_raw_transport *r=p;(void)r->wait(r->semaphore,~(uint64_t)0);}
static void nord2_raw_unlock(void *p) {struct nord2_raw_transport *r=p;(void)r->signal(r->semaphore);}
static void nord2_raw_destroy(void *p) {struct nord2_raw_transport *r=p;r->release(r->semaphore);free(r);}
/* The bridge is lost for good (MMU fault: the kernel disabled this context's
 * address space and refuses its work; or unreaped/unknown GPU work). Every
 * later command would fail, and a CoreAnimation client's screen would stay
 * frozen (C6t). As ansuz's CA watchdog does (CA-METAL.md: kill backboardd,
 * launchd restarts it), leave so launchd starts the process again on a fresh
 * kernel context. The bridge has already logged NORD2_METAL lost/fault lines
 * through the kernel. NORD2_METAL_LOST=stay keeps the process (refusing). */
#ifndef NORD2_METAL_LOST_EXIT_DEFAULT
#define NORD2_METAL_LOST_EXIT_DEFAULT 1
#endif
static void nord2_raw_lost(void *p) {
 (void)p;extern char *getenv(const char *);extern void _exit(int);
 const char *v=getenv("NORD2_METAL_LOST");
 int leave=v&&v[0]?v[0]=='e':NORD2_METAL_LOST_EXIT_DEFAULT; /* "exit" | "stay" */
 if(leave)_exit(75); /* EX_TEMPFAIL: launchd KeepAlive restarts it */
}
static uintptr_t nord2_transport_open(void *io,void *sys,unsigned service,unsigned task) {
 struct nord2_raw_transport *r=calloc(1,sizeof(*r));if(!r)return 0;r->task=task;
 int (*open)(unsigned,unsigned,unsigned,unsigned *)=dlsym(io,"IOServiceOpen");
 void *(*create)(long)=dlsym(sys,"dispatch_semaphore_create");
 r->call=dlsym(io,"IOConnectCallMethod");r->map=dlsym(io,"IOConnectMapMemory64");
 r->unmap=dlsym(io,"IOConnectUnmapMemory64");r->close=dlsym(io,"IOServiceClose");
 r->wait=dlsym(sys,"dispatch_semaphore_wait");r->signal=dlsym(sys,"dispatch_semaphore_signal");r->release=dlsym(sys,"dispatch_release");
 if(!open||!create||!r->call||!r->map||!r->unmap||!r->close||!r->wait||!r->signal||!r->release){free(r);return 0;}
 r->semaphore=create(1);if(!r->semaphore){free(r);return 0;}
 if(open(service,task,0,&r->port)){nord2_raw_destroy(r);return 0;}
 r->trap6=dlsym(io,"IOConnectTrap6");
 {extern char *getenv(const char *);const char *v=getenv("NORD2_METAL_TRAP");
  if(r->trap6&&!(v&&v[0]=='0'&&!v[1])) {
   /* Probe with STATUS (no side effect): the trap must execute it and agree
    * with the method call's answer shape. */
   /* Selector 0 = NORD2_GPU_SEL_STATUS, reply struct nord2_gpu_status
    * (22 x uint32, include/nord2_gpu_transport.h; not visible here). */
   uint8_t st[88];size_t stsz=sizeof(st);unsigned no=0;int result=0;
   if(!nord2_trap_call(r,0u,NULL,0,NULL,0,NULL,&no,st,&stsz,&result)&&!result&&stsz==sizeof(st))r->trap=1;
  }}
 struct n2mb_ops ops={.opaque=r,.call=nord2_raw_call,.map=nord2_raw_map,.unmap=nord2_raw_unmap,.close=nord2_raw_close,
  .lock=nord2_raw_lock,.unlock=nord2_raw_unlock,.destroy=nord2_raw_destroy,.lost=nord2_raw_lost};
 uintptr_t bridge=n2mb_open(&ops);
 if(!bridge){(void)r->close(r->port);nord2_raw_destroy(r);}
 /* c6n A/B (ui654, 60 Hz): batch cap 128 vs 32 = 11.8 vs 19.1 ms/cmd,
  * 0.26 vs 0.32 kernel jobs per draw, no failures in 871 commands. 128 is
  * the device default; NORD2_METAL_BATCH_MAX=32 (or a BM0 A/B phase)
  * restores 32. */
 else {extern char *getenv(const char *);if(!getenv("NORD2_METAL_BATCH_MAX"))(void)n2mb_batch_capacity(bridge,N2MB_BATCH_CAPACITY);}
 return bridge;
}
