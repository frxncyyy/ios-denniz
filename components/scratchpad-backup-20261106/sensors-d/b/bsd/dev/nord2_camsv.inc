/*
 * nord2_camsv.inc - frames into memory: SENINF cam mux -> CAMSV -> IMGO DMA (MT6893).
 *
 * CAMSV is the simple capture channel HakiOS used on the MT6768 (redwood_mtkcam.c). On the MT6893 its registers and the
 * values the vendor HAL writes were read out of the Nord 2's own /vendor/lib64/libimageio_plat_drv.so (C++ symbols kept:
 * CAMSV_TOP_CTRL::_config/_enable, CAMSV_TG_CTRL::_config/_enable, DMAO_CAMSV_B::_config/_enable/setBaseAddr,
 * CAMSV_BUF_CTRL::_config) and its register-dump strings (Reg(CAMSV_IMGO_XSIZE) ...); the TG block is the CAM TG's layout
 * (mt6893/isp_ccu_reg.h in the CCU firmware's DWARF) at +0x130:
 *   0x010 MODULE_EN  bit0 TG, bit2 PAK, bit4 IMGO, bit30 (set by _config and _enable)
 *   0x014 FMT_SEL    TG format: 0 RAW8, 1 RAW10, 2 RAW12 (the HAL's switch)
 *   0x018 INT_EN     0x31871            0x01c INT_STATUS     0x020 SW_CTL (1 IMGO reset, wait bits 0-1, 4 SW reset, 0)
 *   0x030 (top)      |= 0x8005 (_enable)  0x04c PAK  0x058 &~0x80  0x05c &~1  0x2d0[20:16] = 0xe  0x418 |= 0x61000000
 *   0x7b0 &= 0x7fffffff  0x7c4 = ~0  0x800 = 0, then |= 1 (DMAO_CAMSV_B::_enable)
 *   0x130 TG_SEN_MODE (bit0 CMOS_EN, bit1/bit13 double/quad data bus, bit16 time stamp)  0x134 VF_CON (bit0 VFDATA_EN)
 *   0x138 GRAB_PXL (start | end << 16)  0x13c GRAB_LIN  0x168 FRMSIZE_ST  0x16c INTER_ST (frame count bits 16-23)  0x1a4 SUB_PERIOD
 *   0x420 IMGO_BASE_ADDR  0x428 OFST  0x430 XSIZE (bytes - 1)  0x434 YSIZE (lines - 1)  0x438 STRIDE (|= 0x080f0000)
 *   0x43c CON 0x80000080  0x440 CON2  0x444 CON3 0x00200010  0x448 CROP  0xa00 FBC_IMGO_CTL1 (0: off)
 * The DMA goes through SMI larbs 13/14 (CAMSV ports, m4u_port.h): their IOMMU is switched off per port (SMI_LARB_NON_SEC_CON
 * 0x380 + 4 * port, bit 0) so the DMA takes physical addresses, as HakiOS found it must (docs/camera-x6826b.md).
 */
#define N2V_NBUF 2
/*
 * Stock Android measurements (2026-11-09, gpu/build/cam-android-regs-20261109/REPORT.md) replace the guesses above:
 *  - the DMA only arms after an edge on MODULE_EN bit 30 written after TOP (0x30) = 1, 5, 0x8005 (CAMSV_TOP_CTRL::_enable);
 *  - no 0x424 address MSB: the buffers must sit below 4 GB (the iOS window starts at 0x1_42000000, so they are a fixed claim in
 *    the unused lower bank: 0xD0000000, 2 x 16 MiB; live.dts has no carve-out in 0xC0000000..0xDFE70000);
 *  - CAMSV 0 (0x1a090000) is larb14 port 3 (L14_CAM_CAMSV0); cam mux i + 3 feeds CAMSV i;
 *  - SENINF mux pixel mode 2 <-> TG single data bus (SEN_MODE 0x12000); RAW10 PAK = 0x49; STRIDE |= 0x08070000;
 *  - enqueue = 0x420, 0x804, 0x2c = 0x11 (the FBC takes 0x420 at RCNT_INC); FBC CTL2 0xa04: [7:0] enqueued, [15:8] done,
 *    [23:16] pending; 0x400 bit 0 clears and 0x48 bit 1 sets once the DMA runs.
 */
