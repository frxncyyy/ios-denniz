/*
 * 0298 NORD2 JPG: the MT6893 hardware JPEG encoder (gpu/hwcodec, milestone 1).
 *
 * Nothing here runs by itself: each step is autotest command 7 (nord2_autotest.inc, USB request 0x72), argument = step,
 * run on the autotest thread; the host tool is gpu/hwcodec/tools/hwc.py. Every wait is bounded.
 *
 * Where each fact comes from (OnePlus kernel android_kernel_oneplus_mt6893, all GPL-2.0):
 *   block          mt6893.dts jpgenc@17030000 "mediatek,jpgenc", SPI 309, larb7 ports 7 Y, 8 C, 9 Q table, 10 bit stream
 *   registers      drivers/media/platform/mtk-jpeg/mtk_jpeg_enc_reg.h, the sequence mtk_jpeg_enc_hw.c + mtk_jpeg_core.c
 *                  (mtk_jpeg_set_param, mtk_jpeg_set_enc_dst, mtk_jpeg_device_run)
 *   VEN domain     clk-mt6885-pg.c spm_mtcmos_ctrl_ven: SPM 0x10006000 VEN_PWR_CON 0x344, status bit 17 (0x16c / 0x170);
 *                  bus protection infracfg PROTECTEN_MM 0x2d0 (set 0x2d4, clr 0x2d8, ack 0x2ec) bits 26 (step 1), 27 (step 2),
 *                  PROTECTEN_MM_2 0xdc8 (set 0xdcc, clr 0xdd0, ack 0xdd8) bits 0 (step 1), 1 (step 2); parent domain DIS (bit 20)
 *   venc_sel       clk-mt6885.c: topckgen CLK_CFG_13 0xe0 (set 0xe4, clr 0xe8) bits 24-27, gate bit 31, CLK_CFG_UPDATE1 0x08
 *                  bit 24; parent 8 = mainpll_d6_d2 (182 MHz: the PLL every running system has, a low step)
 *   vencsys gates  clk-mt6885-ven.c: 0x17000000 status 0x0, 0x4 = clock on, 0x8 = clock off; bit 0 larb, 4 venc (the larb's
 *                  clock in the device tree), 8 jpgenc, 28 GALS; clk-mt6885-pg.c writes 0x4 to 0x17000004 after power-on
 *   larb7          0x17010000; smi/mt6885/smi_conf.h: 0x24 = 0x300256, 0x40 = 1, 0x524 = 0x300256, OSTDL 0x200 + 4 * port
 *                  (7, 8: 1; 9, 10: 4); 0x380 + 4 * port: bit 0 = IOMMU translation, bits 8-15 = address bits 32+ (pseudo_m4u_v2.c):
 *                  all cleared, so the DMA takes 32-bit physical addresses (what nord2_seninf.inc does on larbs 13/14)
 * The interrupt is not taken (its GIC line is never enabled by this kernel): the status register is polled.
 *
 * Memory: a fixed claim below 4 GB, 0xD8000000..0xD9000000, directly above the cameras' (nord2_camdev.inc: 0xD0000000..
 * 0xD8000000; nothing is carved out up to 0xDFE70000). Source picture in the first half, the JPEG in the second.
 */
#ifndef N2J_HOST
#define n2j_map(pa, bytes, io) ml_io_map_unmappable((pa), (bytes), (io) ? VM_WIMG_IO : VM_WIMG_DEFAULT)
#define n2j_us(us) delay(us)
#define n2j_log printf
static void
n2j_cache(vm_offset_t va, uint32_t len)
{
	for (vm_offset_t a = va & ~63ul; a < va + len; a += 64) { __asm__ volatile("dc civac, %0" :: "r"(a) : "memory"); }
	__asm__ volatile("dsb sy" ::: "memory");
}
#endif

