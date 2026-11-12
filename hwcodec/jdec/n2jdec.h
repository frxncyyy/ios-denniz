/* SPDX-License-Identifier: MIT */
/*
 * n2jdec.h -- baseline JPEG header parser and MT6893 "hybrid" JPEG decoder register-word generator.
 *
 * Package J04 (gpu/hwcodec/jdec). Original code written from ITU-T T.81 and from the register
 * sequences in the Linux drivers named next to every formula; no vendor code, no libjpeg source.
 *
 * Sources of the register formulas (all in /home/.../nord2-current-kernel):
 *   -drivers/media/platform/mtk-jpeg/mtk_jpeg_dec_hw.c   (the open 2015 decoder: 0x090..0x320)
 *   -drivers/media/platform/mtk-jpeg/mtk_jpeg_dec_reg.h  (register offsets and field masks)
 *   -drivers/misc/mediatek/jpeg/v2/jpeg_drv_dec.c        (the 3-core "hybrid" driver: which data[] slot
 *                                                         goes to which offset, jpeg_drv_hybrid_dec_start)
 *
 * Portable C11: no libc calls, no allocation, no floating point, no printing. It compiles both in user
 * space (host tests, tools) and inside the XNU device driver (K01). Little-endian is not assumed.
 *
 * Every word the generator produces carries a verdict: N2JDEC_KNOWN (the register name, field layout and
 * formula are verified in the open sources) or N2JDEC_HYPOTHESIS (the hybrid driver writes the offset but
 * its meaning is not documented anywhere in the tree; see jdec/REGISTERS.md). Words marked HYPOTHESIS are
 * produced by n2jdec_unknown_words() only, which is deliberately a separate function.
 */
#ifndef N2JDEC_H
#define N2JDEC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define N2JDEC_VERSION        1u
#define N2JDEC_MAX_COMPS      3u   /* the hybrid block has three components */
#define N2JDEC_MAX_BLOCKS     10u  /* mtk_jpeg_dec_hw.c:95, MTK_JPEG_BLOCK_MAX */
#define N2JDEC_WORDS          21u  /* struct JPEG_DEC_DRV_HYBRID_TASK.data[21], jpeg_drv.h:442 */
#define N2JDEC_INDEX_REGS     4u   /* 0x338, 0x36C, 0x370, 0x374 written from the index buffer */

/* ---- result codes (negative; 0 = success) ------------------------------------------------ */
enum {
	N2JDEC_OK = 0,
	N2JDEC_E_ARG = -1,          /* NULL pointer, zero length, bad output pointer */
	N2JDEC_E_TRUNC = -2,        /* a segment or the file ends before its declared length */
	N2JDEC_E_MARKER = -3,       /* not a marker, 0xFF 0x00 outside entropy, nested SOI, ... */
	N2JDEC_E_SOF = -4,          /* missing/duplicate SOF0 or malformed SOF0 payload */
	N2JDEC_E_PRECISION = -5,    /* SOF0 precision != 8 */
	N2JDEC_E_COMPONENTS = -6,   /* component count not 1 or 3 */
	N2JDEC_E_SAMPLING = -7,     /* H/V out of 1..4 or sum(H*V) > 10 */
	N2JDEC_E_RANGE = -8,        /* width/height 0 or MCU count out of range */
	N2JDEC_E_DQT = -9,          /* malformed DQT */
	N2JDEC_E_DHT = -10,         /* malformed DHT or a scan references a missing table */
	N2JDEC_E_DRI = -11,         /* malformed DRI */
	N2JDEC_E_SOS = -12,         /* malformed SOS or component mismatch */
	N2JDEC_E_NOT_BASELINE = -13,/* SOF1/2/3/.., DAC, multiple scans */
	N2JDEC_E_NO_EOI = -14,      /* no EOI after the scan */
	N2JDEC_E_UNSUPPORTED = -15, /* valid T.81 but outside the MTK format table (2 comps, 4:1:1, ...) */
	N2JDEC_E_ADDR = -16         /* an address/index argument is misaligned or out of range */
};

/* ---- sampling classification (values match the MTK_JPEG_COLOR_* idea) --------------------- */
enum {
	N2JDEC_S_UNKNOWN = 0,
	N2JDEC_S_400 = 1,    /* MTK_JPEG_COLOR_400    0x00110000, output GREY */
	N2JDEC_S_420 = 2,    /* MTK_JPEG_COLOR_420    0x00221111, output 4:2:0 */
	N2JDEC_S_422 = 3,    /* MTK_JPEG_COLOR_422    0x00211111, output 4:2:2 */
	N2JDEC_S_444 = 4,    /* MTK_JPEG_COLOR_444    0x00111111, output 4:2:2 (uv_brz_w = 1) */
	N2JDEC_S_422V = 5,   /* MTK_JPEG_COLOR_422V   0x00121111, output 4:2:0 (uv_brz_w = 1) */
	N2JDEC_S_422X2 = 6,  /* MTK_JPEG_COLOR_422X2  0x00412121, output 4:2:2 */
	N2JDEC_S_422VX2 = 7  /* MTK_JPEG_COLOR_422VX2 0x00222121, output 4:2:0 (uv_brz_w = 1) */
};