#define N2V_BUF_PA 0xd0000000ULL
#define N2V_BUF_BYTES 0x1000000u
#define N2V_SV 0u               /* the CAMSV in use */
#define N2V_CAMMUX 3
static vm_offset_t n2v_sv[4];
static vm_offset_t n2v_buf[N2V_NBUF];
static uint64_t n2v_pa[N2V_NBUF];
static uint32_t n2v_bufsz;
static uint32_t n2v_xoff;        /* first pixel grabbed: the DMA line must be a multiple of 8 bytes (IMX615: 3264 of 3280) */

static void
n2v_inval(vm_offset_t va, uint32_t len)
{
	for (vm_offset_t a = va & ~63ul; a < va + len; a += 64) { __asm__ volatile("dc civac, %0" :: "r"(a) : "memory"); }
	__asm__ volatile("dsb sy" ::: "memory");
}

static bool
n2v_alloc(uint32_t bytes)
{
	if (bytes > N2V_BUF_BYTES - 4096) { return false; }
	if (n2v_buf[0]) { return true; }
	for (unsigned i = 0; i < N2V_NBUF; i++) {
		n2v_pa[i] = N2V_BUF_PA + (uint64_t)i * N2V_BUF_BYTES;
		n2v_buf[i] = ml_io_map_unmappable(n2v_pa[i], N2V_BUF_BYTES, VM_WIMG_DEFAULT);
		printf("NORD2 CAMSV: buffer %u: va %#lx pa %#llx (%u bytes, fixed claim)\n", i, (unsigned long)n2v_buf[i], (unsigned long long)n2v_pa[i], N2V_BUF_BYTES);
		if (!n2v_buf[i]) { return false; }
	}
	n2v_bufsz = N2V_BUF_BYTES;
	return true;
}

static void
n2v_map(void)
{
	if (n2v_sv[0]) { return; }
	for (unsigned i = 0; i < 4; i++) { n2v_sv[i] = ml_io_map_unmappable(0x1a090000 + 0x1000 * i, 0x1000, VM_WIMG_IO); }
}

/* SENINF mux 1 -> cam mux 3 -> CAMSV 0 */
static void
n2v_cammux(uint32_t w, uint32_t h)
{
	mtk_cam_seninf_set_cammux_vc(&n2s_ctx, N2V_CAMMUX, 0, 0x2b, 1, 1);
	mtk_cam_seninf_set_cammux_src(&n2s_ctx, SENINF_MUX1, N2V_CAMMUX, (int)w, (int)h);
	mtk_cam_seninf_set_cammux_chk_pixel_mode(&n2s_ctx, N2V_CAMMUX, 2);
	mtk_cam_seninf_cammux(&n2s_ctx, N2V_CAMMUX);
}

/* CAMSV_BUF_CTRL::enqueueHwBuf: frame address, header address (64 bytes, after the frame), RCNT_INC */
static void
n2v_enqueue(vm_offset_t sv, uint64_t pa, uint32_t frame_bytes)
{
	ni_wr(sv, 0x420, (uint32_t)pa);
	ni_wr(sv, 0x804, (uint32_t)((pa + frame_bytes + 63) & ~63ULL));
	ni_wr(sv, 0x2c, ni_rd(sv, 0x2c) | 1u);
}

