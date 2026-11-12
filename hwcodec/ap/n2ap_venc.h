/* SPDX-License-Identifier: MIT */
/*
 * n2ap_venc.h -- the AP side of a MediaTek MT6893 VENC session, the part the Linux V4L2 driver
 * (mtk-vcodec venc_vcu_if.c, venc/venc_common_if.c, mt6885/mtk_vcodec_enc_pm.c, mt6885/
 * mtk_vcodec_intr.c) plays on Android, re-done in user space over the /dev/vcu model.
 *
 * Owner: V41 (gpu/hwcodec/ap).  Consumers: the host runner (IV), the daemon (IP), test_v41.
 *
 * What a session is, in kernel terms
 * ----------------------------------
 *   venc_if_init()                 -> n2ap_venc_open(): pick the IPI id from the codec fourcc
 *                                    (venc_common_if.c:232-271), send AP_IPIMSG_ENC_INIT
 *                                    (venc_vcu_if.c:288-327), wait for VCU_IPIMSG_ENC_INIT_DONE,
 *                                    then vsi = vcu_mapping_dm_addr(vcu_inst_addr)
 *                                    (venc_vcu_if.c:17-25).
 *   venc_if_get_param(capability)  -> n2ap_venc_query_cap(): AP_IPIMSG_ENC_QUERY_CAP on
 *                                    IPI_VENC_COMMON, answered by VCU_IPIMSG_ENC_QUERY_CAP_ACK and
 *                                    a memcpy out of the VCU DMEM (venc_vcu_if.c:27-56, :329-368).
 *   venc_if_set_param()            -> n2ap_venc_set_param(): AP_IPIMSG_ENC_SET_PARAM with the
 *                                    data_item/data[] mapping of venc_vcu_if.c:370-513 and the
 *                                    vsi->config writes of venc_common_if.c:330-437.
 *   vb2ops_venc_start_streaming()  -> n2ap_venc_start(): VENC_SET_PARAM_ENC then
 *                                    VENC_SET_PARAM_PREPEND_HEADER unless the header mode is
 *                                    separate (mtk_vcodec_enc.c:1857-1892).
 *   venc_if_encode()               -> n2ap_venc_encode(): AP_IPIMSG_ENC_ENCODE
 *                                    (venc_vcu_if.c:515-609); while the AP waits, the daemon's
 *                                    VCU_SET_OBJECT requests VCU_IPIMSG_ENC_POWER_ON/POWER_OFF,
 *                                    WAIT_ISR and PUT_BUFFER are served by the handler below,
 *                                    exactly as vcu_enc_ipi_handler() does
 *                                    (venc_vcu_if.c:68-191).
 *   venc_if_deinit()               -> n2ap_venc_close(): AP_IPIMSG_ENC_DEINIT.
 *
 * The transport is V40's vcu/n2vcu.h: n2vcu_ap_register() is vcu_ipi_register(),
 * n2vcu_ap_send() + n2vcu_ap_wait_dispatched() are vcu_ipi_send() (queue half and ipi_done half),
 * n2vcu_map_dm_addr() is vcu_mapping_dm_addr().  Nothing here touches a register directly: the
 * codec core belongs to the daemon; the AP powers the block when the daemon asks (POWER_ON) or
 * when the integrator calls n2ap_venc_power() for a daemon that programs the core directly.
 *
 * Errors: every function returns 0 or a negative errno; every wait is bounded by the session
 * timeout (n2ap_venc_open's timeout_ms, default 6000 ms = IPI_TIMEOUT_MS of mtk_vcu.c:72).  A
 * timed-out send marks the session aborted; later calls return -EIO until close, mirroring
 * venc_vcu_if.c:207-208.  No assertion, no printing: use n2ap_venc_set_trace().
 *
 * Wait order (rev 3): like vcu_ipi_send() (mtk_vcu.c:418-446) a send first waits for the ACK --
 * the daemon's reply dispatched to the handler -- and only then for the "dispatched" ipi_done
 * half.  A daemon with a separate receiver thread (vpud) re-enters VCU_GET_OBJECT before its
 * service thread answers; waiting the other way round returned a spurious -EPROTO.  Each half is
 * bounded by timeout_ms, so one send waits at most 2 * timeout_ms.
 */
#ifndef N2AP_VENC_H
#define N2AP_VENC_H
#include <stdint.h>
#include "mtk_venc_ipi.h"
#include "n2a32.h"
#include "n2vc_hw.h"