#define N2J_BUF_PA      0xd8000000ULL
#define N2J_HALF        0x800000u               /* 8 MiB picture, 8 MiB output */
#define N2J_DST_BYTES   (N2J_HALF - 0x1000u)
enum {
	N2J_STEP_STATUS = 0, N2J_STEP_POWER = 1, N2J_STEP_BUS = 2, N2J_STEP_ENCODE = 3, N2J_STEP_OFF = 4,
	N2J_PWR_STA = 1u << 17, N2J_DIS_STA = 1u << 20,
	N2J_CG_BITS = (1u << 0) | (1u << 4) | (1u << 8) | (1u << 28),
};
/* results of a step (the autotest command's result word): 1 done; the rest say what stopped it */
enum {
	N2J_OK = 1, N2J_E_STEP = 16, N2J_E_MAP = 17, N2J_E_DIS_OFF = 18, N2J_E_PWR_TIMEOUT = 19, N2J_E_SRAM_TIMEOUT = 20,
	N2J_E_NOT_ON = 21, N2J_E_ARGS = 22, N2J_E_ENC_TIMEOUT = 23, N2J_E_ENC_STATUS = 24, N2J_E_ENC_SIZE = 25,
	N2J_E_PROT_TIMEOUT = 26, N2J_E_GATES = 27, N2J_E_NO_BUS = 28, N2J_E_MUX = 29,
};

static struct {
	vm_offset_t spm, infra, top, cg, larb, enc, dispsmi, buf;
	bool bus_ready;
	uint32_t out_bytes;
} n2j;

static bool
n2j_wait(vm_offset_t b, uint32_t off, uint32_t mask, uint32_t want)
{
	for (unsigned i = 0; i < 2000; i++) { if ((ni_rd(b, off) & mask) == want) { return true; } n2j_us(10); }
	return false;
}

static bool
n2j_maps(void)
{
	if (n2j.buf) { return true; }
	n2j.spm = n2j_map(0x10006000, 0x1000, 1);
	n2j.infra = n2j_map(0x10001000, 0x1000, 1);
	n2j.top = n2j_map(0x10000000, 0x1000, 1);
	n2j.cg = n2j_map(0x17000000, 0x1000, 1);
	n2j.larb = n2j_map(0x17010000, 0x1000, 1);
	n2j.enc = n2j_map(0x17030000, 0x1000, 1);
	n2j.dispsmi = n2j_map(0x1411f000, 0x1000, 1);
	if (!n2j.spm || !n2j.infra || !n2j.top || !n2j.cg || !n2j.larb || !n2j.enc || !n2j.dispsmi) { return false; }
	n2j.buf = n2j_map(N2J_BUF_PA, 2 * N2J_HALF, 0);
	return n2j.buf != 0;
}

/* the domain has power, is out of reset and isolation, and its bus protection is released: its registers may be touched */
static bool
n2j_domain_on(void)
{
	uint32_t con = ni_rd(n2j.spm, 0x344);
	return (ni_rd(n2j.spm, 0x16c) & N2J_PWR_STA) && (ni_rd(n2j.spm, 0x170) & N2J_PWR_STA) &&
	       (con & 0x1f) == 0x0d && !(con & (1u << 8)) && !(con & (1u << 12)) &&
	       !(ni_rd(n2j.infra, 0x2d0) & (3u << 26)) && !(ni_rd(n2j.infra, 0xdc8) & 3u);
}

static void
n2j_dump_soc(const char *when)
{
	n2j_log("NORD2 JPG: %s: spm status %#x/%#x VEN_PWR_CON %#x DIS_PWR_CON %#x | prot mm %#x ack %#x mm2 %#x ack %#x | CLK_CFG_13 %#x\n",
	    when, ni_rd(n2j.spm, 0x16c), ni_rd(n2j.spm, 0x170), ni_rd(n2j.spm, 0x344), ni_rd(n2j.spm, 0x350),
	    ni_rd(n2j.infra, 0x2d0), ni_rd(n2j.infra, 0x2ec), ni_rd(n2j.infra, 0xdc8), ni_rd(n2j.infra, 0xdd8), ni_rd(n2j.top, 0xe0));
}

