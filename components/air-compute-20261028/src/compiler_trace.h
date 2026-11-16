/* Opt-in diagnostic records for the first sixteen compositor pipelines.
 * No resource contents; image addresses are emitted only in diagnostic builds. */
#ifndef TEGU_COMPILER_TRACE_H
#define TEGU_COMPILER_TRACE_H
#include <stddef.h>
#include <stdint.h>
typedef int32_t (*tegu_task_info_fn)(uint32_t,int,uint32_t *,uint32_t *);
struct tegu_compiler_trace {
 unsigned capture; U64 (*ticks)(void); tegu_task_info_fn task_info; uint32_t task_port;
};
#ifdef TEGU_COMPILER_TRACE
#include "compiler_trace_sha256.h"
/* TASK_VM_INFO revision 1, as defined by the pinned tegu XNU task_info.h.
 * Keep this SDK-free prefix local: iOS headers are not part of the bundle SDK. */
typedef uint32_t tegu_mach_port_t;
typedef int32_t tegu_kern_return_t;
typedef uint32_t tegu_mach_msg_type_number_t;
struct tegu_task_vm_info_rev1 {
 U64 virtual_size; int region_count,page_size;
 U64 resident_size,resident_size_peak,device,device_peak,internal,internal_peak;
 U64 external,external_peak,reusable,reusable_peak,purgeable_volatile_pmap;
 U64 purgeable_volatile_resident,purgeable_volatile_virtual,compressed,compressed_peak;
 U64 compressed_lifetime,phys_footprint;
};
_Static_assert(offsetof(struct tegu_task_vm_info_rev1,resident_size)==16,"TASK_VM_INFO rev1 resident offset");
_Static_assert(offsetof(struct tegu_task_vm_info_rev1,internal)==48,"TASK_VM_INFO rev1 internal offset");
_Static_assert(offsetof(struct tegu_task_vm_info_rev1,external)==64,"TASK_VM_INFO rev1 external offset");
_Static_assert(offsetof(struct tegu_task_vm_info_rev1,reusable)==80,"TASK_VM_INFO rev1 reusable offset");
_Static_assert(offsetof(struct tegu_task_vm_info_rev1,phys_footprint)==144,"TASK_VM_INFO rev1 footprint offset");
_Static_assert(sizeof(struct tegu_task_vm_info_rev1)==152,"TASK_VM_INFO rev1 prefix size");
#define TEGU_TASK_VM_INFO 22
#define TEGU_TASK_VM_INFO_REV1_COUNT 38
static void tegu_compiler_phase(struct tegu_compiler_trace trace,const char *phase,
 int result,NSUInteger vertexBytes,NSUInteger fragmentBytes) {
 if(trace.capture>=16)return;
 char line[224];
 int n=snprintf(line,sizeof(line),
  "TEGU_COMPILER_PHASE capture=%u phase=%s result=%d ticks=%llu vs_bytes=%lu fs_bytes=%lu\n",
  trace.capture,phase,result,(unsigned long long)(trace.ticks?trace.ticks():0),
  (unsigned long)vertexBytes,(unsigned long)fragmentBytes);
 if(n>0&&(NSUInteger)n<sizeof(line))write(2,line,(NSUInteger)n);
}
static void tegu_compiler_memory(struct tegu_compiler_trace trace,const char *phase) {
 if(trace.capture>=16)return;
 if(!trace.task_info) {
  char line[160];int n=snprintf(line,sizeof(line),"TEGU_COMPILER_MEMORY capture=%u phase=%s rc=-1 count=0 valid=0\n",trace.capture,phase);
  if(n>0&&(NSUInteger)n<sizeof(line))write(2,line,(NSUInteger)n);
  return;
 }
 struct tegu_task_vm_info_rev1 vm;
 tegu_mach_msg_type_number_t count=TEGU_TASK_VM_INFO_REV1_COUNT;
 tegu_kern_return_t kr=trace.task_info(trace.task_port,TEGU_TASK_VM_INFO,(uint32_t *)&vm,&count);
 char line[224];int n;
 if(kr==0&&count>=TEGU_TASK_VM_INFO_REV1_COUNT)
  n=snprintf(line,sizeof(line),"TEGU_COMPILER_MEMORY capture=%u phase=%s rc=0 count=%u resident=%llu internal=%llu external=%llu reusable=%llu footprint=%llu\n",
   trace.capture,phase,(unsigned)count,(unsigned long long)vm.resident_size,(unsigned long long)vm.internal,
   (unsigned long long)vm.external,(unsigned long long)vm.reusable,(unsigned long long)vm.phys_footprint);
 else
  n=snprintf(line,sizeof(line),"TEGU_COMPILER_MEMORY capture=%u phase=%s rc=%d count=%u valid=0\n",trace.capture,phase,kr,(unsigned)count);
 if(n>0&&(NSUInteger)n<sizeof(line))write(2,line,(NSUInteger)n);
}
static inline void tegu_compiler_hash(struct tegu_compiler_trace trace,const char *phase,const char *stage,const void *bytes,NSUInteger length) {
 if(trace.capture>=16)return;
 char digest[65],line[192];int valid=length>0&&length<=TEGU_COMPILER_TRACE_HASH_MAX&&
  tegu_compiler_trace_sha256(bytes,(size_t)length,digest)==0;
 int n=valid?snprintf(line,sizeof(line),"TEGU_COMPILER_HASH capture=%u phase=%s stage=%s bytes=%lu sha256=%s\n",
  trace.capture,phase,stage,(unsigned long)length,digest):
  snprintf(line,sizeof(line),"TEGU_COMPILER_HASH capture=%u phase=%s stage=%s bytes=%lu valid=0\n",
  trace.capture,phase,stage,(unsigned long)length);
 if(n>0&&(NSUInteger)n<sizeof(line))write(2,line,(NSUInteger)n);
}
/* Hex keeps arbitrary diagnostic blobs reconstructable across the short,
 * line-oriented console transport.  Each row remains below its truncation
 * threshold and carries an offset so dropped rows are detectable. */