struct n2vcu;
struct n2ap_venc;

/* Codec fourccs from uapi/linux/videodev2.h:657-667 (little-endian v4l2_fourcc). */
#define N2AP_FOURCC_H264  0x34363248u   /* 'H264' */
#define N2AP_FOURCC_H265  0x35363248u   /* 'H265' */
#define N2AP_FOURCC_HEIF  0x46494548u   /* 'HEIF' */
#define N2AP_FOURCC_MPEG4 0x3447504dU   /* 'MPG4' */
#define N2AP_FOURCC_H263  0x33363248u   /* 'H263' */
#define N2AP_FOURCC_NV12  0x3231564eu   /* 'NV12' */

/* IPI ids (mtk_vcu.h:113-141): the encoder ids a session can use. */
#define N2AP_IPI_VENC_COMMON      14
#define N2AP_IPI_VENC_H264        15
#define N2AP_IPI_VENC_H265        16
#define N2AP_IPI_VENC_HEIF        17
#define N2AP_IPI_VENC_VP8         18
#define N2AP_IPI_VENC_MPEG4       19
#define N2AP_IPI_VENC_HYBRID_H264 20
#define N2AP_IPI_VENC_H263        21

/* VENC_YUV_FORMAT_* (mtk_vcodec_drv.h:108-129). */
#define N2AP_YUV_I420 3
#define N2AP_YUV_NV12 6
#define N2AP_YUV_NV21 7

/* Flags for n2ap_venc_encode(); bits 0-1 select enum venc_bs_mode's frame family, bit 2 says the
 * frame buffer is absent (sequence header only). */
#define N2AP_ENC_SEQ_HDR      0x1u   /* VENC_BS_MODE_SEQ_HDR (venc_common_if.c:174) */
#define N2AP_ENC_FRAME        0x0u   /* VENC_BS_MODE_FRAME (venc_common_if.c:210) */
#define N2AP_ENC_FRAME_FINAL  0x2u   /* VENC_BS_MODE_FRAME_FINAL (venc_common_if.c:252) */

/* One encoder configuration, the union of struct venc_enc_param (mtk_vcodec_drv.h:273-318) fields
 * this AP path uses.  Fill, call n2ap_venc_start() once, then use set_param for changes. */
struct n2ap_enc_param {
	/* VENC_SET_PARAM_ENC (venc_common_if.c:331-406) */
	uint32_t input_yuv_fmt;      /* N2AP_YUV_* */
	uint32_t profile;            /* V4L2_MPEG_VIDEO_*_PROFILE, converted for H265/MPEG4 */
	uint32_t level;              /* V4L2_MPEG_VIDEO_*_LEVEL, converted for H265/MPEG4 */
	uint32_t tier;               /* HEVC tier: 0 main, 1 high */
	uint32_t width, height;      /* visible */
	uint32_t buf_width, buf_height;   /* coded, aligned */
	uint32_t frm_rate;           /* framerate_num / framerate_denom */
	uint32_t intra_period;
	uint32_t gop_size;
	uint32_t bitrate;            /* bps */
	uint32_t operationrate;
	uint32_t scenario;
	uint32_t prependheader;
	uint32_t bitratemode;
	uint32_t roion;
	uint32_t heif_grid_size;
	const struct mtk_color_desc *color_desc;   /* NULL = leave the vsi's copy zeroed */
	uint32_t max_w, max_h;
	uint32_t num_b_frame;
	uint32_t slbc_ready;
	uint32_t i_qp, p_qp, b_qp;
	uint32_t svp_mode;
	uint32_t tsvc;
	uint32_t dummynal;
	uint32_t max_qp, min_qp;
	uint32_t i_p_qp_delta;
	uint32_t qp_control_mode;
	uint32_t frame_level_qp;
	uint32_t maxrefpnum;
	/* fields of the per-type messages (venc_vcu_if.c:424-498) */
	uint32_t nonrefp;
	uint32_t nonrefpfreq;
	uint32_t detectframerate;
	uint32_t rfs;
	uint32_t refpdistance;
	uint32_t refpfrmnum;
	const char *log;             /* VENC_SET_PARAM_LOG: the kernel sends it, the VCU rejects it */
	/* V4L2 side only: 0 = V4L2_MPEG_VIDEO_HEADER_MODE_SEPARATE (the control's default,
	 * mtk_vcodec_enc.c:2753-2755), 1 = JOINED_WITH_1ST_FRAME.  n2ap_venc_start() sends
	 * VENC_SET_PARAM_PREPEND_HEADER only when this is not 0 (mtk_vcodec_enc.c:1874-1892); with
	 * 0 the driver encodes the sequence header as a separate job before the first frame
	 * (m2mops_venc_device_run, :2598-2615). */
	uint32_t seq_hdr_mode;
	/* results of VENC_SET_PARAM_ENC (venc_common_if.c:528-537) */
	uint32_t sizeimage[MTK_VCODEC_MAX_PLANES];
	uint32_t sync_mode;          /* vsi->sync_mode; ctx->async_mode = !sync_mode */
};

