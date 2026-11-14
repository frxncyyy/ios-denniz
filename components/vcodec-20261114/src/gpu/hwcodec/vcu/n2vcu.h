/* SPDX-License-Identifier: MIT */
/*
 * n2vcu.h -- the /dev/vcu device model: the Linux mtk_vcu ABI in user space, over n2vc_hw.
 * Owner: V40.  Consumer: the AP side (V41, ap/n2ap_venc.c) and the host runner (IV).
 *
 * What this is
 * ------------
 * On Android the VENC/VDEC cores are programmed by user space (the vpud daemon) through the
 * mtk_vcu character device: an IPI message relay plus the GCE command lists that the kernel
 * replays onto the codec register windows, plus the register-window mmap.  In our architecture
 * the daemon is an interpreted ARM32 process and the kernel side disappears; n2vcu.c is the part
 * of mtk_vcu.c that the daemon can still see, implemented against the frozen n2vc_hw.h back end
 * (host fake T02 on the PC, /dev/redwood-vcodec P03 on the phone).
 *
 * Two directions
 * --------------
 *   daemon -> AP:  the daemon calls VCU_SET_OBJECT; n2vcu hands the payload to the handler the AP
 *                  registered for that IPI id with n2vcu_ap_register().  This replaces the V4L2
 *                  driver's ipi handler (venc_vcodec_msg_handler's peer in the kernel).
 *   AP -> daemon:  the AP calls n2vcu_ap_send(); the daemon's VCU_GET_OBJECT ioctl fetches it.
 *                  This replaces vcu_ipi_send().
 *
 * Threading: one guest thread may sit in VCU_GET_OBJECT while another host thread calls
 * n2vcu_ap_send(); both are safe.  The same holds for VCU_GET_LOG_OBJECT and n2vcu_ap_set_log().
 * The AP handler runs on the thread that entered VCU_SET_OBJECT and must not assume any lock is
 * held (n2vcu takes none around it).  Waits that the kernel makes interruptible have a timeout in
 * the model and return -ETIMEDOUT instead (settable with n2vcu_set_get_timeout_ms()); the two
 * waits the kernel makes indefinite - GET_LOG_OBJECT and GET_OBJECT - block until their event,
 * abort or the calling cpu's stop_request, which returns -EINTR like a signal does.
 */
#ifndef N2VCU_H
#define N2VCU_H
#include <stdint.h>
#include "n2a32.h"
#include "n2vc_hw.h"
#include "n2elf.h"   /* struct n2vfs_dev, n2vfs_register() */

struct n2vcu;

/*
 * AP-side IPI handler, the peer of ipi_handler_t (mtk_vcu.h).  `ctx` is what n2vcu_ap_register()
 * stored, `ipi_id` is the share_obj.id, `buf`/`len` are share_obj.share_buf/share_obj.len.  The
 * buffer is a private copy of the guest's 80-byte share area; write through it if the reply must
 * modify what the daemon reads back (the kernel copies the modified share_obj back to the daemon,
 * mtk_vcu.c:1814-1821).
 *
 * Return 0 when the answer has been produced: the kernel then raises the ACK that the sender's
 * vcu_ipi_send() waits for (mtk_vcu.c:1403-1410).  Non-zero means "no ACK yet".
 */
typedef int (*n2vcu_ipi_handler)(void *ctx, int ipi_id, void *buf, uint32_t len);

/* Create the model.  `mem` is the guest address space (n2a32_mem_create), `hw` is the back end
 * (copied).  Allocates the 0x80000-byte VCU DMEM (mtk_vcu.c:2542 with VCU_SHMEM_SIZE) and the
 * GCE scratch buffer.  NULL on failure. */
struct n2vcu *n2vcu_create(struct n2a32_mem *mem, const struct n2vc_hw *hw);
void n2vcu_destroy(struct n2vcu *vcu);

/* The device nodes to hand to n2vfs_register(): "/dev/vcu" and its alias "/dev/vpu".  Both share
 * the same state; the decoder path of libvpud_vcodec.so opens both (D01 facts/vcu_usage.md 1).
 * n2vfs.h has no unregister, so destroy the model last. */
