/* SPDX-License-Identifier: MIT */
/*
 * n2jdec_words.c -- the 21 register words of the MT6893 hybrid JPEG decoder.
 *
 * Every "known" word is the open 2015 decoder's formula (mtk_jpeg_dec_hw.c) applied to a baseline frame
 * header, rewritten here. Every "hypothesis" word is produced by n2jdec_unknown_words() only; that
 * function is the single place where a guess lives, and REGISTERS.md justifies each one.
 *
 * Nothing here was measured on the phone. See jdec/STATUS.md for the verdict.
 */
#include "n2jdec.h"

static uint32_t
n2j_align(uint32_t v, uint32_t a)
{
	return (v + a - 1u) & ~(a - 1u);
}

/* mtk_jpeg_dec_hw.c:364-378, the DU_NUM sampling word. */
static uint32_t
n2j_du_num(const n2jdec_jpeg *j)
{
	uint32_t y, u, v;

	if (j->ncomp == 1)
		return 0;
	y = ((((uint32_t)j->comp[0].h - 1u) & 3u) << 2) | (((uint32_t)j->comp[0].v - 1u) & 3u);
	u = ((((uint32_t)j->comp[1].h - 1u) & 3u) << 2) | (((uint32_t)j->comp[1].v - 1u) & 3u);
	v = ((((uint32_t)j->comp[2].h - 1u) & 3u) << 2) | (((uint32_t)j->comp[2].v - 1u) & 3u);
	return (y << 8) | (u << 4) | v;
}

