
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
#define REG_CAP         0x00
#define REG_IS          0x20
#define REG_IE          0x24
#define REG_HCS         0x30
#define REG_HCE         0x34
#define REG_UTRLBA      0x50
#define REG_UTRLBAU     0x54
#define REG_UTRLDBR     0x58
#define REG_UTRLCLR     0x5c
#define REG_UTRLRSR     0x60

#define HCS_DP          (1u << 0)
#define HCS_UTRLRDY     (1u << 1)
#define CAP_64AS        (1u << 24)              /* 64-bit DMA addressing */

#define UFS_BLOCK       4096u
#define BOUNCE_SIZE     (1u << 20)
#define PRDT_CHUNK      (256u << 10)            /* PRDT byte count field: 18 bits */
#define PRDT_MAX        (BOUNCE_SIZE / PRDT_CHUNK)
#define CMD_TIMEOUT_NS  (5ull * NSEC_PER_SEC)
#define CMD_SPIN_NS     (50ull * NSEC_PER_USEC)
#define CMD_SLEEP_NS    (200ull * NSEC_PER_USEC)

struct utrd {
	uint32_t dw0, dw1, dw2, dw3;
	uint32_t ucdba, ucdbau;
	uint32_t resp;          /* [31:16] response UPIU offset, [15:0] length, DWORDs */
	uint32_t prdt;          /* [31:16] PRDT offset (DWORDs), [15:0] entries */
};

struct ucd {
	uint8_t cmd[512];
	uint8_t resp[512];
	struct {
		uint32_t addr_lo, addr_hi, rsvd, dbc;
	} prdt[PRDT_MAX];
};

/* Layout of the contiguous DMA allocation. */
#define DMA_UTRL        0x0000                  /* 1 KiB aligned */
#define DMA_UCD         0x1000                  /* 128 byte aligned */
#define DMA_BOUNCE      0x4000
#define DMA_SIZE        (DMA_BOUNCE + BOUNCE_SIZE)

static struct {
	vm_offset_t regs;
	vm_offset_t dma;                        /* kernel VA of the DMA allocation */
	uint64_t dma_pa;
	uint8_t lun;
	uint8_t tag;
	uint64_t first_block, nblocks;          /* partition, in 4 KiB device blocks */
	uint32_t secsize;                       /* logical sector size for the BSD layer */
	bool writable;                          /* boot-arg ufsrw=1 */
	bool cache;                             /* boot-arg ufscache=1 */
	bool wait;                              /* boot-arg ufswait=1 */
	int bmajor, cmajor;
	dev_t bdev;
	bool ready;
#if defined(NORD2)
	bool probe_only;                        /* bounded diagnostic, no media writes */
#endif
} su;


static inline uint32_t
ufs_rd(uint32_t off)
{
	return mock_read(off);
}

static inline void
ufs_wr(uint32_t off, uint32_t v)
{
	mock_write(off, v);
}

static void
dma_sync(uint32_t off, uint32_t len)
{
	flush_dcache64((addr64_t)(su.dma_pa + off), len, TRUE);
}

static void
dma_clean(uint32_t off, uint32_t len)
{
	clean_dcache64((addr64_t)(su.dma_pa + off), len, TRUE);
}

#if defined(NORD2)
static bool nord2_ufs_wait(uint32_t off, uint32_t mask, uint32_t value);
/*
 * The pre-root diagnostic accepts exactly TUR, READ CAPACITY(10), and
 * READ(10) of the primary GPT area (LBAs 1..63). No arbitrary CDB, media
 * write, cache flush, root publication or Qualcomm peripheral startup.
 */
static bool
nord2_ufs_probe_command(const uint8_t *cdb, unsigned n, uint32_t len, bool write)
{
	if (write || !cdb || n == 0 || n > 16) {
		return false;
	}
	if (cdb[0] == 0x00 || cdb[0] == 0x25) {
		if (n != (cdb[0] == 0 ? 6u : 10u) || len != (cdb[0] == 0 ? 0u : 8u)) {
			return false;
		}
		for (unsigned i = 1; i < n; i++) {
			if (cdb[i]) {
				return false;
			}
		}
		return true;
	}
	if (cdb[0] != 0x28 || n != 10 || cdb[1] || cdb[6] || cdb[9]) {
		return false;
	}
	uint32_t lba = (uint32_t)cdb[2] << 24 | (uint32_t)cdb[3] << 16 |
	    (uint32_t)cdb[4] << 8 | cdb[5];
	uint32_t count = (uint32_t)cdb[7] << 8 | cdb[8];
	return lba >= 1 && lba < 64 && count >= 1 && count <= 64 - lba &&
	       len == count * UFS_BLOCK;
}
#endif

/*
 * One SCSI command in slot 0, data (if any) to/from the bounce buffer
 * (to the device when write is set). Returns 0 or an errno.
 */
