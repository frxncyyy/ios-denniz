/* SPDX-License-Identifier: MIT */
/*
 * n2ap_vdec.h -- the AP side of a MediaTek MT6893 VDEC session in user space: what the Linux V4L2
 * decoder driver does (mtk-vcodec vdec_vcu_if.c, vdec/vdec_common_if.c, mt6885/mtk_vcodec_dec_pm.c
 * and the decoder part of mt6885/mtk_vcodec_intr.c), re-done over the /dev/vcu model.
 *
 * Owner: DEC01 (gpu/hwcodec/ap).  Consumers: the host runner's decode probe (test_dec01), the daemon
 * (package IP/DEC05) and the VideoToolbox decoder object (DEC06).  The encoder twin of this file is
 * ap/n2ap_venc.c (V41); the message flow is documented in ap/VDEC_FLOW.md.
 *
 * What a session is, in kernel terms
 * ----------------------------------
 *   vdec_if_init()            -> n2ap_vdec_open(): IPI id from the codec fourcc (vdec_common_if.c:104-150),
 *                                AP_IPIMSG_DEC_INIT (vdec_vcu_if.c:511-544) -> VCU_IPIMSG_DEC_INIT_ACK,
 *                                then the vsi = vcu_mapping_dm_addr(vcu_inst_addr) (:22-37).
 *   vdec_if_get_param(cap)    -> n2ap_vdec_query_cap(): AP_IPIMSG_DEC_QUERY_CAP on IPI_VDEC_COMMON
 *                                (:594-625, answers in :39-68).
 *   vdec_if_set_param()       -> n2ap_vdec_set_param(): FRAME_SIZE / DECODE_MODE / NAL_SIZE_LENGTH /
 *                                WAIT_KEY_FRAME / OPERATING_RATE are messages (:627-651), FB_NUM_PLANES
 *                                and the CRC/GOLDEN paths are vsi-only writes (vdec_common_if.c:597-640).
 *   vdec_if_decode(bs, fb)    -> n2ap_vdec_decode(): vsi.dec.bs_dma/bs_fd/... and fb_dma/fb_fd (the
 *                                kernel installs a dmabuf fd per buffer, :72-158 and :217-260), then
 *                                AP_IPIMSG_DEC_START with data[] = {size, length, flags} (:243-290).
 *                                bs == NULL means flush -> AP_IPIMSG_DEC_RESET.
 *   the daemon's requests    -> WAITISR / GET_FRAME_BUFFER / PUT_FRAME_BUFFER / LOCK/UNLOCK_CORE/LAT
 *                                are answered synchronously on the daemon's thread, exactly as
 *                                vcu_dec_ipi_handler() does (vdec_vcu_if.c:185-407).
 *   vdec_if_deinit()          -> n2ap_vdec_close(): AP_IPIMSG_DEC_DEINIT + power release.
 *
 * The LOCK/UNLOCK role (package DEC01's second half)
 * -------------------------------------------------
 * On Android the V4L2 driver owns the clocks: LOCK_CORE/LOCK_LAT call vdec_decode_prepare() ->
 * mtk_vcodec_dec_clock_on() (mtk_vcodec_dec_pm.c:154-251), UNLOCK_* call vdec_decode_unprepare() ->
 * ..._clock_off() (:253-288) and mtk_vdec_hw_break() (:296-460).  This library plays that role:
 *   - the first LOCK (either core) restores the 68 racing words at VDEC_RACING+0x100;
 *   - the last UNLOCK saves them (mtk_vcodec_dec_pm.c:269-280); every UNLOCK runs the HW-break sequence
 *     of the core it releases, before that block is powered down (:282, :296-460);
 *   - a LOCK of VDEC_CORE takes RWVC_BLK_VDEC_CORE (and first its parent VDEC_SOC, which the kernel
 *     holds for the whole active period), a LOCK of VDEC_LAT takes RWVC_BLK_VDEC_SOC.
 *
 * Threading: the VCU -> AP requests arrive on the daemon's thread while the caller sits in
 * n2ap_vdec_decode()/close(); the client state they touch is protected by one mutex.  One session per
 * struct n2ap_vdec; no global state except the QUERY_CAP handler slot.
 *
 * Errors: every function returns 0 or a negative errno; every wait is bounded by the session timeout
 * (default 6000 ms, IPI_TIMEOUT_MS of mtk_vcu.c:72).  A timed-out send marks the session aborted and
 * later calls return -EIO until close, mirroring vdec_vcu_if.c:416-417.  No printing: use the trace hook.
 *
 * Send ordering is the same as ap/n2ap_venc.c rev 3 (V41): wait for the ACK (the daemon's reply
 * dispatched to the handler) first, then for the "dispatched" ipi_done half, because the phone's vpud
 * re-enters VCU_GET_OBJECT from its receiver thread before its service thread answers.
 */
