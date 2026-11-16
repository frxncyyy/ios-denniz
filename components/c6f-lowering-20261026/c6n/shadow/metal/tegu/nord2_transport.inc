/* Runtime IOKit binding: no SDK dependency and no global connection table. */
#include "../nord2_metal_bridge.h"
#include "../nord2_metal_render.h"
struct nord2_raw_transport {
 unsigned port,task;void *semaphore;
 int (*call)(unsigned,unsigned,const uint64_t *,unsigned,const void *,size_t,uint64_t *,unsigned *,void *,size_t *);
 int (*map)(unsigned,unsigned,unsigned,uint64_t *,uint64_t *,unsigned);
 int (*unmap)(unsigned,unsigned,unsigned,uint64_t);
 int (*close)(unsigned);
 long (*wait)(void *,uint64_t);
 long (*signal)(void *);
 void (*release)(void *);
};
static int nord2_raw_call(void *p,unsigned s,const uint64_t *in,unsigned ni,const void *bytes,size_t size,uint64_t *out,unsigned *no,void *outb,size_t *outsz) {
 struct nord2_raw_transport *r=p;return r->call(r->port,s,in,ni,bytes,size,out,no,outb,outsz);
}
static int nord2_raw_map(void *p,unsigned h,uint64_t *a,uint64_t *size) {
 struct nord2_raw_transport *r=p;return r->map(r->port,h,r->task,a,size,1);
}
static int nord2_raw_unmap(void *p,unsigned h,uint64_t a) {
 struct nord2_raw_transport *r=p;return r->unmap(r->port,h,r->task,a);
}
static int nord2_raw_close(void *p) {struct nord2_raw_transport *r=p;return r->close(r->port);}
static void nord2_raw_lock(void *p) {struct nord2_raw_transport *r=p;(void)r->wait(r->semaphore,~(uint64_t)0);}
static void nord2_raw_unlock(void *p) {struct nord2_raw_transport *r=p;(void)r->signal(r->semaphore);}
static void nord2_raw_destroy(void *p) {struct nord2_raw_transport *r=p;r->release(r->semaphore);free(r);}
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
 struct n2mb_ops ops={.opaque=r,.call=nord2_raw_call,.map=nord2_raw_map,.unmap=nord2_raw_unmap,.close=nord2_raw_close,
  .lock=nord2_raw_lock,.unlock=nord2_raw_unlock,.destroy=nord2_raw_destroy};
 uintptr_t bridge=n2mb_open(&ops);
 if(!bridge){(void)r->close(r->port);nord2_raw_destroy(r);}
 return bridge;
}
