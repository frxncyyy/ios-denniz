/* SPDX-License-Identifier: MIT */
/*
 * t_vendor.c -- DEC03: differential test of the VLD/AVC_VLD model against the phone's own decoder
 * library running under the AArch32 interpreter.
 *
 * The vendor library is read at run time from gpu/build/cam-vendor-20261109/vfs (never copied):
 * the test loads libvcodecdrv.so, supplies vdec_hal_read_avc_vld as a host shim that asks the model
 * for the AVC_VLD word, and calls the vendor's own syntax readers:
 *   vdec_hal_h264_uecodenum / secodenum / getrealshtbitstream / getshtbitstream /
 *   getbitstreamflg / trailingbits / vldshiftbits / shift_startcode
 *   H264_HAL_UeCodeNum / SeCodeNum / GetRealBitStream / GetBitStreamShift / GetBitStreamFlg
 * If those functions return the values that the H.264 specification gives for the same bytes, the
 * model's peek/advance/ue/se behaviour matches what the vendor expects.
 *
 * It also runs the vendor's software SPS parser (parseSPS) and vdec_com_h264_getinfofromsps on real
 * ffmpeg streams and compares the width/height to ffprobe's values, which are the numbers the
 * header-only START oracle will compare vsi.pic.pic_w/pic_h against.  vsi.dec.dpb_sz and the full
 * header-only START path need DEC01 and are not claimed here (see STATUS.md).
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "n2a32.h"
#include "n2elf.h"
#include "n2vld.h"

static int checks, fails;
static struct n2a32_mem *g_mem;
static struct n2elf_set *g_set;
static struct n2a32_cpu g_cpu;
static struct n2vld *g_vld;
static uint32_t g_hw;
static uint32_t g_stack;

#define CK(c) do { \
	checks++; \
	if (!(c)) { \
		fails++; \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
	} \
} while (0)

#define CKU(a, b) do { \
	uint32_t ck_a = (uint32_t)(a), ck_b = (uint32_t)(b); \
	checks++; \
	if (ck_a != ck_b) { \
		fails++; \
		fprintf(stderr, "FAIL %s:%d: %s = 0x%08x, want 0x%08x\n", __FILE__, __LINE__, #a, ck_a, ck_b); \
	} \
} while (0)

static void sh_log(struct n2a32_cpu *c)
{
	(void)c;
}

static void sh_stack_chk(struct n2a32_cpu *c)
{
	fprintf(stderr, "FAIL vendor stack protector fired\n");
	c->stop_request = 1;
}

/* The one hardware function the syntax readers use.  The guest's hw pointer and core index are
 * irrelevant to the model: it is the only VLD/AVC_VLD in the process. */
static void sh_avc_read(struct n2a32_cpu *c)
{
	uint32_t reg = n2shim_arg(c, 2);
	uint32_t val = 0;
	int rc = n2vld_hook(g_vld, N2VLD_WIN_VDEC_ALL, N2VLD_AVC_VLD_OFF + reg * 4u, 0, &val);

	n2shim_ret(c, rc == 1 ? val : 0xffffu);
}

static void sh_hevc_read(struct n2a32_cpu *c)
{
	uint32_t reg = n2shim_arg(c, 2);
	uint32_t val = 0;
	int rc = n2vld_hook(g_vld, N2VLD_WIN_VDEC_ALL, N2VLD_HEVC_VLD_OFF + reg * 4u, 0, &val);

	n2shim_ret(c, rc == 1 ? val : 0xffffu);
}

static const unsigned char zero4[4] = { 0, 0, 0, 0 };
static const struct n2shim_data data_local[] = {
	{ "vcodec_public_dbg_level", 4, zero4 },
	{ "vpud_log_level", 4, zero4 },
	{ "stderr", 4, NULL },
	{ NULL, 0, NULL }
};