static int
ufs_scsi(const uint8_t *cdb, unsigned cdb_len, uint32_t len, bool write)
{
#if defined(NORD2)
	if (su.probe_only && !nord2_ufs_probe_command(cdb, cdb_len, len, write)) {
		return EPERM;
	}
#endif
	struct utrd *utrd = (struct utrd *)(su.dma + DMA_UTRL);
	struct ucd *ucd = (struct ucd *)(su.dma + DMA_UCD);
	uint64_t ucd_pa = su.dma_pa + DMA_UCD, bounce_pa = su.dma_pa + DMA_BOUNCE;

	assert(len <= BOUNCE_SIZE);
	bzero(utrd, sizeof(*utrd));
	bzero(ucd, sizeof(*ucd));

	ucd->cmd[0] = 0x01;                     /* COMMAND UPIU */
	ucd->cmd[1] = len ? (write ? 0x20 : 0x40) : 0x00;      /* W: data out, R: data in */
	ucd->cmd[2] = su.lun;
	ucd->cmd[3] = ++su.tag;
	ucd->cmd[12] = (uint8_t)(len >> 24);
	ucd->cmd[13] = (uint8_t)(len >> 16);
	ucd->cmd[14] = (uint8_t)(len >> 8);
	ucd->cmd[15] = (uint8_t)len;
	memcpy(&ucd->cmd[16], cdb, cdb_len);

	/* DD: 1 = memory to device, 2 = device to memory. */
	utrd->dw0 = (1u << 28) | ((len ? (write ? 1u : 2u) : 0u) << 25) | (1u << 24);
	utrd->dw2 = 0x0f;                       /* OCS invalid until updated */
	utrd->ucdba = (uint32_t)ucd_pa;
	utrd->ucdbau = (uint32_t)(ucd_pa >> 32);
	utrd->resp = (512u / 4) << 16 | (512u / 4);
	if (len) {
		uint32_t n = 0;
		for (uint32_t done = 0; done < len; done += PRDT_CHUNK, n++) {
			uint32_t chunk = MIN(PRDT_CHUNK, len - done);
			ucd->prdt[n].addr_lo = (uint32_t)(bounce_pa + done);
			ucd->prdt[n].addr_hi = (uint32_t)((bounce_pa + done) >> 32);
			ucd->prdt[n].dbc = chunk - 1;
		}
		utrd->prdt = (1024u / 4) << 16 | n;
	}

	if (su.cache) {
		dma_clean(DMA_UTRL, sizeof(*utrd));
		dma_clean(DMA_UCD, 16 + cdb_len);
		dma_sync(DMA_UCD + offsetof(struct ucd, resp), sizeof(ucd->resp));
		if (len) {
			dma_clean(DMA_UCD + offsetof(struct ucd, prdt),
			    (utrd->prdt & 0xffff) * sizeof(ucd->prdt[0]));
		}
	} else {
		dma_sync(DMA_UTRL, sizeof(*utrd));
		dma_sync(DMA_UCD, sizeof(*ucd));
	}
	if (len) {
		if (su.cache && write) {
			dma_clean(DMA_BOUNCE, len);
		} else {
			dma_sync(DMA_BOUNCE, len);
		}
	}
	__builtin_arm_dsb(0xf);

	ufs_wr(REG_UTRLDBR, 1);
	uint64_t deadline;
	nanoseconds_to_absolutetime(CMD_TIMEOUT_NS, &deadline);
	deadline += mach_absolute_time();
	bool sleepable = su.wait && su.ready && ml_get_interrupts_enabled() &&
	    !ml_at_interrupt_context() && get_preemption_level() == 0;
	uint64_t spin_until = 0, sleep_interval = 0;
	if (sleepable) {
		nanoseconds_to_absolutetime(CMD_SPIN_NS, &spin_until);
		spin_until += mach_absolute_time();
		nanoseconds_to_absolutetime(CMD_SLEEP_NS, &sleep_interval);
	}
	while (ufs_rd(REG_UTRLDBR) & 1) {
		uint64_t now = mach_absolute_time();
		if (now > deadline) {
			ufs_wr(REG_UTRLCLR, ~1u);
			printf("SWEET UFS: command 0x%02x timed out\n", cdb[0]);
			return ETIMEDOUT;
		}
		if (sleepable && now >= spin_until) {
			uint64_t wake = now + MIN(sleep_interval, deadline - now);
			/* Keep the mutex: slot 0 and the bounce buffer are still owned. */
			if (assert_wait_deadline_with_leeway((event_t)&su.wait, THREAD_UNINT,
			    TIMEOUT_URGENCY_SYS_CRITICAL, wake, 0) == THREAD_WAITING) {
				thread_block(THREAD_CONTINUE_NULL);
			}
		} else {
			delay(1);
		}
	}
	__builtin_arm_dsb(0xf);
	ufs_wr(REG_IS, ufs_rd(REG_IS));         /* acknowledge completion status */

	dma_sync(DMA_UTRL, sizeof(*utrd));
	if (su.cache) {
		dma_sync(DMA_UCD + offsetof(struct ucd, resp), sizeof(ucd->resp));
	} else {
		dma_sync(DMA_UCD, sizeof(*ucd));
	}
	if (len && !(su.cache && write)) {
		dma_sync(DMA_BOUNCE, len);
	}

	uint32_t ocs = utrd->dw2 & 0xff;
	if (ocs || ucd->resp[0] != 0x21 || ucd->resp[6] || ucd->resp[7]) {
		printf("SWEET UFS: command 0x%02x failed: ocs 0x%x upiu 0x%x response 0x%x status 0x%x\n",
		    cdb[0], ocs, ucd->resp[0], ucd->resp[6], ucd->resp[7]);
		return EIO;
	}
#if defined(NORD2)
	if (ucd->resp[2] != su.lun || ucd->resp[3] != su.tag ||
	    (ucd->resp[1] & 0x60)) {
		printf("NORD2 UFS: response identity or transfer length mismatch\n");
		return EIO;
	}
#endif
	return 0;
}