#ifndef N2AP_VDEC_H
#define N2AP_VDEC_H
#include <stddef.h>
#include <stdint.h>
#include "mtk_vdec_ipi.h"
#include "n2a32.h"
#include "n2vc_hw.h"

struct n2vcu;
struct n2ap_vdec;

/* V4L2 fourccs (uapi/linux/videodev2.h:657-667 of the Android tree), little-endian. */
#define N2AP_VDEC_FOURCC_H264  0x34363248u   /* 'H264' */
#define N2AP_VDEC_FOURCC_H265  0x35363248u   /* 'H265' */
#define N2AP_VDEC_FOURCC_HEIF  0x46494548u   /* 'HEIF' */
/* opus-fmt: the other decoders (videodev2.h:664-683 of the Android tree); IPI ids mtk_vcu.h:119-127 */
#define N2AP_VDEC_FOURCC_VP8   0x30385056u   /* 'VP80' */
#define N2AP_VDEC_FOURCC_VP9   0x30395056u   /* 'VP90' */
#define N2AP_VDEC_FOURCC_AV1   0x30315641u   /* 'AV10' */
#define N2AP_VDEC_FOURCC_MPEG2 0x3247504du   /* 'MPG2' (MPEG-1 too: IPI_VDEC_MPEG12) */
#define N2AP_VDEC_FOURCC_MPEG4 0x3447504du   /* 'MPG4' */
#define N2AP_VDEC_FOURCC_H263  0x33363248u   /* 'H263' */
#define N2AP_VDEC_FOURCC_MT2T  0x5432544du   /* 'MT2T' 10-bit tiled output (VP9 profile 2, HEVC Main10) */
#define N2AP_VDEC_FOURCC_MM21  0x31324d4du   /* 'MM21', the layout ap/mm21 detiles */
#define N2AP_VDEC_FOURCC_MT21  0x3132544du   /* 'MT21', UFO-compressed (property layout.mode unset) */
#define N2AP_VDEC_FOURCC_M21S  0x5331324du   /* 'M21S', the small-picture variant */

/* IPI ids (mtk_vcu.h:113-127).  QUERY_CAP goes on IPI_VDEC_COMMON, like vcu_dec_query_cap() maps a
 * not-yet-initialised instance's id 0 to it (vdec_vcu_if.c:606). */
#define N2AP_VDEC_IPI_COMMON  1
#define N2AP_VDEC_IPI_H264    2
#define N2AP_VDEC_IPI_H265    3
#define N2AP_VDEC_IPI_HEIF    4
#define N2AP_VDEC_IPI_VP8     5
#define N2AP_VDEC_IPI_VP9     6
#define N2AP_VDEC_IPI_MPEG4   7
#define N2AP_VDEC_IPI_H263    8
#define N2AP_VDEC_IPI_MPEG12  9
#define N2AP_VDEC_IPI_AV1     13

/* Bounds of this implementation (fixed-size arrays, no allocation in the request path).  The three
 * harvest queues hold one entry per ring slot: the daemon's rings are 64 entries each
 * (VIDEO_MAX_FRAME), so a smaller queue would drop a cookie and leak the buffer. */