static const struct n2shim_entry extra[] = {
	{ "__android_log_print", sh_log },
	{ "__stack_chk_fail", sh_stack_chk },
	{ "vdec_hal_read_avc_vld", sh_avc_read },
	{ "vdec_hal_read_hevc_vld", sh_hevc_read },
	{ NULL, NULL }
};

static int setup(const char *lib)
{
	struct n2elf_image *img;
	char err[160];
	unsigned unresolved = 0;

	g_mem = n2a32_mem_create();
	if (!g_mem)
		return -1;
	g_set = n2elf_set_create(g_mem);
	if (!g_set)
		return -1;
	n2elf_set_add_shims(g_set, n2shim_string);
	n2elf_set_add_shims(g_set, n2shim_heap);
	n2elf_set_add_shims(g_set, n2shim_thread);
	n2elf_set_add_shims(g_set, n2shim_stdio);
	n2elf_set_add_shims(g_set, n2shim_android);
	n2elf_set_add_shims(g_set, extra);
	n2elf_set_add_data(g_set, n2shim_data_all);
	n2elf_set_add_data(g_set, data_local);
	img = n2elf_load(g_set, lib, "libvcodecdrv.so", err);
	if (!img) {
		fprintf(stderr, "load %s: %s\n", lib, err);
		return -1;
	}
	if (n2elf_link(g_set, &unresolved, err) != 0) {
		fprintf(stderr, "link: %s\n", err);
		return -1;
	}
	n2a32_cpu_init(&g_cpu, g_mem);
	(void)n2elf_run_init(g_set, &g_cpu);
	g_stack = n2a32_mem_alloc(g_mem, 0x20000, 0x10);
	if (!g_stack)
		return -1;
	g_cpu.r[13] = g_stack + 0x1f000;
	g_hw = n2a32_mem_alloc(g_mem, 0x2000, 0x1000);
	if (!g_hw)
		return -1;
	memset(n2a32_ptr(g_mem, g_hw), 0, 0x2000);
	n2a32_st32(&g_cpu, g_hw + 0x7e0, g_hw);       /* H264_HAL_* sub-hw pointer */
	n2a32_st8(&g_cpu, g_hw + 0x7e8, 0);           /* core index */
	g_vld = n2vld_create(NULL);
	return g_vld ? 0 : -1;
}

static uint32_t vcall(const char *name, uint32_t *args, unsigned nargs)
{
	uint32_t fn = n2elf_sym(g_set, name);
	uint32_t r;

	if (!fn) {
		checks++;
		fails++;
		fprintf(stderr, "FAIL missing vendor symbol %s\n", name);
		return 0;
	}
	g_cpu.stop = N2A32_STOP_NONE;
	r = n2a32_call(&g_cpu, fn, args, nargs, NULL);
	checks++;
	if (g_cpu.stop != N2A32_STOP_RETURN) {
		fails++;
		fprintf(stderr, "FAIL %s stopped %d at 0x%08x\n", name, g_cpu.stop, g_cpu.stop_addr);
	}
	return r;
}

static void load(const void *b, uint32_t n)
{
	CK(n2vld_set_bytes(g_vld, b, n) == 0);
}

/* 32 bits at bit position `pos`, zero past the end: the AVC_VLD read(n) result. */
static uint32_t ref_peek32(const uint8_t *b, uint32_t bytes, uint64_t pos)
{
	uint32_t x = 0;
	unsigned i;

	for (i = 0; i < 32u; i++) {
		unsigned bit = 0;

		if ((pos + i) / 8u < bytes)
			bit = (b[(pos + i) / 8u] >> (7u - (unsigned)((pos + i) % 8u))) & 1u;
		x = (x << 1) | bit;
	}
	return x;
}