/* Encode result.  `bytes` is what the V4L2 layer would put in planes[0].bytesused. */
struct n2ap_enc_result {
	uint32_t bytes;
	uint32_t state;              /* enum venc_ipi_msg_enc_state, if a DONE carried it */
	uint32_t is_key_frm;
	uint32_t from_free_list;     /* 1: size/key came from venc_vsi.list_free (kernel path) */
};

/* Trace hook: one call per notable event.  `event` is a static string; no printing in the library.
 * Events: "init", "init_done", "set_param", "param_done", "encode", "encode_done", "power_on",
 * "power_off", "wait_isr", "put_buffer", "deinit", "timeout", "proto", "hw". */
typedef void (*n2ap_trace_fn)(void *arg, const char *event, uint32_t a, uint32_t b, uint32_t c);
void n2ap_venc_set_trace(struct n2ap_venc *e, n2ap_trace_fn fn, void *arg);

/*
 * Open a session.  `vcu` is V40's /dev/vcu model, `hw` the hardware back end (only hw->power and
 * hw->wait_irq are used from here).  `timeout_ms` 0 means 6000 (IPI_TIMEOUT_MS).  On success
 * *out is a session with the vsi mapped; on failure nothing is leaked except the IPI handler,
 * which is replaced by a dropper.
 *
 * VP8 is rejected with -EINVAL exactly like venc_if_init() (the VCU encoder list has no VP8 entry,
 * venc_drv_if.c:34-52; the VP8 path is the VPU driver, which this kernel does not build).
 */
int n2ap_venc_open(struct n2ap_venc **out, struct n2vcu *vcu, const struct n2vc_hw *hw,
                   uint32_t codec_fourcc, unsigned timeout_ms);

/*
 * Explicit power reference, for a daemon that programs the VENC through GCE command lists and
 * never sends VCU_IPIMSG_ENC_POWER_ON (libvcodecdrv.so does; the Linux kernel powers the block in
 * vcu_gce_set_cmd_flush, mtk_vcu.c:994).  `on` is 0 or 1.  Reference counted per session and
 * shared with the daemon's POWER_ON/POWER_OFF requests; only RWVC_BLK_VENC is taken (VEN domain +
 * vencsys gates + larb7) because RWVC_BLK_VENC_C1 is the whole core-1 power domain, not a bare
 * clock gate (hw-k02).  The integrator takes one reference around a job: on before the first
 * register write, off after the done interrupt (after n2ap_venc_wait_done() for an asynchronous
 * frame).  n2ap_venc_close() drops any reference still held.
 */
int n2ap_venc_power(struct n2ap_venc *e, int on);

/* IPI id selected for the codec (one of N2AP_IPI_VENC_*), 0 on a NULL session. */
unsigned n2ap_venc_ipi_id(const struct n2ap_venc *e);
/* The mapped VCU instance (NULL before open finished / after close). */
struct venc_vsi *n2ap_venc_vsi(const struct n2ap_venc *e);
/* Last status a VCU -> AP message carried (enum venc_ipi_msg_status), 0 before any. */
int32_t n2ap_venc_last_status(const struct n2ap_venc *e);
/* 1 once a send timed out or the daemon reported an error; later sends return -EIO. */
int n2ap_venc_aborted(const struct n2ap_venc *e);

/*
 * Capability query.  id is VENC_GET_PARAM_CAPABILITY_SUPPORTED_FORMATS (0) or
 * VENC_GET_PARAM_CAPABILITY_FRAME_SIZES (1).  out must hold the full array the kernel copies:
 * MTK_MAX_ENC_CODECS_SUPPORT (64) * 12 bytes for formats, 64 * 36 for frame sizes
 * (venc_vcu_if.c:42-51).  The message goes on IPI_VENC_COMMON, as the V4L2 driver's pre-init query
 * does (venc_vcu_if.c:347): a temporary instance has id 0 and is mapped to IPI_VENC_COMMON.
 */
