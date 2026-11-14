/* SPDX-License-Identifier: MIT */
/*
 * t_jpegdec.c -- host test of patch 0315 (RWJD_JOB, the hardware JPEG decoder experiment path of nord2_jpegdev.inc).
 * K01's whole t_jpegdev.c runs first (encoder regression on the patched file, -Dmain=k01_main), with its model extended
 * (N2JD_DEC_MODEL) by the jpgdec core 0 window, vencsys bit 12, larb7 ports 11/12 and the 84 MiB decoder claim.
 * The decoder model below is a stand-in (it does not decode): on the trigger (0x240) it checks that every DMA address
 * register is 0 or inside the decoder claim, that larb7 ports 11/12 bypass the IOMMU, that the bitstream starts with
 * FF D8 and is zero padded, then writes a pattern into the Y bank and raises 0x274 after a few polls.
 * Since 0316 every ioctl goes through t_jpegdev.c's h_sys_ioctl (XNU's ioctl(): kernel buffer, copyout only on success);
 * the result words of a failed job are checked in the kernel copy (K_DEC), the caller's struct must stay as it was.
 */
#define N2JD_DEC_MODEL 1
#define main k01_main
#include "t_jpegdev.c"
#undef main

static struct { unsigned polls; uint32_t result; bool armed, never; uint32_t trig_count; } HD;

static bool hd_addr_ok(uint32_t v) { return v == 0 || (v >= RWJD_PA && v < RWJD_LEGAL_END && !(v & 15u)); }

static void
hd_write(uint32_t o, uint32_t v)
{
	if (o == 0x274) { R(W_DEC, o) &= ~(v & 0x37u); return; }        /* W1C */
	R(W_DEC, o) = v;
	if (o != 0x240) { return; }
	HD.trig_count++;
	static const uint16_t addr[] = { 0x140, 0x144, 0x148, 0x14c, 0x150, 0x154, 0x200, 0x248, 0x31c, 0x338, 0x36c, 0x370, 0x374 };
	for (unsigned i = 0; i < sizeof(addr) / sizeof(addr[0]); i++) {
		if (!hd_addr_ok(R(W_DEC, addr[i]))) { viol("decoder DMA address outside the decoder claim", R(W_DEC, addr[i])); }
	}
	for (uint32_t p = 11; p <= 12; p++) { if (R(W_LARB, 0x380 + 4 * p) & 0xff01u) { viol("trigger with larb7 port translating", p); } }
	uint32_t src = R(W_DEC, 0x200), size = R(W_DEC, 0x24c);
	if (src) {
		uint8_t *b = dram + (src - RWJD_PA);
		if (b[0] != 0xff || b[1] != 0xd8) { viol("bitstream without SOI", src); }
		for (uint32_t i = size; i < size + 16u && size; i++) { if (b[i]) { viol("bitstream padding not zero", i); break; } }
	}
	uint32_t y = R(W_DEC, 0x14c) ? R(W_DEC, 0x14c) : R(W_DEC, 0x140), stride = R(W_DEC, 0x158);
	if (y && stride) {
		for (uint32_t i = 0; i < stride * 16u; i++) { dram[y - RWJD_PA + i] = (uint8_t)(i * 7u + 3u); }
	}
	HD.armed = true;
}

static void
hd_read(uint32_t o)
{
	if (o == 0x274 && HD.armed && !HD.never) {
		if (HD.polls) { HD.polls--; }
		else { R(W_DEC, 0x274) |= HD.result; R(W_DEC, 0x340) = 1234; HD.armed = false; }
	}
}

/* K01 leaves the driver in K02-shared mode (its section N): the decoder runs the same way as on the phone */
static void dec_reset(void) { model_reset(); n2j.bus_ready = false; n2jd.powered = 0; h_ven_refs = 0; memset(&HD, 0, sizeof(HD)); trace_reset(); }
static int h_dec(struct rwjd_job *d) { return h_sys_ioctl(h_dev(), RWJD_JOB, d); }
#define K_DEC (*(const struct rwjd_job *)(const void *)h_kret.b)

static int
w_dec_writes(void)
{
	return trace_count(W_DEC);
}