static void test_vendor_ue_se(void)
{
	static const uint8_t u[] = {
		0xa6, 0x42, 0x98, 0xe2, 0x04, 0x8a, 0x08, 0x04, 0x41, 0x00, 0x21, 0x02,
		0x00, 0x10, 0x40, 0xca, 0x01, 0x00, 0x00, 0x80, 0x80, 0x3e, 0x90
	};
	static const uint32_t want[] = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 15, 16, 31, 32, 63, 64, 100, 255, 256, 1000
	};
	static const uint8_t s[] = {
		0xa6, 0x42, 0x98, 0xe0, 0x3f, 0x80, 0x20, 0x20, 0x07, 0xd0, 0x00, 0x3e, 0x88
	};
	static const int32_t wants[] = { 0, 1, -1, 2, -2, 3, -3, 127, -128, 1000, -1000 };
	uint32_t args[3];
	unsigned i;

	load(u, sizeof u);
	args[0] = g_hw;
	args[1] = 0;
	for (i = 0; i < sizeof want / sizeof want[0]; i++)
		CKU(vcall("vdec_hal_h264_uecodenum", args, 2), want[i]);
	CKU(n2vld_bitpos(g_vld), 180);

	load(s, sizeof s);
	for (i = 0; i < sizeof wants / sizeof wants[0]; i++)
		CKU(vcall("vdec_hal_h264_secodenum", args, 2), (uint32_t)wants[i]);
	CKU(n2vld_bitpos(g_vld), 101);

	/* The exported HAL entry points the task names, over the same vectors. */
	load(u, sizeof u);
	args[0] = g_hw;
	args[1] = 0;
	for (i = 0; i < sizeof want / sizeof want[0]; i++)
		CKU(vcall("H264_HAL_UeCodeNum", args, 2), want[i]);
	load(s, sizeof s);
	for (i = 0; i < sizeof wants / sizeof wants[0]; i++)
		CKU(vcall("H264_HAL_SeCodeNum", args, 2), (uint32_t)wants[i]);

	/* vdec_hal_h264_uecodenum must refuse a zero peek word the way the vendor's code does. */
	load((const uint8_t[]){ 0, 0, 0, 0, 0, 0, 0, 0 }, 8);
	CKU(vcall("vdec_hal_h264_uecodenum", args, 2), 0xffffffffu);
}

static void test_vendor_bits(void)
{
	static const uint8_t d[] = { 0x12, 0x34, 0x56, 0x78, 0x9a };
	struct { unsigned n; uint32_t want; } cases[] = {
		{ 1, 0x0u }, { 4, 0x1u }, { 8, 0x12u }, { 9, 0x024u }, { 16, 0x1234u },
		{ 24, 0x123456u }, { 31, 0x091a2b3cu }, { 32, 0x12345678u }
	};
	uint32_t args[3];
	unsigned i;

	for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
		load(d, sizeof d);
		args[0] = g_hw;
		args[1] = 0;                          /* core */
		args[2] = cases[i].n;                 /* bits */
		CKU(vcall("vdec_hal_h264_getrealshtbitstream", args, 3), cases[i].want);
		CKU(n2vld_bitpos(g_vld), cases[i].n);
	}
	for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
		load(d, sizeof d);
		args[0] = g_hw;
		args[1] = cases[i].n;
		args[2] = 0;
		CKU(vcall("H264_HAL_GetRealBitStream", args, 3), cases[i].want);
		CKU(n2vld_bitpos(g_vld), cases[i].n);
	}
	for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
		load(d, sizeof d);
		args[0] = g_hw;
		args[1] = cases[i].n;
		args[2] = 0;
		/* GetBitStreamShift returns the peek before consuming. */
		CKU(vcall("H264_HAL_GetBitStreamShift", args, 3), 0x12345678u);
		CKU(n2vld_bitpos(g_vld), cases[i].n);
	}
	/* The exported thunks that return read_avc_vld(n) directly: shift n, then the next
	 * 32-bit window.  H264_HAL_ShiftGetBitStream/AVCVLDGetBitS take (hw, n, unused);
	 * the core index is in the hw+0x7e8 byte. */
	for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
		load(d, sizeof d);
		args[0] = g_hw;
		args[1] = cases[i].n;
		args[2] = 0;
		CKU(vcall("H264_HAL_ShiftGetBitStream", args, 3),
		    ref_peek32(d, sizeof d, cases[i].n));
		CKU(n2vld_bitpos(g_vld), cases[i].n);
		load(d, sizeof d);
		args[0] = g_hw;
		args[1] = cases[i].n;
		args[2] = 0;
		CKU(vcall("H264_HAL_AVCVLDGetBitS", args, 3),
		    ref_peek32(d, sizeof d, cases[i].n));
		CKU(n2vld_bitpos(g_vld), cases[i].n);
	}

	load(d, sizeof d);
	args[0] = g_hw;
	args[1] = 0;
	for (i = 0; i < 5; i++)
		CKU(vcall("vdec_hal_h264_getbitstreamflg", args, 2), (0x12u >> (7u - i)) & 1u);

	load(d, sizeof d);
	args[0] = g_hw;
	args[1] = 0;                                  /* core */
	args[2] = 3;                                  /* consume the top three bits: 000 */
	CKU(vcall("vdec_hal_h264_getrealshtbitstream", args, 3), 0);
	CKU(n2vld_bitpos(g_vld), 3);
	args[0] = g_hw;
	args[1] = 0;
	CKU(vcall("vdec_hal_h264_vldshiftbits", args, 2), 3);
	CKU(vcall("vdec_hal_h264_trailingbits", args, 2), 0x3456789au); /* read(5): post-shift window */
	CKU(n2vld_bitpos(g_vld), 8);                  /* 5 trailing bits consumed */

	load(d, sizeof d);
	args[0] = g_hw;
	args[1] = 0;
	args[2] = 13;
	CKU(vcall("vdec_hal_h264_getrealshtbitstream", args, 3), 0x0246u);
	args[0] = g_hw;
	args[1] = 0;
	CKU(vcall("vdec_hal_h264_vldshiftbits", args, 2), 13);
}