int n2ap_venc_query_cap(struct n2ap_venc *e, unsigned id, void *out, uint32_t out_bytes);

/*
 * One VENC_SET_PARAM_* message, with the data_item/data[] mapping of venc_vcu_if.c:387-502 and,
 * for VENC_SET_PARAM_ENC / COLOR_DESC, the vsi->config write of venc_common_if.c:331-406.
 * `p` may be NULL only for the types the kernel passes NULL for: FORCE_INTRA, SKIP_FRAME,
 * PREPEND_HEADER and PREPEND_SPSPPS_TO_IDR (venc_vcu_if.c:391-415, mtk_vcodec_enc.c:2100).
 * Types the VCU switch does not know (TS_MODE, LOG) return -EINVAL without sending, which is what
 * vcu_enc_set_param() does (:499-501).
 */
int n2ap_venc_set_param(struct n2ap_venc *e, unsigned type, struct n2ap_enc_param *p);

/* The stream-start sequence of vb2ops_venc_start_streaming(): VENC_SET_PARAM_ENC, then
 * VENC_SET_PARAM_PREPEND_HEADER when p->seq_hdr_mode != 0 and the codec is one the kernel's
 * condition covers (H.264, H.265, HEIF, MPEG4; H.263 is excluded, mtk_vcodec_enc.c:1874-1892).
 * Writes p->sizeimage[] and p->sync_mode from the vsi afterwards. */
int n2ap_venc_start(struct n2ap_venc *e, struct n2ap_enc_param *p);

/*
 * One AP_IPIMSG_ENC_ENCODE.  The buffers are n2vc_hw DMA handles (hw->dma_alloc); when they have a
 * guest mapping the AP syncs them through n2vc_dma_sync() around the job, as the frozen n2vc_hw.h
 * asks ("to_device before the trigger, from_device after the done interrupt").  flags select
 * N2AP_ENC_SEQ_HDR / N2AP_ENC_FRAME / N2AP_ENC_FRAME_FINAL.  frame_handle is ignored for a
 * sequence header (the kernel passes frm_buf = NULL, venc_common_if.c:174-186) and may be 0 for
 * N2AP_ENC_FRAME_FINAL, the EOS flush (VENC_START_OPT_ENCODE_FRAME_FINAL without buffers,
 * mtk_vcodec_enc.c:1928); only N2AP_ENC_FRAME requires a frame.
 *
 * Before the result lookup the AP drains venc_vsi.list_free itself, as the kernel's synchronous
 * mode does after every venc_if_encode() (mtk_enc_put_buf, mtk_vcodec_enc.c:2590/:1931).  This is
 * what carries the sequence-header size: libvpud_vcodec answers that job with ENCODE_ACK and
 * leaves the size only in list_free, without a PUT_BUFFER.
 */
int n2ap_venc_encode(struct n2ap_venc *e, uint32_t frame_handle, uint32_t bs_handle,
                     unsigned flags, struct n2ap_enc_result *res);

/*
 * Same, carrying the dma-buf share fds in venc_ap_ipi_msg_enc.input_fd[0]/bs_fd (the vendor ring
 * buffer calls ion_import() on them; 0 is what a daemon without dmabufs gets and only works for the
 * sequence-header job).  The integrator registers the AP's own DMA handles with V34's
 * n2shim_ion_register_share() and passes the ids it got back.  n2ap_venc_encode() does that itself
 * when the V34 shim is linked (weak symbol): it publishes each handle once with a stable id in
 * 0x520..0x55f and reuses it for later jobs, so the product daemon's plain encode call works.
 */
int n2ap_venc_encode_fd(struct n2ap_venc *e, uint32_t frame_handle, int frame_fd,
                        uint32_t bs_handle, int bs_fd, unsigned flags,
                        struct n2ap_enc_result *res);

/*
 * Wait up to timeout_ms for the daemon to report the job's size/key asynchronously (it sends
 * VCU_IPIMSG_ENC_POWER_ON / WAIT_ISR / PUT_BUFFER after the encode ACK; venc_vcu_if.c:135-173).
 * Fills *res and returns 0 when a nonzero size arrives, -ETIMEDOUT otherwise.  The kernel path
 * reads venc_vsi.list_free in venc_get_free_buffers() (venc_common_if.c:354-391); this does the
 * same through the PUT_BUFFER captures, and then syncs the bitstream of the last encode from the
 * device into its guest mirror (pa-only buffers skip the copy).
 */
