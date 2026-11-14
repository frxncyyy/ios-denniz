#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
apply_patches.py -- the changes outside fakevenc/ that a full host encode session (SEQ_HDR + one IDR frame)
needs, as anchored, idempotent edits.  Owner: opus-venc.  Each block names the owning package; the owners
get the same text on the board.  Usage:

    python3 apply_patches.py <root of a COPY of gpu/hwcodec>      # never run it on the work tree itself

Prints one line per block: APPLIED / ALREADY / SKIP (anchor not found: the owner changed the file; the
board post has the intent).  Exit 1 if a block that is required is neither applied nor already present.
"""
import os
import sys

root = sys.argv[1] if len(sys.argv) > 1 else None
if not root or not os.path.isdir(os.path.join(root, 'host')):
    sys.exit('usage: apply_patches.py <copy of gpu/hwcodec>')
if '/tmp/' not in os.path.realpath(root) + '/':
    sys.exit('apply_patches.py: refusing to edit outside /tmp (work on a copy)')

failed = []


def edit(owner, rel, marker, pairs, required=True, unless=None, unless_file=None):
    """pairs: [(old, new)]; marker: text that is present once the block is applied.
    unless: text whose presence (in rel, or in unless_file when given) means the owner merged an equivalent."""
    if any(k and k in marker for k in os.environ.get('N2VENC_SKIP', '').split('|')):
        print(f'SKIPPED {owner:4s} {rel}: {marker[:60]!r} (N2VENC_SKIP)')
        return
    p = os.path.join(root, rel)
    s = open(p).read()
    us = open(os.path.join(root, unless_file)).read() if unless_file else s
    if marker in s or (unless and unless in us):
        print(f'ALREADY {owner:4s} {rel}: {marker[:60]!r}')
        return
    t = s
    for old, new in pairs:
        if t.count(old) != 1:
            print(f'SKIP    {owner:4s} {rel}: anchor not unique/found: {old[:70]!r}')
            if required:
                failed.append(rel)
            return
        t = t.replace(old, new, 1)
    open(p, 'w').write(t)
    print(f'APPLIED {owner:4s} {rel}: {marker[:60]!r}')


# ---------------------------------------------------------------------------------------------- IV
edit('IV', 'host/Makefile', 'fakevenc/n2venc_fake.c', [
    ('-I$(HWD)/vcu -I$(HWD)/ap\n', '-I$(HWD)/vcu -I$(HWD)/ap -I$(HWD)/fakevenc\n'),
    ('HW_SRCS     := $(sort $(wildcard $(HWD)/tests/hw/n2vc_hw_fake.c))',
     'HW_SRCS     := $(sort $(wildcard $(HWD)/tests/hw/n2vc_hw_fake.c) $(wildcard $(HWD)/fakevenc/n2venc_fake.c))'),
    ('DEFS   := $(if $(VCU_OK),-DN2VHOST_HAVE_VCU=1,) $(if $(AP_OK),-DN2VHOST_HAVE_AP=1,)',
     'FAKEVENC_OK := $(if $(and $(wildcard $(HWD)/fakevenc/n2venc_fake.h),$(wildcard $(HWD)/fakevenc/n2venc_fake.c)),1,)\n'
     'DEFS   := $(if $(VCU_OK),-DN2VHOST_HAVE_VCU=1,) $(if $(AP_OK),-DN2VHOST_HAVE_AP=1,) '
     '$(if $(FAKEVENC_OK),-DN2VHOST_HAVE_FAKEVENC=1,)'),
])

edit('IV', 'host/n2vhost.c', 'n2ap_venc_wait_output(e, bs.handle', unless='n2ap_venc_wait_done', required=False, pairs=[
    ("""		rc = n2ap_venc_encode(e, frame.handle, bs.handle, N2AP_ENC_FRAME_FINAL, &r);
		if (rc != 0) {""",
     """		/* venc_encode_frame (VENC_BS_MODE_FRAME), then the EOS flush below, as mtk_vcodec_enc.c does.
		 * libvpud_vcodec runs frames through GCE asynchronously: ENCODE is answered with ENCODE_ACK
		 * before the hardware ran and the size arrives with PUT_BUFFER / list_free (kernel
		 * ctx->async_mode), so wait for it.  (opus-venc) */
		rc = n2ap_venc_encode(e, frame.handle, bs.handle, N2AP_ENC_FRAME, &r);
		if (rc == 0 && r.bytes == 0)
			rc = n2ap_venc_wait_output(e, bs.handle, ap_timeout_ms(), &r);
		if (rc != 0) {"""),
    ("""		printf("N2VHOST encode frame bytes=%u key=%u sync=%u\\n", r.bytes, r.is_key_frm, p.sync_mode);""",
     """		printf("N2VHOST encode frame bytes=%u key=%u sync=%u\\n", r.bytes, r.is_key_frm, p.sync_mode);
		{   /* EOS: VENC_START_OPT_ENCODE_FRAME_FINAL without buffers (mtk_vcodec_enc.c:1928) */
			struct n2ap_enc_result fr;
			int frc = n2ap_venc_encode(e, 0, bs.handle, N2AP_ENC_FRAME_FINAL, &fr);

			printf("N2VHOST encode final rc=%d bytes=%u\\n", frc, frc == 0 ? fr.bytes : 0u);
		}"""),
])

edit('IV', 'host/n2vhost.c', 'n2venc_fake_wrap(&hw, &vhw', [
    ('#include "n2vc_hw_fake.h"       /* T02: script/violations/stats of the fake back end */',
     '#include "n2vc_hw_fake.h"       /* T02: script/violations/stats of the fake back end */\n'
     '#if defined(N2VHOST_HAVE_FAKEVENC)\n#include "n2venc_fake.h"        /* opus-venc: VENC behaviour on top of T02\'s fake */\n#endif'),
    ("""	if (!quiet)
		printf("N2VHOST fake hardware record=%s script=%s\\n", record, script ? script : "none");
""",
     """	if (!quiet)
		printf("N2VHOST fake hardware record=%s script=%s\\n", record, script ? script : "none");
#if defined(N2VHOST_HAVE_FAKEVENC)
	/* opus-venc: VENC behaviour (trigger -> bitstream + BYTE_CNT + IRQ status + interrupt) on top of
	 * T02's register/recorder fake.  `hw` stays T02's for its own controls; the runner uses hw_used. */
	struct n2vc_hw vhw;
	struct n2venc_fake *venc = NULL;
	char venclog[600];

	snprintf(venclog, sizeof venclog, "%s/venc-fake.txt", run_dir());
	if (n2venc_fake_wrap(&hw, &vhw, &venc, venclog) == 0) {
		hw_used = &vhw;
		if (!quiet)
			printf("N2VHOST fake VENC model on (log %s)\\n", venclog);
	}
#endif
"""),
    ('	rc = n2vhost_start_entry(&host, &hw, vendor, entry);', '	rc = n2vhost_start_entry(&host, hw_used, vendor, entry);'),
    ("""		n2vhost_stop(host);
		if (hw.close)
			hw.close(hw.ctx);
		return 1;""",
     """		n2vhost_stop(host);
		if (hw_used->close)
			hw_used->close(hw_used->ctx);
		return 1;"""),
    ("""	rc = n2vhost_report(host, stdout);
	n2vhost_stop(host);
	if (hw.close)
		hw.close(hw.ctx);""",
     """#if defined(N2VHOST_HAVE_FAKEVENC)
	if (venc && !quiet) {
		struct n2venc_fake_stats vs;

		n2venc_fake_get_stats(venc, &vs);
		printf("N2VHOST fake-venc resets=%u triggers=%u frames=%u irqs=%u/%u acks=%u polls=%u/%u "
		       "overflow=%u dma_errors=%u unsupported=%u headers=%u last_bytes=%u mb=%ux%u\\n",
		       vs.resets, vs.triggers, vs.frames, vs.irqs_taken, vs.irqs_raised, vs.acks, vs.polls,
		       vs.poll_fail, vs.overflow, vs.dma_errors, vs.unsupported, vs.headers_learned, vs.last_bytes,
		       vs.last_mb_w, vs.last_mb_h);
	}
#endif
	rc = n2vhost_report(host, stdout);
	n2vhost_stop(host);
	if (hw_used->close)
		hw_used->close(hw_used->ctx);"""),
    ('	struct n2vc_hw hw;\n	struct n2vhost *host = NULL;\n',
     '	struct n2vc_hw hw;\n	const struct n2vc_hw *hw_used = &hw;\n	struct n2vhost *host = NULL;\n'),
])

# ---------------------------------------------------------------------------------------------- V33 + V40
# DMEM commit: vpud remap_shmem (vpud+0x3312..0x334c) mmaps the COMMIT range MAP_FIXED over libvpud_vcodec's
# .data; the kernel sets VCU_DMEM0_VMA = vm_start (mtk_vcu.c:1707-1709).  IV posted an equivalent variant
# (commit_vma translation); this block is skipped when that one is in the file.
edit('V33', 'shim/n2vfs.c', 'n2vfs_dev_mmap_fixed', [
    ("""uint32_t n2vfs_mmap(struct n2a32_cpu *c, uint32_t addr, uint32_t len, int prot, int flags, int fd, uint64_t off)
{
	struct vf_fd e;
	int err = VF_ENODEV;

	(void)addr;
	(void)flags;
""",
     """/* MAP_FIXED (0x10) on a device mapping: the device decides whether it can place its pages at the caller's
 * address (/dev/vcu's DMEM commit, vpud remap_shmem at vpud+0x334c).  Weak: without it the device's own
 * address is returned as before.  (opus-venc) */
extern uint32_t n2vfs_dev_mmap_fixed(const struct n2vfs_dev *dev, struct n2a32_cpu *c, uint32_t p, uint32_t addr,
				     uint32_t len, uint64_t off) __attribute__((weak));

uint32_t n2vfs_mmap(struct n2a32_cpu *c, uint32_t addr, uint32_t len, int prot, int flags, int fd, uint64_t off)
{
	struct vf_fd e;
	int err = VF_ENODEV;

"""),
    ("""			return 0;
		}
		return p;
	}
}""",
     """			return 0;
		}
		if ((flags & 0x10) && addr != 0u && p != addr && n2vfs_dev_mmap_fixed)
			p = n2vfs_dev_mmap_fixed(e.dev, c, p, addr, len, off);
		return p;
	}
}"""),
], unless='commit_vma', unless_file='vcu/n2vcu.c')

V40_FIXED = '''/* MAP_FIXED mmap of the DMEM commit range (called by n2vfs_mmap; opus-venc).  vpud's remap_shmem
 * (vpud+0x3312..0x334c) does mmap(0, n, RW, SHARED, fd, 0x80000000), memcpy(dmem, .data, n), munmap, then
 * mmap(.data, n, RW, SHARED|FIXED, fd, 0x88000000); the kernel records VCU_DMEM0_VMA = vm_start
 * (mtk_vcu.c:1707-1709), so every vcu_inst_addr the daemon reports lies in libvpud_vcodec's .data.  The
 * interpreter cannot alias pages, so the DMEM moves: the device copy is written over the caller's range and
 * the DMEM is that range from now on (same bytes, one copy). */
uint32_t n2vfs_dev_mmap_fixed(const struct n2vfs_dev *dev, struct n2a32_cpu *c, uint32_t p, uint32_t addr,
			      uint32_t len, uint64_t off);
uint32_t n2vfs_dev_mmap_fixed(const struct n2vfs_dev *dev, struct n2a32_cpu *c, uint32_t p, uint32_t addr,
			      uint32_t len, uint64_t off)
{
	struct n2vcu *v;
	void *src, *dst;

	(void)c;
	if (!dev || dev->mmap != vcu_dev_mmap || (addr & (VCU_PAGE - 1u)) != 0u || len == 0u)
		return p;
	v = (struct n2vcu *)dev->ctx;
	if (off < VCU_SHMEM_COMMIT_BASE || off + len > VCU_SHMEM_COMMIT_BASE + VCU_SHMEM_RANGE)
		return p;
	pthread_mutex_lock(&v->lock);
	src = n2a32_ptr(v->mem, p);
	dst = n2a32_ptr(v->mem, addr);
	if (src && dst) {
		memmove(dst, src, len);
		v->dmem_guest = addr - (uint32_t)(off - VCU_SHMEM_COMMIT_BASE);
		v->dmem_bytes = (uint32_t)(off - VCU_SHMEM_COMMIT_BASE) + len;
		p = addr;
	}
	pthread_mutex_unlock(&v->lock);
	return p;
}

'''
edit('V40', 'vcu/n2vcu.c', 'n2vfs_dev_mmap_fixed', [
    ('void *n2vcu_map_dm_addr(struct n2vcu *v, uint32_t guest_addr)\n',
     V40_FIXED + 'void *n2vcu_map_dm_addr(struct n2vcu *v, uint32_t guest_addr)\n'),
], unless='commit_vma')

edit('V40', 'vcu/n2vcu.c', 'venc_encode_prepare: clocks', [
    ("""	v->stats.gce_flushes++;
	h->pending++;
	rc = gce_run_list(v, c, cmds_addr, cmd_cnt, q.codec_type);
	h->pending--;
	if (rc == 0)
		h->flush_done++;   /* the kernel's vcu_gce_flush_callback */
	pthread_mutex_unlock(&v->lock);
	return rc ? rc : ret;""",
     """	v->stats.gce_flushes++;
	h->pending++;
	/* mtk_vcu.c:994-997 / :822: the kernel powers the encoder core (venc_encode_prepare: clocks,
	 * larb, VENC domain) when the first GCE job of that core is flushed and unprepares it in the
	 * flush callback when the job count drops to 0.  The model runs the list synchronously, so it is
	 * power on -> run -> power off.  The daemon never sends VCU_IPIMSG_ENC_POWER_ON in GCE mode.
	 * (opus-venc) */
	if (q.codec_type == VCU_VENC && v->hw.power)
		(void)v->hw.power(v->hw.ctx, q.core_id ? RWVC_BLK_VENC_C1 : RWVC_BLK_VENC, 1);
	rc = gce_run_list(v, c, cmds_addr, cmd_cnt, q.codec_type);
	if (q.codec_type == VCU_VENC && v->hw.power)
		(void)v->hw.power(v->hw.ctx, q.core_id ? RWVC_BLK_VENC_C1 : RWVC_BLK_VENC, 0);
	h->pending--;
	if (rc != 0)
		v->stats.gce_errors++;
	/* The kernel's cmdq callback runs for a failed/timed-out packet too (vcu_gce_timeout_callback,
	 * then vcu_gce_flush_callback), so the daemon's VCU_GCE_WAIT_CALLBACK always returns.  The
	 * model keeps reporting the run's error on the flush ioctl as before. */
	h->flush_done++;
	pthread_mutex_unlock(&v->lock);
	return rc ? rc : ret;"""),
])

# ---------------------------------------------------------------------------------------------- V34
edit('V34', 'shim/n2shim_android.c', 'extern int n2vc_dma_lookup_handle(', [
    ('#include "n2fmt.h"\n',
     '#include "n2fmt.h"\n\n/* V41 (ap/n2vc_dma.c); weak so the shim links without the AP side.  (opus-venc) */\n'
     'extern int n2vc_dma_lookup_handle(uint32_t handle, uint32_t *pa, uint32_t *guest_addr, uint32_t *bytes)\n'
     '\t__attribute__((weak));\n'),
])
edit('V34', 'shim/n2shim_android.c', 'uint8_t foreign;', [
    ("""	uint8_t shared;
	uint32_t handle;""",
     """	uint8_t shared;
	uint8_t foreign;          /* registered by the AP side (n2shim_ion_register_share): the daemon only imported it */
	uint32_t handle;"""),
    ("""		uint32_t h = b->handle;

		memset(b, 0, sizeof *b);
		pthread_mutex_unlock(&g_lock);""",
     """		uint32_t h = b->handle;

		if (b->foreign) {
			/* ion_free of an imported handle drops the importer's reference only (Linux ion/dma-buf); the
			 * buffer belongs to the AP side.  The daemon frees every imported bitstream buffer after each
			 * job (libvpud_vcodec 0x9a981); freeing it here broke the AP's next job.  (opus-venc) */
			pthread_mutex_unlock(&g_lock);
			n2shim_ret(c, 0);
			return;
		}
		memset(b, 0, sizeof *b);
		pthread_mutex_unlock(&g_lock);"""),
    ("""		b->used = 1;
		b->handle = dma_handle;""",
     """		b->used = 1;
		b->foreign = 1;
		b->handle = dma_handle;"""),
    ("""	uint32_t addr = n2shim_arg(c, 1);
	int rc = 0;

	if (n2shim_hw != NULL)
		rc = n2vc_dma_guest_unmap(n2shim_hw, c->mem, addr);""",
     """	uint32_t addr = n2shim_arg(c, 1);
	int rc = 0;
	unsigned i;

	/* An AP-registered (imported) buffer keeps its one guest mapping: the importer's munmap must not drop
	 * the owner's mapping and registry entry (libvpud_vcodec 0x9a953 unmaps every imported bitstream
	 * buffer after each job).  (opus-venc) */
	pthread_mutex_lock(&g_lock);
	for (i = 0; i < N2AND_ION_BUFS; i++) {
		uint32_t ga = 0;

		if (g_ion_buf[i].used && g_ion_buf[i].foreign && n2vc_dma_lookup_handle &&
		    n2vc_dma_lookup_handle(g_ion_buf[i].handle, NULL, &ga, NULL) == 0 && ga == addr) {
			pthread_mutex_unlock(&g_lock);
			n2shim_ret(c, 0);
			return;
		}
	}
	pthread_mutex_unlock(&g_lock);
	if (n2shim_hw != NULL)
		rc = n2vc_dma_guest_unmap(n2shim_hw, c->mem, addr);"""),
])

edit('V34', 'shim/n2shim_android.c', 'extern int n2vc_dma_sync(', [
    ('#include "n2fmt.h"\n',
     '#include "n2fmt.h"\n\n/* V41 (ap/n2vc_dma.c); weak so the shim links without the AP side.  (opus-venc) */\n'
     'extern int n2vc_dma_sync(const struct n2vc_hw *h, struct n2a32_mem *m, uint32_t guest_addr, uint32_t offset,\n'
     '\tuint32_t bytes, int to_device) __attribute__((weak));\n'),
])
edit('V34', 'shim/n2shim_android.c', 'ION_SYS_CACHE_SYNC: {handle @8', [
    ("""		/* ION_SYS_CACHE_SYNC and everything else: the DMA mirror is synced by the AP side around each job,
		 * so the cache request needs no work here */
		n2shim_ret(c, 0);
		return;""",
     """		if (sub == 0) {          /* ION_SYS_CACHE_SYNC: {handle @8, va @12, size @16, sync_type @20} */
			/* The guest mirror and the device copy are two memories here, so a cache operation is a copy:
			 * clean/flush pushes the guest bytes to the device, invalidate pulls them back.  The daemon
			 * writes SPS/PPS with the CPU and flushes (h264_enc_encode_Header -> VENC_FlushRangeForIon)
			 * and invalidates the bitstream after the hardware (VENC_gceEncodeEnd); without this the AP's
			 * from_device sync wiped the header.  Types: mtk ion_drv.h ION_CACHE_SYNC_TYPE.  (opus-venc) */
			uint32_t va = n2a32_ld32(c, ga + 12), size = n2a32_ld32(c, ga + 16), type = n2a32_ld32(c, ga + 20);
			uint32_t gbase = 0, gbytes = 0;

			handle = n2a32_ld32(c, ga + 8);
			pthread_mutex_lock(&g_lock);
			b = ion_find_handle(handle);
			if (b != NULL)
				bytes = b->bytes;
			pthread_mutex_unlock(&g_lock);
			if (b != NULL && n2shim_hw != NULL && n2vc_dma_lookup_handle && n2vc_dma_sync &&
			    n2vc_dma_lookup_handle(handle, NULL, &gbase, &gbytes) == 0 && gbase != 0) {
				uint32_t off = 0, len = bytes;

				if (type < 6 && va >= gbase && va - gbase < bytes) {   /* by range */
					off = va - gbase;
					len = size < bytes - off ? size : bytes - off;
				}
				if (len)
					(void)n2vc_dma_sync(n2shim_hw, c->mem, gbase, off, len,
							    (type == 1 || type == 4 || type == 7) ? 0 : 1);
			}
			n2shim_ret(c, 0);
			return;
		}
		/* everything else needs no work here */
		n2shim_ret(c, 0);
		return;"""),
])

# ---------------------------------------------------------------------------------------------- V41
edit('V41', 'ap/n2ap_venc.c', 'ven_share_fd(', unless='msg.bs_fd = (int16_t)bs_fd;', required=False, pairs=[
    ('#include "vcu/n2vcu.h"\n', '''#include "vcu/n2vcu.h"

/* V34's integrator hook (shim/n2shim_android.c): a share fd the daemon can ion_import().  The daemon's
 * venc_vcodec_msg_handler resolves every buffer of AP_IPIMSG_ENC_ENCODE through ion_import(input_fd/bs_fd)
 * first (libvpud_vcodec.so 0x13e00, "ion_import failed, ion_buf_fd = %d") and fails the job for fd 0, so
 * each buffer gets a token share id (re-registered per job: the daemon's own ion_share replaces the
 * buffer's share id).  Weak: without V34 the fd stays 0.  (opus-venc) */
extern int n2shim_ion_register_share(int share_id, uint32_t dma_handle, uint32_t pa, uint32_t bytes)
	__attribute__((weak));
static int16_t ven_share_fd(uint32_t handle, uint32_t pa, uint32_t bytes)
{
	int id = 0x500 + (int)(handle & 0xffu);

	if (!n2shim_ion_register_share || handle == 0u)
		return 0;
	(void)n2shim_ion_register_share(id, handle, pa, bytes);   /* -EINVAL: already registered */
	return (int16_t)id;
}
'''),
    ('		msg.input_fd[0] = 0;     /* no dma-buf fd in our model */',
     '		msg.input_fd[0] = ven_share_fd(frame_handle, frame_pa, frame_bytes);'),
    ('	msg.bs_fd = 0;\n', '	msg.bs_fd = ven_share_fd(bs_handle, bs_pa, bs_bytes);\n'),
])

edit('V41', 'ap/n2ap_venc.c', 'FRAME_FINAL without a frame = the EOS flush', [
    ('	if (bs_mode != VENC_BS_MODE_SEQ_HDR && frame_handle == 0u) {',
     '	if (bs_mode == VENC_BS_MODE_FRAME && frame_handle == 0u) {   /* FRAME_FINAL without a frame = the EOS flush (mtk_vcodec_enc.c:1928) */'),
])

edit('V41', 'ap/n2ap_venc.c', 'the AP drains list_free itself',
     unless='The kernel drains the free-buffer ring itself after every encode', pairs=[
    ("""	memset(&result, 0, sizeof(result));
	bs_cookie = e->vsi->venc.venc_bs_va;""",
     """	/* mtk_vcodec_enc.c:2590 / :1931: in the kernel's synchronous mode every venc_if_encode() is
	 * followed by mtk_enc_put_buf(), i.e. the AP drains list_free itself; libvpud_vcodec answers a
	 * SEQ_HDR job with ENCODE_ACK and leaves the header size in list_free.bs_size, no PUT_BUFFER.
	 * (opus-venc) */
	ven_put_buffer(e);
	memset(&result, 0, sizeof(result));
	bs_cookie = e->vsi->venc.venc_bs_va;"""),
])

WAIT_OUTPUT = r'''
/* Asynchronous (GCE) frame jobs: libvpud_vcodec answers AP_IPIMSG_ENC_ENCODE with VCU_IPIMSG_ENC_ENCODE_ACK
 * before the hardware ran; the result arrives later through VCU_IPIMSG_ENC_PUT_BUFFER + venc_vsi.list_free
 * (the kernel's ctx->async_mode path, venc_vcu_if.c:170-172 -> mtk_enc_put_buf).  Wait for the entry of
 * this bitstream buffer, then bring the buffer's device bytes into the guest mirror.  (opus-venc) */
int n2ap_venc_wait_output(struct n2ap_venc *e, uint32_t bs_handle, unsigned timeout_ms,
                          struct n2ap_enc_result *res)
{
	uint32_t bs_pa = 0, bs_guest = 0, bs_bytes = 0;
	uint64_t cookie;
	struct timespec t0, t;

	if (e == NULL || !e->opened || e->vsi == NULL || res == NULL || bs_handle == 0u)
		return -EINVAL;
	if (n2vc_dma_lookup_handle(bs_handle, &bs_pa, &bs_guest, &bs_bytes) != 0)
		return -EINVAL;
	cookie = ven_cookie(bs_handle, bs_guest);
	clock_gettime(CLOCK_MONOTONIC, &t0);
	for (;;) {
		int found = 0;

		ven_put_buffer(e);
		pthread_mutex_lock(&e->reply_lock);
		for (unsigned i = 0; i < e->nfreed; i++) {
			if (e->freed[i].bs_va == cookie) {
				memset(res, 0, sizeof(*res));
				res->bytes = e->freed[i].bs_size;
				res->is_key_frm = e->freed[i].is_key;
				res->state = VEN_IPI_MSG_ENC_STATE_FRAME;
				res->from_free_list = 1;
				found = 1;
				break;
			}
		}
		pthread_mutex_unlock(&e->reply_lock);
		if (found) {
			if (bs_guest != 0u)
				(void)n2vc_dma_sync(e->hw, NULL, bs_guest, 0, bs_bytes, 0);
			return 0;
		}
		if (e->aborted)
			return -EIO;
		clock_gettime(CLOCK_MONOTONIC, &t);
		if ((t.tv_sec - t0.tv_sec) * 1000 + (t.tv_nsec - t0.tv_nsec) / 1000000 >= (long)timeout_ms)
			return -ETIMEDOUT;
		{
			struct timespec d = {0, 1000000};

			nanosleep(&d, NULL);
		}
	}
}
'''
p = os.path.join(root, 'ap/n2ap_venc.c')
s = open(p).read()
if 'int n2ap_venc_wait_output(' in s or 'int n2ap_venc_wait_done(' in s:   # IV/V41 wait_done supersedes it
    print('ALREADY V41  ap/n2ap_venc.c: n2ap_venc_wait_output')
else:
    open(p, 'a').write(WAIT_OUTPUT)
    print('APPLIED V41  ap/n2ap_venc.c: n2ap_venc_wait_output')

p = os.path.join(root, 'ap/n2ap_venc.h')
s = open(p).read()
if 'n2ap_venc_wait_output' in s or 'n2ap_venc_wait_done' in s:
    print('ALREADY V41  ap/n2ap_venc.h: n2ap_venc_wait_output')
else:
    anchor = 'int n2ap_venc_encode(struct n2ap_venc *e, uint32_t frame_handle, uint32_t bs_handle,'
    i = s.find(anchor)
    if i < 0:
        print('SKIP    V41  ap/n2ap_venc.h: anchor')
        failed.append('ap/n2ap_venc.h')
    else:
        j = s.index(';', i) + 1
        s = s[:j] + '''
/* Wait (bounded) for the asynchronous result of a frame job whose ENCODE was only acknowledged:
 * the PUT_BUFFER / list_free entry of bs_handle.  0, -ETIMEDOUT, -EIO, -EINVAL.  (opus-venc) */
int n2ap_venc_wait_output(struct n2ap_venc *e, uint32_t bs_handle, unsigned timeout_ms,
                          struct n2ap_enc_result *res);''' + s[j:]
        open(p, 'w').write(s)
        print('APPLIED V41  ap/n2ap_venc.h: n2ap_venc_wait_output')

if failed:
    print('apply_patches: FAILED blocks in', sorted(set(failed)))
    sys.exit(1)
print('apply_patches: OK')