static void test_vendor_startcode(void)
{
	static const uint8_t nal[] = {
		0x00, 0x00, 0x01, 0x67, 0x4d, 0x40, 0x0d, 0xda, 0x05, 0x07, 0xec, 0x04,
		0x40, 0x00, 0x00, 0x03, 0x00, 0x40, 0x00, 0x00, 0x0c, 0x83, 0xc5, 0x0a,
		0xa8, 0x00
	};
	uint32_t args[3];

	load(nal, sizeof nal);
	args[0] = g_hw;
	args[1] = 0;
	CKU(vcall("vdec_hal_h264_shift_startcode", args, 2), 1);
	CKU(n2vld_bitpos(g_vld), 32);
	/* profile_idc, constraint flags and level_idc, then the vendor's ue reader */
	args[0] = g_hw;
	args[1] = 0;
	args[2] = 8;
	CKU(vcall("vdec_hal_h264_getrealshtbitstream", args, 3), 0x4du);
	CKU(vcall("vdec_hal_h264_getrealshtbitstream", args, 3), 0x40u);
	CKU(vcall("vdec_hal_h264_getrealshtbitstream", args, 3), 0x0du);
	args[0] = g_hw;
	args[1] = 0;
	CKU(vcall("vdec_hal_h264_uecodenum", args, 2), 0);   /* seq_parameter_set_id */
	CKU(vcall("vdec_hal_h264_uecodenum", args, 2), 0);   /* log2_max_frame_num_minus4 */
	CKU(vcall("vdec_hal_h264_uecodenum", args, 2), 2);   /* pic_order_cnt_type */

	/* not a start code: the function still consumes 32 bits and answers 0 */
	load((const uint8_t[]){ 0x12, 0x34, 0x56, 0x78, 0x9a }, 5);
	CKU(vcall("vdec_hal_h264_shift_startcode", args, 2), 0);
	CKU(n2vld_bitpos(g_vld), 32);
}