#define N2AP_VDEC_FB_QUEUE    64   /* output buffers the client may hand to the daemon */
#define N2AP_VDEC_OUT_QUEUE   64   /* displayed frames remembered until popped */
#define N2AP_VDEC_FREE_QUEUE  64   /* released buffers harvested from vsi.list_free/list_free_bs */
#define N2AP_VDEC_RACING_WORDS 68  /* MTK_VDEC_RACING_INFO_SIZE, mt6885/mtk_vcodec_pm.h:16 */

/* HW-break timing (mt6885/mtk_vcodec_dec_pm.c:373-374: 20 ms, then 1 s, then give up). */
#define N2AP_VDEC_HW_BREAK_TIMEOUT_US 20000u

/* One frame buffer the daemon may use as output (GET_FRAME_BUFFER serves these).
 *
 * Two-plane rule (the vendor's MM21 output): plane 1 is either a second DMA allocation
 * (c_handle != 0 && c_handle != handle) or lives right after plane 0 in the same allocation
 * (c_handle == handle, chroma at pa + vsi.pic.fb_sz[0], same share id).  A buffer with neither
 * when the vendor reports fb_sz[1] != 0 is refused: programming a zero chroma target is a DMA
 * write to physical address 0.
 *
 * `in_disp`/`in_free` are library-private state (the harvest uses them to handle list_free
 * arriving before list_disp with B-frames); callers leave them zero.  A buffer may be pushed
 * back only after the harvest dropped it, i.e. after BOTH rings returned it. */
struct n2ap_vdec_fb {
	uint32_t handle;      /* n2vc_hw DMA handle of plane 0 (Y / NV12) */
	uint32_t c_handle;    /* plane 1 DMA handle; 0 = single plane; == handle = same allocation */
	int32_t fd;           /* V34 share id the daemon imports for plane 0, 0 = none */
	int32_t c_fd;         /* ... plane 1 */
	uint32_t bytes;       /* size of plane 0, for the sync (0 = the whole allocation) */
	uint32_t index;       /* the buffer index the daemon sees (vsi.dec.index) */
	uint64_t cookie;      /* opaque token the AP gets back as vdec_fb_va; 0 = use `handle` */
	uint32_t in_disp;     /* library-private: seen in vsi.list_disp */
	uint32_t in_free;     /* library-private: seen in vsi.list_free */
};

/* One displayed frame (PUT_FRAME_BUFFER harvest of vsi.list_disp). */
struct n2ap_vdec_out {
	uint64_t cookie;
	int32_t poc;
	uint64_t timestamp;
	uint64_t y_dma;
	uint64_t c_dma;
};

/* One frame buffer the daemon has released (vsi.list_free harvest).  Reusable by the client only
 * together with n2ap_vdec_pop_displayed(): a buffer must not be pushed back to the free queue until
 * BOTH the daemon returned it here AND the client has consumed the displayed picture with the same
 * cookie.  With B-frames list_free can arrive before list_disp for the same cookie, so the two
 * queues are independent and the caller does the intersection (see VDEC_FLOW.md section 6). */
struct n2ap_vdec_freed {
	uint64_t cookie;
	uint64_t y_dma;
	uint64_t c_dma;
};

/* What one header START reported (GET_PARAM_PIC_INFO / CROP / DPB_SIZE, vdec_vcu_if.c:39-68).  The
 * display rectangle is vsi.crop, not pic_h: pic_w/pic_h and buf_w/buf_h are the coded size (DEC03's
 * 1920x1080 vector is pic 1920x1088 with crop 1920x1080). */
