/*
 * SWEET internal UFS root device.
 *
 * The Qualcomm UFS host (UFSHCI 2.1) is left enabled with the link up by the
 * bootloader, which loaded our boot image from it. This driver reuses that
 * state: it points the transfer request list at its own memory (verifying the
 * host took it before ringing any doorbell) and issues polled SCSI commands,
 * one at a time, to LUN 0. It finds one GPT partition by name (boot-arg
 * ufsroot=, default "userdata") and exposes it as the BSD disk ufsroot0
 * (rufsroot0), with 512-byte logical sectors over the device's 4 KiB blocks.
 * Root on it with rd=ufsroot0.
 *
 * All DMA goes through one physically contiguous allocation (descriptors and
 * a 1 MiB bounce buffer); the host is not cache coherent, so caches are
 * cleaned/invalidated around each command.
 *
 * Writes are refused unless boot-arg ufsrw=1 is set. Every write is checked
 * against the partition bounds again right before it is issued, so nothing
 * outside the named partition can be written.
 */
#if defined(ARM64_BOARD_CONFIG_SWEET)
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/disk.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/uio.h>
#include <miscfs/devfs/devfs.h>
#include <kern/locks.h>
#include <kern/clock.h>
#include <kern/sched_prim.h>
#include <machine/machine_routines.h>
#include <arm/caches_internal.h>
#include <vm/pmap.h>
#include <vm/vm_kern.h>
#include <pexpert/pexpert.h>
#include <pexpert/device_tree.h>
#include <libkern/libkern.h>
#include <machine/atomic.h>
#if defined(NORD2)
#include <arm64/sweet_early.h>
#include <libkern/crypto/sha2.h>
#include "wifi_firmware_probe.h"
#include "wifi_emi_probe.h"
#include "ufs_two_lane.h"
#endif

extern ppnum_t pmap_find_phys(pmap_t pmap, addr64_t va);
/* osfmk/arm/machine_routines.h, not exported to the BSD layer */
extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
/* osfmk/arm/cpu_data.h, not exported to the BSD layer */
extern int get_preemption_level(void);
extern kern_return_t kernel_thread_start(thread_continue_t, void *, thread_t *);
extern void thread_deallocate(thread_t);
extern int hz;
static void sweet_log_start(void);
extern void sweet_sample_start(void);
extern void sweet_keys_init(void);
extern void sweet_touch_init(void);
extern void sweet_usb_probe(void);

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

static LCK_GRP_DECLARE(sweet_ufs_grp, "sweet_ufs");
static LCK_MTX_DECLARE(sweet_ufs_lock, &sweet_ufs_grp);

static inline uint32_t
ufs_rd(uint32_t off)
{
	return *(volatile uint32_t *)(su.regs + off);
}