static int read_file(const char *path, uint8_t **out, uint32_t *len)
{
	FILE *f = fopen(path, "rb");
	long n;
	uint8_t *b;

	if (!f)
		return -1;
	if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return -1;
	}
	b = malloc((size_t)n + 1u);
	if (!b) {
		fclose(f);
		return -1;
	}
	if (fread(b, 1, (size_t)n, f) != (size_t)n) {
		free(b);
		fclose(f);
		return -1;
	}
	fclose(f);
	*out = b;
	*len = (uint32_t)n;
	return 0;
}

/* First Annex-B NAL: returns a pointer to its NAL header byte and its length. */
static int first_nal(const uint8_t *b, uint32_t n, const uint8_t **nal, uint32_t *len)
{
	uint32_t i = 0;

	while (i + 3u < n) {
		if (b[i] == 0 && b[i + 1] == 0 && b[i + 2] == 1) {
			uint32_t s = i + 3u, e = s;
			while (e + 3u <= n) {
				if (b[e] == 0 && b[e + 1] == 0 && b[e + 2] == 1)
					break;
				e++;
			}
			if (e + 3u > n)
				e = n;
			*nal = b + s;
			*len = e - s;
			return 0;
		}
		i++;
	}
	return -1;
}

/* First Annex-B NAL of the given nal_unit_type; returns its header byte and length. */
static int find_nal(const uint8_t *b, uint32_t n, unsigned type, const uint8_t **nal, uint32_t *len)
{
	uint32_t i = 0;

	while (i + 3u < n) {
		if (b[i] == 0 && b[i + 1] == 0 && b[i + 2] == 1) {
			uint32_t s = i + 3u, e = s;

			while (e + 3u <= n) {
				if (b[e] == 0 && b[e + 1] == 0 && b[e + 2] == 1)
					break;
				e++;
			}
			if (e + 3u > n)
				e = n;
			if (s < e && (b[s] & 0x1fu) == type) {
				*nal = b + s;
				*len = e - s;
				return 0;
			}
			i = e > s ? e : s;
			continue;
		}
		i++;
	}
	return -1;
}

static void test_vendor_slice(const char *path)
{
	uint8_t *b = NULL;
	uint32_t n = 0;
	const uint8_t *nal = NULL;
	uint32_t nal_len = 0;
	uint32_t args[3];

	if (read_file(path, &b, &n) != 0) {
		fprintf(stderr, "FAIL read %s\n", path);
		checks++;
		fails++;
		return;
	}
	if (find_nal(b, n, 5, &nal, &nal_len) != 0 && find_nal(b, n, 1, &nal, &nal_len) != 0) {
		fprintf(stderr, "FAIL no slice NAL in %s\n", path);
		checks++;
		fails++;
		free(b);
		return;
	}
	load(nal, nal_len);
	args[0] = g_hw;
	args[1] = 0;
	args[2] = 8;
	/* the NAL header byte, then first_mb_in_slice (ue(v); 0 for the first slice) */
	CKU(vcall("vdec_hal_h264_getrealshtbitstream", args, 3), nal[0]);
	args[0] = g_hw;
	args[1] = 0;
	CKU(vcall("vdec_hal_h264_uecodenum", args, 2), 0);
	CK(n2vld_bitpos(g_vld) > 8);
	free(b);
}

