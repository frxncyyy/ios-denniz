from pathlib import Path
import re,subprocess
base=Path(__file__).resolve().parent
src=Path('research/ci-39158169212/buildtree/xnu/bsd/dev/sweet_ufs.c').read_text()
fullsrc=src
src=src[src.index('#define REG_CAP'):src.index('static int\nsweet_ufs_open')]
src=re.sub(r'^static LCK_.*\n','',src,flags=re.M)
src=src.replace('return *(volatile uint32_t *)(su.regs + off);','return mock_read(off);')
src=src.replace('*(volatile uint32_t *)(su.regs + off) = v;','mock_write(off, v);')
src=src.replace('__asm__ volatile("wfe");','mock_halt();')
src += '\n' + fullsrc[fullsrc.index('static int\nsweet_ufs_init(void)'):fullsrc.index('/*\n * Persistent console log')]
pre=r'''
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <setjmp.h>
#define NORD2 1
#define NSEC_PER_SEC 1000000000ull
#define NSEC_PER_USEC 1000ull
#define TRUE 1
#define MIN(a,b) ((a)<(b)?(a):(b))
#define __builtin_arm_dsb(x) ((void)(x))
#define THREAD_UNINT 0
#define TIMEOUT_URGENCY_SYS_CRITICAL 0
#define THREAD_WAITING 0
#define THREAD_CONTINUE_NULL 0
#define PAGE_MASK 16383
#define KMA_DATA 1
#define KMA_ZERO 2
#define KMA_PERMANENT 4
#define VM_KERN_MEMORY_IOKIT 0
#define KERN_SUCCESS 0
#define DEV_BSIZE 512
#define ptoa_64(x) ((uint64_t)(x)<<14)
typedef uintptr_t vm_offset_t,addr64_t;
typedef size_t vm_size_t;
typedef uint64_t ppnum_t;
typedef void *event_t;
static int kernel_map, kernel_pmap;
static bool smp_pass;
static int nord2_smp_userland_ready(void) { return smp_pass; }
static uint64_t mock_clock, mock_pa;
static uint32_t regs[0x2300/4];
static unsigned want, reads, writes, commands, stop_point, halt_count;
static int result, fault, tur_attempts, map_failure, root_result, wanted_bad, rw_requested, registered;
static uint8_t header[4096], entries[16384], memory[0x104000];
static jmp_buf exit_probe;
static uint32_t mock_read(uint32_t);
static void mock_write(uint32_t,uint32_t);
static void mock_halt(void) { halt_count++; longjmp(exit_probe,1); }
static void nord2_stop_at(unsigned p,const char *s) {(void)s;stop_point=p;longjmp(exit_probe,1);}
static int mock_printf(const char *fmt,...) {
 va_list ap;va_start(ap,fmt);
 if (!strcmp(fmt,"NORD2 UFS: read probe result=%d\n")) result=va_arg(ap,int);
 va_end(ap); return 0;
}
#define printf mock_printf
static bool PE_parse_boot_argn(const char *s,void *p,unsigned n){
 if(!strcmp(s,"nord2stopat")){assert(n==4);memcpy(p,&want,4);return true;}
 if(!strcmp(s,"ufsrw")){assert(n==4);memcpy(p,&rw_requested,4);return true;}
 if(!strcmp(s,"ufsroot")&&wanted_bad){assert(n>=5);strcpy(p,"boot");return true;}return false;
}
typedef int DTEntry;
#define kSuccess 0
#define DEVFS_BLOCK 1
#define DEVFS_CHAR 2
#define UID_ROOT 0
#define GID_OPERATOR 5
static int sweet_ufs_lock,sweet_ufs_bdevsw,sweet_ufs_cdevsw;
static int SecureDTLookupEntry(void *r,const char *path,DTEntry *node){(void)r;assert(!strcmp(path,"/sweet-ufs"));*node=1;return 0;}
static int SecureDTGetProperty(DTEntry node,const char *name,const void **p,unsigned *n){static const uintptr_t reg[]={0x11270000,0x2300};assert(node==1&&!strcmp(name,"reg"));*p=reg;*n=sizeof(reg);return 0;}
static void lck_mtx_lock(int *p){assert(p==&sweet_ufs_lock);}
static void lck_mtx_unlock(int *p){assert(p==&sweet_ufs_lock);}
static int bdevsw_add(int n,const void *p){(void)p;assert(n==-1);return 1;}
static int cdevsw_add_with_bdev(int n,const void *p,int major){(void)p;assert(n==-1&&major==1);return 2;}
#define makedev(major,minor) (((major)<<8)|(minor))
static void devfs_make_node(int dev,int kind,int uid,int gid,int mode,const char *name){(void)dev;assert(uid==0&&gid==5&&mode==0640);assert((kind==1&&!strcmp(name,"ufsroot0"))||(kind==2&&!strcmp(name,"rufsroot0")));registered++;}

static vm_offset_t ml_io_map(vm_offset_t p,vm_size_t n){assert(p==0x11270000&&n==0x2300);return map_failure?0:(uintptr_t)regs;}
static int kmem_alloc_contig(int m,vm_offset_t *p,vm_size_t n,unsigned mask,int a,int b,int c,int d){(void)m;(void)mask;(void)a;(void)b;(void)c;(void)d;assert(n==sizeof(memory));memset(memory,0,sizeof(memory));*p=(uintptr_t)memory;return 0;}
static ppnum_t pmap_find_phys(int p,addr64_t v){(void)p;assert(v==(uintptr_t)memory);return mock_pa>>14;}
static void flush_dcache64(addr64_t p,unsigned n,int phys){assert(phys&&p>=mock_pa&&p+n<=mock_pa+sizeof(memory));}
static void clean_dcache64(addr64_t p,unsigned n,int phys){flush_dcache64(p,n,phys);}
static void nanoseconds_to_absolutetime(uint64_t n,uint64_t *v){*v=n;}
static uint64_t mach_absolute_time(void){return mock_clock+=100000000;}
static void delay(unsigned n){mock_clock+=n*1000;}
static bool ml_get_interrupts_enabled(void){return true;}
static bool ml_at_interrupt_context(void){return false;}
static int get_preemption_level(void){return 0;}
static int assert_wait_deadline_with_leeway(event_t e,int a,int b,uint64_t c,int d){(void)e;(void)a;(void)b;(void)c;(void)d;abort();}
static void thread_block(int c){(void)c;abort();}
'''
post=r'''
#undef printf
static uint32_t mock_read(uint32_t off) { assert(off<sizeof(regs)&&!(off&3));reads++;return regs[off/4]; }
static void mock_write(uint32_t off,uint32_t v) {
 assert(off<sizeof(regs)&&!(off&3));writes++;
 assert(off==REG_IS||off==REG_IE||off==REG_UTRLBA||off==REG_UTRLBAU||off==REG_UTRLRSR||off==REG_UTRLCLR||off==REG_UTRLDBR);
 if (off==REG_IS) {regs[off/4]&=~v;return;}
 if (off==REG_UTRLCLR) {assert(v==~1u);if(fault!=5)regs[REG_UTRLDBR/4]=0;return;}
 if (off==REG_UTRLBA && fault==7 && v==(uint32_t)mock_pa) return;
 regs[off/4]=v;
 if(off!=REG_UTRLDBR)return;
 assert(v==1&&regs[REG_IE/4]==0&&regs[REG_UTRLRSR/4]==1);
 assert(regs[REG_UTRLBA/4]==(uint32_t)mock_pa&&regs[REG_UTRLBAU/4]==mock_pa>>32);
 struct utrd *tr=(void *)(memory+DMA_UTRL);struct ucd *cd=(void *)(memory+DMA_UCD);
 const uint8_t *cdb=cd->cmd+16; uint32_t len=nord2_ufs_be32(cd->cmd+12);
 assert(tr->ucdba==(uint32_t)(mock_pa+DMA_UCD)&&tr->ucdbau==(mock_pa+DMA_UCD)>>32);
 assert(tr->resp==((512/4)<<16|(512/4))&&!(tr->dw0&(1<<23)));
 assert(((tr->dw0>>25)&3)==(len?2:0)&&cd->cmd[0]==1&&cd->cmd[2]==2&&cd->cmd[1]==(len?0x40:0));
 assert(nord2_ufs_probe_command(cdb,cdb[0]?10:6,len,false));
 assert(tr->dw2==15&&tr->dw1==0&&tr->dw3==0);
 if(len){assert(tr->prdt==((1024/4)<<16|1));assert(cd->prdt[0].dbc==len-1&&cd->prdt[0].addr_lo==(uint32_t)(mock_pa+DMA_BOUNCE)&&cd->prdt[0].addr_hi==(mock_pa+DMA_BOUNCE)>>32);}
 commands++;
 if(fault==4||fault==5)return;
 uint8_t *bounce=memory+DMA_BOUNCE;
 if(cdb[0]==0x25){uint32_t last=31240192-1;bounce[0]=last>>24;bounce[1]=last>>16;bounce[2]=last>>8;bounce[3]=last;bounce[4]=0;bounce[5]=0;bounce[6]=fault==3?2:16;bounce[7]=0;}
 if(cdb[0]==0x28){uint32_t lba=nord2_ufs_be32(cdb+2);if(lba==1){assert(len==4096);memcpy(bounce,header,len);if(fault==1)bounce[24]^=1;}else{assert(lba==2&&len==sizeof(entries));memcpy(bounce,entries,len);if(fault==2)bounce[33]^=1;}}
 tr->dw2=0;cd->resp[0]=0x21;cd->resp[2]=2;cd->resp[3]=su.tag;
 if(fault==6)cd->resp[3]++;
 if(fault==8&&cdb[0]==0&&!tur_attempts++){cd->resp[7]=2;}
 regs[REG_UTRLDBR/4]=0;regs[REG_IS/4]=1;
}
static void reset(unsigned point,int injected){
 memset(&su,0,sizeof(su));memset(regs,0,sizeof(regs));
 regs[REG_CAP/4]=0x3587071f;regs[REG_HCE/4]=1;regs[REG_HCS/4]=0x10f;
 regs[REG_IE/4]=0x30e75;regs[REG_UTRLBA/4]=0x483ba000;regs[REG_UTRLRSR/4]=1;
 smp_pass=false;nord2_root_owned=false;root_result=-999;wanted_bad=rw_requested=registered=0;want=point;fault=injected;reads=writes=commands=stop_point=halt_count=0;result=-999;tur_attempts=map_failure=0;mock_pa=0x150000000ull;mock_clock=0;
}
static void run(void){if(!setjmp(exit_probe))nord2_ufs_probe();}
static void restored(void){assert(stop_point==11&&!halt_count);assert(regs[REG_IE/4]==0x30e75&&regs[REG_UTRLBA/4]==0x483ba000&&regs[REG_UTRLBAU/4]==0&&regs[REG_UTRLRSR/4]==1&&regs[REG_UTRLDBR/4]==0);}
int main(int argc,char **argv){
 assert(argc==3);FILE *f=fopen(argv[1],"rb");assert(f&&fread(header,1,sizeof(header),f)==sizeof(header));fclose(f);
 f=fopen(argv[2],"rb");assert(f&&fread(entries,1,sizeof(entries),f)==sizeof(entries));fclose(f);
 reset(10,0);run();assert(!reads&&!writes&&!commands&&!stop_point);
 reset(11,0);map_failure=1;run();assert(result==ENXIO&&!commands&&!writes);
 reset(11,0);regs[REG_UTRLDBR/4]=2;run();assert(result==ENXIO&&!commands&&!writes);
 reset(11,0);regs[0x78/4]=1;run();assert(result==ENXIO&&!commands&&!writes);
 reset(11,0);regs[REG_CAP/4]&=~CAP_64AS;run();assert(result==ENXIO&&!commands&&!writes);
 reset(11,0);run();assert(result==0&&commands==4);restored();
 for(int fault_case=1;fault_case<=8;fault_case++){
  reset(11,fault_case);run();
  if(fault_case==5){assert(halt_count==1&&!stop_point&&result==-999&&regs[REG_UTRLDBR/4]==1);}
  else{restored();assert(result==(fault_case==8?0:fault_case==3?EINVAL:fault_case==4?ETIMEDOUT:fault_case==7?ENXIO:EIO));}
 }
 reset(11,0);su.probe_only=true;
 for(unsigned op=0;op<256;op++){uint8_t cdb[16]={op};if(op!=0&&op!=0x25&&op!=0x28)assert(ufs_scsi(cdb,10,4096,false)==EPERM);assert(ufs_scsi(cdb,10,4096,true)==EPERM);}
 assert(!commands&&!writes);
 uint8_t read10[10]={0x28,0,0,0,0,63,0,0,1,0};assert(nord2_ufs_probe_command(read10,10,4096,false));
 read10[8]=2;assert(!nord2_ufs_probe_command(read10,10,8192,false));
 read10[5]=0;assert(!nord2_ufs_probe_command(read10,10,8192,false));
 assert(nord2_ufs_crc32((const uint8_t *)"123456789",9)==0xcbf43926);
 reset(12,0);rw_requested=1;
 if(!setjmp(exit_probe)){root_result=sweet_ufs_init();assert(root_result==0&&su.ready&&!su.writable&&su.lun==2&&registered==2);assert(su.first_block==3153920&&su.nblocks==28082168);assert(ufs_write_blocks(su.first_block,1)==EPERM);nord2_ufs_root_stop();}
 assert(stop_point==12&&!nord2_root_owned);stop_point=11;restored();
 reset(12,1);if(!setjmp(exit_probe))root_result=sweet_ufs_init();assert(stop_point==12&&!su.ready&&!nord2_root_owned&&!registered);stop_point=11;restored();
 reset(12,4);if(!setjmp(exit_probe))root_result=sweet_ufs_init();assert(stop_point==12&&!su.ready&&!nord2_root_owned&&!registered);stop_point=11;restored();
 reset(12,0);wanted_bad=1;assert(sweet_ufs_init()==EINVAL&&!writes&&!commands&&!registered);
 reset(22,0);assert(sweet_ufs_init()==EPERM&&!reads&&!writes&&!commands&&!su.ready);
 reset(22,0);smp_pass=true;rw_requested=1;
 assert(sweet_ufs_init()==0&&su.ready&&!su.writable&&nord2_root_owned&&su.lun==2);
 assert(ufs_write_blocks(su.first_block,1)==EPERM);
 assert(nord2_ufs_prepare_return()==1&&!nord2_root_owned);
 reset(0,0);assert(sweet_ufs_init()==0&&su.ready&&!su.writable&&nord2_root_owned);assert(su.first_block==3153920&&su.nblocks==28082168);nord2_ufs_release_root();assert(!nord2_root_owned&&!stop_point);
 puts("PASS: root publication, forced read-only point12, userdata extents, failure cleanup, other-partition refusal; exact probe code, real GPT fixtures, DMA descriptors, no-write CDB guard, inactive gate, host refusal, success, unit attention, bad CRC/capacity/tag, takeover refusal, timeout cleanup, unsafe-DMA halt");
}
'''
(base/'probe-host-check.c').write_text(pre+src+post)
subprocess.run(['cc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined',str(base/'probe-host-check.c'),'-o',str(base/'probe-host-check')],check=True)
subprocess.run([str(base/'probe-host-check'),'backup/nord2/ufs-baseline/header.bin','backup/nord2/ufs-baseline/entries.bin'],check=True)