static inline void
ufs_wr(uint32_t off, uint32_t v)
{
	*(volatile uint32_t *)(su.regs + off) = v;
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

#if defined(NORD2)
/* Opt-in observation only. All updates and snapshots use sweet_ufs_lock.
 * DME_GET is a UIC transaction, not a passive MMIO read: command writes are
 * restricted to local PA observation attributes, with no SET/peer/power commands. */
#define N2_UIC_CMD 0x90u
#define N2_UIC_ARG1 0x94u
#define N2_UIC_ARG2 0x98u
#define N2_UIC_ARG3 0x9cu
#define N2_UIC_READY (1u << 3)
#define N2_UIC_COMPLETE (1u << 10)
#define N2_UIC_TIMEOUT_TICKS_DIV 10u /* 100ms maximum per transaction */
#define N2_UIC_POLL_LIMIT 100000u
static bool nord2_ufs_profile_enabled, nord2_ufs_profile_reported;
static bool nord2_ufs_uic_pending;
static bool nord2_root_owned;
/* Future isolated diagnostic: never publish/root-return after entering it. */
static bool nord2_x2_in_progress, nord2_x2_uncertain, nord2_x2_stop;
#include "ufs_two_lane_live.inc"
static struct nord2_ufs_profile_stats {
	uint64_t commands, reads, writes, syncs, read_bytes, write_bytes;
	uint64_t command_ticks, command_max_ticks, doorbell_ticks, doorbell_max_ticks;
	uint64_t errors, timeouts;
} nord2_ufs_stats;
/* Candidate instrumentation only: mapped nonzero strategy buffers. Times
 * include descheduling; aggregate waiter times overlap and are not boot wall. */
static struct nord2_ufs_strategy_stats {
	uint64_t buffers, read_buffers, requested_bytes, rmw_reads;
	uint64_t map_ticks, mutex_wait_ticks, mutex_wait_max, held_ticks, copy_ticks;
	uint64_t raw_buffers, cluster_buffers, pageio_buffers, async_buffers;
	uint64_t sizes[8]; /* <=4/8/16/32/64/256/1024KiB, then larger */
} nord2_ufs_strategy_stats;
static struct nord2_ufs_link_stats {
	uint32_t values[6], valid_mask, completed, result;
	int error;
} nord2_ufs_link;
static const uint16_t nord2_ufs_attrs[6] = {
	0x1583, /* PA_RXGEAR */ 0x1568, /* PA_TXGEAR */
	0x1580, /* PA_ACTIVERXDATALANES */ 0x1560, /* PA_ACTIVETXDATALANES */
	0x156a, /* PA_HSSERIES */ 0x1571 /* PA_PWRMODE: RX high/TX low nibble */
};

/* Additional local GETs: selected Android unipro.h:95–96,113–117,
 * 126,143–149. Command validity and understood value ranges are separate;
 * unknown/reserved values are retained, never clamped or used for tuning. */
#define N2_LIMIT_COUNT 8u
static const uint16_t nord2_ufs_limit_attrs[N2_LIMIT_COUNT] = {
	0x1540, 0x1520, /* PA_AVAILRXDATALANES / PA_AVAILTXDATALANES */
	0x1581, 0x1561, /* PA_CONNECTEDRXDATALANES / PA_CONNECTEDTXDATALANES */
	0x1584, 0x1569, /* PA_RXTERMINATION / PA_TXTERMINATION */
	0x15a9, 0x15d4  /* PA_LOCALVERINFO / PA_TXHSADAPTTYPE */
};
static struct nord2_ufs_limit_stats {
	uint32_t values[N2_LIMIT_COUNT], results[N2_LIMIT_COUNT];
	int errors[N2_LIMIT_COUNT];
	uint32_t valid_mask, understood_mask, attempted;
	uint32_t ahit, hcs, is;
	bool context_valid;
} nord2_ufs_limits;

static uint64_t
nord2_ufs_counter(void)
{
	return __builtin_arm_rsr64("CNTVCT_EL0");
}

/* Independent hard iteration cap also bounds a stalled architected counter. */
static bool
nord2_ufs_uic_poll(uint32_t off, uint32_t mask, uint32_t want)
{
	uint64_t frequency = __builtin_arm_rsr64("CNTFRQ_EL0");
	if (!frequency) { return false; }
	uint64_t start = nord2_ufs_counter();
	for (unsigned i = 0; i < N2_UIC_POLL_LIMIT; i++) {
		if ((ufs_rd(off) & mask) == want) { return true; }
		if (nord2_ufs_counter() - start >=
		    (frequency + N2_UIC_TIMEOUT_TICKS_DIV - 1) / N2_UIC_TIMEOUT_TICKS_DIV) {
			return false;
		}
		delay(1);
	}
	return false;
}

/* May also run from final recovery without taking a mutex. Never reset or
 * abort a UIC transaction: refuse handoff if our issued GET has not finished. */
static bool
nord2_ufs_uic_drain(void)
{
	if (!nord2_ufs_uic_pending) { return true; }
	if (!nord2_ufs_uic_poll(REG_IS, N2_UIC_COMPLETE, N2_UIC_COMPLETE) ||
	    !nord2_ufs_uic_poll(REG_HCS, N2_UIC_READY, N2_UIC_READY)) {
		return false;
	}
	ufs_wr(REG_IS, N2_UIC_COMPLETE); /* W1C only our completed UIC */
	__builtin_arm_dsb(0xf);
	nord2_ufs_uic_pending = false;
	return true;
}

static int
nord2_ufs_dme_get(uint16_t attr, uint32_t *value, uint32_t *result)
{
	bool allowed = false;
	for (unsigned i = 0; i < 6; i++) {
		if (attr == nord2_ufs_attrs[i]) { allowed = true; }
	}
	for (unsigned i = 0; i < N2_LIMIT_COUNT; i++) {
		if (attr == nord2_ufs_limit_attrs[i]) { allowed = true; }
	}
	if (!allowed || !value || !result || !nord2_ufs_profile_enabled) { return EPERM; }
	/* Caller owns root controller and its mutex; never interfere with SCSI,
	 * task management, an inherited completion, or enabled interrupt handling. */
	if (!nord2_root_owned || su.probe_only || !su.regs || !(ufs_rd(REG_HCE) & 1) ||
	    (ufs_rd(REG_HCS) & (HCS_DP | HCS_UTRLRDY | N2_UIC_READY)) !=
	    (HCS_DP | HCS_UTRLRDY | N2_UIC_READY) || ufs_rd(REG_UTRLDBR) ||
	    ufs_rd(0x78) || ufs_rd(REG_IE) || ufs_rd(REG_IS) || nord2_ufs_uic_pending) {
		return EBUSY;
	}
	if (!__builtin_arm_rsr64("CNTFRQ_EL0")) { return EINVAL; }
	ufs_wr(N2_UIC_ARG1, (uint32_t)attr << 16); /* selector zero, local GET */
	ufs_wr(N2_UIC_ARG2, 0);
	ufs_wr(N2_UIC_ARG3, 0);
	nord2_ufs_uic_pending = true;
	__builtin_arm_dsb(0xf);
	ufs_wr(N2_UIC_CMD, 1); /* DME_GET: never DME_SET or link-state commands */
	__builtin_arm_dsb(0xf);
	if (!nord2_ufs_uic_poll(REG_IS, N2_UIC_COMPLETE, N2_UIC_COMPLETE)) {
		return ETIMEDOUT; /* pending retained; no further GETs or recovery reset */
	}
	*result = ufs_rd(N2_UIC_ARG2) & 0xffu;
	uint32_t observed = ufs_rd(N2_UIC_ARG3);
	if (!nord2_ufs_uic_drain()) { return ETIMEDOUT; }
	if (*result) { return EIO; }
	*value = observed;
	return 0;
}

static void
nord2_ufs_profile_link(void)
{
	if (!nord2_ufs_profile_enabled) { return; }
	for (unsigned i = 0; i < 6; i++) {
		int error = nord2_ufs_dme_get(nord2_ufs_attrs[i],
		    &nord2_ufs_link.values[i], &nord2_ufs_link.result);
		if (error) { nord2_ufs_link.error = error; return; }
		nord2_ufs_link.valid_mask |= 1u << i;
		nord2_ufs_link.completed++;
	}
	/* Baseline UFSHCI context is read-only and captured after six successful
	 * gated GETs while the caller still holds the controller mutex. AHIT is
	 * raw, HCS UPMCRS bits10:8 are context, not proof of a new transition. */
	nord2_ufs_limits.ahit = ufs_rd(0x18);
	nord2_ufs_limits.hcs = ufs_rd(REG_HCS);
	nord2_ufs_limits.is = ufs_rd(REG_IS);
	nord2_ufs_limits.context_valid = true;
	for (unsigned i = 0; i < N2_LIMIT_COUNT; i++) {
		uint32_t value = 0, result = 0;
		nord2_ufs_limits.attempted++;
		int error = nord2_ufs_dme_get(nord2_ufs_limit_attrs[i], &value, &result);
		nord2_ufs_limits.results[i] = result;
		nord2_ufs_limits.errors[i] = error;
		if (error) {
			/* A completed unsupported attribute may be followed by another
			 * gated GET; timeout/ownership loss stops without retry/reset. */
			if (error == EIO) { continue; }
			break;
		}
		nord2_ufs_limits.values[i] = value;
		nord2_ufs_limits.valid_mask |= 1u << i;
		bool understood = i < 4 ? (value >= 1 && value <= 2) :
		    i < 6 ? value <= 1 : i == 6 ?
		    ((value & 0xf) >= 1 && (value & 0xf) <= 5) : value <= 1;
		if (understood) { nord2_ufs_limits.understood_mask |= 1u << i; }
	}
}

static void
nord2_ufs_profile_wait(uint64_t begin)
{
	uint64_t ticks = nord2_ufs_counter() - begin;
	nord2_ufs_stats.doorbell_ticks += ticks;
	if (ticks > nord2_ufs_stats.doorbell_max_ticks) {
		nord2_ufs_stats.doorbell_max_ticks = ticks;
	}
}

/* Public hook at first GUI, called after the console lock is released.
 * Copy only under the I/O mutex; printf must never hold it. */
void
nord2_ufs_profile_report(void)
{
	if (!nord2_ufs_profile_enabled) { return; }
	struct nord2_ufs_profile_stats stats;
	struct nord2_ufs_strategy_stats strategy;
	struct nord2_ufs_link_stats link;
	struct nord2_ufs_limit_stats limits;
	bool pending;
	lck_mtx_lock(&sweet_ufs_lock);
	if (nord2_ufs_profile_reported) { lck_mtx_unlock(&sweet_ufs_lock); return; }
	nord2_ufs_profile_reported = true;
	stats = nord2_ufs_stats;
	strategy = nord2_ufs_strategy_stats;
	link = nord2_ufs_link;
	limits = nord2_ufs_limits;
	pending = nord2_ufs_uic_pending;
	lck_mtx_unlock(&sweet_ufs_lock);
	printf("NORD2 UFS STRATEGY: buffers=%llu reads=%llu requested_bytes=%llu rmw_reads=%llu\n",
	    strategy.buffers, strategy.read_buffers, strategy.requested_bytes, strategy.rmw_reads);
	printf("NORD2 UFS PHASES: map=%llu mutex_wait=%llu mutex_wait_max=%llu held=%llu copy=%llu\n",
	    strategy.map_ticks, strategy.mutex_wait_ticks, strategy.mutex_wait_max, strategy.held_ticks, strategy.copy_ticks);
	printf("NORD2 UFS CALLERS: raw=%llu cluster=%llu pageio=%llu async=%llu\n",
	    strategy.raw_buffers, strategy.cluster_buffers, strategy.pageio_buffers, strategy.async_buffers);
	printf("NORD2 UFS SIZES: le4k=%llu le8k=%llu le16k=%llu le32k=%llu le64k=%llu le256k=%llu le1m=%llu larger=%llu\n",
	    strategy.sizes[0], strategy.sizes[1], strategy.sizes[2], strategy.sizes[3],
	    strategy.sizes[4], strategy.sizes[5], strategy.sizes[6], strategy.sizes[7]);
	printf("NORD2 UFS PROFILE: freq=%llu commands=%llu reads=%llu writes=%llu syncs=%llu read_bytes=%llu write_bytes=%llu cmd_ticks=%llu cmd_max=%llu doorbell_ticks=%llu doorbell_max=%llu errors=%llu timeouts=%llu\n",
	    __builtin_arm_rsr64("CNTFRQ_EL0"), stats.commands, stats.reads, stats.writes,
	    stats.syncs, stats.read_bytes, stats.write_bytes, stats.command_ticks,
	    stats.command_max_ticks, stats.doorbell_ticks, stats.doorbell_max_ticks,
	    stats.errors, stats.timeouts);
	printf("NORD2 UFS LINK: valid=%#x completed=%u error=%d result=%#x pending=%u rx_gear=%u tx_gear=%u rx_lanes=%u tx_lanes=%u hs_series=%u pwr_mode=%#x\n",
	    link.valid_mask, link.completed, link.error, link.result, pending,
	    link.values[0], link.values[1], link.values[2], link.values[3],
	    link.values[4], link.values[5]);
	/* printf's kernel staging buffer is 256 bytes: keep each record short. */
	printf("NORD2 UFS LIMITS: valid=%#x understood=%#x attempted=%u rx_available=%u tx_available=%u rx_connected=%u tx_connected=%u\n",
	    limits.valid_mask, limits.understood_mask, limits.attempted,
	    limits.values[0], limits.values[1], limits.values[2], limits.values[3]);
	printf("NORD2 UFS CONFIG: rx_term=%u tx_term=%u local_ver=%#x unipro=%u tx_adapt=%#x\n",
	    limits.values[4], limits.values[5], limits.values[6], limits.values[6] & 0xf,
	    limits.values[7]);
	printf("NORD2 UFS CONTEXT: context=%u ahit=%#x hcs=%#x upmcrs=%u is=%#x\n",
	    limits.context_valid, limits.ahit, limits.hcs, (limits.hcs >> 8) & 7, limits.is);
	printf("NORD2 UFS STATUS: results=%#x/%#x/%#x/%#x/%#x/%#x/%#x/%#x errors=%d/%d/%d/%d/%d/%d/%d/%d\n",
	    limits.results[0], limits.results[1], limits.results[2], limits.results[3],
	    limits.results[4], limits.results[5], limits.results[6], limits.results[7],
	    limits.errors[0], limits.errors[1], limits.errors[2], limits.errors[3],
	    limits.errors[4], limits.errors[5], limits.errors[6], limits.errors[7]);
}
#endif /* NORD2 */

/*
 * One SCSI command in slot 0, data (if any) to/from the bounce buffer
 * (to the device when write is set). Returns 0 or an errno.
 */
static int
ufs_scsi_impl(const uint8_t *cdb, unsigned cdb_len, uint32_t len, bool write)
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

#if defined(NORD2)
	uint64_t profile_issued = nord2_ufs_profile_enabled ? nord2_ufs_counter() : 0;
#endif
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
#if defined(NORD2)
	unsigned nord2_x2_polls = 0;
#endif
	while (ufs_rd(REG_UTRLDBR) & 1) {
#if defined(NORD2)
		/* A stopped counter must not strand the pre-root diagnostic read.
		 * Never abort/ack/free its outstanding DMA on this hard bound. */
		if ((nord2_x2_in_progress || nord2_live_armed) && ++nord2_x2_polls >= 1000000u) {
			if (nord2_ufs_profile_enabled) { nord2_ufs_profile_wait(profile_issued); }
			nord2_x2_uncertain = true;
			return ETIMEDOUT;
		}
#endif
		uint64_t now = mach_absolute_time();
		if (now > deadline) {
#if defined(NORD2)
			if (nord2_ufs_profile_enabled) { nord2_ufs_profile_wait(profile_issued); }
#endif
#if defined(NORD2)
			if (nord2_x2_in_progress || nord2_live_armed) {
				nord2_x2_uncertain = true;
				return ETIMEDOUT; /* preserve outstanding DMA; never abort */
			}
#endif
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
#if defined(NORD2)
	if (nord2_ufs_profile_enabled) { nord2_ufs_profile_wait(profile_issued); }
#endif
	__builtin_arm_dsb(0xf);
#if defined(NORD2)
	/* A timed-out observation may finish late. Preserve its completion for
	 * the bounded recovery drain; SCSI must not consume another owner's UIC. */
	uint32_t status = ufs_rd(REG_IS);
	if ((nord2_x2_in_progress || nord2_live_armed) && status != 1u) {
		nord2_x2_uncertain = true;
		return EIO; /* retain all unexpected status; no hidden fatal ACK */
	}
	if (nord2_ufs_uic_pending) { status &= ~N2_UIC_COMPLETE; }
	ufs_wr(REG_IS, status);
#else
	ufs_wr(REG_IS, ufs_rd(REG_IS));         /* acknowledge completion status */
#endif

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

static int
ufs_scsi(const uint8_t *cdb, unsigned cdb_len, uint32_t len, bool write)
{
#if defined(NORD2)
	if (nord2_ufs_profile_enabled) {
		uint64_t begin = nord2_ufs_counter();
		int error = ufs_scsi_impl(cdb, cdb_len, len, write);
		uint64_t ticks = nord2_ufs_counter() - begin;
		nord2_ufs_stats.commands++;
		nord2_ufs_stats.command_ticks += ticks;
		if (ticks > nord2_ufs_stats.command_max_ticks) { nord2_ufs_stats.command_max_ticks = ticks; }
		if (cdb_len && cdb[0] == 0x28) {
			nord2_ufs_stats.reads++;
			if (!error) { nord2_ufs_stats.read_bytes += len; }
		} else if (cdb_len && cdb[0] == 0x2a) {
			nord2_ufs_stats.writes++;
			if (!error) { nord2_ufs_stats.write_bytes += len; }
		} else if (cdb_len && cdb[0] == 0x35) {
			nord2_ufs_stats.syncs++;
		}
		if (error) { nord2_ufs_stats.errors++; }
		if (error == ETIMEDOUT) { nord2_ufs_stats.timeouts++; }
		if (error && nord2_live_active) {
			nord2_x2_uncertain = true;
			panic("NORD2 UFS X2 LIVE: runtime I/O failed; retain DMA/ownership; manual reset required");
		}
		return error;
	}
#endif
#if defined(NORD2)
	if (nord2_live_active) {
		int error = ufs_scsi_impl(cdb, cdb_len, len, write);
		if (error) {
			nord2_x2_uncertain = true;
			panic("NORD2 UFS X2 LIVE: runtime I/O failed; retain DMA/ownership; manual reset required");
		}
		return error;
	}
#endif
	return ufs_scsi_impl(cdb, cdb_len, len, write);
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

/* Immutable read-only vendor mapping. SHA-256 identities bind the complete
 * GPT entry array and both LP geometry/slot0 metadata copies, including their
 * embedded checksums and padding, to the independently verified rescue dump.
 * No generic LP layout, alternate slot, changed geometry, or stale backup is
 * accepted. This does not authenticate vendor contents; the firmware loader
 * separately checks its pinned extents/digests before consuming those bytes.
 */
static bool nord2_ufs_vendor_enabled, nord2_ufs_vendor_checked;
static int nord2_ufs_vendor_error;
enum nord2_vendor_stage { N2V_NONE,N2V_CAPACITY,N2V_PRIMARY_HEADER,
 N2V_PRIMARY_ARRAY,N2V_PRIMARY_SUPER,N2V_BACKUP_HEADER,N2V_BACKUP_ARRAY,
 N2V_BACKUP_SUPER,N2V_GEOMETRY0,N2V_GEOMETRY1,N2V_LP0,N2V_LP1,N2V_PAYLOAD };
static struct nord2_vendor_diagnostic {
 uint32_t stage; int error;
 uint64_t capacity_blocks,current,other,first,last,entry_lba;
 uint32_t capacity_bytes,size,reserved,stored_crc,actual_crc,count,entry_size,entries_crc;
 bool signature_equal,revision_equal,guid_equal,backup,header_read;
} nord2_vendor_diag;

#define N2_VENDOR_LUN_BLOCKS 31240192ull
#define N2_SUPER_FIRST 344064ull
#define N2_SUPER_BYTES 11039408128ull
#define N2_VENDOR_OFFSET 7708082176ull
#define N2_VENDOR_BYTES 544354304ull
#define N2_VENDOR_MAX_READ 65536u
static const uint8_t nord2_vendor_gpt_sha[32] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static const uint8_t nord2_vendor_geometry_sha[32] = { 0xf7, 0x96, 0x25, 0x85, 0xff, 0x73, 0xe9, 0x34, 0xe1, 0x8b, 0xba, 0x02, 0x79, 0xce, 0x1c, 0xb5, 0x06, 0xf9, 0x70, 0x3c, 0x96, 0x9a, 0x9d, 0xbb, 0xb4, 0x67, 0x50, 0xbc, 0x45, 0xcb, 0x02, 0x24 };
static const uint8_t nord2_vendor_lp_sha[32] = { 0x10, 0xc9, 0xf4, 0xda, 0xd0, 0x96, 0xce, 0xff, 0x32, 0x09, 0x45, 0x63, 0x4b, 0x0d, 0x31, 0xfe, 0x86, 0x7b, 0xa6, 0x7b, 0xb6, 0x85, 0x7c, 0x85, 0xc5, 0x20, 0xb3, 0xab, 0x20, 0xa8, 0x4f, 0xf2 };

static bool
nord2_vendor_hash(const void *data, uint32_t bytes, const uint8_t expected[32])
{
	SHA256_CTX ctx;
	uint8_t digest[32];
	SHA256_Init(&ctx);
	SHA256_Update(&ctx, data, bytes);
	SHA256_Final(digest, &ctx);
	return memcmp(digest, expected, sizeof(digest)) == 0;
}

/* Live read verified backup entries start at total-8 (eight reserved tail
 * blocks), with the header at total-1. Do not assume packed total-5 layout.
 * Standalone GPT validation: never calls select_userdata or rebinds any root
 * state. Both headers/arrays must agree with the verified LUN2 layout. */
static int
nord2_vendor_gpt_copy(bool backup)
{
	uint8_t *b = (uint8_t *)(su.dma + DMA_BOUNCE);
	uint64_t current, other, first, last, entry_lba;
	uint32_t size, stored_crc, count, entry_size, entries_crc, reserved;
	uint64_t header_lba = backup ? N2_VENDOR_LUN_BLOCKS - 1 : 1;
 nord2_vendor_diag.stage = backup ? N2V_BACKUP_HEADER : N2V_PRIMARY_HEADER;
 nord2_vendor_diag.backup = backup;
 nord2_vendor_diag.header_read = false;
	int error = ufs_read_blocks(header_lba, 1);
	if (error) { return error; }
	nord2_vendor_diag.header_read = true;
 /* Capture bounded scalar header metadata only; never GUID or data bytes. */
 nord2_vendor_diag.signature_equal = !memcmp(b,"EFI PART",8);
 nord2_vendor_diag.revision_equal = !memcmp(b+8,"\0\0\1\0",4);
 memcpy(&nord2_vendor_diag.size,b+12,4);
 memcpy(&nord2_vendor_diag.stored_crc,b+16,4);
 memcpy(&nord2_vendor_diag.reserved,b+20,4);
 memcpy(&nord2_vendor_diag.current,b+24,8);
 memcpy(&nord2_vendor_diag.other,b+32,8);
 memcpy(&nord2_vendor_diag.first,b+40,8);
 memcpy(&nord2_vendor_diag.last,b+48,8);
 static const uint8_t diagnostic_zero_guid[16]={0};
 nord2_vendor_diag.guid_equal = !memcmp(b+56,diagnostic_zero_guid,16);
 memcpy(&nord2_vendor_diag.entry_lba,b+72,8);
 memcpy(&nord2_vendor_diag.count,b+80,4);
 memcpy(&nord2_vendor_diag.entry_size,b+84,4);
 memcpy(&nord2_vendor_diag.entries_crc,b+88,4);
 nord2_vendor_diag.actual_crc=0;
 if (memcmp(b, "EFI PART", 8) || memcmp(b + 8, "\0\0\1\0", 4)) { return EINVAL; }
	memcpy(&size, b + 12, 4);
	memcpy(&stored_crc, b + 16, 4);
	memcpy(&reserved, b + 20, 4);
	if (size != 92 || reserved) { return EINVAL; }
	bzero(b + 16, 4);
	uint32_t actual_crc = nord2_ufs_crc32(b, size);
 nord2_vendor_diag.actual_crc=actual_crc;
	memcpy(b + 16, &stored_crc, 4);
	if (actual_crc != stored_crc) { return EIO; }
	memcpy(&current, b + 24, 8);
	memcpy(&other, b + 32, 8);
	memcpy(&first, b + 40, 8);
	memcpy(&last, b + 48, 8);
	memcpy(&entry_lba, b + 72, 8);
	memcpy(&count, b + 80, 4);
	memcpy(&entry_size, b + 84, 4);
	memcpy(&entries_crc, b + 88, 4);
	static const uint8_t disk_guid[16] = { 0 }; /* exact saved GPT identity */
	if (current != header_lba || other != (backup ? 1 : N2_VENDOR_LUN_BLOCKS - 1) ||
	    first != 34 || last != 31240183ull || memcmp(b + 56, disk_guid, 16) ||
	    entry_lba != (backup ? N2_VENDOR_LUN_BLOCKS - 8 : 2) || count != 128 ||
	    entry_size != 128 || entries_crc != 0x00000000u) { return EINVAL; }
	nord2_vendor_diag.stage = backup ? N2V_BACKUP_ARRAY : N2V_PRIMARY_ARRAY;
 error = ufs_read_blocks(entry_lba, 4);
	if (error) { return error; }
	if (nord2_ufs_crc32(b, 16384) != entries_crc ||
	    !nord2_vendor_hash(b, 16384, nord2_vendor_gpt_sha)) { return EIO; }
	/* Super is entry54 in the pinned table. Independently check its named
	 * extent, not su.first_block/nblocks (which describe iOS userdata). */
	nord2_vendor_diag.stage = backup ? N2V_BACKUP_SUPER : N2V_PRIMARY_SUPER;
 const uint8_t *e = b + 54u * 128u;
	static const uint8_t name[12] = { 's',0,'u',0,'p',0,'e',0,'r',0,0,0 };
	memcpy(&first, e + 32, 8);
	memcpy(&last, e + 40, 8);
	if (memcmp(e + 56, name, sizeof(name)) || first != N2_SUPER_FIRST ||
	    last < first || last - first + 1 != N2_SUPER_BYTES / UFS_BLOCK) { return EINVAL; }
	return 0;
}

static int
nord2_vendor_super_hash(uint64_t offset, uint32_t bytes, const uint8_t expected[32])
{
	if ((offset | bytes) % UFS_BLOCK || !bytes || bytes > N2_VENDOR_MAX_READ ||
	    offset > N2_SUPER_BYTES || bytes > N2_SUPER_BYTES - offset) { return EINVAL; }
	int error = ufs_read_blocks(N2_SUPER_FIRST + offset / UFS_BLOCK, bytes / UFS_BLOCK);
	if (error) { return error; }
	return nord2_vendor_hash((void *)(su.dma + DMA_BOUNCE), bytes, expected) ? 0 : EIO;
}

static int
nord2_vendor_validate(void)
{
	/* Controller mutex is held throughout; use inherited LUN2 without changing
	 * it or root partition bounds. READ CAPACITY does not write media. */
	static const uint8_t capacity[10] = { 0x25 };
	nord2_vendor_diag.stage=N2V_CAPACITY;
 int error = ufs_scsi(capacity, sizeof(capacity), 8, false);
 if (error) { return error; }
 const uint8_t *b = (const uint8_t *)(su.dma + DMA_BOUNCE);
 nord2_vendor_diag.capacity_blocks=(uint64_t)nord2_ufs_be32(b)+1;
 nord2_vendor_diag.capacity_bytes=nord2_ufs_be32(b+4);
	if ((uint64_t)nord2_ufs_be32(b) + 1 != N2_VENDOR_LUN_BLOCKS ||
	    nord2_ufs_be32(b + 4) != UFS_BLOCK) { return EINVAL; }
	error = nord2_vendor_gpt_copy(false);
	if (!error) { error = nord2_vendor_gpt_copy(true); }
	if (!error) { nord2_vendor_diag.stage=N2V_GEOMETRY0; error = nord2_vendor_super_hash(4096, 4096, nord2_vendor_geometry_sha); }
	if (!error) { nord2_vendor_diag.stage=N2V_GEOMETRY1; error = nord2_vendor_super_hash(8192, 4096, nord2_vendor_geometry_sha); }
	if (!error) { nord2_vendor_diag.stage=N2V_LP0; error = nord2_vendor_super_hash(12288, 65536, nord2_vendor_lp_sha); }
	/* metadata_max65536, slots2, slot0: backup base12288+2*65536. */
	if (!error) { nord2_vendor_diag.stage=N2V_LP1; error = nord2_vendor_super_hash(143360, 65536, nord2_vendor_lp_sha); }
	return error;
}

/* Kernel-only API. Zero means the complete requested byte range was copied;
 * every failure leaves the caller buffer untouched. Validation is cached only
 * for this boot: root writes are confined to userdata, disjoint from super.
 * Neither this API nor validation changes LUN, root bounds, or write policy.
 */
int
sweet_ufs_read_vendor(uint64_t offset, void *buffer, uint32_t bytes)
{
	uintptr_t addr = (uintptr_t)buffer;
	if (!buffer || !bytes || bytes > N2_VENDOR_MAX_READ || offset > N2_VENDOR_BYTES ||
	    bytes > N2_VENDOR_BYTES - offset || addr > ~(uintptr_t)0 - bytes) { return EINVAL; }
	if (!nord2_ufs_vendor_enabled) { return EPERM; }
	if (!su.ready || !su.dma || !nord2_root_owned || su.lun != 2 || su.probe_only ||
	    !ml_get_interrupts_enabled() || ml_at_interrupt_context() ||
	    get_preemption_level() != 0) { return EBUSY; }
	/* Do not allow a caller to overwrite the shared descriptors/bounce memory. */
	if (su.dma && addr < su.dma + DMA_SIZE && addr + bytes > su.dma) { return EINVAL; }
	lck_mtx_lock(&sweet_ufs_lock);
	int error = EBUSY;
	if (!su.ready || !nord2_root_owned || su.lun != 2 || su.probe_only ||
	    nord2_ufs_uic_pending || ufs_rd(REG_UTRLDBR) || ufs_rd(0x78) ||
	    !(ufs_rd(REG_HCE) & 1) ||
	    (ufs_rd(REG_HCS) & (HCS_DP | HCS_UTRLRDY)) != (HCS_DP | HCS_UTRLRDY) ||
	    ufs_rd(REG_UTRLBA) != (uint32_t)(su.dma_pa + DMA_UTRL) ||
	    ufs_rd(REG_UTRLBAU) != (uint32_t)((su.dma_pa + DMA_UTRL) >> 32) ||
	    !(ufs_rd(REG_UTRLRSR) & 1)) { goto done; }
	if (!nord2_ufs_vendor_checked) {
		nord2_ufs_vendor_checked = true;
		nord2_ufs_vendor_error = nord2_vendor_validate();
	}
	error = nord2_ufs_vendor_error;
 if (error) { goto done; }
 nord2_vendor_diag.stage=N2V_PAYLOAD;
	uint64_t physical = N2_VENDOR_OFFSET + offset;
	uint32_t skip = (uint32_t)(physical % UFS_BLOCK);
	uint32_t blocks = (skip + bytes + UFS_BLOCK - 1) / UFS_BLOCK;
	if (physical > N2_SUPER_BYTES || bytes > N2_SUPER_BYTES - physical ||
	    N2_SUPER_FIRST + physical / UFS_BLOCK > UINT32_MAX ||
	    blocks > BOUNCE_SIZE / UFS_BLOCK) { error = EINVAL; goto done; }
	error = ufs_read_blocks(N2_SUPER_FIRST + physical / UFS_BLOCK, blocks);
	if (!error) { memcpy(buffer, (const void *)(su.dma + DMA_BOUNCE + skip), bytes); }
 done:
 nord2_vendor_diag.error=error;
	lck_mtx_unlock(&sweet_ufs_lock);
	return error;
}

/* Called by firmware bridge only after read callback released UFS mutex.
 * Snapshot under the mutex, print outside it: never nest console/UFS locks. */
void
sweet_ufs_vendor_report(void)
{
 struct nord2_vendor_diagnostic d;
 lck_mtx_lock(&sweet_ufs_lock);d=nord2_vendor_diag;lck_mtx_unlock(&sweet_ufs_lock);
 printf("NORD2 WIFI VENDOR stage=%u error=%d capacity_blocks=%llu capacity_bytes=%u\n",
 d.stage,d.error,(unsigned long long)d.capacity_blocks,d.capacity_bytes);
 printf("NORD2 WIFI GPT HEADER backup=%u read=%u sign=%u rev=%u guid_equal=%u size=%u reserved=%u crc=%#x/%#x\n",
 d.backup,d.header_read,d.signature_equal,d.revision_equal,d.guid_equal,d.size,d.reserved,
 d.stored_crc,d.actual_crc);
 printf("NORD2 WIFI GPT RANGE current=%llu other=%llu first=%llu last=%llu entry_lba=%llu count=%u entry_size=%u entries_crc=%#x\n",
 (unsigned long long)d.current,(unsigned long long)d.other,
 (unsigned long long)d.first,(unsigned long long)d.last,(unsigned long long)d.entry_lba,
 d.count,d.entry_size,d.entries_crc);
}

/* Included exactly once; do not also compile a separate module object. */
#include "ufs_two_lane.c"
#include "ufs_two_lane_adapter.inc"

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

static bool
nord2_ufs_restore(const struct nord2_ufs_saved *saved)
{
	if (nord2_live_armed || nord2_x2_stop || nord2_x2_uncertain) { return false; }
	if (!nord2_ufs_uic_drain()) { return false; }
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
	nord2_live_arm();
	if (nord2_live_armed) {
		if (nord2_live_invalid) { nord2_live_early_halt(EINVAL); }
		return; /* valid live mode bypasses point11 before any takeover */
	}
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
			__asm__ volatile("wfe");
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
	if (nord2_live_armed || nord2_x2_stop || nord2_x2_uncertain) { return -1; }
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
			__asm__ volatile("wfe");
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
sweet_ufs_open(__unused dev_t dev, int flags, __unused int devtype, __unused struct proc *p)
{
	if (!su.ready) {
		return ENXIO;
	}
	return ((flags & FWRITE) && !su.writable) ? EROFS : 0;
}

static int
sweet_ufs_close(__unused dev_t dev, __unused int flags, __unused int devtype, __unused struct proc *p)
{
	return 0;
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
#if defined(NORD2)
	bool measure = nord2_ufs_profile_enabled;
	uint64_t map_begin = measure ? nord2_ufs_counter() : 0;
#endif
	if (buf_map_range(bp, &vaddr)) {
		error = EIO;
		goto done;
	}

#if defined(NORD2)
	uint64_t wait_begin = measure ? nord2_ufs_counter() : 0;
#endif
	lck_mtx_lock(&sweet_ufs_lock);
#if defined(NORD2)
	uint64_t held_begin = measure ? nord2_ufs_counter() : 0;
	if (measure) {
		struct nord2_ufs_strategy_stats *p = &nord2_ufs_strategy_stats;
		p->buffers++; p->read_buffers += rd; p->requested_bytes += count;
		p->map_ticks += wait_begin - map_begin;
		uint64_t wait = held_begin - wait_begin;
		p->mutex_wait_ticks += wait;
		if (wait > p->mutex_wait_max) { p->mutex_wait_max = wait; }
		unsigned flags = buf_flags(bp);
		p->raw_buffers += (flags & B_RAW) != 0;
		p->cluster_buffers += (flags & B_CLUSTER) != 0;
		p->pageio_buffers += (flags & B_PAGEIO) != 0;
		p->async_buffers += (flags & B_ASYNC) != 0;
		static const uint32_t ends[7] = {4096,8192,16384,32768,65536,262144,1048576};
		unsigned bucket = 0;
		while (bucket < 7 && count > ends[bucket]) { bucket++; }
		p->sizes[bucket]++;
	}
#endif
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
#if defined(NORD2)
				uint64_t copy_begin = measure ? nord2_ufs_counter() : 0;
#endif
				memcpy(vaddr + done, (const char *)bounce + skip, want);
#if defined(NORD2)
				if (measure) { nord2_ufs_strategy_stats.copy_ticks += nord2_ufs_counter() - copy_begin; }
#endif
			}
		} else {
			/* Partial device blocks at either end: read, merge, write back. */
			if (skip || (want % UFS_BLOCK)) {
#if defined(NORD2)
				if (measure) { nord2_ufs_strategy_stats.rmw_reads++; }
#endif
				error = ufs_read_blocks(su.first_block + lba, blocks);
			}
			if (!error) {
#if defined(NORD2)
				uint64_t copy_begin = measure ? nord2_ufs_counter() : 0;
#endif
				memcpy((char *)bounce + skip, vaddr + done, want);
#if defined(NORD2)
				if (measure) { nord2_ufs_strategy_stats.copy_ticks += nord2_ufs_counter() - copy_begin; }
#endif
				error = ufs_write_blocks(su.first_block + lba, blocks);
			}
		}
		if (error) {
			break;
		}
		done += want;
		buf_setresid(bp, count - done);
	}
#if defined(NORD2)
	if (measure) { nord2_ufs_strategy_stats.held_ticks += nord2_ufs_counter() - held_begin; }
#endif
	lck_mtx_unlock(&sweet_ufs_lock);
	buf_unmap_range(bp);

done:
	if (error) {
		buf_seterror(bp, error);
	}
	buf_biodone(bp);
}

static int
sweet_ufs_ioctl(__unused dev_t dev, u_long cmd, caddr_t data, __unused int flag,
    __unused struct proc *p, int is_char)
{
	uint32_t *f = (uint32_t *)data;
	uint64_t *o = (uint64_t *)data;

	switch (cmd) {
	case DKIOCGETBLOCKSIZE:
		*f = su.secsize;
		return 0;
	case DKIOCSETBLOCKSIZE:
		if (is_char) {
			return ENODEV;
		}
		if (*f < DEV_BSIZE || *f > UFS_BLOCK || (*f & (*f - 1))) {
			return EINVAL;
		}
		su.secsize = *f;
		return 0;
	case DKIOCGETPHYSICALBLOCKSIZE:
		*f = UFS_BLOCK;
		return 0;
	case DKIOCGETBLOCKCOUNT:
		*o = su.nblocks * UFS_BLOCK / su.secsize;
		return 0;
	case DKIOCISWRITABLE:
		*f = su.writable;
		return 0;
	case DKIOCSYNCHRONIZE:
	case DKIOCSYNCHRONIZECACHE: {
		if (!su.writable) {
			return 0;
		}
		lck_mtx_lock(&sweet_ufs_lock);
		int error = ufs_sync_cache();
		lck_mtx_unlock(&sweet_ufs_lock);
		return error;
	}
	case DKIOCGETMAXBLOCKCOUNTREAD:
		*o = BOUNCE_SIZE / su.secsize;
		return 0;
	case DKIOCGETMAXBLOCKCOUNTWRITE:
		*o = su.writable ? BOUNCE_SIZE / su.secsize : 0;
		return 0;
	case DKIOCGETMAXSEGMENTCOUNTREAD:
	case DKIOCGETMAXSEGMENTCOUNTWRITE:
		*o = 1;
		return 0;
	default:
		return ENOTTY;
	}
}

static int
sweet_ufs_bioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	return sweet_ufs_ioctl(dev, cmd, data, flag, p, 0);
}

static int
sweet_ufs_cioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	return sweet_ufs_ioctl(dev, cmd, data, flag, p, 1);
}