static void test_vendor_hevc(void)
{
	static const uint8_t u[] = {
		0xa6, 0x42, 0x98, 0xe2, 0x04, 0x8a, 0x08, 0x04, 0x41, 0x00, 0x21, 0x02,
		0x00, 0x10, 0x40, 0xca, 0x01, 0x00, 0x00, 0x80, 0x80, 0x3e, 0x90
	};
	static const uint32_t want[] = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 15, 16, 31, 32, 63, 64, 100, 255, 256, 1000
	};
	static const uint8_t s[] = {
		0xa6, 0x42, 0x98, 0xe0, 0x3f, 0x80, 0x20, 0x20, 0x07, 0xd0, 0x00, 0x3e, 0x88
	};
	static const int32_t wants[] = { 0, 1, -1, 2, -2, 3, -3, 127, -128, 1000, -1000 };
	static const uint8_t d[] = { 0x12, 0x34, 0x56, 0x78, 0x9a };
	uint32_t args[4];
	unsigned i;

	load(u, sizeof u);
	args[0] = 0;                                  /* the utility functions ignore the first word */
	args[1] = g_hw;                               /* hw */
	args[2] = 0;                                  /* core */
	for (i = 0; i < sizeof want / sizeof want[0]; i++)
		CKU(vcall("vdec_hal_h265_ue_code_num", args, 3), want[i]);
	CKU(n2vld_bitpos(g_vld), 180);

	load(s, sizeof s);
	for (i = 0; i < sizeof wants / sizeof wants[0]; i++)
		CKU(vcall("vdec_hal_h265_se_code_num", args, 3), (uint32_t)wants[i]);
	CKU(n2vld_bitpos(g_vld), 101);

	for (i = 0; i < 8; i++) {
		static const unsigned nbits[] = { 1, 4, 8, 9, 16, 24, 31, 32 };
		static const uint32_t vals[] = { 0x0u, 0x1u, 0x12u, 0x024u, 0x1234u,
						  0x123456u, 0x091a2b3cu, 0x12345678u };
		load(d, sizeof d);
		args[0] = 0;
		args[1] = g_hw;
		args[2] = 0;
		args[3] = nbits[i];
		CKU(vcall("vdec_hal_h265_get_real_bit_stream", args, 4), vals[i]);
		CKU(n2vld_bitpos(g_vld), nbits[i]);
	}
	load(d, sizeof d);
	args[0] = 0;
	args[1] = g_hw;
	args[2] = 0;
	for (i = 0; i < 5; i++)
		CKU(vcall("vdec_hal_h265_get_bit_stream_flg", args, 3), (0x12u >> (7u - i)) & 1u);

	load(d, sizeof d);
	args[0] = 0;
	args[1] = g_hw;
	args[2] = 0;
	args[3] = 3;
	CKU(vcall("vdec_hal_h265_get_real_bit_stream", args, 4), 0);
	CKU(n2vld_bitpos(g_vld), 3);
	args[0] = 0;
	args[1] = g_hw;
	args[2] = 0;
	CKU(vcall("vdec_hal_h265_trailing_bits", args, 3), 0x3456789au);
	CKU(n2vld_bitpos(g_vld), 8);
}