/* READ(10) of count device blocks at lba into the bounce buffer. */
static int
ufs_read_blocks(uint64_t lba, uint32_t count)
{
	if (lba > UINT32_MAX || count > BOUNCE_SIZE / UFS_BLOCK) {
		return EINVAL;
	}
	uint8_t cdb[10] = {
		0x28, 0,
		(uint8_t)(lba >> 24), (uint8_t)(lba >> 16), (uint8_t)(lba >> 8), (uint8_t)lba,
		0, (uint8_t)(count >> 8), (uint8_t)count, 0
	};
	return ufs_scsi(cdb, sizeof(cdb), count * UFS_BLOCK, false);
}

/*
 * WRITE(10) of count device blocks from the bounce buffer to lba. The range
 * must lie inside the partition: this is the only place data goes out.
 */
static int
ufs_write_blocks(uint64_t lba, uint32_t count)
{
	if (!su.writable || !su.nblocks || count == 0 || count > BOUNCE_SIZE / UFS_BLOCK ||
	    lba < su.first_block || lba - su.first_block >= su.nblocks ||
	    count > su.nblocks - (lba - su.first_block) || lba + count - 1 > UINT32_MAX) {
		printf("SWEET UFS: refusing write of %u blocks at 0x%llx\n", count, lba);
		return EPERM;
	}
	uint8_t cdb[10] = {
		0x2a, 0,
		(uint8_t)(lba >> 24), (uint8_t)(lba >> 16), (uint8_t)(lba >> 8), (uint8_t)lba,
		0, (uint8_t)(count >> 8), (uint8_t)count, 0
	};
	return ufs_scsi(cdb, sizeof(cdb), count * UFS_BLOCK, true);
}

static int
ufs_sync_cache(void)
{
	static const uint8_t cdb[10] = { 0x35 };       /* SYNCHRONIZE CACHE(10), whole LUN */
	return ufs_scsi(cdb, sizeof(cdb), 0, false);
}

static int
ufs_find_partition(const char *want)
{
	const uint8_t *b = (const uint8_t *)(su.dma + DMA_BOUNCE);
	int error = ufs_read_blocks(1, 1);
	if (error) {
		return error;
	}
	if (memcmp(b, "EFI PART", 8) != 0) {
		printf("SWEET UFS: LUN %u has no GPT\n", su.lun);
		return ENXIO;
	}
	uint64_t entry_lba;
	uint32_t nent, esz;
	memcpy(&entry_lba, b + 72, 8);
	memcpy(&nent, b + 80, 4);
	memcpy(&esz, b + 84, 4);
	if (esz != 128 || nent == 0 || nent * esz > BOUNCE_SIZE) {
		printf("SWEET UFS: unsupported GPT entries %u x %u\n", nent, esz);
		return ENXIO;
	}
	error = ufs_read_blocks(entry_lba, (nent * esz + UFS_BLOCK - 1) / UFS_BLOCK);
	if (error) {
		return error;
	}
	for (uint32_t i = 0; i < nent; i++) {
		const uint8_t *e = b + i * esz;
		char name[37];
		unsigned c;
		for (c = 0; c < 36; c++) {
			uint16_t ch = (uint16_t)(e[56 + 2 * c] | e[57 + 2 * c] << 8);
			if (ch == 0 || ch > 0x7f) {
				break;
			}
			name[c] = (char)ch;
		}
		name[c] = 0;
		if (strcmp(name, want) != 0) {
			continue;
		}
		uint64_t first, last;
		memcpy(&first, e + 32, 8);
		memcpy(&last, e + 40, 8);
		if (last < first) {
			return ENXIO;
		}
		su.first_block = first;
		su.nblocks = last - first + 1;
		return 0;
	}
	printf("SWEET UFS: no GPT partition named \"%s\"\n", want);
	return ENOENT;
}