static int
sweet_ufs_psize(__unused dev_t dev)
{
	return su.ready ? (int)MIN(su.nblocks * UFS_BLOCK / su.secsize, INT_MAX) : -1;
}

static int
sweet_ufs_read(dev_t dev, struct uio *uio, __unused int ioflag)
{
	return physio(sweet_ufs_strategy, NULL, dev, B_READ, minphys, uio, (int)su.secsize);
}

static int
sweet_ufs_write(dev_t dev, struct uio *uio, __unused int ioflag)
{
	if (!su.writable) {
		return EROFS;
	}
	return physio(sweet_ufs_strategy, NULL, dev, B_WRITE, minphys, uio, (int)su.secsize);
}

static const struct bdevsw sweet_ufs_bdevsw = {
	.d_open     = sweet_ufs_open,
	.d_close    = sweet_ufs_close,
	.d_strategy = sweet_ufs_strategy,
	.d_ioctl    = sweet_ufs_bioctl,
	.d_dump     = eno_dump,
	.d_psize    = sweet_ufs_psize,
	.d_type     = D_DISK,
};

static const struct cdevsw sweet_ufs_cdevsw = {
	.d_open       = sweet_ufs_open,
	.d_close      = sweet_ufs_close,
	.d_read       = sweet_ufs_read,
	.d_write      = sweet_ufs_write,
	.d_ioctl      = sweet_ufs_cioctl,
	.d_stop       = eno_stop,
	.d_reset      = eno_reset,
	.d_ttys       = NULL,
	.d_select     = eno_select,
	.d_mmap       = eno_mmap,
	.d_strategy   = eno_strat,
	.d_reserved_1 = eno_getc,
	.d_reserved_2 = eno_putc,
	.d_type       = D_DISK,
};