static void
n2j_dump_block(const char *when)
{
	if (!n2j_domain_on()) { n2j_log("NORD2 JPG: %s: VEN domain is not on: its registers are not read\n", when); return; }
	uint32_t cg = ni_rd(n2j.cg, 0x0);
	n2j_log("NORD2 JPG: %s: vencsys CG %#x | larb7 stat %#x 0x24 %#x 0x40 %#x ostdl7-10 %#x %#x %#x %#x nonsec7-10 %#x %#x %#x %#x\n",
	    when, cg, ni_rd(n2j.larb, 0x0), ni_rd(n2j.larb, 0x24), ni_rd(n2j.larb, 0x40),
	    ni_rd(n2j.larb, 0x21c), ni_rd(n2j.larb, 0x220), ni_rd(n2j.larb, 0x224), ni_rd(n2j.larb, 0x228),
	    ni_rd(n2j.larb, 0x39c), ni_rd(n2j.larb, 0x3a0), ni_rd(n2j.larb, 0x3a4), ni_rd(n2j.larb, 0x3a8));
	if ((cg & ((1u << 8) | (1u << 0))) != ((1u << 8) | (1u << 0))) { n2j_log("NORD2 JPG: %s: jpgenc clock gated: its registers are not read\n", when); return; }
	n2j_log("NORD2 JPG: %s: enc RSTB %#x CTRL %#x QUALITY %#x BLK_NUM %#x BLK_CNT %#x INT %#x DST %#x DMA %#x STALL %#x OFS %#x\n",
	    when, ni_rd(n2j.enc, 0x100), ni_rd(n2j.enc, 0x104), ni_rd(n2j.enc, 0x108), ni_rd(n2j.enc, 0x10c), ni_rd(n2j.enc, 0x110),
	    ni_rd(n2j.enc, 0x11c), ni_rd(n2j.enc, 0x120), ni_rd(n2j.enc, 0x124), ni_rd(n2j.enc, 0x128), ni_rd(n2j.enc, 0x138));
	n2j_log("NORD2 JPG: %s: enc RSTMCU %#x SIZE %#x DBG %#x %#x CYCLE %#x MASK %#x LUMA %#x CHROMA %#x STRIDE %#x IMGSTRIDE %#x DCM %#x SEL %#x ULTRA %#x\n",
	    when, ni_rd(n2j.enc, 0x150), ni_rd(n2j.enc, 0x154), ni_rd(n2j.enc, 0x160), ni_rd(n2j.enc, 0x164), ni_rd(n2j.enc, 0x168),
	    ni_rd(n2j.enc, 0x16c), ni_rd(n2j.enc, 0x170), ni_rd(n2j.enc, 0x174), ni_rd(n2j.enc, 0x178), ni_rd(n2j.enc, 0x17c),
	    ni_rd(n2j.enc, 0x300), ni_rd(n2j.enc, 0x314), ni_rd(n2j.enc, 0x318));
}