static void tegu_compiler_blob(struct tegu_compiler_trace trace,const char *kind,const void *bytes,NSUInteger length) {
 if(trace.capture>=16||!kind||!bytes||!length||length>TEGU_COMPILER_TRACE_HASH_MAX)return;
 static const char hex[]="0123456789abcdef";const unsigned char *p=(const unsigned char *)bytes;
 for(NSUInteger offset=0;offset<length;offset+=64) {
  NSUInteger count=length-offset<64?length-offset:64;char line[224];
  int head=snprintf(line,sizeof(line),"TEGU_COMPILER_BLOB capture=%u kind=%s offset=%lu total=%lu data=",
   trace.capture,kind,(unsigned long)offset,(unsigned long)length);
  if(head<=0||(NSUInteger)head+count*2+1>sizeof(line))return;
  for(NSUInteger i=0;i<count;i++){line[head+i*2]=hex[p[offset+i]>>4];line[head+i*2+1]=hex[p[offset+i]&15];}
  line[head+count*2]='\n';write(2,line,(NSUInteger)head+count*2+1);
 }
}
static struct tegu_compiler_trace tegu_compiler_trace_begin(void) {
 static unsigned reported;
 struct tegu_compiler_trace trace={__atomic_fetch_add(&reported,1,__ATOMIC_RELAXED),0,0,0};
 if(trace.capture>=16)return trace;
#ifdef TEGU_FRONTEND_HOST
 void *scope=0;
#else
 void *scope=(void *)(long)-2; /* Darwin RTLD_DEFAULT. */
#endif
 trace.ticks=(U64 (*)(void))dlsym(scope,"mach_absolute_time");
 trace.task_info=(tegu_task_info_fn)dlsym(scope,"task_info");
 uint32_t *task=(uint32_t *)dlsym(scope,"mach_task_self_");
 if(task)trace.task_port=*task;
 else trace.task_info=0;
 struct tegu_dl_info {const char *path;void *base;const char *symbol;void *address;};
 int (*lookup)(const void *,struct tegu_dl_info *)=
  (int (*)(const void *,struct tegu_dl_info *))dlsym(scope,"dladdr");
 struct tegu_dl_info info={0};
 int found=lookup?lookup((const void *)tegu_compiler_trace_begin,&info):0;
 char line[192];
 int n=snprintf(line,sizeof(line),
  "TEGU_COMPILER_IMAGE capture=%u lookup=%d base=0x%llx anchor=0x%llx\n",
  trace.capture,found,(unsigned long long)(U64)(unsigned long)info.base,
  (unsigned long long)(U64)(unsigned long)tegu_compiler_trace_begin);
 if(n>0&&(NSUInteger)n<sizeof(line))write(2,line,(NSUInteger)n);
 tegu_compiler_phase(trace,"pipeline_begin",0,0,0);
 return trace;
}
#else
static struct tegu_compiler_trace tegu_compiler_trace_begin(void) {
 return (struct tegu_compiler_trace){0xffffffffu,0,0,0};
}
#define tegu_compiler_phase(trace,phase,result,vertexBytes,fragmentBytes) ((void)(trace))
#define tegu_compiler_memory(trace,phase) ((void)(trace))
#define tegu_compiler_hash(trace,phase,stage,bytes,length) ((void)(trace))
#define tegu_compiler_blob(trace,kind,bytes,length) ((void)(trace))
#endif
#endif