/* the stock HAL's writes, in its order (uprobes on IspDrvCamsv::writeReg: logs/camsv_hal_writes_ordered.txt) */
static void
n2v_camsv_cfg(vm_offset_t sv, uint32_t w, uint32_t h, uint32_t stride, uint64_t pa)
{
	/* CAMSV_TG_CTRL::_config */
	ni_wr(sv, 0x130, 0x10000); ni_wr(sv, 0x194, 0x19); ni_wr(sv, 0x1b0, 1); ni_wr(sv, 0x1a4, 0); ni_wr(sv, 0x134, 0);
	ni_wr(sv, 0x130, 0x12000);                                  /* time stamp, single data bus (SENINF mux pixel mode 2) */
	ni_wr(sv, 0x138, n2v_xoff | (n2v_xoff + w) << 16); ni_wr(sv, 0x13c, h << 16);
	/* reset (ISP_RESET_BY_HWMODULE and CAMSV_TOP_CTRL::_config) */
	ni_wr(sv, 0x20, 0); ni_wr(sv, 0x20, 1); (void)n2s_wait(sv, 0x20, 3u, 3u); ni_wr(sv, 0x20, 4); ni_wr(sv, 0x20, 0);
	/* CAMSV_TOP_CTRL::_config */
	ni_wr(sv, 0x10, 0x50000001); ni_wr(sv, 0x10, 0x40000001);
	ni_wr(sv, 0x5c, 0); ni_wr(sv, 0x58, 0);
	ni_wr(sv, 0x14, 1);                                         /* RAW10 */
	ni_wr(sv, 0x18, 0x31871);
	ni_wr(sv, 0x10, 0x40000005);
	ni_wr(sv, 0x4c, 0x09); ni_wr(sv, 0x2d0, 0xe0010); ni_wr(sv, 0x4c, 0x49);   /* PAK: RAW10, mode 2 */
	ni_wr(sv, 0x800, 0); ni_wr(sv, 0x7b0, 0); ni_wr(sv, 0x7c4, 0xffffffffu); ni_wr(sv, 0x418, 0x61000000u);
	/* CAMSV_BUF_CTRL::_config, DMAO_CAMSV_B::_config */
	ni_wr(sv, 0xa00, 0); ni_wr(sv, 0xa00, 0x800);
	ni_wr(sv, 0x430, stride - 1); ni_wr(sv, 0x434, h - 1); ni_wr(sv, 0x438, stride); ni_wr(sv, 0x438, stride | 0x08070000u);
	ni_wr(sv, 0x448, 0); ni_wr(sv, 0x43c, 0x80000080u); ni_wr(sv, 0x440, 0x00400030u); ni_wr(sv, 0x444, 0x00200010u);
	/* CAMSV_TG_CTRL::_enable, CAMSV_BUF_CTRL::_enable, DMAO_CAMSV_B::_enable */
	ni_wr(sv, 0x130, 0x12001);
	ni_wr(sv, 0xa00, 0x800); ni_wr(sv, 0xa00, 0x8800); ni_wr(sv, 0xa00, 0x8c00);
	ni_wr(sv, 0x10, 0x40000015);
	ni_wr(sv, 0x800, 1);
	n2v_enqueue(sv, pa, stride * h);
}
/* CAMSV_TOP_CTRL::_enable and the view finder (the stock kernel's ISP_VF_LOG): the bit 30 edge arms the DMA */
static void
n2v_camsv_go(vm_offset_t sv)
{
	ni_wr(sv, 0x30, 0x1); ni_wr(sv, 0x30, 0x5); ni_wr(sv, 0x30, 0x8005);
	ni_wr(sv, 0x10, 0x15); ni_wr(sv, 0x10, 0x40000015);
	ni_wr(sv, 0x134, 1);
}

static void
n2v_camsv_stop(vm_offset_t sv)
{
	ni_wr(sv, 0x134, 0);
	ni_wr(sv, 0x130, 0x12000);
	ni_wr(sv, 0x10, 0x40000005);
	ni_wr(sv, 0x800, 0);
	ni_wr(sv, 0xa00, 0);
	ni_wr(sv, 0x30, 0);
}

static void __attribute__((unused))
n2v_status(const char *what, vm_offset_t sv, unsigned i)
{
	printf("NORD2 CAMSV: %s camsv%u: tg inter %#x frmsize %#x datno %#x module %#x sw %#x fbc %#x/%#x base %#x dma rst %#x busy %#x cnt %#x\n", what, i,
	    ni_rd(sv, 0x16c), ni_rd(sv, 0x168), ni_rd(sv, 0x160), ni_rd(sv, 0x10), ni_rd(sv, 0x20), ni_rd(sv, 0xa00), ni_rd(sv, 0xa04),
	    ni_rd(sv, 0x420), ni_rd(sv, 0x400), ni_rd(sv, 0x48), ni_rd(sv, 0x0));
}