/* step 1: venc_sel, the VEN domain, its bus protection, the vencsys gates */
static uint32_t
n2j_power(void)
{
	const uint32_t con = 0x344;
	uint32_t s0 = ni_rd(n2j.spm, 0x16c), s1 = ni_rd(n2j.spm, 0x170);
	if (!(s0 & N2J_DIS_STA) || !(s1 & N2J_DIS_STA)) { n2j_log("NORD2 JPG: the display domain (the parent) is off: nothing done\n"); return N2J_E_DIS_OFF; }
	uint32_t cfg = ni_rd(n2j.top, 0xe0);
	if ((cfg & (1u << 31)) || ((cfg >> 24) & 0xf) == 0) {
		ni_wr(n2j.top, 0xe8, 0x8fu << 24); ni_wr(n2j.top, 0xe4, 8u << 24); ni_wr(n2j.top, 0x08, 1u << 24);
		n2j_us(100);
		n2j_log("NORD2 JPG: venc_sel %#x -> %#x (mainpll_d6_d2)\n", cfg, ni_rd(n2j.top, 0xe0));
		cfg = ni_rd(n2j.top, 0xe0);
		if ((cfg & (1u << 31)) || ((cfg >> 24) & 0xf) != 8) { return N2J_E_MUX; }
	} else {
		n2j_log("NORD2 JPG: venc_sel left as found (%#x)\n", cfg);
	}
	ni_wr(n2j.spm, 0x000, (0xb16u << 16) | 1u);                      /* POWERON_CONFIG_EN key */
	if ((s0 & N2J_PWR_STA) && (s1 & N2J_PWR_STA)) {
		n2j_log("NORD2 JPG: VEN domain already has power (con %#x)\n", ni_rd(n2j.spm, con));
	} else {
		ni_mask(n2j.spm, con, 1u << 2, 1u << 2);                  /* PWR_ON */
		ni_mask(n2j.spm, con, 1u << 3, 1u << 3);                  /* PWR_ON_2ND */
		if (!n2j_wait(n2j.spm, 0x16c, N2J_PWR_STA, N2J_PWR_STA) || !n2j_wait(n2j.spm, 0x170, N2J_PWR_STA, N2J_PWR_STA)) {
			ni_mask(n2j.spm, con, (1u << 2) | (1u << 3), 0);      /* back to what it was: still isolated and in reset */
			n2j_log("NORD2 JPG: VEN power status timeout (con %#x): power request withdrawn\n", ni_rd(n2j.spm, con));
			return N2J_E_PWR_TIMEOUT;
		}
	}
	ni_mask(n2j.spm, con, 1u << 4, 0);                               /* PWR_CLK_DIS */
	ni_mask(n2j.spm, con, 1u << 1, 0);                               /* PWR_ISO */
	ni_mask(n2j.spm, con, 1u << 0, 1u << 0);                         /* PWR_RST_B */
	ni_mask(n2j.spm, con, 1u << 8, 0);                               /* SRAM_PDN */
	if (!n2j_wait(n2j.spm, con, 1u << 12, 0)) { n2j_log("NORD2 JPG: VEN SRAM ack timeout (con %#x): bus protection left on\n", ni_rd(n2j.spm, con)); return N2J_E_SRAM_TIMEOUT; }
	ni_wr(n2j.infra, 0x2d8, 1u << 27);                               /* release step 2: MM, MM_2; then step 1 */
	ni_wr(n2j.infra, 0xdd0, 1u << 1);
	ni_wr(n2j.infra, 0x2d8, 1u << 26);
	ni_wr(n2j.infra, 0xdd0, 1u << 0);
	n2j_us(200);
	n2j_dump_soc("power");
	if (!n2j_domain_on()) { return N2J_E_NOT_ON; }
	ni_wr(n2j.cg, 0x4, 0x4);                                         /* as clk-mt6885-pg.c after power-on */
	ni_wr(n2j.cg, 0x4, N2J_CG_BITS);
	n2j_us(100);
	uint32_t cg = ni_rd(n2j.cg, 0x0);
	n2j_log("NORD2 JPG: vencsys CG %#x (want bits %#x)\n", cg, (uint32_t)N2J_CG_BITS);
	return (cg & N2J_CG_BITS) == N2J_CG_BITS ? N2J_OK : N2J_E_GATES;
}

static bool
n2j_clocked(void)
{
	return n2j_domain_on() && (ni_rd(n2j.cg, 0x0) & N2J_CG_BITS) == N2J_CG_BITS;
}

static void
n2j_reset(void)
{
	ni_wr(n2j.enc, 0x100, 0); ni_wr(n2j.enc, 0x100, 1); ni_wr(n2j.enc, 0x314, 0);
}

/* step 2: larb7 for the four JPEG ports, encoder reset, register defaults */
static uint32_t
n2j_bus(void)
{
	static const uint8_t ostd[4] = {1, 1, 4, 4};
	if (!n2j_clocked()) { n2j_log("NORD2 JPG: bus: the domain or its clocks are not on\n"); return N2J_E_NOT_ON; }
	n2j_log("NORD2 JPG: display smi common L1LEN %#x BUS_SEL %#x DCM %#x (read only)\n", ni_rd(n2j.dispsmi, 0x100), ni_rd(n2j.dispsmi, 0x220), ni_rd(n2j.dispsmi, 0x300));
	n2j_dump_block("before");
	ni_wr(n2j.larb, 0x24, 0x300256); ni_wr(n2j.larb, 0x40, 1); ni_wr(n2j.larb, 0x524, 0x300256);
	for (unsigned p = 7; p <= 10; p++) {
		ni_wr(n2j.larb, 0x200 + 4 * p, ostd[p - 7]);
		ni_mask(n2j.larb, 0x380 + 4 * p, 0xff01u, 0);            /* pseudo_m4u_v2.c m4u_config_port(Virtuality 0): translation off, address bits 32+ zero */
	}
	n2j_reset();
	n2j_dump_block("after");
	for (unsigned p = 7; p <= 10; p++) { if (ni_rd(n2j.larb, 0x380 + 4 * p) & 0xff01u) { return N2J_E_NO_BUS; } }
	n2j.bus_ready = true;
	return N2J_OK;
}

