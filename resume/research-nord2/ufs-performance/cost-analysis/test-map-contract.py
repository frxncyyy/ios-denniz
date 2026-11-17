from pathlib import Path
import subprocess,tempfile
b=Path(__file__).resolve().parent;root=b.parents[3]
s=(root/'research/ci-39158169212/buildtree/xnu/bsd/vfs/vfs_bio.c').read_text()
source=s[s.index('static errno_t\nbuf_map_range_internal('):s.index('\n\nvoid\nbuf_clear(',s.index('static errno_t\nbuf_map_range_internal('))]
mock=r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
typedef int errno_t;typedef int boolean_t;typedef int vm_prot_t;typedef uintptr_t vm_offset_t;typedef int kern_return_t;typedef char* caddr_t;
#define true 1
#define false 0
#define VM_PROT_DEFAULT 3
#define VM_PROT_READ 1
#define VM_PROT_WRITE 2
#define VM_PROT_NONE 0
#define KERN_SUCCESS 0
#define B_CLUSTER 1
#define B_PAGEIO 2
#define B_READ 4
#define B_AGE 8
#define BL_IOBUF 1
struct buf {unsigned b_flags,b_lflags;struct buf*b_real_bp;char*b_datap;void*b_upl;unsigned b_uploffset,b_bcount;};typedef struct buf* buf_t;
static char pages[0x20000];static unsigned mapped,unmapped,offset,length,fail;
static int ubc_upl_map(void*u,uintptr_t*out){assert(u);mapped++;offset=0;length=sizeof(pages);*out=(uintptr_t)pages;return fail?1:0;}
static int ubc_upl_map_range(void*u,unsigned off,unsigned len,int prot,uintptr_t*out){assert(u&&off+len<=sizeof(pages)&&prot==3);mapped++;offset=off;length=len;*out=(uintptr_t)(pages+off);return fail?1:0;}
static int ubc_upl_unmap(void*u){assert(u);unmapped++;return 0;}
static int ubc_upl_unmap_range(void*u,unsigned off,unsigned len){assert(u&&off==offset&&len==length);unmapped++;return 0;}
'''
with tempfile.TemporaryDirectory() as td:
 c=Path(td)/'test.c';exe=Path(td)/'test'
 c.write_text(mock+source+r'''
int main(void){
 for(unsigned clustered=0;clustered<2;clustered++)for(unsigned real=0;real<2;real++)for(unsigned read=0;read<2;read++)for(unsigned pageio=0;pageio<2;pageio++)for(unsigned failure=0;failure<2;failure++){
  struct buf parent={.b_datap=pages+32},a={.b_flags=(clustered?B_CLUSTER:0)|(read?B_READ:0)|(pageio?B_PAGEIO:0),.b_lflags=BL_IOBUF,.b_real_bp=real?&parent:0,.b_datap=pages+16,.b_upl=pages,.b_uploffset=123,.b_bcount=8192},b=a;
  caddr_t old=0,new=0;fail=failure;mapped=unmapped=0;int e1=buf_map(&a,&old);if(!e1)buf_unmap(&a);unsigned oldmaps=mapped,oldunmaps=unmapped;
  mapped=unmapped=0;int e2=buf_map_range(&b,&new);if(!e2)buf_unmap_range(&b);
  assert(e1==e2&&old==new&&a.b_flags==b.b_flags&&mapped==oldmaps&&unmapped==oldunmaps);
  if(clustered&&!real&&!failure)assert(offset==123&&length==8192);
 }
 return 0;}
''')
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
print('PASS exact XNU buf_map/range paired contract: mapped pointer, fast paths, failures, B_AGE, unmap ownership; requested range shrinks to bp offset/count')