/* what is in a buffer: lines written, high bytes of RAW10, and a 64x48 thumbnail (HakiOS tools/camthumb.py) */
static void __attribute__((unused))
n2v_look(unsigned b, uint32_t w, uint32_t h, uint32_t stride)
{
	volatile uint8_t *p = (volatile uint8_t *)n2v_buf[b];
	n2v_inval(n2v_buf[b], stride * h);
	unsigned written = 0, first = ~0u, last = 0;
	uint32_t mn = 255, mx = 0; uint64_t sum = 0, n = 0;
	for (uint32_t y = 0; y < h; y += 8) {
		bool any = false;
		for (uint32_t x = 0; x < stride; x += 97) { if (p[y * stride + x] != 0x5a) { any = true; break; } }
		if (any) { written++; if (y < first) { first = y; } last = y; }
	}
	for (uint32_t y = 0; y < h; y += 16) {
		for (uint32_t g = 0; g < w / 4; g += 13) {
			uint8_t v = p[y * stride + g * 5];
			mn = v < mn ? v : mn; mx = v > mx ? v : mx; sum += v; n++;
		}
	}
	printf("NORD2 CAMSV: buffer %u: %u of %u sampled lines written (first %u last %u), high bytes min %u max %u mean %llu\n", b, written,
	    (h + 7) / 8, first, last, mn, mx, n ? (unsigned long long)(sum / n) : 0ULL);
	printf("NORD2 CAMSV: line 0: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x | mid: %02x %02x %02x %02x %02x\n", p[0], p[1], p[2], p[3], p[4],
	    p[5], p[6], p[7], p[8], p[9], p[(h / 2) * stride + stride / 2], p[(h / 2) * stride + stride / 2 + 1], p[(h / 2) * stride + stride / 2 + 2],
	    p[(h / 2) * stride + stride / 2 + 3], p[(h / 2) * stride + stride / 2 + 4]);
	if (!written) { return; }
	uint32_t cw = w / 64, ch = h / 48;
	for (uint32_t cy = 0; cy < 48; cy++) {
		char row[140];
		for (uint32_t cx = 0; cx < 64; cx++) {
			uint32_t acc = 0;
			for (uint32_t s = 0; s < 16; s++) {
				uint32_t y = cy * ch + (s / 4) * (ch / 4), x = cx * cw + (s % 4) * (cw / 4);
				const volatile uint8_t *g = p + y * stride + (x / 4) * 5;      /* little-endian 40-bit groups */
				uint64_t v = (uint64_t)g[0] | (uint64_t)g[1] << 8 | (uint64_t)g[2] << 16 | (uint64_t)g[3] << 24 | (uint64_t)g[4] << 32;
				acc += (uint32_t)(v >> (10 * (x % 4) + 2)) & 0xff;
			}
			snprintf(row + cx * 2, 3, "%02x", acc / 16);
		}
		printf("NORD2 CAMSV: THUMB %02u: %s\n", cy, row);
	}
}

/* T2b: before the sensor streams: the receiver on its port, cam mux 3, CAMSV 0 armed on buffer 0 */
static bool __attribute__((unused))
n2v_start(const char *name, int port, bool cphy, int lanes, int64_t rate, uint32_t w, uint32_t h)
{
	uint32_t stride = w * 10 / 8;
	n2v_map();
	if (!n2v_alloc(stride * h + 4096)) { printf("NORD2 CAMSV: %s: no buffer for %u bytes\n", name, stride * h); return false; }
	memset((void *)n2v_buf[0], 0x5a, stride * h + 64); n2v_inval(n2v_buf[0], stride * h + 64);
	printf("NORD2 CAMSV: %s STEP rx next (port %d %s)\n", name, port, cphy ? "C-PHY" : "D-PHY"); n2c_sleep(300);
	n2s_rx(port, cphy, lanes, rate);
	printf("NORD2 CAMSV: %s STEP rx done\n", name); n2c_sleep(300);
	n2v_cammux(w, h);
	printf("NORD2 CAMSV: %s: camsv%u -> pa %#llx, %ux%u stride %u\n", name, N2V_SV, (unsigned long long)n2v_pa[0], w, h, stride);
	n2v_camsv_cfg(n2v_sv[N2V_SV], w, h, stride, n2v_pa[0]);
	n2v_camsv_go(n2v_sv[N2V_SV]);
	return true;
}
static void __attribute__((unused))
n2v_finish(const char *name, uint32_t w, uint32_t h)
{
	uint32_t stride = w * 10 / 8;
	for (unsigned t = 0; t < 4; t++) {
		n2c_sleep(150);
		n2v_status(name, n2v_sv[N2V_SV], N2V_SV);
		n2s_report(name);
	}
	n2v_camsv_stop(n2v_sv[N2V_SV]);
	n2c_sleep(50);
	n2v_look(0, w, h, stride);
}
