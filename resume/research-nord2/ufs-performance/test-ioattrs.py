#!/usr/bin/env python3
"""Exact candidate ioctl + actual VFS query sequence. No device operations."""
from pathlib import Path
import re,subprocess,tempfile
P=Path(__file__).resolve().parent;ROOT=P.parents[2]
s=(P/'sweet_ufs.c').read_text();base=(P/'sweet_ufs.base.c').read_text()
a=s.index('static int\nsweet_ufs_ioctl(');b=s.index('\nstatic int\nsweet_ufs_bioctl',a);ioctl=s[a:b]
vfs=(ROOT/'research/ci-39158169212/buildtree/xnu/bsd/vfs/vfs_subr.c').read_text()
a=vfs.index('\tmp->mnt_maxreadcnt = MAX_UPL_SIZE_BYTES;',vfs.index('vfs_init_io_attributes('));b=vfs.index('\tmp->mnt_ioscale = MNT_IOSCALE(mp->mnt_ioqueue_depth);',a)+len('\tmp->mnt_ioscale = MNT_IOSCALE(mp->mnt_ioqueue_depth);');query=vfs[a:b]
cluster=(ROOT/'research/ci-39158169212/buildtree/xnu/bsd/vfs/vfs_cluster.c').read_text();a=cluster.index('uint32_t\ncluster_max_io_size(');b=cluster.index('\n}',a)+2;cluster=cluster[a:b]
commands=sorted(set(re.findall(r'\bDKIOC\w+',ioctl+query)))
code=r'''
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#define __unused __attribute__((unused))
typedef uint32_t u_int32_t;typedef uint16_t u_int16_t;typedef unsigned dev_t;typedef unsigned long u_long;typedef char *caddr_t;struct proc;
static struct {uint32_t secsize;uint64_t nblocks;bool writable;}su;
static int sweet_ufs_lock;
static void lck_mtx_lock(int *p){assert(p==&sweet_ufs_lock);}
static void lck_mtx_unlock(int *p){assert(p==&sweet_ufs_lock);}
static int ufs_sync_cache(void){assert(0);return EIO;}
#define UFS_BLOCK 4096u
#define BOUNCE_SIZE 1048576u
#define DEV_BSIZE 512u
'''+''.join(f'#define {name} {i+1}u\n' for i,name in enumerate(commands))+ioctl+r'''
#define MAX_UPL_SIZE_BYTES (64u*1024u*1024u)
#define MAX_UPL_TRANSFER_BYTES 1048576u
#define MNT_DEFAULT_IOQUEUE_DEPTH 32u
#define MNT_IOSCALE(n) (((n)+31)/32)
#define PAGE_SIZE 16384u
#define PAGE_MASK (PAGE_SIZE-1)
#define PAGE_SHIFT 14u
#define MNTK_VIRTUALDEV 1u
#define MNT_REMOVABLE 2u
#define MNTK_SSD 4u
#define CL_READ 1
#define CL_WRITE 2
#define min(a,b) ((a)<(b)?(a):(b))
struct mount {uint32_t mnt_kern_flag,mnt_flag,mnt_maxreadcnt,mnt_maxwritecnt,
 mnt_segreadcnt,mnt_segwritecnt,mnt_maxsegreadsize,mnt_maxsegwritesize,
 mnt_alignmentmask,mnt_ioqueue_depth,mnt_ioscale;};
typedef struct mount *mount_t;
static unsigned last_command;
static int model_ioctl(unsigned cmd,char *data){last_command=cmd;return sweet_ufs_ioctl(0,cmd,data,0,0,0);}
#define VNOP_IOCTL(v,c,d,f,x) model_ioctl(c,d)
static int actual_vfs_query(mount_t mp){
 int error;uint64_t readblockcnt=0,writeblockcnt=0,readmaxcnt=0,writemaxcnt=0,
 readsegcnt=0,writesegcnt=0,readsegsize=0,writesegsize=0,alignment=0,temp;
 uint32_t ioqueue_depth=0,features,blksize=su.secsize;int isvirtual=0,isssd=0;
'''+query+r'''
 return 0;
}
'''+cluster+r'''
int main(void){
 su.secsize=512;su.nblocks=1024;su.writable=true;
 struct {unsigned char before[8];uint32_t value;unsigned char after[8];}guard;
 memset(&guard,0xa5,sizeof(guard));assert(!sweet_ufs_ioctl(0,DKIOCGETFEATURES,(char *)&guard.value,0,0,0));
 assert(guard.value==0);for(unsigned i=0;i<8;i++)assert(guard.before[i]==0xa5&&guard.after[i]==0xa5);
 struct mount m={.mnt_maxreadcnt=65536,.mnt_maxwritecnt=65536,.mnt_segreadcnt=32,.mnt_segwritecnt=32,
 .mnt_maxsegreadsize=65536,.mnt_maxsegwritesize=65536,.mnt_ioqueue_depth=32,.mnt_ioscale=1};
 assert(actual_vfs_query(&m)==ENOTTY&&last_command==DKIOCGETMAXBYTECOUNTREAD);
 assert(m.mnt_maxreadcnt==MAX_UPL_SIZE_BYTES&&m.mnt_maxwritecnt==MAX_UPL_SIZE_BYTES);
 assert(m.mnt_segreadcnt==32&&m.mnt_segwritecnt==32&&m.mnt_ioqueue_depth==32&&m.mnt_ioscale==1);
 assert(cluster_max_io_size(&m,CL_READ)==1048576&&cluster_max_io_size(&m,CL_WRITE)==1048576);
 uint64_t n=0;assert(!sweet_ufs_ioctl(0,DKIOCGETMAXBLOCKCOUNTREAD,(char *)&n,0,0,0)&&n==2048);
 assert(!sweet_ufs_ioctl(0,DKIOCGETMAXSEGMENTCOUNTREAD,(char *)&n,0,0,0)&&n==1);
 assert(sweet_ufs_ioctl(0,DKIOCGETMAXBYTECOUNTREAD,(char *)&n,0,0,0)==ENOTTY);
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='nord2-ioattrs-') as td:
 p=Path(td);c=p/'test.c';exe=p/'test';c.write_text(code)
 subprocess.run(['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print('PASS: exact candidate32-bit FEATURES0/canaries; actual VFS query exits at missing MAXBYTECOUNTREAD; nonzero64MiB/32seg/32queue defaults; actual cluster max1MiB; ASan+UBSan')