/* The test picture: the host repeats this formula (gpu/hwcodec/tools/hwc.py pattern()). stride x rows, then chroma */
static void
n2j_pattern(uint8_t *p, uint32_t w, uint32_t h, uint32_t stride, uint32_t rows)
{
	for (uint32_t y = 0; y < rows; y++) {
		for (uint32_t x = 0; x < stride; x++) {
			uint32_t xx = x < w ? x : w - 1, yy = y < h ? y : h - 1;
			p[y * stride + x] = (uint8_t)(16 + xx * 180 / w + ((((xx >> 4) + (yy >> 4)) & 1) ? 40 : 0) + ((xx * 7 + yy * 13) & 7));
		}
	}
	uint8_t *c = p + stride * rows;
	for (uint32_t y = 0; y < rows / 2; y++) {
		for (uint32_t x = 0; x < stride / 2; x++) {
			uint32_t xx = 2 * x < w ? 2 * x : w - 1, yy = 2 * y < h ? 2 * y : h - 1;
			c[y * stride + 2 * x] = (uint8_t)(78 + yy * 100 / h);         /* Cb */
			c[y * stride + 2 * x + 1] = (uint8_t)(78 + xx * 100 / w);     /* Cr */
		}
	}
}

/*
 * step 3: one encode. w, h: picture size (even, 16..4096); q: the hardware's quality code (mtk_jpeg_core.h: 4 Q30, 0 Q38, 0xe Q44,
 * 0xa Q52, 1 Q60, 0xf Q72, 6 Q78, 2 Q85, 7 Q90, 3 Q95, 0x10 Q97). Returns the result; *bytes = the JPEG's size at n2j.buf + N2J_HALF.
 */
