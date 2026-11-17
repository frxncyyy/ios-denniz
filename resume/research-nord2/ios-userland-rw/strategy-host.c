
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#define B_READ 1
#define UFS_BLOCK 4096
#define BOUNCE_SIZE (1024*1024)
#define DMA_BOUNCE 0
#define MIN(a,b) ((a)<(b)?(a):(b))
#define PART_BYTES (3*BOUNCE_SIZE)
typedef char *caddr_t;
struct buffer { uint64_t block; uint32_t count, flags, resid; int error, done; unsigned char *data; };
typedef struct buffer *buf_t;
static unsigned char bounce[BOUNCE_SIZE],disk[PART_BYTES],expected[PART_BYTES],data[2*BOUNCE_SIZE+4096];
static struct {uint64_t nblocks,first_block;uint32_t secsize;bool writable;uintptr_t dma;} su;
static int sweet_ufs_lock,locked,reads,writes,fail_read;
static uint64_t buf_blkno(buf_t b){return b->block;}
static uint32_t buf_count(buf_t b){return b->count;}
static uint32_t buf_flags(buf_t b){return b->flags;}
static void buf_setresid(buf_t b,uint32_t n){b->resid=n;}
static int buf_map(buf_t b,caddr_t *p){*p=(char*)b->data;return 0;}
static void buf_unmap(buf_t b){(void)b;assert(!locked);}
static void buf_seterror(buf_t b,int e){b->error=e;}
static void buf_biodone(buf_t b){b->done++;}
static void lck_mtx_lock(int *p){assert(p==&sweet_ufs_lock&&!locked);locked=1;}
static void lck_mtx_unlock(int *p){assert(p==&sweet_ufs_lock&&locked);locked=0;}
static int ufs_read_blocks(uint64_t lba,uint32_t n){
 assert(locked&&n&&n<=BOUNCE_SIZE/UFS_BLOCK&&lba>=su.first_block&&lba+n<=su.first_block+su.nblocks);
 reads++;if(fail_read)return EIO;memcpy(bounce,disk+(lba-su.first_block)*UFS_BLOCK,n*UFS_BLOCK);return 0;
}
static int ufs_write_blocks(uint64_t lba,uint32_t n){
 assert(locked&&su.writable&&n&&n<=BOUNCE_SIZE/UFS_BLOCK&&lba>=su.first_block&&lba+n<=su.first_block+su.nblocks);
 writes++;memcpy(disk+(lba-su.first_block)*UFS_BLOCK,bounce,n*UFS_BLOCK);return 0;
}
static void
sweet_ufs_strategy(buf_t bp)
{
	int error = 0;
	caddr_t vaddr = NULL;
	uint64_t off = (uint64_t)buf_blkno(bp) * su.secsize;
	uint32_t count = buf_count(bp);
	uint64_t part_bytes = su.nblocks * UFS_BLOCK;

	bool rd = buf_flags(bp) & B_READ;

	buf_setresid(bp, count);
	if (!rd && !su.writable) {
		error = EROFS;
		goto done;
	}
	if (off > part_bytes || count > part_bytes - off) {
		error = EINVAL;
		goto done;
	}
	if (count == 0) {
		goto done;
	}
	if (buf_map(bp, &vaddr)) {
		error = EIO;
		goto done;
	}

	lck_mtx_lock(&sweet_ufs_lock);
	for (uint32_t done = 0; done < count;) {
		uint64_t pos = off + done;
		uint64_t lba = pos / UFS_BLOCK;
		uint32_t skip = (uint32_t)(pos % UFS_BLOCK);
		uint32_t want = MIN(count - done, BOUNCE_SIZE - skip);
		uint32_t blocks = (skip + want + UFS_BLOCK - 1) / UFS_BLOCK;
		void *bounce = (void *)(su.dma + DMA_BOUNCE);
		if (rd) {
			error = ufs_read_blocks(su.first_block + lba, blocks);
			if (!error) {
				memcpy(vaddr + done, (const char *)bounce + skip, want);
			}
		} else {
			/* Partial device blocks at either end: read, merge, write back. */
			if (skip || (want % UFS_BLOCK)) {
				error = ufs_read_blocks(su.first_block + lba, blocks);
			}
			if (!error) {
				memcpy((char *)bounce + skip, vaddr + done, want);
				error = ufs_write_blocks(su.first_block + lba, blocks);
			}
		}
		if (error) {
			break;
		}
		done += want;
		buf_setresid(bp, count - done);
	}
	lck_mtx_unlock(&sweet_ufs_lock);
	buf_unmap(bp);

done:
	if (error) {
		buf_seterror(bp, error);
	}
	buf_biodone(bp);
}


static void request(uint64_t offset,uint32_t count,bool rd,int error){
 struct buffer b={.block=offset/su.secsize,.count=count,.flags=rd?B_READ:0,.data=data};
 sweet_ufs_strategy(&b);assert(b.error==error&&b.done==1&&!locked);
 assert(b.resid==(error?count:0));
 if(!error){
  if(rd)assert(!memcmp(data,expected+offset,count));
  else memcpy(expected+offset,data,count);
 }
 assert(!memcmp(disk,expected,sizeof(disk)));
}
int main(void){
 su.nblocks=PART_BYTES/UFS_BLOCK;su.first_block=3153920;su.secsize=512;su.writable=true;su.dma=(uintptr_t)bounce;
 for(unsigned i=0;i<PART_BYTES;i++)disk[i]=expected[i]=i*17+31;
 for(unsigned i=0;i<sizeof(data);i++)data[i]=i*7+19;
 const unsigned cases[][2]={{0,512},{512,512},{3584,1024},{512,1048576},{0,2097152},{PART_BYTES-512,512},{512,513}};
 for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++)request(cases[i][0],cases[i][1],false,0);
 for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++)request(cases[i][0],cases[i][1],true,0);
 for(unsigned i=0;i<500;i++){
  uint64_t offset=(rand()%(PART_BYTES/512-32))*512;unsigned count=rand()%16384;
  for(unsigned j=0;j<count;j++)data[j]=rand();request(offset,count,false,0);request(offset,count,true,0);
 }
 int old_writes=writes,old_reads=reads;
 request(PART_BYTES,512,false,EINVAL);request(PART_BYTES-512,1024,false,EINVAL);
 su.writable=false;request(0,512,false,EROFS);assert(writes==old_writes&&reads==old_reads);
 su.writable=true;fail_read=1;request(512,512,false,EIO);assert(writes==old_writes);
 puts("PASS: exact UFS strategy preserves neighboring bytes for partial4K RMW, multi-bounce I/O,500 varied cases; rejects partition overflow/RO; read failure prevents write");
}