static const n2jdec_word_info g_words[N2JDEC_WORDS] = {
	{ 0, 0x090, N2JDEC_HYPOTHESIS, "JPGDEC_REG_RESET write 1 (soft)",
	  "data[0] is written first to 0x090. The open driver writes 0x00 then 0x10 there "
	  "(mtk_jpeg_dec_hw.c:223-240); jpeg_drv.c:1699-1700 repeats 0x00/0x10 after every job. "
	  "Value 0x00 is the proven first value; that the vendor's data[0] is also 0 is [I]." },
	{ 1, 0x090, N2JDEC_HYPOTHESIS, "JPGDEC_REG_RESET write 2 (hard)",
	  "data[1] follows data[0]. 0x10 is bit 4, BIT_HARD_RST_SHIFT (jpeg_drv_reg.h:97), the second "
	  "value of the proven reset sequence. Exact vendor value [I]." },
	{ 2, 0x0FC, N2JDEC_KNOWN, "JPGDEC_REG_DU_NUM",
	  "(mask(h0)<<2|mask(v0))<<8 | (mask(h1)<<2|mask(v1))<<4 | (mask(h2)<<2|mask(v2)), "
	  "mask(x)=((x)-1)&3; 0 for one component (mtk_jpeg_dec_hw.c:364-378)." },
	{ 3, 0x14C, N2JDEC_KNOWN, "JPGDEC_REG_DEST_ADDR1_Y",
	  "Output Y address, 16-byte aligned (mtk_jpeg_dec_reg.h:28; alignment check "
	  "mtk_jpeg_dec_hw.c:252-261). The open driver uses bank 0 (0x140) and writes 0 here "
	  "(mtk_jpeg_dec_hw.c:409-411); the hybrid writes its output addresses here instead [I use]." },
	{ 4, 0x150, N2JDEC_KNOWN, "JPGDEC_REG_DEST_ADDR1_U",
	  "Output Cb address, 16-byte aligned (mtk_jpeg_dec_reg.h:29). Same bank note as slot 3." },
	{ 5, 0x154, N2JDEC_KNOWN, "JPGDEC_REG_DEST_ADDR1_V",
	  "Output Cr address, 16-byte aligned (mtk_jpeg_dec_reg.h:30). Same bank note as slot 3." },
	{ 6, 0x17C, N2JDEC_HYPOTHESIS, "JPGDEC_REG_OPERATION_MODE",
	  "Bits 1:0. The open driver writes 0 = full-frame mode (mtk_jpeg_dec_hw.c:290-293,386). The "
	  "hybrid has progress registers 0x340/0x348 and index tables, which suggests a row/pause mode, "
	  "so the value the vendor passes is [I]. Default 0." },
	{ 7, 0x200, N2JDEC_KNOWN, "JPGDEC_REG_FILE_ADDR",
	  "Physical address of the JPEG bitstream, 16-byte aligned (mtk_jpeg_dec_hw.c:307-313). The "
	  "hybrid turns an ION fd into this with jpg_translate_fd(data[7],0,236) (jpeg_drv_dec.c:61), "
	  "port 236 = larb7 port 12." },
	{ 8, 0x20C, N2JDEC_KNOWN, "JPGDEC_REG_COMP_ID",
	  "(id0&0xff)<<24 | (id1&0xff)<<16 | (id2&0xff)<<8 (mtk_jpeg_dec_hw.c:315-323)." },
	{ 9, 0x210, N2JDEC_KNOWN, "JPGDEC_REG_TOTAL_MCU_NUM",
	  "total_mcu - 1 (mtk_jpeg_dec_hw.c:325-328); total_mcu = ceil(w/(8*Hmax)) * ceil(h/(8*Vmax))." },
	{ 10, 0x224, N2JDEC_KNOWN, "JPGDEC_REG_COMP0_DATA_UNIT_NUM",
	  "unit_num - 1 with unit_num = ceil(w/8)*ceil(h/8) (mtk_jpeg_dec_hw.c:83,330-333): the visible "
	  "luma data units, not the MCU-padded count." },
	{ 11, 0x23C, N2JDEC_KNOWN, "JPGDEC_REG_DU_CTRL",
	  "membership | (gmc<<30) | (isgray<<31); membership has 3 bits per block slot (4+component, 7 "
	  "= unused/unused blocks), gmc=1, gray sets 0x3FFFFFFC first (mtk_jpeg_dec_hw.c:94-108,335-342)." },
	{ 12, 0x24C, N2JDEC_KNOWN, "JPGDEC_REG_FILE_TOTAL_SIZE",
	  "Bitstream size, multiple of 128 (mtk_jpeg_dec_hw.c:307-313 checks the alignment). We pass "
	  "align(file_bytes,128); the 2015 V4L2 driver passes align(V4L2 buffer size,128) "
	  "(mtk_jpeg_core.c:1331-1334). Which one the hybrid wants is [I]." },
	{ 13, 0x270, N2JDEC_KNOWN, "JPGDEC_REG_QT_ID",
	  "(tq0&0xf)<<8 | (tq1&0xf)<<4 | (tq2&0xf) (mtk_jpeg_dec_hw.c:344-351)." },
	{ 14, 0x31C, N2JDEC_HYPOTHESIS, "unknown 0x31C",
	  "Not in the open register header. Candidates: (a) Huffman/working-table base (the vendor "
	  "prints 'Huffman Data ION va/pa/size'; 0x320 is ST_HUFFMAN_EN in the 2015 map); (b) BRZ_FACTOR "
	  "for 4:4:4/4:2:2V (0x0F8 in 2015, which the hybrid never writes); (c) FILE_BRP (0x248 in 2015, "
	  "also never written by the hybrid). Default 0." },
	{ 15, 0x330, N2JDEC_HYPOTHESIS, "unknown 0x330",
	  "Candidates: STRIDE_Y (the 2015 map's 0x158/0x160; the hybrid writes no stride register at all, "
	  "so it must get a stride or a width), or the picture width. Default: the open driver's "
	  "stride_y = align(mcu_w*8*h0,32) (mtk_jpeg_dec_hw.c:157-158)." },
	{ 16, 0x334, N2JDEC_HYPOTHESIS, "unknown 0x334",
	  "Candidates: STRIDE_UV (0x15C/0x164 in 2015), or the picture height. Default: "
	  "align((mcu_w*8*h1)>>uv_brz_w,16) (mtk_jpeg_dec_hw.c:157-158)." },
	{ 17, 0x33C, N2JDEC_HYPOTHESIS, "unknown 0x33C",
	  "Sits just below the progress register 0x340: candidates are the first MCU index of this run "
	  "(resume), or PAUSE_MCU_NUM (0x170 in the 2015 map, which the hybrid does not write). "
	  "Default 0 (start at MCU 0)." },
	{ 18, 0x344, N2JDEC_HYPOTHESIS, "unknown 0x344",
	  "Sits between progress 0x340 and status 0x348: candidates are the last MCU index of this run, "
	  "or the MCU count of the run. Default total_mcu-1 (whole frame)." },
	{ 19, 0x240, N2JDEC_HYPOTHESIS, "JPGDEC_REG_TRIG",
	  "The hybrid writes data[19] here last (jpeg_drv_dec.c:82). The 2015 driver writes 0 to start "
	  "(mtk_jpeg_dec_hw.c:218-221). Default 0; whether the hybrid needs another trigger value is [I]." },
	{ 20, 0x000, N2JDEC_KNOWN, "index buffer size (data[20], not a register)",
	  "Size in bytes of the kernel-allocated, 128-byte-aligned, zeroed buffer (jpeg_drv_dec.c:40-52); "
	  "returned to user space as an fd. It is also the offset between the three index pointers "
	  "0x36C/0x370/0x374 (jpeg_drv_dec.c:74-79)." },
};