/* Output formats proposed for struct rwj_decode.out_format (see jdec/STATUS.md and the board post). */
enum {
	N2JDEC_FMT_NONE = 0,
	N2JDEC_FMT_420P = 4,   /* Y plane + Cb plane + Cr plane, 4:2:0 */
	N2JDEC_FMT_422P = 5,   /* Y plane + Cb plane + Cr plane, 4:2:2 */
	N2JDEC_FMT_GREY = 6    /* Y plane only */
};

typedef struct n2jdec_comp {
	uint8_t present;       /* 1 if the component exists in SOF0 */
	uint8_t id;            /* Ci */
	uint8_t h, v;          /* Hi, Vi (1..4) */
	uint8_t tq;            /* Tqi (0..3) */
	uint8_t td, ta;        /* Huffman table ids from SOS (0..3) */
	uint8_t pad_;
	uint32_t blocks_per_mcu; /* h * v */
	uint32_t du_w, du_h;     /* padded data units across/down: mcu_w*h, mcu_h*v */
	uint32_t du_count;       /* du_w * du_h (the component's blocks in the MCU grid) */
	uint32_t comp_w, comp_h; /* padded component width/height, mtk_jpeg_dec_hw.c:151-169 */
	uint32_t stride;         /* Y: align(comp_w,32); chroma: align(comp_w,16) */
	uint32_t plane_bytes;    /* stride * comp_h */
} n2jdec_comp;

typedef struct n2jdec_jpeg {
	uint32_t width, height;  /* SOF0 X, Y */
	uint8_t precision;       /* 8 */
	uint8_t ncomp;           /* 1 or 3 */
	uint8_t sof_marker;      /* 0xC0 for a baseline frame; set even when we reject it */
	uint8_t baseline;        /* 1 = SOF0 */
	uint8_t hmax, vmax;
	uint8_t huffman_exist;   /* 1 if at least one DHT was seen */
	uint32_t restart_markers; /* count of RSTn markers met in the scan (diagnostic) */
	uint16_t restart_interval; /* DRI, 0 if none */
	uint32_t mcu_w, mcu_h;     /* T.81: ceil(X/(8*Hmax)), ceil(Y/(8*Vmax)) */
	uint32_t total_mcu;
	uint32_t unit_num;         /* mtk_jpeg_dec_hw.c:83: ceil(X/8)*ceil(Y/8), comp0 units */
	uint32_t blk_num;          /* sum(h*v) over components */
	uint32_t membership;       /* mtk_jpeg_dec_hw.c:94-108, 3 bits per block slot */
	uint32_t du_ctrl;          /* membership | gmc<<30 | gray<<31 */
	uint32_t dma_mcu, dma_group, dma_last_mcu; /* mtk_jpeg_dec_hw.c:111-135 */
	uint32_t sampling;         /* N2JDEC_S_* */
	uint32_t uv_brz_w;         /* 0 or 1, mtk_jpeg_dec_hw.c:44-68 */
	uint32_t out_fmt;          /* N2JDEC_FMT_* */
	uint32_t qt_id_word;       /* 0x270 value, mtk_jpeg_dec_hw.c:344-351 */
	uint32_t comp_id_word;     /* 0x20C value, mtk_jpeg_dec_hw.c:315-323 */
	uint32_t file_bytes;       /* bytes from SOI through EOI inclusive */
	uint32_t scan_bytes;       /* entropy-coded bytes after SOS (excluding EOI) */
	n2jdec_comp comp[N2JDEC_MAX_COMPS];
	uint8_t dqt_present[4], dqt_prec[4];
	uint8_t dqt[4][64];        /* values in the order stored in the file (zig-zag) */
	uint8_t dc_present[4], ac_present[4];
} n2jdec_jpeg;

/* Parse one baseline JPEG. `buf`/`len` need not be aligned. Returns N2JDEC_OK or a negative code.
 * On error `out` is still zeroed except fields filled before the failure (diagnostics only). */
int n2jdec_parse(const uint8_t *buf, size_t len, n2jdec_jpeg *out);

/* Static description of an error code; never NULL. */
const char *n2jdec_strerror(int code);