static int
ufs_take_over_host(void)
{
	uint32_t hce = ufs_rd(REG_HCE), hcs = ufs_rd(REG_HCS);
	if (!(hce & 1) || !(hcs & HCS_DP) || !(hcs & HCS_UTRLRDY)) {
		printf("SWEET UFS: host not ready (hce 0x%x hcs 0x%x)\n", hce, hcs);
		return ENXIO;
	}
	if (ufs_rd(REG_UTRLDBR)) {
		printf("SWEET UFS: requests outstanding from the bootloader\n");
		return EBUSY;
	}
	/* With RAM above 4 GiB (mkadt.py --highmem) the DMA memory can be there too. */
	if (((su.dma_pa + DMA_SIZE - 1) >> 32) && !(ufs_rd(REG_CAP) & CAP_64AS)) {
		printf("SWEET UFS: DMA memory at 0x%llx but the host has no 64-bit addressing\n",
		    su.dma_pa);
		return ENXIO;
	}
	ufs_wr(REG_IE, 0);                      /* polled operation */
	ufs_wr(REG_IS, ufs_rd(REG_IS));
	ufs_wr(REG_UTRLRSR, 0);
#if defined(NORD2)
	__builtin_arm_dsb(0xf);
	if (!nord2_ufs_wait(REG_UTRLRSR, 1, 0)) {
		return ETIMEDOUT;
	}
#endif
	uint64_t list = su.dma_pa + DMA_UTRL;
	ufs_wr(REG_UTRLBA, (uint32_t)list);
	ufs_wr(REG_UTRLBAU, (uint32_t)(list >> 32));
	ufs_wr(REG_UTRLRSR, 1);
	/* Never ring a doorbell unless the host uses our list. */
	if (ufs_rd(REG_UTRLBA) != (uint32_t)list || ufs_rd(REG_UTRLBAU) != (uint32_t)(list >> 32) ||
	    !(ufs_rd(REG_UTRLRSR) & 1)) {
		printf("SWEET UFS: host did not take our request list\n");
		return EIO;
	}
	return 0;
}

#if defined(NORD2)
static uint32_t
nord2_ufs_crc32(const uint8_t *p, uint32_t n)
{
	uint32_t crc = ~0u;
	while (n--) {
		crc ^= *p++;
		for (unsigned i = 0; i < 8; i++) {
			crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
		}
	}
	return ~crc;
}

static uint32_t
nord2_ufs_be32(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
	       (uint32_t)p[2] << 8 | p[3];
}

/* Inspect only GPT metadata; do not read any partition contents. */
static int
nord2_ufs_read_gpt(uint64_t blocks, bool select_userdata)
{
	uint8_t *b = (uint8_t *)(su.dma + DMA_BOUNCE);
	uint32_t hsize, hcrc, nent, esz, ecrc;
	uint64_t current, backup, first_usable, last_usable, entry_lba;
	int error = ufs_read_blocks(1, 1);
	if (error) {
		return error;
	}
	if (memcmp(b, "EFI PART", 8) || memcmp(b + 8, "\0\0\1\0", 4)) {
		return EINVAL;
	}
	memcpy(&hsize, b + 12, 4);
	memcpy(&hcrc, b + 16, 4);
	if (hsize < 92 || hsize > UFS_BLOCK) {
		return EINVAL;
	}
	bzero(b + 16, 4);
	uint32_t computed = nord2_ufs_crc32(b, hsize);
	memcpy(b + 16, &hcrc, 4);
	if (computed != hcrc) {
		printf("NORD2 UFS: GPT header CRC mismatch\n");
		return EIO;
	}
	memcpy(&current, b + 24, 8);
	memcpy(&backup, b + 32, 8);
	memcpy(&first_usable, b + 40, 8);
	memcpy(&last_usable, b + 48, 8);
	memcpy(&entry_lba, b + 72, 8);
	memcpy(&nent, b + 80, 4);
	memcpy(&esz, b + 84, 4);
	memcpy(&ecrc, b + 88, 4);
	if (current != 1 || backup != blocks - 1 || first_usable > last_usable ||
	    last_usable >= backup || esz != 128 || nent == 0 || nent > 1024) {
		return EINVAL;
	}
	uint32_t bytes = nent * esz;
	uint32_t count = (bytes + UFS_BLOCK - 1) / UFS_BLOCK;
	if (entry_lba < 2 || entry_lba >= 64 || count > 64 - entry_lba ||
	    entry_lba + count > first_usable) {
		return EINVAL;
	}
	error = ufs_read_blocks(entry_lba, count);
	if (error) {
		return error;
	}
	if (nord2_ufs_crc32(b, bytes) != ecrc) {
		printf("NORD2 UFS: GPT entry CRC mismatch\n");
		return EIO;
	}
	unsigned found = 0;
	uint64_t userdata_first = 0, userdata_blocks = 0;
	static const char *const wanted[] = { "boot", "userdata" };
	for (uint32_t i = 0; i < nent; i++) {
		const uint8_t *e = b + i * esz;
		static const uint8_t empty[16] = { 0 };
		if (memcmp(e, empty, sizeof(empty)) == 0) {
			continue;
		}
		for (unsigned w = 0; w < sizeof(wanted) / sizeof(wanted[0]); w++) {
			unsigned j = 0;
			while (wanted[w][j] && e[56 + 2 * j] == wanted[w][j] &&
			    e[57 + 2 * j] == 0) {
				j++;
			}
			if (wanted[w][j] || e[56 + 2 * j] || e[57 + 2 * j]) {
				continue;
			}
			uint64_t first, last;
			memcpy(&first, e + 32, 8);
			memcpy(&last, e + 40, 8);
			if (first < first_usable || last > last_usable || last < first ||
			    (found & (1u << w))) {
				return EINVAL;
			}
			found |= 1u << w;
			if (w == 1) {
				userdata_first = first;
				userdata_blocks = last - first + 1;
			}
			printf("NORD2 UFS: GPT %s first=%llu blocks=%llu\n",
			    wanted[w], first, last - first + 1);
		}
	}
	if (found != 3) {
		return ENOENT;
	}
	printf("NORD2 UFS: GPT verified entries=%u header_crc=0x%08x entries_crc=0x%08x\n",
	    nent, hcrc, ecrc);
	if (select_userdata) {
		su.first_block = userdata_first;
		su.nblocks = userdata_blocks;
	}
	return 0;
}