const struct n2vfs_dev *n2vcu_dev(struct n2vcu *vcu);
const struct n2vfs_dev *n2vcu_dev_vpu(struct n2vcu *vcu);

/* AP side.  ipi_id must be in [IPI_VCU_INIT, IPI_MAX) (mtk_vcu.c:365-368); handler != NULL. */
int n2vcu_ap_register(struct n2vcu *vcu, int ipi_id, n2vcu_ipi_handler handler, void *ctx);
/* Queue one message the daemon's VCU_GET_OBJECT will return.  buf/len is the share payload
 * (len <= 80), ipi_id the share_obj.id; the kernel's vcu_ipi_send() checks id > IPI_VCU_INIT,
 * id < IPI_MAX, len <= 80 (mtk_vcu.c:392-400).  Returns 0 or -EINVAL/-ENOMEM.  Does not block:
 * the kernel additionally waits for the daemon to fetch and dispatch it.  Use
 * n2vcu_ap_wait_dispatched() for that half. */
int n2vcu_ap_send(struct n2vcu *vcu, int ipi_id, const void *buf, uint32_t len);
/* 1 when a queued message for this IPI id has not been fetched yet. */
int n2vcu_ap_pending(struct n2vcu *vcu, int ipi_id);
/* The AP side of the kernel's vcu_set_log() (mtk_vcu.c:2113-2145): copy `text` (NUL-terminated,
 * truncated to 1023 bytes, :2123) into the log buffer, set vdec_log_got and wake a daemon blocked
 * in VCU_GET_LOG_OBJECT.  The tail of the kernel's buffer keeps its old bytes; so does this one.
 * Returns 0, or -EINVAL for a NULL vcu/text.  Safe from any thread, no lock held by the caller. */
int n2vcu_ap_set_log(struct n2vcu *vcu, const char *text);
/* Wait until the daemon has entered VCU_GET_OBJECT for this IPI's instance after the send
 * (the kernel's ipi_done spin, mtk_vcu.c:441-446).  0, -ETIMEDOUT or -EINTR (aborted). */
int n2vcu_ap_wait_dispatched(struct n2vcu *vcu, int ipi_id, unsigned timeout_ms);

/* What vcu_mapping_dm_addr() does (mtk_vcu.c:1199-1230): translate a guest address the daemon got
 * from the MAP_SHMEM_ALLOC/COMMIT mmap into a host pointer to the DMEM.  NULL if guest_addr is 0,
 * below the DMEM or past DMEM+len. */
void *n2vcu_map_dm_addr(struct n2vcu *vcu, uint32_t guest_addr);
/* The DMEM's guest address and size (n2vcu_map_dm_addr's valid window); 0 while unavailable. */
int n2vcu_dmem(struct n2vcu *vcu, uint32_t *guest_addr, uint32_t *bytes);

/* Test/phone tuning.  Defaults: GET_OBJECT 10000 ms, GCE wait-for-event 1000 ms, poll-address
 * 1000 ms, wait-for-callback 5000 ms, GCE threads 1 VDEC / 2 VENC (mt6893.dts:6219-6220). */
void n2vcu_set_get_timeout_ms(struct n2vcu *vcu, unsigned ms);
void n2vcu_set_irq_timeout_ms(struct n2vcu *vcu, unsigned ms);
void n2vcu_set_poll_timeout_ms(struct n2vcu *vcu, unsigned ms);
void n2vcu_set_callback_timeout_ms(struct n2vcu *vcu, unsigned ms);
void n2vcu_set_gce_threads(struct n2vcu *vcu, unsigned vdec_threads, unsigned venc_threads);