struct n2ap_vdec_pic {
	uint32_t disp_w, disp_h;    /* vsi.crop.width/height: the visible picture */
	uint32_t pic_w, pic_h;      /* vsi.pic.pic_w/pic_h: coded */
	uint32_t buf_w, buf_h;      /* vsi.pic.buf_w/buf_h: allocation, buf_h is 32-aligned for MM21 */
	uint32_t fb_sz[2];          /* Y and C plane sizes (fb_sz[1] != 0 = planar output) */
	uint32_t fourcc;            /* vsi.pic.fourcc (N2AP_VDEC_FOURCC_MM21 when layout.mode is 0) */
	uint32_t bitdepth;
	uint32_t layout_mode;
	uint32_t dpb_sz;            /* vsi.dec.dpb_sz; the DPB count is dpb_sz + 1 */
	uint32_t changed;           /* vsi.dec.vdec_changed_info */
};

/* Open configuration.  Only `vcu` and `hw` are required. */
struct n2ap_vdec_cfg {
	struct n2vcu *vcu;
	const struct n2vc_hw *hw;
	uint32_t codec_fourcc;      /* N2AP_VDEC_FOURCC_*; default H264 when 0 */
	unsigned timeout_ms;        /* 0 = 6000 (IPI_TIMEOUT_MS) */
	uint32_t pic_w, pic_h;      /* SET_PARAM_FRAME_SIZE; 0/0 = do not send it */
	uint32_t decode_mode;       /* SET_PARAM_DECODE_MODE; sent only when set_decode_mode is 1 */
	int set_decode_mode;
	uint32_t fb_num_planes;     /* vsi.dec.fb_num_planes (SET_PARAM_FB_NUM_PLANES); 0 = 2 for H.264/
	                             * H.265 (the vendor's MM21 path needs both planes) and 1 for HEIF */
	uint32_t output_fourcc;     /* 0 = accept the vendor's output format; otherwise require this
	                             * fourcc after a header START that reports RES_CHANGE and fail with
	                             * -ENOTSUP otherwise.  DEC05/DEC06 set N2AP_VDEC_FOURCC_MM21: with
	                             * vendor.mtk.vdec.frame.layout.mode unset the library returns UFO
	                             * (MT21, >= 1080p) or MT21S (M21S, small pictures), neither of
	                             * which ap/mm21's detile understands. */
	uint32_t ring_entries;      /* vsi ring entry count the daemon uses; 0 = N2AP_DEC_MAX_FB_NUM (64).
	                             * Diagnostic: a 32-entry vpud needs 32 (see n2ap_vdec_set_ring_entries). */
	int query_cap;              /* 1 = send AP_IPIMSG_DEC_QUERY_CAP on IPI_VDEC_COMMON before INIT */
	int no_power;               /* 1 = answer LOCK/UNLOCK without touching hw->power (tests only) */
	int fake_irq;               /* 1 = answer WAITISR with success without waiting (host tests only:
	                             * T02's fake raises no VDEC interrupt; the reference client's
	                             * VR_FAKEIRQ does the same).  Never set on the phone. */
};

/* Trace hook: one call per notable event, no printing in the library.
 * Events: "init", "init_done", "query_cap", "cap_done", "set_param", "param_done", "decode",
 * "decode_done", "flush", "reset_ack", "deinit", "lock", "unlock", "power", "racing_restore",
 * "racing_save", "hw_break", "wait_isr", "get_fb", "put_fb", "fb_free", "timeout", "proto", "hw". */
typedef void (*n2ap_vdec_trace_fn)(void *arg, const char *event, uint32_t a, uint32_t b, uint32_t c);
void n2ap_vdec_set_trace(struct n2ap_vdec *d, n2ap_vdec_trace_fn fn, void *arg);

/*
 * Open a session.  `cfg->vcu` is V40's /dev/vcu model and `cfg->hw` the hardware back end (only
 * hw->power, hw->regs, hw->wait_irq and hw->irq_clear are used).  On success the vsi is mapped and the
 * INIT + SET_PARAM sequence has been acknowledged by the daemon.
 */
int n2ap_vdec_open(struct n2ap_vdec **out, const struct n2ap_vdec_cfg *cfg);