static int
sweet_ufs_init(void)
{
#if defined(NORD2)
	nord2_live_arm();
	if (nord2_live_armed && nord2_live_invalid) { nord2_live_early_halt(EINVAL); }
#endif
#if defined(NORD2)
	/* Arm before any allocation/DT failure or panic can enter fallback. */
	uint32_t x2_stop_optin = 0;
	PE_parse_boot_argn("nord2ufs2lane", &x2_stop_optin, sizeof(x2_stop_optin));
	if (x2_stop_optin == 1) { nord2_x2_stop = true; }
#endif
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
#if defined(NORD2)
	uint32_t profile = 0;
	PE_parse_boot_argn("nord2ufsprofile", &profile, sizeof(profile));
	nord2_ufs_profile_enabled = (profile == 1);
	uint32_t firmware = 0;
	PE_parse_boot_argn("nord2wififw", &firmware, sizeof(firmware));
	nord2_ufs_vendor_enabled = (firmware == 1);
#endif

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
#if defined(NORD2)
	if (!error) { nord2_ufs_profile_link(); }
	if (nord2_live_armed) {
		struct n2l_result result = { 0 };
		if (!error) { error = nord2_x2_run(&result); }
		else { result.error = error; }
		lck_mtx_unlock(&sweet_ufs_lock);
		printf("NORD2 UFS X2 LIVE: rc=%d phase=%u error=%d active=%u uncertain=%d root_allowed=%d handoff_allowed=%d\n",
		    error,result.phase,result.error,(unsigned)nord2_live_active,result.uncertain,result.root_allowed,result.handoff_allowed);
		printf("NORD2 UFS X2 LIVE PROOF: commands=%u sets=%u reads=%u verified=%d rollback=%d original=%d\n",
		    result.commands,result.sets,result.reads,result.active_verified,result.rollback_attempted,result.original_verified);
		printf("NORD2 UFS X2 LIVE PROFILE: link_fields=pre-transition-baseline return_policy=manual-reset-only\n");
		if (error || !nord2_live_active || !result.root_allowed || result.handoff_allowed) {
			nord2_x2_halt(error ? error : EIO, &result); /* no root on any failure */
		}
		goto live_verified; /* mutex already released; normal publication below */
	}
	uint32_t x2_optin = 0;
	PE_parse_boot_argn("nord2ufs2lane", &x2_optin, sizeof(x2_optin));
	if (x2_optin == 1) {
		/* Dedicated stop-only mode: even a prior init/profile error cannot
		 * fall through to root publication or normal Linux chain-back. */
		nord2_x2_stop = true;
		struct n2l_result result = { 0 };
		if (!error) { error = nord2_x2_run(&result); }
		else { result.error = error; }
		lck_mtx_unlock(&sweet_ufs_lock);
		nord2_x2_halt(error, &result); /* never returns, no root published */
	}
#endif
	lck_mtx_unlock(&sweet_ufs_lock);
	if (error) {
		goto failed;
	}

#if defined(NORD2)
live_verified:
	if (nord2_live_armed && !nord2_live_active) { nord2_live_early_halt(EIO); }
#endif
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
#else
	nord2_input_init();
	/* Separate sleepable thread, root published and UFS mutex released. */
	(void)nord2_wifi_firmware_start();
	(void)nord2_wifi_emi_start();
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

/*
 * Persistent console log ("sweetlog"). Filming the phone screen is the only
 * other way to see the console, so every character that reaches the
 * console (kernel printf and /dev/console writes of daemons, both through
 * _cnputs) is also kept in a 4 MiB ring, and once the partition is known
 * and writable a thread copies new ring blocks to the LAST 4 MiB + 4 KiB of
 * the partition every ufslogsecs seconds (default 10), far behind the HFS
 * image at its start. Device block 0 of that area is a header:
 *   "SWEETLOG" | u32 version 1 | u32 ring size | u64 total bytes written
 *   | u64 flush sequence
 * ring offset of byte n = n % ring size. Read it from a recovery with
 * tools/sweet-log-pull.sh. Disabled with boot-arg sweetlog=0.
 */
#define SWEET_LOG_SIZE          (4u << 20)
#define SWEET_LOG_BLOCKS        (1 + SWEET_LOG_SIZE / UFS_BLOCK)
#define SWEET_LOG_SECS          10u

static char sweet_log_ring[SWEET_LOG_SIZE];
static uint64_t _Atomic sweet_log_total;
static int sweet_log_chan;
static uint32_t sweet_log_secs = SWEET_LOG_SECS;

/* Called for all console output, any context: never blocks. */
void sweet_log_append(const char *c, int size);

void
sweet_log_append(const char *c, int size)
{
	if (size <= 0) {
		return;
	}
	uint64_t at = os_atomic_add_orig(&sweet_log_total, (uint64_t)size, relaxed);
	for (int i = 0; i < size; i++) {
		sweet_log_ring[(at + (uint64_t)i) % SWEET_LOG_SIZE] = c[i];
	}
}

/*
 * Copy console bytes [*pos, total) (at most max) for live log streaming
 * (sweet_usb.c). A reader that fell more than the ring behind skips ahead.
 * Returns the byte count; *pos advances.
 */
uint32_t sweet_log_copy(uint64_t *pos, char *out, uint32_t max);

uint32_t
sweet_log_copy(uint64_t *pos, char *out, uint32_t max)
{
	uint64_t total = os_atomic_load(&sweet_log_total, relaxed);
	if (total - *pos > SWEET_LOG_SIZE) {
		*pos = total - SWEET_LOG_SIZE;
	}
	uint32_t n = (uint32_t)MIN((uint64_t)max, total - *pos);
	for (uint32_t i = 0; i < n; i++) {
		out[i] = sweet_log_ring[(*pos + i) % SWEET_LOG_SIZE];
	}
	*pos += n;
	return n;
}

uint64_t sweet_log_position(void);

uint64_t
sweet_log_position(void)
{
	return os_atomic_load(&sweet_log_total, relaxed);
}

struct sweet_log_header {
	char magic[8];
	uint32_t version, size;
	uint64_t total, seq;
};

static void
sweet_log_thread(__unused void *arg, __unused wait_result_t wr)
{
	uint64_t base = su.first_block + su.nblocks - SWEET_LOG_BLOCKS;
	uint64_t flushed = 0, seq = 0;
	void *bounce = (void *)(su.dma + DMA_BOUNCE);

	printf("SWEET LOG: console log to the last %u KiB of the partition (block 0x%llx)\n",
	    SWEET_LOG_BLOCKS * UFS_BLOCK >> 10, base);
	for (;;) {
		(void)tsleep(&sweet_log_chan, PZERO, "sweetlog", hz * (int)sweet_log_secs);
		uint64_t total = os_atomic_load(&sweet_log_total, relaxed);
		if (total == flushed) {
			continue;
		}
		uint64_t pos = total - flushed >= SWEET_LOG_SIZE ? total - SWEET_LOG_SIZE : flushed;
		int error = 0;

		lck_mtx_lock(&sweet_ufs_lock);
		while (pos < total && !error) {
			uint32_t off = (uint32_t)(pos % SWEET_LOG_SIZE);
			uint32_t blk = off / UFS_BLOCK;
			uint64_t need = ((uint64_t)off % UFS_BLOCK + (total - pos) + UFS_BLOCK - 1) / UFS_BLOCK;
			uint32_t n = (uint32_t)MIN(MIN(need, (uint64_t)(SWEET_LOG_SIZE / UFS_BLOCK - blk)),
			    (uint64_t)(BOUNCE_SIZE / UFS_BLOCK));
			memcpy(bounce, sweet_log_ring + (uint64_t)blk * UFS_BLOCK, n * UFS_BLOCK);
			error = ufs_write_blocks(base + 1 + blk, n);
			pos += (uint64_t)(blk + n) * UFS_BLOCK - off;
		}
		if (!error) {
			struct sweet_log_header h = {
				.magic = { 'S', 'W', 'E', 'E', 'T', 'L', 'O', 'G' },
				.version = 1, .size = SWEET_LOG_SIZE, .total = total, .seq = ++seq,
			};
			bzero(bounce, UFS_BLOCK);
			memcpy(bounce, &h, sizeof(h));
			error = ufs_write_blocks(base, 1);
		}
		if (!error) {
			error = ufs_sync_cache();
		}
		lck_mtx_unlock(&sweet_ufs_lock);
		if (error) {
			printf("SWEET LOG: write failed (%d), stopping\n", error);
			return;
		}
		flushed = total;
	}
}

static void
sweet_log_start(void)
{
	uint32_t on = 1;
	thread_t thread;

	PE_parse_boot_argn("sweetlog", &on, sizeof(on));
	if (!on || !su.writable || su.nblocks < 2 * SWEET_LOG_BLOCKS) {
		return;
	}
	PE_parse_boot_argn("ufslogsecs", &sweet_log_secs, sizeof(sweet_log_secs));
	if (sweet_log_secs == 0 || sweet_log_secs > 60) {
		sweet_log_secs = SWEET_LOG_SECS;
	}
	if (kernel_thread_start(sweet_log_thread, NULL, &thread) == KERN_SUCCESS) {
		thread_deallocate(thread);
	}
}

/* Root device for rd=ufsroot0 (IOFindBSDRoot). */
int sweet_ufs_rootdev(dev_t *dev);

int
sweet_ufs_rootdev(dev_t *dev)
{
#if defined(NORD2)
	nord2_live_arm();
	if (nord2_live_armed && (nord2_live_invalid || (su.ready && !nord2_live_active))) {
		nord2_live_early_halt(EINVAL);
	}
#endif
	if (!su.ready) {
		int error = sweet_ufs_init();
		if (error) {
#if defined(NORD2)
			if (nord2_live_armed) { nord2_live_early_halt(error); }
#endif
			return error;
		}
	}
#if defined(NORD2)
	if (nord2_live_armed && !nord2_live_active) { nord2_live_early_halt(EIO); }
#endif
	*dev = su.bdev;
	return 0;
}
#endif /* ARM64_BOARD_CONFIG_SWEET */