/* DEC04: combine guest register stores.  OFF by default.
 *
 * ON: an aligned 32-bit guest store to a mapped register window is queued and leaves as one
 * RWVC_REGS batch instead of a read-modify-write pair.  The queue is flushed, in order, before
 * any register read, poll, wait_irq, GCE list, power change, sub-word or unaligned access, any
 * ioctl entry, close/destroy, and when it reaches 256 ops (one RWVC_REG_MAX_OPS-safe batch).
 * A guest read that arrives with stores queued carries them in the same batch, ahead of the
 * read (one ioctl instead of a flush plus a read; the value is still read after the stores).
 * Two guest threads keep their single lock order; the queue is per model, not per thread.
 * OFF: every store is executed immediately, exactly as before rev DEC04.  Turning it off
 * flushes whatever is queued first.  A failed batch loses its stores asynchronously (the guest
 * store itself cannot report it); it is counted in stats.wc_errors. */
void n2vcu_set_write_combining(struct n2vcu *vcu, int on);

/* Break every bounded wait (teardown, guest abort); waits return -EINTR from then on. */
void n2vcu_abort(struct n2vcu *vcu);

struct n2vcu_stats {
	uint64_t set_object, get_object, get_object_timeouts, get_object_interrupted;
	uint64_t ipi_queued, ipi_overwritten, ipi_acks;
	uint64_t allocations, allocation_failures, frees, free_failures, cache_syncs;
	uint64_t gce_flushes, gce_cmds, gce_skipped, gce_errors, gce_waits, gce_wait_timeouts;
	uint64_t gce_callbacks;
	/* VENC bitstream mirror refresh (VENC_PrependHeader reads the buffer on the CPU inside the
	 * job): one sync per 0x064 write in a VENC GCE list, and the bytes copied device->guest. */
	uint64_t gce_bitstream_syncs, gce_bitstream_bytes;
	uint64_t mmaps, mmap_failures, mmio_reads, mmio_writes, mmio_bad;
	uint64_t log_sets, log_gets, log_waits, log_blocked, map_types, unknown_requests;
	uint64_t last_bad_mmio_addr, last_skipped_cmd_addr, last_irq_status;
	/* DEC04.  hw_regs_calls counts every hw->regs() invocation (one RWVC_REGS ioctl per call on
	 * the phone): an uncombined aligned store costs two (the read-modify-write), a combined one
	 * shares a batch.  wc_writes is the number of guest stores that entered the queue;
	 * wc_flushes/wc_batch_ops/wc_max_batch describe the batches it left in; wc_forced counts the
	 * flushes caused by the 256-op cap; wc_errors counts batches the back end rejected (their
	 * stores are lost, like a posted write to a powered-off block). */
	uint64_t hw_regs_calls;
	uint64_t wc_writes, wc_flushes, wc_batch_ops, wc_max_batch, wc_forced, wc_errors;
	/* opus-decint: guest stores to larb SLP_CON sent as a bit-0 masked write (kernel allow-list) */
	uint64_t larb_slp_writes;
	/* opus-enc4k: per-phase wall time of the VENC GCE flushes (microseconds, cumulative) for vcodecd's per-frame phase
	 * line: hw->power on/off calls, the whole list run (gce_run_us, includes the waits), the interrupt waits alone, and
	 * the lists per core (the vendor alternates core 0 / core 1 per frame). */
	uint64_t venc_power_on_us, venc_power_off_us, venc_power_ops, gce_run_us, gce_wait_us;
	uint64_t venc_lists_c0, venc_lists_c1;
};
/* opus-enc4k: hold = 1 keeps a VENC core powered after its first GCE list instead of powering it off after every list
 * (the Linux driver's per-job venc_encode_prepare/unprepare); n2vcu_venc_release() drops the held references (vcodecd at
 * session end).  Default 0 = per-list power cycle, as before. */
void n2vcu_set_venc_hold(struct n2vcu *vcu, int hold);
void n2vcu_venc_release(struct n2vcu *vcu);
/* out[0..6] = venc_power_on_us, venc_power_off_us, venc_power_ops, gce_run_us, gce_wait_us, venc_lists_c0, venc_lists_c1
 * (cumulative); for callers that do not include this header's struct (vcodecd declares it weak). */
void n2vcu_venc_phases(struct n2vcu *vcu, uint64_t out[7]);
void n2vcu_stats(const struct n2vcu *vcu, struct n2vcu_stats *out);

#endif /* N2VCU_H */