/* Send AP_IPIMSG_DEC_DEINIT, release any POWER reference still held, save the racing words if this
 * session left the decoder powered, replace the IPI handler with a dropper and free the session.
 * Returns the first error of that sequence; the pointer is invalid afterwards. */
int n2ap_vdec_close(struct n2ap_vdec *d);

/*
 * Capability query.  `id` is GET_PARAM_CAPABILITY_SUPPORTED_FORMATS (8) or
 * GET_PARAM_CAPABILITY_FRAME_SIZES (9); `out` receives MTK_MAX_DEC_CODECS_SUPPORT entries of
 * sizeof(struct mtk_video_fmt) (12) or sizeof(struct mtk_codec_framesizes) (36) respectively, the same
 * copy vdec_vcu_if.c:39-68 makes.  The message goes on IPI_VDEC_COMMON.
 */
int n2ap_vdec_query_cap(struct n2ap_vdec *d, unsigned id, void *out, uint32_t out_bytes);

/* One vdec_set_param call: `id` is enum vdec_set_param_type, `data`/`count` the words the kernel
 * copies into the message (vdec_vcu_if.c:641-645).  The vsi-only types (FB_NUM_PLANES, CRC_PATH,
 * GOLDEN_PATH) are handled locally, exactly like vdec_common_if.c:633-640. */
int n2ap_vdec_set_param(struct n2ap_vdec *d, unsigned id, const uint32_t *data, unsigned count);

/* Convenience wrappers over the two message types the V4L2 driver always sends (mtk_vcodec_dec.c:
 * 2200-2211).  Return 0 or -errno. */
int n2ap_vdec_set_frame_size(struct n2ap_vdec *d, uint32_t w, uint32_t h);
int n2ap_vdec_set_decode_mode(struct n2ap_vdec *d, uint32_t mode);

/*
 * Diagnostic only: tell the client which ring entry count the daemon's `struct vdec_vsi` has.  The vsi
 * is allocated by the daemon, so the offset of `dec`/`pic`/`video_formats` depends on its
 * DEC_MAX_FB_NUM; this kernel's VIDEO_MAX_FRAME is 64 (mtk_vdec_ipi.h) and a vendor library built
 * against a 32-entry header would put `dec` 3072 bytes earlier.  Must be called before the first
 * decode(); 0 (the default) means N2AP_DEC_MAX_FB_NUM.
 */
int n2ap_vdec_set_ring_entries(struct n2ap_vdec *d, unsigned entries);

/* One decode job.  `bs_handle == 0` means flush: AP_IPIMSG_DEC_RESET, exactly vdec_if_decode(NULL).
 *
 * Bitstream cookie: vsi.dec.vdec_bs_va gets `bs_handle` (non-zero; the vendor's get_ringbuf @0x8c70
 * rejects a zero first word with "input handle NULL", DEC02's trace) and it comes back in
 * vsi.list_free_bs.  The frame-buffer cookie is `fb_handle` for the same reason (vdec_fb_va must be
 * non-zero; the vendor echoes it in list_disp/list_free and leaves y/c_dma and poc zero there, so
 * identify buffers by cookie only).
 *
 * Planes: the vendor reports a planar output (MM21) with one size per plane in vsi.pic.fb_sz[] and
 * programs vsi.dec.fb_dma[1]/fb_fd[1] as the chroma target.  A caller with two separate allocations
 * sets fb_c_handle/fb_c_fd; a caller that reserves both planes inside one allocation sets
 * fb_c_handle = fb_handle and the chroma address becomes fb_pa + vsi.pic.fb_sz[0] (same share id).
 * With fb_planes == 2 and neither form available (and vsi.pic.fb_sz[1] != 0) the call is refused with
 * -EINVAL: writing a zero chroma target is a DMA write to physical address 0. */