static bool
nord2_ufs_wait(uint32_t off, uint32_t mask, uint32_t value)
{
	uint64_t deadline;
	nanoseconds_to_absolutetime(NSEC_PER_SEC, &deadline);
	deadline += mach_absolute_time();
	while ((ufs_rd(off) & mask) != value) {
		if (mach_absolute_time() >= deadline) {
			return false;
		}
		delay(10);
	}
	return true;
}

static void
nord2_ufs_write_reg(uint32_t off, uint32_t value)
{
	ufs_wr(off, value);
	__builtin_arm_dsb(0xf);
}

struct nord2_ufs_saved {
	uint32_t ie, ba, bau, rsr;
};
static struct nord2_ufs_saved nord2_root_saved;
static bool nord2_root_owned;

static bool
nord2_ufs_restore(const struct nord2_ufs_saved *saved)
{
	if (ufs_rd(REG_UTRLDBR)) {
		nord2_ufs_write_reg(REG_UTRLCLR, ~1u);
	}
	if (!nord2_ufs_wait(REG_UTRLDBR, ~0u, 0)) {
		return false;
	}
	nord2_ufs_write_reg(REG_UTRLRSR, 0);
	if (!nord2_ufs_wait(REG_UTRLRSR, 1, 0)) {
		return false;
	}
	nord2_ufs_write_reg(REG_UTRLBA, saved->ba);
	nord2_ufs_write_reg(REG_UTRLBAU, saved->bau);
	if (ufs_rd(REG_UTRLBA) != saved->ba || ufs_rd(REG_UTRLBAU) != saved->bau) {
		return false;
	}
	nord2_ufs_write_reg(REG_UTRLRSR, saved->rsr);
	if (!nord2_ufs_wait(REG_UTRLRSR, 1, saved->rsr & 1)) {
		return false;
	}
	nord2_ufs_write_reg(REG_IS, ufs_rd(REG_IS));
	nord2_ufs_write_reg(REG_IE, saved->ie);
	return ufs_rd(REG_IE) == saved->ie;
}

void nord2_ufs_probe(void);