static const n2jdec_word_info g_index[N2JDEC_INDEX_REGS] = {
	{ 21, 0x338, N2JDEC_HYPOTHESIS, "index table base",
	  "Written with index_iova (jpeg_drv_dec.c:72-73), 128-byte aligned. Hypothesis: base of the "
	  "huffman/index area for the hybrid engine." },
	{ 22, 0x36C, N2JDEC_HYPOTHESIS, "index table 0 / core 0",
	  "Written with index_iova (jpeg_drv_dec.c:74-75). Hypothesis: pointer to table 0." },
	{ 23, 0x370, N2JDEC_HYPOTHESIS, "index table 1 / core 1",
	  "Written with index_iova + data[20] (jpeg_drv_dec.c:76-77). This is past the end of the "
	  "kernel allocation unless data[20] is the size of one table; our kernel allocates 3*size." },
	{ 24, 0x374, N2JDEC_HYPOTHESIS, "index table 2 / core 2",
	  "Written with index_iova + 2*data[20] (jpeg_drv_dec.c:78-79); same out-of-range caveat." },
};

const n2jdec_word_info *
n2jdec_word_table(unsigned *count)
{
	if (count != NULL)
		*count = N2JDEC_WORDS;
	return g_words;
}

const n2jdec_word_info *
n2jdec_index_table(unsigned *count)
{
	if (count != NULL)
		*count = N2JDEC_INDEX_REGS;
	return g_index;
}

static int
n2j_check_addr(const n2jdec_jpeg *j, const n2jdec_addr *a)
{
	if (a == NULL)
		return N2JDEC_E_ARG;
	if ((a->src_pa & 15u) != 0)
		return N2JDEC_E_ADDR;
	if (((a->out_y | a->out_u | a->out_v) & 15u) != 0)
		return N2JDEC_E_ADDR;
	if (j->ncomp > 1 && (a->out_y == 0 || a->out_u == 0 || a->out_v == 0))
		return N2JDEC_E_ADDR;
	if ((a->index_pa & 127u) != 0)
		return N2JDEC_E_ADDR;
	if ((a->index_size & 127u) != 0 || a->index_size < 128u)
		return N2JDEC_E_ADDR;

	/* 32-bit hardware addresses: no wrap. */
	if ((uint64_t)a->src_pa + n2j_align(j->file_bytes, 128u) > 0x100000000ull)
		return N2JDEC_E_ADDR;
	if (j->ncomp > 0 &&
	    (uint64_t)a->out_y + j->comp[0].plane_bytes > 0x100000000ull)
		return N2JDEC_E_ADDR;
	if (j->ncomp > 1 &&
	    ((uint64_t)a->out_u + j->comp[1].plane_bytes > 0x100000000ull ||
	     (uint64_t)a->out_v + j->comp[2].plane_bytes > 0x100000000ull))
		return N2JDEC_E_ADDR;
	if ((uint64_t)a->index_pa + 3ull * a->index_size > 0x100000000ull)
		return N2JDEC_E_ADDR;
	return N2JDEC_OK;
}