struct n2ap_vdec_job {
	uint32_t bs_handle;     /* n2vc_hw DMA handle of the bitstream */
	int32_t bs_fd;          /* V34 ion share id of the same buffer (vsi.dec.bs_fd) */
	uint64_t bs_cookie;     /* vsi.dec.vdec_bs_va; 0 = bs_handle.  The kernel's value is the
	                         * V4L2 buffer pointer, distinct per queued src buffer; a client that
	                         * cycles several bitstream buffers should pass one here, otherwise
	                         * the daemon sees the same input handle for every access unit. */
	uint32_t bs_bytes;      /* allocation size -> message data[1] (kernels's bs->length) */
	uint32_t bs_length;     /* valid AU bytes -> message data[0] (kernel's bs->size = bytesused);
	                         * 0 = bs_bytes */
	uint32_t bs_flags;      /*                 message data[2] */
	uint32_t fb_handle;     /* 0 = header / no output buffer (vsi.dec.index = 0xFF) */
	int32_t fb_fd;          /* share id of plane 0 */
	uint32_t fb_bytes;      /* size of plane 0 (Y), for the from_device sync */
	uint32_t fb_planes;     /* 1 for NV12, 2 for a planar format; 0 = the session default
	                         * (cfg.fb_num_planes: 2 for H.264/H.265, 1 for HEIF).  A 2-plane
	                         * job must name the chroma plane (see the two-plane rule). */
	uint32_t fb_c_handle;   /* plane 1 DMA handle; 0 = none, == fb_handle = inside the same buffer */
	int32_t fb_c_fd;        /* share id of plane 1 (ignored when fb_c_handle == fb_handle) */
	uint32_t fb_c_bytes;    /* size of plane 1 (C), for the from_device sync */
	uint32_t fb_index;      /* vsi.dec.index the daemon sees */
	uint64_t fb_cookie;     /* vsi.dec.vdec_fb_va; 0 = use fb_handle.  Non-zero and distinct per
	                         * buffer is mandatory; a large value such as the address the buffer
	                         * would have in the client is what the vendor expects (dec1/vdec_ref.c
	                         * uses 0xF0000000+index; small DMA handles made 3 of 12 frames
	                         * disappear in the vendor probe's first multi-AU run). */
	uint64_t timestamp;     /* vsi.dec.timestamp */
};

/*
 * Send one access unit.  Syncs the bitstream to_device before the trigger and (when the buffer has a
 * guest mirror) from_device after the START is acknowledged, as the frozen n2vc_hw.h asks.  Output
 * planes are synced from_device when they appear in vsi.list_disp (n2ap_vdec_pop_displayed()).
 * While the daemon works, its LOCK/UNLOCK/WAITISR/GET/PUT_FRAME_BUFFER requests are answered by the
 * registered handler on the daemon's thread.
 *
 * The three vsi rings (list_disp, list_free, list_free_bs) are harvested after every acknowledged
 * START, whatever the caller then reads: in synchronous mode (one output buffer in vsi.dec per
 * START) the vendor never sends PUT_FRAME_BUFFER, and the kernel harvests them in its own worker
 * (vdec_common_if.c GET_PARAM_DISP/FREE_FRAME_BUFFER, vdec_vcu_if.c:287-384).  A frame buffer passed
 * in the job is registered as in-use until list_free returns it, so its planes are synced from the
 * device when the matching list_disp entry appears.  With fb_planes == 2 the chroma plane is
 * required (see struct n2ap_vdec_job).
 */
int n2ap_vdec_decode(struct n2ap_vdec *d, const struct n2ap_vdec_job *job);

/* Flush: AP_IPIMSG_DEC_RESET (vdec_decode(bs == NULL), vdec_common_if.c:243), then the same ring
 * harvest, so frames the daemon releases during the reset are not lost. */
int n2ap_vdec_flush(struct n2ap_vdec *d);

/* End of stream: AP_IPIMSG_DEC_END (vcu_dec_end(); the V4L2 layer sends it from vdec_if_decode's EOS
 * path).  Optional for a playback client. */