static uint32_t
n2j_encode(uint32_t w, uint32_t h, uint32_t q, uint32_t *bytes)
{
	*bytes = 0;
	if (!n2j.bus_ready || !n2j_clocked()) { n2j_log("NORD2 JPG: encode: steps 1 and 2 first\n"); return N2J_E_NOT_ON; }
	if (w < 16 || h < 16 || w > 4096 || h > 4096 || (w & 1) || (h & 1) || q > 0x10) { return N2J_E_ARGS; }
	uint32_t stride = (w + 15) & ~15u, rows = (h + 15) & ~15u, src_bytes = stride * rows * 3 / 2;
	if (src_bytes > N2J_HALF) { return N2J_E_ARGS; }
	uint8_t *src = (uint8_t *)n2j.buf, *dst = src + N2J_HALF;
	const uint32_t src_pa = (uint32_t)N2J_BUF_PA, dst_pa = (uint32_t)N2J_BUF_PA + N2J_HALF;
	n2j_pattern(src, w, h, stride, rows);
	memset(dst, 0xa5, N2J_HALF);
	n2j_cache((vm_offset_t)src, src_bytes);
	n2j_cache((vm_offset_t)dst, N2J_HALF);

	vm_offset_t e = n2j.enc;
	n2j_reset();
	ni_wr(e, 0x154, w << 16 | h);                                            /* IMG_SIZE */
	ni_mask(e, 0x104, 0x18, 2u << 3);                                        /* NV12 */
	ni_wr(e, 0x10c, (stride >> 4) * (rows >> 4) * 6 - 1);                    /* BLK_NUM */
	ni_wr(e, 0x17c, stride);                                                 /* IMG_STRIDE */
	ni_wr(e, 0x178, stride);                                                 /* STRIDE (bytes per line in memory) */
	ni_wr(e, 0x170, src_pa);
	ni_wr(e, 0x174, src_pa + stride * rows);
	ni_wr(e, 0x138, 0);                                                      /* OFFSET_ADDR */
	ni_wr(e, 0x16c, dst_pa & 0xf);                                           /* BYTE_OFFSET_MASK */
	ni_wr(e, 0x120, dst_pa & ~0xfu);                                         /* DST_ADDR0 */
	ni_wr(e, 0x128, (dst_pa + N2J_DST_BYTES) & ~0xfu);                       /* STALL_ADDR0 */
	ni_mask(e, 0x108, 0xffff, q);                                            /* QUALITY */
	ni_mask(e, 0x104, 1u << 10, 0); ni_wr(e, 0x150, 0);                      /* no restart markers */
	ni_mask(e, 0x104, 1u << 5, 0);                                           /* plain JPEG file, no EXIF gap */
	uint32_t ctrl = (ni_rd(e, 0x104) | (1u << 2) | (1u << 0) | (1u << 20) | (1u << 29)) & ~(1u << 30);
	n2j_log("NORD2 JPG: encode %ux%u stride %u rows %u q-code %#x src %#x chroma %#x dst %#x blk %u ctrl %#x\n", w, h, stride, rows, q,
	    src_pa, src_pa + stride * rows, dst_pa, (stride >> 4) * (rows >> 4) * 6 - 1, ctrl);
	uint64_t t0 = mach_absolute_time();
	ni_wr(e, 0x104, ctrl);                                                   /* start */
	uint32_t st = 0;
	unsigned polls = 0;
	for (; polls < 20000; polls++) {                                         /* 2 s */
		st = ni_rd(e, 0x11c) & 0x13;
		if (st) { break; }
		n2j_us(100);
	}
	uint64_t ns;
	absolutetime_to_nanoseconds(mach_absolute_time() - t0, &ns);
	uint32_t dma = ni_rd(e, 0x124), base = ni_rd(e, 0x120), size = dma - base;
	n2j_log("NORD2 JPG: status %#x after %u polls (%llu us): DMA %#x DST %#x size %u BLK_CNT %#x CYCLE %#x DBG %#x %#x larb7 stat %#x\n", st, polls,
	    (unsigned long long)(ns / 1000), dma, base, size, ni_rd(e, 0x110), ni_rd(e, 0x168), ni_rd(e, 0x160), ni_rd(e, 0x164), ni_rd(n2j.larb, 0x0));
	if (st) { ni_wr(e, 0x11c, 0); }
	if (!st) { n2j_dump_block("timeout"); n2j_reset(); return N2J_E_ENC_TIMEOUT; }
	if (!(st & 1)) { n2j_reset(); return N2J_E_ENC_STATUS; }
	if (size < 4 || size > N2J_DST_BYTES) { return N2J_E_ENC_SIZE; }
	n2j_cache((vm_offset_t)dst, N2J_HALF);
	n2j_log("NORD2 JPG: head %02x %02x %02x %02x %02x %02x %02x %02x tail %02x %02x %s\n", dst[0], dst[1], dst[2], dst[3], dst[4], dst[5], dst[6], dst[7],
	    dst[size - 2], dst[size - 1], dst[0] == 0xff && dst[1] == 0xd8 ? "(SOI)" : "(NOT a JPEG start)");
	*bytes = size;
	return N2J_OK;
}