int
n2jdec_unknown_words(const n2jdec_jpeg *j, const n2jdec_addr *a, n2jdec_unknown *out,
		     unsigned cap, unsigned *count)
{
	unsigned n = 0;
	uint32_t stride_y, stride_uv, total_m1;

	if (count != NULL)
		*count = 0;
	if (j == NULL || a == NULL || out == NULL)
		return N2JDEC_E_ARG;
	if ((uint64_t)a->index_pa + 3ull * a->index_size > 0x100000000ull)
		return N2JDEC_E_ADDR;
	stride_y = (j->ncomp > 0) ? j->comp[0].stride : 0;
	stride_uv = (j->ncomp > 1) ? j->comp[1].stride : 0;
	total_m1 = (j->total_mcu > 0) ? j->total_mcu - 1u : 0;

#define N2J_UNK(off_, slot_, val_, text_) do { \
	if (n < cap) { \
		out[n].off = (off_); out[n].slot = (slot_); \
		out[n].value = (val_); out[n].hypothesis = (text_); \
	} \
	n++; \
} while (0)

	N2J_UNK(0x090, 0, 0x00000000u,
		"soft reset: first value of the proven 0x00/0x10 sequence (mtk_jpeg_dec_hw.c:223-240)");
	N2J_UNK(0x090, 1, 0x00000010u,
		"hard reset: second value of the same sequence; bit 4 = BIT_HARD_RST_SHIFT (jpeg_drv_reg.h:97)");
	N2J_UNK(0x17C, 6, 0u,
		"operation mode: 0 = the open driver's full-frame mode; a row/pause mode for the index tables is possible");
	N2J_UNK(0x31C, 14, 0u,
		"huffman/working-table base (vendor 'Huffman Data ION'), or BRZ_FACTOR, or FILE_BRP; see REGISTERS.md");
	N2J_UNK(0x330, 15, stride_y,
		"stride Y (or picture width): default is the open driver's align(mcu_w*8*h0,32)");
	N2J_UNK(0x334, 16, stride_uv,
		"stride UV (or picture height): default is align((mcu_w*8*h1)>>uv_brz_w,16)");
	N2J_UNK(0x33C, 17, 0u,
		"first MCU index of this run (resume), or PAUSE_MCU_NUM; default 0");
	N2J_UNK(0x344, 18, total_m1,
		"last MCU index of this run (or the run's MCU count); default total_mcu-1");
	N2J_UNK(0x240, 19, 0u,
		"trigger: the 2015 driver writes 0; the hybrid's data[19] value is unmeasured");
	N2J_UNK(0x338, 21, a->index_pa,
		"index table base = index_iova");
	N2J_UNK(0x36C, 22, a->index_pa,
		"index table 0 = index_iova");
	N2J_UNK(0x370, 23, a->index_pa + a->index_size,
		"index table 1 = index_iova + size");
	N2J_UNK(0x374, 24, a->index_pa + 2u * a->index_size,
		"index table 2 = index_iova + 2*size");
#undef N2J_UNK

	if (count != NULL)
		*count = n;
	return (n <= cap) ? N2JDEC_OK : N2JDEC_E_ARG;
}

int
n2jdec_fill_words(const n2jdec_jpeg *j, const n2jdec_addr *a, n2jdec_words *w)
{
	n2jdec_unknown unk[16];
	unsigned n = 0, i;
	int rc;

	if (j == NULL || a == NULL || w == NULL)
		return N2JDEC_E_ARG;
	if (!n2jdec_words_supported(j))
		return N2JDEC_E_UNSUPPORTED;
	rc = n2j_check_addr(j, a);
	if (rc != N2JDEC_OK)
		return rc;

	{
		uint8_t *z = (uint8_t *)w;
		size_t k;
		for (k = 0; k < sizeof(*w); k++)
			z[k] = 0;
	}
	w->total_mcu = j->total_mcu;
	w->unit_num = j->unit_num;
	w->stride_y = j->comp[0].stride;
	w->stride_uv = (j->ncomp > 1) ? j->comp[1].stride : 0;
	w->uv_bytes = (j->ncomp > 1) ? j->comp[1].plane_bytes : 0;
	w->index_size = a->index_size;

	w->data[2] = n2j_du_num(j);
	w->data[3] = a->out_y;
	w->data[4] = a->out_u;
	w->data[5] = a->out_v;
	w->data[7] = a->src_pa;
	w->data[8] = j->comp_id_word;
	w->data[9] = j->total_mcu - 1u;
	w->data[10] = j->unit_num - 1u;
	w->data[11] = j->du_ctrl;
	w->data[12] = n2j_align(j->file_bytes, 128u);
	w->data[13] = j->qt_id_word;
	w->data[20] = a->index_size;
	w->known_mask = (1u << 2) | (1u << 3) | (1u << 4) | (1u << 5) | (1u << 7) |
			(1u << 8) | (1u << 9) | (1u << 10) | (1u << 11) | (1u << 12) |
			(1u << 13) | (1u << 20);
	w->hypothesis_mask = ((1u << N2JDEC_WORDS) - 1u) & ~w->known_mask;

	rc = n2jdec_unknown_words(j, a, unk, sizeof(unk) / sizeof(unk[0]), &n);
	if (rc != N2JDEC_OK)
		return rc;
	for (i = 0; i < n; i++) {
		if (unk[i].slot < N2JDEC_WORDS)
			w->data[unk[i].slot] = unk[i].value;
	}
	w->index_pa[0] = a->index_pa;
	w->index_pa[1] = a->index_pa;
	w->index_pa[2] = a->index_pa + a->index_size;
	w->index_pa[3] = a->index_pa + 2u * a->index_size;
	return N2JDEC_OK;
}