int n2ap_vdec_end(struct n2ap_vdec *d);

/*
 * Output buffers.  n2ap_vdec_push_fb() queues one for the daemon's GET_FRAME_BUFFER request (the V4L2
 * driver's ready queue, mtk_vcodec_get_fb); the handler pops it, fills vsi.dec.fb_dma/fb_fd/index/
 * vdec_fb_va and the client remembers it in the "in use" set.  n2ap_vdec_push_fb returns -ENOSPC when
 * the queue is full.
 */
int n2ap_vdec_push_fb(struct n2ap_vdec *d, const struct n2ap_vdec_fb *fb);
/* Number of free buffers queued for the daemon. */
unsigned n2ap_vdec_free_fb_count(const struct n2ap_vdec *d);

/* Harvest the display list.  Returns 1 and fills *o, 0 when the queue is empty, -errno on a defect. */
int n2ap_vdec_pop_displayed(struct n2ap_vdec *d, struct n2ap_vdec_out *o);
/* Harvest a buffer the daemon released (vsi.list_free).  1/0/-errno. */
int n2ap_vdec_pop_freed(struct n2ap_vdec *d, struct n2ap_vdec_freed *f);
/* Harvest a bitstream buffer the daemon consumed (vsi.list_free_bs): the vendor pushes one entry per
 * START into a 64-entry ring, so a client that never drains it wedges after 64 access units.  1/0/-errno. */
int n2ap_vdec_pop_free_bs(struct n2ap_vdec *d, uint64_t *bs_cookie);
/* Find the queued buffer a displayed frame came from (same cookie), 1 = found.  The caller then owns it
 * again (its output planes have been synced from the device). */
int n2ap_vdec_fb_of(const struct n2ap_vdec *d, uint64_t cookie, struct n2ap_vdec_fb *fb);

/* Copy the picture description the last header START produced.  Safe after n2ap_vdec_decode() with a
 * header job (no frame buffer); before it, all zeros and -EAGAIN.  DEC05 uses disp_w/disp_h for the
 * CVPixelBuffer and fb_sz[] for the two-plane output job. */
int n2ap_vdec_get_pic_info(const struct n2ap_vdec *d, struct n2ap_vdec_pic *o);

/* Session state. */
uint32_t n2ap_vdec_vsi_addr(const struct n2ap_vdec *d);       /* VCU address of the vsi, 0 before open */
struct vdec_vsi *n2ap_vdec_vsi(const struct n2ap_vdec *d);    /* the mapped vsi (64-entry layout) */
unsigned n2ap_vdec_ipi_id(const struct n2ap_vdec *d);
int32_t n2ap_vdec_last_status(const struct n2ap_vdec *d);
uint32_t n2ap_vdec_changed(const struct n2ap_vdec *d);        /* vsi.dec.vdec_changed_info */
int n2ap_vdec_aborted(const struct n2ap_vdec *d);
unsigned n2ap_vdec_proto_errors(const struct n2ap_vdec *d);

struct n2ap_vdec_stats {
	uint32_t locks_core, unlocks_core, locks_lat, unlocks_lat;
	uint32_t power_on, power_off;
	uint32_t racing_restores, racing_saves, hw_breaks, hw_break_timeouts;
	uint32_t wait_isr, wait_isr_timeouts;
	uint32_t get_fb, get_fb_empty, put_fb;   /* put_fb counts VCU -> AP PUT_FRAME_BUFFER requests */
	uint32_t harvests;                        /* AP-side ring harvests after START/RESET as well */
	uint32_t frames_displayed, fbs_freed, bs_freed;
	uint32_t syncs;
};
void n2ap_vdec_stats(const struct n2ap_vdec *d, struct n2ap_vdec_stats *out);

/* Text for an error code this library returns ("-ETIMEDOUT" style); never NULL. */
const char *n2ap_vdec_strerror(int rc);

#endif /* N2AP_VDEC_H */