/* step 4: gates off, bus protection on (acknowledged), domain off: spm_mtcmos_ctrl_ven(STA_POWER_DOWN) */
static uint32_t
n2j_off(void)
{
	const uint32_t con = 0x344;
	n2j.bus_ready = false;
	if (n2j_domain_on()) { n2j_reset(); ni_wr(n2j.cg, 0x8, N2J_CG_BITS); }
	if (!(ni_rd(n2j.spm, 0x16c) & N2J_PWR_STA)) { n2j_dump_soc("off (was off)"); return N2J_OK; }
	ni_wr(n2j.spm, 0x000, (0xb16u << 16) | 1u);
	ni_wr(n2j.infra, 0x2d4, 1u << 26); if (!n2j_wait(n2j.infra, 0x2ec, 1u << 26, 1u << 26)) { goto prot; }
	ni_wr(n2j.infra, 0xdcc, 1u << 0);  if (!n2j_wait(n2j.infra, 0xdd8, 1u << 0, 1u << 0)) { goto prot; }
	ni_wr(n2j.infra, 0x2d4, 1u << 27); if (!n2j_wait(n2j.infra, 0x2ec, 1u << 27, 1u << 27)) { goto prot; }
	ni_wr(n2j.infra, 0xdcc, 1u << 1);  if (!n2j_wait(n2j.infra, 0xdd8, 1u << 1, 1u << 1)) { goto prot; }
	ni_mask(n2j.spm, con, 1u << 8, 1u << 8);                         /* SRAM_PDN */
	if (!n2j_wait(n2j.spm, con, 1u << 12, 1u << 12)) { n2j_log("NORD2 JPG: off: SRAM ack timeout (con %#x)\n", ni_rd(n2j.spm, con)); return N2J_E_SRAM_TIMEOUT; }
	ni_mask(n2j.spm, con, 1u << 1, 1u << 1);                         /* PWR_ISO */
	ni_mask(n2j.spm, con, 1u << 4, 1u << 4);                         /* PWR_CLK_DIS */
	ni_mask(n2j.spm, con, 1u << 0, 0);                               /* PWR_RST_B */
	ni_mask(n2j.spm, con, 1u << 2, 0);                               /* PWR_ON */
	ni_mask(n2j.spm, con, 1u << 3, 0);                               /* PWR_ON_2ND */
	bool ok = n2j_wait(n2j.spm, 0x16c, N2J_PWR_STA, 0) && n2j_wait(n2j.spm, 0x170, N2J_PWR_STA, 0);
	n2j_dump_soc("off");
	return ok ? N2J_OK : N2J_E_PWR_TIMEOUT;
prot:
	n2j_log("NORD2 JPG: off: bus protection not acknowledged (mm %#x ack %#x, mm2 %#x ack %#x): domain left powered\n",
	    ni_rd(n2j.infra, 0x2d0), ni_rd(n2j.infra, 0x2ec), ni_rd(n2j.infra, 0xdc8), ni_rd(n2j.infra, 0xdd8));
	return N2J_E_PROT_TIMEOUT;
}

/*
 * Autotest command 7. arg = step; for the encode r0 = width, r1 = height, r2 = quality code (0 width: 640x480, code 7 = Q90).
 * *out / *out_bytes: the JPEG after a good encode (the caller publishes it on the picture channel).
 */
static uint32_t
n2j_command(uint32_t step, const uint32_t *r, const uint8_t **out, uint32_t *out_bytes)
{
	*out = NULL; *out_bytes = 0;
	if (step > N2J_STEP_OFF) { return N2J_E_STEP; }
	if (!n2j_maps()) { n2j_log("NORD2 JPG: mapping failed\n"); return N2J_E_MAP; }
	n2j_log("NORD2 JPG: step %u\n", step);
	uint32_t res = N2J_OK;
	switch (step) {
	case N2J_STEP_STATUS: n2j_dump_soc("status"); n2j_dump_block("status"); break;
	case N2J_STEP_POWER: n2j_dump_soc("before"); res = n2j_power(); break;
	case N2J_STEP_BUS: res = n2j_bus(); break;
	case N2J_STEP_ENCODE: {
		uint32_t w = r[0] ? r[0] : 640, h = r[0] ? r[1] : 480, q = r[0] ? r[2] : 7;
		res = n2j_encode(w, h, q, &n2j.out_bytes);
		if (res == N2J_OK) { *out = (const uint8_t *)n2j.buf + N2J_HALF; *out_bytes = n2j.out_bytes; }
		break;
	}
	default: res = n2j_off(); break;
	}
	n2j_log("NORD2 JPG: step %u result %u\n", step, res);
	return res;
}