int
n2jdec_supplementary(const n2jdec_jpeg *j, const n2jdec_addr *a, n2jdec_reg *out,
		     unsigned cap, unsigned *count)
{
	unsigned n = 0;

	if (count != NULL)
		*count = 0;
	if (j == NULL || a == NULL || out == NULL)
		return N2JDEC_E_ARG;
	if (!n2jdec_words_supported(j))
		return N2JDEC_E_UNSUPPORTED;
	if (n2j_check_addr(j, a) != N2JDEC_OK)
		return N2JDEC_E_ADDR;
	if ((uint64_t)a->src_pa + n2j_align(j->file_bytes, 16u) > 0x100000000ull)
		return N2JDEC_E_ADDR;

#define N2J_REG(off_, val_, name_) do { \
	if (n < cap) { out[n].off = (off_); out[n].value = (val_); out[n].name = (name_); } \
	n++; \
} while (0)

	N2J_REG(0x090, 0x00000000u, "RESET soft");
	N2J_REG(0x090, 0x00000010u, "RESET hard");
	N2J_REG(0x0F8, (uint32_t)j->uv_brz_w << 8, "BRZ_FACTOR (y=0, ch=uv_brz_w)");
	N2J_REG(0x140, a->out_y, "DEST_ADDR0_Y");
	N2J_REG(0x144, a->out_u, "DEST_ADDR0_U");
	N2J_REG(0x148, a->out_v, "DEST_ADDR0_V");
	N2J_REG(0x158, j->comp[0].stride, "STRIDE_Y");
	N2J_REG(0x15C, (j->ncomp > 1) ? j->comp[1].stride : 0u, "STRIDE_UV");
	N2J_REG(0x160, j->comp[0].stride, "IMG_STRIDE_Y");
	N2J_REG(0x164, (j->ncomp > 1) ? j->comp[1].stride : 0u, "IMG_STRIDE_UV");
	N2J_REG(0x16C,
		(((j->dma_mcu - 1u) & 0xffu) << 16) |
		(((j->dma_group - 1u) & 0x7fu) << 8) |
		((j->dma_last_mcu - 1u) & 0xffu), "WDMA_CTRL");
	N2J_REG(0x170, j->total_mcu, "PAUSE_MCU_NUM");
	N2J_REG(0x17C, 0u, "OPERATION_MODE frame");
	N2J_REG(0x200, a->src_pa, "FILE_ADDR");
	N2J_REG(0x20C, j->comp_id_word, "COMP_ID");
	N2J_REG(0x210, j->total_mcu - 1u, "TOTAL_MCU_NUM-1");
	N2J_REG(0x224, j->unit_num - 1u, "COMP0_DATA_UNIT_NUM-1");
	N2J_REG(0x23C, j->du_ctrl, "DU_CTRL");
	N2J_REG(0x240, 0u, "TRIG");
	N2J_REG(0x248, a->src_pa + n2j_align(j->file_bytes, 16u), "FILE_BRP (end)");
	N2J_REG(0x24C, n2j_align(j->file_bytes, 128u), "FILE_TOTAL_SIZE");
	N2J_REG(0x270, j->qt_id_word, "QT_ID");
	N2J_REG(0x320, j->huffman_exist ? 0u : 1u, "ST_HUFFMAN_EN (only when no DHT)");
#undef N2J_REG

	if (count != NULL)
		*count = n;
	return (n <= cap) ? N2JDEC_OK : N2JDEC_E_ARG;
}