/* Point 11, opt-in only. Always stop before filesystem selection. */
void
nord2_ufs_probe(void)
{
	uint32_t want = 0;
	(void)PE_parse_boot_argn("nord2stopat", &want, sizeof(want));
	if (want != 11) {
		return;
	}
	int error = ENXIO;
	bool changed = false;
	struct nord2_ufs_saved saved = { 0 };
	printf("NORD2 UFS: bounded LUN2 read probe begin\n");
	if (su.ready || su.dma) {
		goto done;
	}
	su.regs = ml_io_map(0x11270000UL, 0x2300);
	if (!su.regs || !(ufs_rd(REG_HCE) & 1) ||
	    (ufs_rd(REG_HCS) & (HCS_DP | HCS_UTRLRDY)) != (HCS_DP | HCS_UTRLRDY) ||
	    ufs_rd(REG_UTRLDBR) || ufs_rd(0x78) || ufs_rd(REG_IS)) {
		printf("NORD2 UFS: refusing non-idle inherited host\n");
		goto done;
	}
	saved.ie = ufs_rd(REG_IE);
	saved.ba = ufs_rd(REG_UTRLBA);
	saved.bau = ufs_rd(REG_UTRLBAU);
	saved.rsr = ufs_rd(REG_UTRLRSR);
	if (kmem_alloc_contig(kernel_map, &su.dma, DMA_SIZE, PAGE_MASK, 0, 0,
	    KMA_DATA | KMA_ZERO | KMA_PERMANENT, VM_KERN_MEMORY_IOKIT) != KERN_SUCCESS) {
		error = ENOMEM;
		goto done;
	}
	ppnum_t pn = pmap_find_phys(kernel_pmap, (addr64_t)su.dma);
	if (!pn) {
		error = ENOMEM;
		goto done;
	}
	su.dma_pa = ptoa_64(pn);
	if (((su.dma_pa + DMA_SIZE - 1) >> 32) && !(ufs_rd(REG_CAP) & CAP_64AS)) {
		goto done;
	}
	su.lun = 2;
	su.tag = 0;
	su.probe_only = true;
	su.cache = su.wait = su.writable = false;
	/* Flush the zeroed descriptor memory before exposing its address. */
	dma_sync(0, DMA_SIZE);
	__builtin_arm_dsb(0xf);
	changed = true;
	nord2_ufs_write_reg(REG_IE, 0);
	nord2_ufs_write_reg(REG_UTRLRSR, 0);
	if (!nord2_ufs_wait(REG_UTRLRSR, 1, 0)) {
		goto done;
	}
	nord2_ufs_write_reg(REG_UTRLBA, (uint32_t)su.dma_pa);
	nord2_ufs_write_reg(REG_UTRLBAU, (uint32_t)(su.dma_pa >> 32));
	nord2_ufs_write_reg(REG_UTRLRSR, 1);
	if (ufs_rd(REG_UTRLBA) != (uint32_t)su.dma_pa ||
	    ufs_rd(REG_UTRLBAU) != (uint32_t)(su.dma_pa >> 32) ||
	    !nord2_ufs_wait(REG_UTRLRSR, 1, 1)) {
		goto done;
	}
	printf("NORD2 UFS: request list accepted DMA=0x%llx\n", su.dma_pa);
	static const uint8_t tur[6] = { 0 };
	for (unsigned i = 0; i < 3; i++) {
		error = ufs_scsi(tur, sizeof(tur), 0, false);
		if (error != EIO) {
			break;
		}
	}
	if (error) {
		goto done;
	}
	static const uint8_t capacity[10] = { 0x25 };
	error = ufs_scsi(capacity, sizeof(capacity), 8, false);
	if (error) {
		goto done;
	}
	const uint8_t *b = (const uint8_t *)(su.dma + DMA_BOUNCE);
	uint32_t last = nord2_ufs_be32(b), block_size = nord2_ufs_be32(b + 4);
	printf("NORD2 UFS: LUN2 capacity blocks=%llu block_size=%u\n",
	    (uint64_t)last + 1, block_size);
	if (last == UINT32_MAX || last < 64 || block_size != UFS_BLOCK) {
		error = EINVAL;
		goto done;
	}
	error = nord2_ufs_read_gpt((uint64_t)last + 1, false);
done:
	if (changed && !nord2_ufs_restore(&saved)) {
		printf("NORD2 UFS: cannot safely restore host; manual reboot required\n");
		/* Keep all DMA memory intact. Never chain into Android with active DMA. */
		for (;;) {
			mock_halt();
		}
	}
	if (changed) {
		printf("NORD2 UFS: inherited request list and interrupts restored\n");
	}
	printf("NORD2 UFS: read probe result=%d\n", error);
	nord2_stop_at(11, "after bounded UFS read probe");
}

/*
 * Final return path: interrupts are masked and the caller will never resume
 * XNU. No allocation or mutex acquisition is safe here (a panic may hold the
 * UFS mutex). Keep DMA memory and ownership intact if the controller cannot
 * be quiesced. Zero means no takeover happened, one means state restored.
 */
int
nord2_ufs_prepare_return(void)
{
	if (!nord2_root_owned) {
		return 0;
	}
	if (!nord2_ufs_restore(&nord2_root_saved)) {
		return -1;
	}
	nord2_root_owned = false;
	return 1;
}

static void
nord2_ufs_release_root(void)
{
	int restored = nord2_ufs_prepare_return();
	if (restored < 0) {
		printf("NORD2 UFS: root host restore failed; manual reboot required\n");
		for (;;) {
			mock_halt();
		}
	}
	if (restored > 0) {
		printf("NORD2 UFS: root probe host restored, ready=%u writable=%u\n",
		    su.ready, su.writable);
	}
}