int n2ap_venc_wait_done(struct n2ap_venc *e, unsigned timeout_ms,
                        struct n2ap_enc_result *res);

/* Send AP_IPIMSG_ENC_DEINIT, power the block down if the daemon left it on, replace the IPI
 * handler with a dropper and free the session.  Returns the first error of that sequence. */
int n2ap_venc_close(struct n2ap_venc *e);

/* Fill the V4L2 default values of a 1080p30 H.264 session (mtk_vcodec_enc.c:2727-2742 controls,
 * :986-1112 parameter mapping): 1920x1080 visible, 1920x1088 coded, 30 fps, 20 Mbit/s, GOP and
 * intra period 30, NV12, profile 0 (BASELINE), level 0 (1.0), header mode joined. */
void n2ap_enc_param_1080p30_h264(struct n2ap_enc_param *p);

/* The same session with the HEVC control defaults of mtk_vcodec_enc.c:2760-2776: profile 0
 * (MAIN), level 5 (V4L2_MPEG_VIDEO_HEVC_LEVEL_4) and tier 0 (MAIN).  The rest is identical to the
 * H.264 defaults; an HEVC session uses the same IPI messages and the same free-buffer ring. */
void n2ap_enc_param_1080p30_hevc(struct n2ap_enc_param *p);

/*
 * V41 extensions to the frozen include/n2vc_hw.h DMA mirror (a defect proposal is on the board:
 * the frozen n2vc_dma_guest_map() takes a handle but no physical address, so a mapping it creates
 * cannot be found by n2vc_dma_lookup_pa()).  These carry the `pa` that hw->dma_alloc returned:
 *
 *   n2vc_dma_guest_register() -- register a buffer the caller allocated itself (V34's ion shim,
 *                                V40's VCU_PA_ALLOCATION, vcodecd's client handles) and record
 *                                its pa.  `m` may be NULL: the entry is then pa-only (guest
 *                                address 0, no copy possible), which is what vcodecd needs for a
 *                                client buffer that lives outside the daemon's guest address
 *                                space; `guest_addr` may be NULL too.  A later
 *                                n2vc_dma_guest_map(m, handle, ...) attaches a guest mapping to
 *                                the same entry and keeps the pa.  On a handle guest_map()
 *                                already mapped, register() fills the pa in and keeps the guest
 *                                address.  The caller keeps ownership: unmap/forget do not free
 *                                a handle register() created.  Because the daemon's ion_munmap
 *                                arrives through the same guest_unmap(), an entry register()
 *                                created survives guest_unmap() (only the importer's view is
 *                                dropped); the AP releases it with n2vc_dma_guest_forget().
 *   n2vc_dma_guest_alloc()    -- hw->dma_alloc + register + n2a32_mem_alloc; ownership moves to
 *                                the registry, unmap/forget frees the DMA handle.
 *   n2vc_dma_guest_forget()   -- drop an entry by handle.  The only way to release a pa-only
 *                                entry (unmap needs the guest address it does not have).  Frees
 *                                the DMA handle only when guest_alloc() created the entry.
 *   n2vc_dma_lookup_handle()  -- handle -> pa/guest/bytes, for n2ap_venc_encode()'s message fields
 *                                and sync.  guest_addr is 0 for a pa-only entry; encode() then
 *                                skips the sync and uses a tagged handle as the vsi cookie.
 * The frozen n2vc_dma_guest_map() itself maps (allocating the guest pages) but leaves pa unknown,
 * which is enough for the daemon's VCU_PA_ALLOCATION path (V40 keeps its own pa table).
 */
int n2vc_dma_guest_register(const struct n2vc_hw *h, struct n2a32_mem *m, uint32_t handle,
                            uint32_t pa, uint32_t bytes, uint32_t *guest_addr);
int n2vc_dma_guest_alloc(const struct n2vc_hw *h, struct n2a32_mem *m, uint32_t bytes,
                         uint32_t flags, uint32_t *handle, uint32_t *pa, uint32_t *guest_addr);
int n2vc_dma_guest_forget(const struct n2vc_hw *h, uint32_t handle);
int n2vc_dma_lookup_handle(uint32_t handle, uint32_t *pa, uint32_t *guest_addr, uint32_t *bytes);

#endif /* N2AP_VENC_H */