int
main(int argc, char **argv)
{
	dram = calloc(1, RWJD_BYTES);
	if (!dram) { printf("no memory\n"); return 2; }
	int k = k01_main(argc, argv);
	int f0 = fails, c0 = checks;
	struct rwj_info inf;
	CHECK(h_inf(&inf) == 0 && inf.reserved[0] == RWJD_REV && inf.caps == 1u);

	/* ---- probe (E0/E1): reads only, nothing written to the decoder ---- */
	dec_reset();
	R(W_DEC, 0x090) = 0x10; R(W_DEC, 0x31c) = 0xabc; R(W_DEC, 0x3fc) = 0x55;
	struct rwjd_reg pr[3] = { { 0x090, 0 }, { 0x31c, 0 }, { 0x3fc, 0 } };
	struct rwjd_job d; memset(&d, 0, sizeof(d));
	d.regs = (uint64_t)(uintptr_t)pr; d.nregs = 3; d.flags = RWJD_PROBE;
	CHECK(h_dec(&d) == 0 && d.out_status == 0);
	CHECK(pr[0].value == 0x10 && pr[1].value == 0xabc && pr[2].value == 0x55);
	CHECK(w_dec_writes() == 0 && HD.trig_count == 0);
	CHECK((R(W_CG, 0) & 0x1000u) == 0);                               /* jpgdec gated again */
	CHECK(!trace_has(W_LARB, 0x3ac, 0) && trace_count_val(W_LARB, 0x22c, 1) == 0);   /* ports 11/12 untouched by a probe */
	pr[1].off = 0x400; CHECK(h_dec(&d) == EINVAL); pr[1].off = 0x31e; CHECK(h_dec(&d) == EINVAL); pr[1].off = 0x31c;

	/* ---- validation: every refusal happens before any register access ---- */
	dec_reset();
	uint8_t jpg[1000]; memset(jpg, 0x42, sizeof(jpg)); jpg[0] = 0xff; jpg[1] = 0xd8; jpg[998] = 0xff; jpg[999] = 0xd9;
	uint8_t *out = malloc(1u << 20); CHECK(out != NULL); memset(out, 0xee, 1u << 20);
	struct rwjd_reg r[8] = {
		{ 0x090, 0 }, { 0x090, 0x10 }, { 0x200, RWJD_SRC_PA }, { 0x24c, 1024 }, { 0x14c, RWJD_OUT_PA },
		{ 0x158, 64 }, { 0x274, 0x37 }, { 0x240, 0 },
	};
	struct rwjd_job g; memset(&g, 0, sizeof(g));
	g.src = (uint64_t)(uintptr_t)jpg; g.src_bytes = sizeof(jpg); g.regs = (uint64_t)(uintptr_t)r; g.nregs = 8;
	g.dst = (uint64_t)(uintptr_t)out; g.dst_bytes = 4096; g.timeout_ms = 100;
	struct { uint32_t idx, off, val; int err; } bad[] = {
		{ 4, 0x168, 0, EPERM },                           /* not in the allow-list */
		{ 4, 0x340, 0, EPERM },                           /* progress counter is read-only */
		{ 4, 0x380, 0, EINVAL }, { 4, 0x14e, 0, EINVAL },
		{ 2, 0x200, 0xd8000000u, EFAULT },                /* the encoder's claim */
		{ 2, 0x200, RWJD_PA - 16u, EFAULT },
		{ 4, 0x14c, RWJD_LEGAL_END, EFAULT },             /* the guard */
		{ 4, 0x14c, RWJD_OUT_PA + 4u, EFAULT },           /* not 16-aligned */
		{ 4, 0x374, 0xa0000000u, EFAULT },                /* the vcodec pool */
		{ 4, 0x31c, 0x40000000u, EFAULT },
		{ 4, 0x330, 0x40000000u, EFAULT },                /* an [I] scalar slot holding an address-like value */
		{ 4, 0x17c, 0x01000000u, EFAULT },
	};
	for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		struct rwjd_reg save = r[bad[i].idx];
		r[bad[i].idx].off = bad[i].off; r[bad[i].idx].value = bad[i].val;
		int e = h_dec(&g);
		if (e != bad[i].err) { printf("FAIL bad[%u] off %#x val %#x: %d\n", i, bad[i].off, bad[i].val, e); }
		CHECK(e == bad[i].err);
		r[bad[i].idx] = save;
	}
	struct rwjd_job b2;
	b2 = g; b2.nregs = 0; CHECK(h_dec(&b2) == EINVAL);
	b2 = g; b2.nregs = RWJD_REGS_MAX + 1u; CHECK(h_dec(&b2) == EINVAL);
	b2 = g; b2.timeout_ms = RWJD_TIMEOUT_MAX_MS + 1u; CHECK(h_dec(&b2) == EINVAL);
	b2 = g; b2.flags = 2; CHECK(h_dec(&b2) == EINVAL);
	b2 = g; b2.src_bytes = RWJD_SRC_MAX; CHECK(h_dec(&b2) == EINVAL);
	b2 = g; b2.src_bytes = 3; CHECK(h_dec(&b2) == EINVAL);
	b2 = g; b2.src = 0; CHECK(h_dec(&b2) == EINVAL);
	b2 = g; b2.dst_bytes = RWJD_OUT_MAX + 1u; CHECK(h_dec(&b2) == EINVAL);
	b2 = g; b2.regs = 0; CHECK(h_dec(&b2) == EINVAL);
	CHECK(trn == 0 && HD.trig_count == 0);

	/* ---- a decode: words in order, trigger, EOF, reset, gate off, planes copied out ---- */
	dec_reset(); memset(dram, 0x99, RWJD_BYTES);
	HD.result = 1; HD.polls = 3;
	CHECK(h_inf(&inf) == 0); uint32_t done0 = inf.jobs_done;
	CHECK(h_dec(&g) == 0 && g.out_status == 0 && g.out_irq == 1 && g.out_mcu == 1234);
	CHECK(HD.trig_count == 1);
	int iw = trace_idx(W_DEC, 0x090, 0), it = trace_idx(W_DEC, 0x240, 0);
	CHECK(iw >= 0 && it > iw && trace_idx(W_DEC, 0x14c, RWJD_OUT_PA) < it);
	uint32_t last = 0; CHECK(trace_last(W_DEC, 0x090, &last) && last == 0x10);   /* post-job hard reset */
	CHECK(trace_count_val(W_DEC, 0x274, 0x37) == 2);                              /* the list's clear + the post-job clear */
	CHECK((R(W_CG, 0) & 0x1000u) == 0);
	CHECK(R(W_LARB, 0x22c) == 1 && R(W_LARB, 0x230) == 4);
	CHECK(memcmp(dram, jpg, sizeof(jpg)) == 0);
	bool pad0 = true; for (uint32_t i = sizeof(jpg); i < 1152u; i++) { if (dram[i]) { pad0 = false; } } CHECK(pad0);
	bool idx0 = true; for (uint32_t i = 0; i < 0x10000u; i++) { if (dram[RWJD_IDX_PA - RWJD_PA + i]) { idx0 = false; break; } } CHECK(idx0);
	bool pix = true; for (uint32_t i = 0; i < 64u * 16u; i++) { if (out[i] != (uint8_t)(i * 7u + 3u)) { pix = false; break; } } CHECK(pix);
	CHECK(out[4096] == 0xee);                                                     /* exactly dst_bytes copied */
	CHECK(h_inf(&inf) == 0 && inf.jobs_done == done0 + 1u && inf.powered == 1);

	/* ---- decoder error bits -> RWJ_E_STALL; timeout -> RWJ_E_TIMEOUT; both reset and gate ---- */
	trace_reset(); HD.result = 0x04; HD.polls = 1;
	CHECK(h_dec(&g) == EIO && K_DEC.out_status == RWJ_E_STALL && K_DEC.out_irq == 0x04);
	CHECK(g.out_status == 0 && g.out_irq == 1 && g.out_mcu == 1234);              /* 0316: still the last good job's words */
	CHECK(trace_last(W_DEC, 0x090, &last) && last == 0x10 && (R(W_CG, 0) & 0x1000u) == 0);
	trace_reset(); HD.never = true; g.timeout_ms = 3;
	CHECK(h_dec(&g) == EIO && K_DEC.out_status == RWJ_E_TIMEOUT && K_DEC.out_hw_us >= 3000u && g.out_status == 0);
	CHECK(trace_last(W_DEC, 0x090, &last) && last == 0x10 && (R(W_CG, 0) & 0x1000u) == 0);
	HD.never = false; HD.armed = false; g.timeout_ms = 100;

	/* ---- an encode after decodes still works (shared lock / power / larb) ---- */
	struct pic p = pic_make(64, 64, RWJ_FMT_NV12, 64, 0, 90);
	CHECK(h_enc(&p.j) == 0 && p.j.out_status == 0 && p.j.out_bytes > 4u);
	pic_free(&p);
	/* a copyout fault of the planes is EFAULT, booked as failed */
	HD.result = 1; HD.polls = 0;
	h_fault_out(out + 100); CHECK(h_dec(&g) == EFAULT); h_fault_clear();
	/* 0316: the struct itself: a fault on sys_ioctl's copyin is EFAULT before anything runs, on its copyout after the job */
	unsigned t0 = HD.trig_count;
	h_fault_in((uint8_t *)(uintptr_t)&g); CHECK(h_dec(&g) == EFAULT && HD.trig_count == t0); h_fault_clear();
	h_fault_out((uint8_t *)(uintptr_t)&g); CHECK(h_dec(&g) == EFAULT && HD.trig_count == t0 + 1u && K_DEC.out_status == 0); h_fault_clear();
	free(out);

	if (violations) { printf("%d model violations\n", violations); }
	printf("0315 checks: %d/%d\n", (checks - c0) - (fails - f0), checks - c0);
	printf("N2TEST JPEGDEC %s %d/%d (violations %d)\n", k || fails || violations ? "FAIL" : "PASS", checks - fails, checks, violations);
	free(dram);
	return k || fails || violations ? 1 : 0;
}