/* Called before any root mount for the point-12 publication diagnostic. */
void nord2_ufs_root_stop(void);
void
nord2_ufs_root_stop(void)
{
	uint32_t want = 0;
	(void)PE_parse_boot_argn("nord2stopat", &want, sizeof(want));
	if (want == 12) {
		nord2_ufs_release_root();
		nord2_stop_at(12, su.ready ?
		    "after UFS root publication, before mounting" : "UFS root initialization failed");
	}
}
#endif /* NORD2 */


static int
sweet_ufs_init(void)
{
	DTEntry node;
	uintptr_t const *reg = NULL;
	unsigned int size = 0;
	char want[64] = "userdata";
	int error;

	if (SecureDTLookupEntry(NULL, "/sweet-ufs", &node) != kSuccess ||
	    SecureDTGetProperty(node, "reg", (void const **)&reg, &size) != kSuccess ||
	    size < 2 * sizeof(uintptr_t)) {
		printf("SWEET UFS: no /sweet-ufs node in the device tree\n");
		return ENXIO;
	}
	PE_parse_boot_argn("ufsroot", want, sizeof(want));
#if defined(NORD2)
	/* The Nord 2 port is restricted to its known controller and userdata. */
	if (reg[0] != 0x11270000UL || reg[1] != 0x2300 || strcmp(want, "userdata")) {
		return EINVAL;
	}
#endif
	uint32_t rw = 0;
	PE_parse_boot_argn("ufsrw", &rw, sizeof(rw));
#if defined(NORD2)
	uint32_t stop = 0;
	PE_parse_boot_argn("nord2stopat", &stop, sizeof(stop));
	if (stop == 12 || stop == 22) {
		rw = 0;
	}
	if ((stop == 22 || stop == 23) && !nord2_smp_userland_ready()) {
		printf("NORD2 UFS: all8 SMP pass required before iOS root\n");
		return EPERM;
	}
#endif
	uint32_t cache = 0, wait = 0;
	PE_parse_boot_argn("ufscache", &cache, sizeof(cache));
	PE_parse_boot_argn("ufswait", &wait, sizeof(wait));
	su.cache = (cache == 1);
	su.wait = (wait == 1);

	su.regs = ml_io_map(reg[0], reg[1]);
	if (!su.regs) {
		return ENOMEM;
	}
	if (kmem_alloc_contig(kernel_map, &su.dma, DMA_SIZE, PAGE_MASK, 0, 0,
	    KMA_DATA | KMA_ZERO | KMA_PERMANENT, VM_KERN_MEMORY_IOKIT) != KERN_SUCCESS) {
		printf("SWEET UFS: no contiguous DMA memory\n");
		return ENOMEM;
	}
	ppnum_t pn = pmap_find_phys(kernel_pmap, (addr64_t)su.dma);
	if (!pn) {
		return ENOMEM;
	}
	su.dma_pa = ptoa_64(pn);
	su.lun = 0;
#if defined(NORD2)
	su.lun = 2;
#endif
	su.secsize = DEV_BSIZE;

	lck_mtx_lock(&sweet_ufs_lock);
#if defined(NORD2)
	if (!(ufs_rd(REG_HCE) & 1) ||
	    (ufs_rd(REG_HCS) & (HCS_DP | HCS_UTRLRDY)) != (HCS_DP | HCS_UTRLRDY) ||
	    ufs_rd(REG_UTRLDBR) || ufs_rd(0x78) || ufs_rd(REG_IS)) {
		lck_mtx_unlock(&sweet_ufs_lock);
		return EBUSY;
	}
	nord2_root_saved.ie = ufs_rd(REG_IE);
	nord2_root_saved.ba = ufs_rd(REG_UTRLBA);
	nord2_root_saved.bau = ufs_rd(REG_UTRLBAU);
	nord2_root_saved.rsr = ufs_rd(REG_UTRLRSR);
	dma_sync(0, DMA_SIZE);
	__builtin_arm_dsb(0xf);
	nord2_root_owned = true;
#endif
	error = ufs_take_over_host();
	if (!error) {
		static const uint8_t tur[6] = { 0x00 };
		/* The first command after the bootloader may report a unit attention. */
		for (int i = 0; i < 3; i++) {
			error = ufs_scsi(tur, sizeof(tur), 0, false);
			if (error != EIO) {
				break;
			}
		}
	}
	if (!error) {
#if defined(NORD2)
		static const uint8_t capacity[10] = { 0x25 };
		error = ufs_scsi(capacity, sizeof(capacity), 8, false);
		if (!error) {
			const uint8_t *b = (const uint8_t *)(su.dma + DMA_BOUNCE);
			uint32_t last = nord2_ufs_be32(b), block_size = nord2_ufs_be32(b + 4);
			if (last == UINT32_MAX || last < 64 || block_size != UFS_BLOCK) {
				error = EINVAL;
			} else {
				error = nord2_ufs_read_gpt((uint64_t)last + 1, true);
			}
		}
#else
		error = ufs_find_partition(want);
#endif
	}
	lck_mtx_unlock(&sweet_ufs_lock);
	if (error) {
		goto failed;
	}

	su.bmajor = bdevsw_add(-1, &sweet_ufs_bdevsw);
	if (su.bmajor < 0) {
		error = ENXIO;
		goto failed;
	}
	su.cmajor = cdevsw_add_with_bdev(-1, &sweet_ufs_cdevsw, su.bmajor);
	if (su.cmajor < 0) {
		error = ENXIO;
		goto failed;
	}
	su.bdev = makedev(su.bmajor, 0);
	devfs_make_node(su.bdev, DEVFS_BLOCK, UID_ROOT, GID_OPERATOR, 0640, "ufsroot0");
	devfs_make_node(makedev(su.cmajor, 0), DEVFS_CHAR, UID_ROOT, GID_OPERATOR, 0640, "rufsroot0");
	/* Only now, with the partition bounds known, can writes be allowed. */
	su.writable = (rw != 0);
	su.ready = true;
	printf("SWEET UFS: \"%s\" on LUN %u: first block 0x%llx, %llu MiB, %s, DMA at 0x%llx\n",
	    want, su.lun, su.first_block, su.nblocks * UFS_BLOCK >> 20,
	    su.writable ? "read-write" : "read-only", su.dma_pa);
#if !defined(NORD2)
	sweet_log_start();
	sweet_sample_start();
	/* Input drivers (HID services in iokit/Kernel/arm/hid/SweetHID.cpp). */
	sweet_keys_init();
	sweet_touch_init();
	sweet_usb_probe();
#endif
	return 0;
failed:
#if defined(NORD2)
	/* Never pass active DMA on to a panic/chain-back after an init failure. */
	printf("NORD2 UFS: root initialization error=%d\n", error);
	nord2_ufs_release_root();
	nord2_ufs_root_stop();
#endif
	return error;
}


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
 bool is_write=cdb[0]==0x2a;
 assert(((tr->dw0>>25)&3)==(len?(is_write?1:2):0)&&cd->cmd[0]==1&&cd->cmd[2]==2&&cd->cmd[1]==(len?(is_write?0x20:0x40):0));
 if (is_write) {
  assert(want==23&&smp_pass&&su.writable&&len==4096);
  uint32_t lba=nord2_ufs_be32(cdb+2);
  assert(lba>=su.first_block&&lba<su.first_block+su.nblocks&&cdb[7]==0&&cdb[8]==1);
 } else assert(nord2_ufs_probe_command(cdb,cdb[0]?10:6,len,false));
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
 reset(23,0);rw_requested=1;assert(sweet_ufs_init()==EPERM&&!reads&&!writes&&!commands&&!su.ready);
 reset(23,0);smp_pass=true;rw_requested=1;
 assert(sweet_ufs_init()==0&&su.ready&&su.writable&&nord2_root_owned&&su.lun==2);
 assert(ufs_write_blocks(su.first_block,1)==0);
 assert(ufs_write_blocks(su.first_block+su.nblocks-1,1)==0);
 unsigned before=commands;
 assert(ufs_write_blocks(su.first_block-1,1)!=0);
 assert(ufs_write_blocks(su.first_block+su.nblocks,1)!=0);
 assert(ufs_write_blocks(su.first_block+su.nblocks-1,2)!=0);
 assert(ufs_write_blocks(su.first_block,0)!=0);
 assert(ufs_write_blocks(su.first_block,257)!=0);
 assert(ufs_write_blocks(UINT64_MAX,1)!=0);
 assert(commands==before);
 assert(nord2_ufs_prepare_return()==1&&!nord2_root_owned);
 reset(23,0);smp_pass=true;rw_requested=1;wanted_bad=1;
 assert(sweet_ufs_init()==EINVAL&&!reads&&!writes&&!commands&&!registered);
 reset(0,0);assert(sweet_ufs_init()==0&&su.ready&&!su.writable&&nord2_root_owned);assert(su.first_block==3153920&&su.nblocks==28082168);nord2_ufs_release_root();assert(!nord2_root_owned&&!stop_point);
 puts("PASS: mode23 pre-MMIO SMP gate, WRITE10 direction/LUN/partition bounds, mode22 forced RO; root publication, forced read-only point12, userdata extents, failure cleanup, other-partition refusal; exact probe code, real GPT fixtures, DMA descriptors, no-write CDB guard, inactive gate, host refusal, success, unit attention, bad CRC/capacity/tag, takeover refusal, timeout cleanup, unsafe-DMA halt");
}