/* 1 if the parser's geometry fits the MTK format table and n2jdec_fill_words can produce words,
 * 0 if N2JDEC_S_UNKNOWN or a sampling geometry the open driver's formula does not cover (H0 or V0 > 2,
 * i.e. MTK_JPEG_COLOR_422X2 / 422VX2 / a 4:1:1 stream). See REGISTERS.md. */
int n2jdec_words_supported(const n2jdec_jpeg *j);

/* ---- addresses the hardware needs (all physical, below 4 GiB) ---------------------------- */
typedef struct n2jdec_addr {
	uint32_t src_pa;       /* bitstream, 16-byte aligned (mtk_jpeg_dec_hw.c:309) */
	uint32_t out_y;        /* output plane addresses, 16-byte aligned (mtk_jpeg_dec_hw.c:255-260) */
	uint32_t out_u;
	uint32_t out_v;
	uint32_t index_pa;     /* hybrid index table area, 128-byte aligned (jpeg_drv_dec.c:40,47) */
	uint32_t index_size;   /* size of ONE index table, multiple of 128 (jpeg_drv_dec.c:40,76-79) */
} n2jdec_addr;

/* ---- the 21 data[] words of struct JPEG_DEC_DRV_HYBRID_TASK ------------------------------- */
typedef struct n2jdec_words {
	uint32_t data[N2JDEC_WORDS]; /* exactly what the vendor library passes in data[0..20] */
	uint32_t known_mask;         /* bit i: data[i] is [V] in REGISTERS.md */
	uint32_t hypothesis_mask;    /* bit i: data[i] is [I]; the values came from n2jdec_unknown_words */
	uint32_t total_mcu;
	uint32_t unit_num;
	uint32_t stride_y, stride_uv;
	uint32_t uv_bytes;           /* bytes of one chroma plane (Cb or Cr) */
	uint32_t index_pa[N2JDEC_INDEX_REGS]; /* 0x338, 0x36C, 0x370, 0x374 */
	uint32_t index_size;
} n2jdec_words;

/* Fill all 21 words for a parsed frame and the caller's buffers.
 * Verified words come from the open driver's formulas; the slots whose meaning is a hypothesis are
 * filled by n2jdec_unknown_words() (values are the documented best guesses, not measurements).
 * Returns N2JDEC_OK, N2JDEC_E_ADDR or N2JDEC_E_UNSUPPORTED. */
int n2jdec_fill_words(const n2jdec_jpeg *j, const n2jdec_addr *a, n2jdec_words *w);

/* ---- verdict / documentation table ------------------------------------------------------- */
enum { N2JDEC_KNOWN = 1, N2JDEC_HYPOTHESIS = 2 };

typedef struct n2jdec_word_info {
	uint8_t slot;            /* 0..20, or 21..24 for the four index-table registers */
	uint16_t off;            /* register offset in the decoder window */
	uint8_t verdict;         /* N2JDEC_KNOWN or N2JDEC_HYPOTHESIS */
	const char *name;        /* short register name */
	const char *note;        /* field layout / evidence / hypothesis */
} n2jdec_word_info;

/* 21 entries, slot == array index. */
const n2jdec_word_info *n2jdec_word_table(unsigned *count);
/* 4 entries for 0x338/0x36C/0x370/0x374 (slot 21..24). */
const n2jdec_word_info *n2jdec_index_table(unsigned *count);

/* ---- the deliberately separated unknown words ------------------------------------------- */
typedef struct n2jdec_unknown {
	uint16_t off;
	uint8_t slot;            /* data[] slot, or 21..24 for index registers */
	uint32_t value;          /* best-guess value n2jdec_fill_words() used */
	const char *hypothesis;  /* what the word could be, and why */
} n2jdec_unknown;

/* Returns the number of unknown registers written (currently 13: 0x090 x2, 0x17C, 0x31C, 0x330,
 * 0x334, 0x338, 0x33C, 0x344, 0x36C, 0x370, 0x374, 0x240). `cap` must be >= that number. */
int n2jdec_unknown_words(const n2jdec_jpeg *j, const n2jdec_addr *a, n2jdec_unknown *out,
			 unsigned cap, unsigned *count);

/* ---- supplementary words: the open 2015 decoder's complete frame-mode configuration ------ */
typedef struct n2jdec_reg {
	uint16_t off;
	uint32_t value;
	const char *name;
} n2jdec_reg;

/* The registers mtk_jpeg_dec_set_config() writes, for phone experiment A (see PHONE_EXPERIMENTS.md).
 * Includes the words that the hybrid driver does NOT write (strides, bank 0, BRZ, FILE_BRP, ...). */
int n2jdec_supplementary(const n2jdec_jpeg *j, const n2jdec_addr *a, n2jdec_reg *out,
			 unsigned cap, unsigned *count);

#ifdef __cplusplus
}
#endif
#endif /* N2JDEC_H */