static void test_vendor_sps(const char *expect_path)
{
	FILE *f = fopen(expect_path, "r");
	char path[512];
	unsigned want_w, want_h;

	if (!f) {
		fprintf(stderr, "FAIL cannot open %s\n", expect_path);
		checks++;
		fails++;
		return;
	}
	while (fscanf(f, "%511s %u %u", path, &want_w, &want_h) == 3) {
		uint8_t *b = NULL;
		uint32_t n = 0;
		const uint8_t *nal = NULL;
		uint32_t nal_len = 0;
		uint32_t sps_g, ctx_g, args[3], ret;

		if (read_file(path, &b, &n) != 0) {
			fprintf(stderr, "FAIL read %s\n", path);
			checks++;
			fails++;
			continue;
		}
		if (first_nal(b, n, &nal, &nal_len) != 0 || nal_len < 5u) {
			fprintf(stderr, "FAIL no NAL in %s\n", path);
			checks++;
			fails++;
			free(b);
			continue;
		}
		sps_g = n2a32_mem_alloc(g_mem, 0x2000, 0x1000);
		ctx_g = n2a32_mem_alloc(g_mem, 0x6000, 0x1000);
		memset(n2a32_ptr(g_mem, sps_g), 0, 0x2000);
		memset(n2a32_ptr(g_mem, ctx_g), 0, 0x6000);
		args[0] = sps_g;
		/* The guest copy of the NAL: allocate, copy, then rebuild the argument. */
		{
			uint32_t g = n2a32_mem_alloc(g_mem, (nal_len + 0xfffu) & ~0xfffu, 0x1000);
			memcpy(n2a32_ptr(g_mem, g), nal, nal_len);
			args[1] = g;
		}
		{
			uint32_t fn = n2elf_sym(g_set, "parseSPS");
			args[2] = nal_len;
			g_cpu.stop = N2A32_STOP_NONE;
			ret = n2a32_call(&g_cpu, fn, args, 3, NULL);
			CKU(ret, 1);
			CK(g_cpu.stop == N2A32_STOP_RETURN);
		}
		{
			uint32_t a2[2] = { ctx_g, sps_g };
			uint32_t fn = n2elf_sym(g_set, "vdec_com_h264_getinfofromsps");
			g_cpu.stop = N2A32_STOP_NONE;
			(void)n2a32_call(&g_cpu, fn, a2, 2, NULL);
			CK(g_cpu.stop == N2A32_STOP_RETURN);
		}
		{
			uint32_t *q = n2a32_ptr(g_mem, ctx_g + 0x4434);
			uint32_t *sps = n2a32_ptr(g_mem, sps_g);
			uint8_t *sb = (uint8_t *)sps;
			uint32_t mbs_w = sps[0x544 / 4] + 1u;
			uint32_t mbs_h = sps[0x548 / 4] + 1u;
			uint32_t coded_w = mbs_w * 16u;
			uint32_t coded_h = mbs_h * 16u * (2u - sb[0x54c]);
			uint32_t crop = sb[0x54f] ? 1u : 0u;
			uint32_t profile = sps[1];
			uint32_t chroma = sb[0x540];            /* chroma_format_idc, high profiles only */
			uint32_t high = (profile == 100u || profile == 110u || profile == 122u || profile == 244u ||
					 profile == 44u || profile == 83u || profile == 86u || profile == 118u ||
					 profile == 128u || profile == 138u || profile == 139u || profile == 134u ||
					 profile == 135u);
			if (!high)
				chroma = 1u;                  /* H.264 7.3.2.1.1: inferred 4:2:0 */
			uint32_t unit_x = (chroma == 1u || chroma == 2u) ? 2u : 1u;
			uint32_t unit_y = (chroma == 1u) ? 2u * (2u - sb[0x54c]) :
					  (chroma == 2u ? (2u - sb[0x54c]) : (2u - sb[0x54c]));
			uint32_t disp_w = coded_w, disp_h = coded_h;

			/* vdec_com_h264_getinfofromsps writes the coded size at ctx+0x4434. */
			CKU(q[0], coded_w);
			CKU(q[1], coded_h);
			/* Vdec_Drv_H264_open applies the SPS cropping rectangle (H.264 7.4.2.1.1);
			 * the SPS fields are already in pixel units, as parseSPS stores them. */
			if (crop) {
				disp_w = coded_w - unit_x * (sps[0x550 / 4] + sps[0x554 / 4]);
				disp_h = coded_h - unit_y * (sps[0x558 / 4] + sps[0x55c / 4]);
			}
			CKU(disp_w, want_w);
			CKU(disp_h, want_h);
			CK(crop == (want_w != coded_w || want_h != coded_h) || !crop);
		}
		free(b);
	}
	fclose(f);
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: %s libvcodecdrv.so expect.txt\n", argv[0]);
		return 2;
	}
	if (setup(argv[1]) != 0)
		return 2;

	test_vendor_ue_se();
	test_vendor_bits();
	test_vendor_startcode();
	test_vendor_hevc();
	test_vendor_sps(argv[2]);
	{
		FILE *f = fopen(argv[2], "r");
		char path[512];

		if (!f) {
			fprintf(stderr, "FAIL cannot open %s for the slice test\n", argv[2]);
			checks++;
			fails++;
		} else {
			unsigned w, h;

			while (fscanf(f, "%511s %u %u", path, &w, &h) == 3)
				test_vendor_slice(path);
			fclose(f);
		}
	}

	printf("N2VLDV checks %d pass %d\n", checks, checks - fails);
	return fails ? 1 : 0;
}
