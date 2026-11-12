/* SPDX-License-Identifier: MIT */
/*
 * n2jdec_tool.c -- host tool: parse a baseline JPEG and print the hybrid decoder's 21 register words
 * plus the open 2015 decoder's complete configuration, ready to be pasted into a phone experiment.
 *
 * Usage:
 *   n2jdec_tool info FILE
 *   n2jdec_tool words FILE --src-pa HEX --out-y HEX --out-c HEX --index-pa HEX [--index-size HEX]
 *                [--out-v HEX]
 *
 * --out-c gives the Cb plane address; the Cr address is out_c + uv_bytes (printed), which is the
 * proposed struct rwj_decode convention. --out-v overrides it explicitly.
 *
 * Exit codes: 0 ok, 1 parse/word error, 2 usage or I/O error.
 */
#include "n2jdec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *
read_file(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	long n;
	uint8_t *b;

	if (f == NULL)
		return NULL;
	if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
	n = ftell(f);
	if (n < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
	b = (uint8_t *)malloc((size_t)n + 1u);
	if (b == NULL) { fclose(f); return NULL; }
	if (fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
	fclose(f);
	*len = (size_t)n;
	return b;
}

static int
parse_hex(const char *s, uint32_t *v)
{
	char *end = NULL;
	unsigned long x;

	if (s == NULL || *s == 0)
		return -1;
	x = strtoul(s, &end, 0);
	if (end == NULL || *end != 0 || x > 0xfffffffful)
		return -1;
	*v = (uint32_t)x;
	return 0;
}

static const char *
sampling_name(unsigned s)
{
	switch (s) {
	case N2JDEC_S_400: return "400";
	case N2JDEC_S_420: return "420";
	case N2JDEC_S_422: return "422";
	case N2JDEC_S_444: return "444";
	case N2JDEC_S_422V: return "422V";
	case N2JDEC_S_422X2: return "422X2";
	case N2JDEC_S_422VX2: return "422VX2";
	default: return "unknown";
	}
}

static const char *
fmt_name(unsigned f)
{
	switch (f) {
	case N2JDEC_FMT_420P: return "420P";
	case N2JDEC_FMT_422P: return "422P";
	case N2JDEC_FMT_GREY: return "GREY";
	default: return "none";
	}
}

static void
print_info(const n2jdec_jpeg *j, size_t len)
{
	unsigned i;

	printf("N2JDEC file_bytes=%u scan_bytes=%u size=%zu precision=%u comps=%u sampling=%s fmt=%s\n",
	       j->file_bytes, j->scan_bytes, len, j->precision, j->ncomp,
	       sampling_name(j->sampling), fmt_name(j->out_fmt));
	printf("N2JDEC width=%u height=%u hmax=%u vmax=%u mcu=%ux%u total_mcu=%u unit_num=%u "
	       "restart=%u huffman=%u rst_markers=%u\n",
	       j->width, j->height, j->hmax, j->vmax, j->mcu_w, j->mcu_h, j->total_mcu,
	       j->unit_num, j->restart_interval, j->huffman_exist, j->restart_markers);
	printf("N2JDEC comp_id_word=0x%08x qt_id_word=0x%04x membership=0x%08x du_ctrl=0x%08x\n",
	       j->comp_id_word, j->qt_id_word, j->membership, j->du_ctrl);
	printf("N2JDEC dma_mcu=%u dma_group=%u dma_last_mcu=%u brz=%u\n",
	       j->dma_mcu, j->dma_group, j->dma_last_mcu, j->uv_brz_w);
	for (i = 0; i < j->ncomp; i++) {
		printf("N2JDEC comp%u id=%u %ux%u tq=%u td=%u ta=%u du=%ux%u stride=%u "
		       "w=%u h=%u bytes=%u\n",
		       i, j->comp[i].id, j->comp[i].h, j->comp[i].v, j->comp[i].tq,
		       j->comp[i].td, j->comp[i].ta, j->comp[i].du_w, j->comp[i].du_h,
		       j->comp[i].stride, j->comp[i].comp_w, j->comp[i].comp_h,
		       j->comp[i].plane_bytes);
	}
	printf("N2JDEC words_supported=%d\n", n2jdec_words_supported(j));
}

static void
print_words(const n2jdec_jpeg *j, const n2jdec_words *w)
{
	const n2jdec_word_info *t;
	n2jdec_unknown u[16];
	unsigned n = 0, nt = 0, i;
	int rc;

	printf("N2JDEC words total_mcu=%u unit_num=%u stride_y=%u stride_uv=%u uv_bytes=%u "
	       "index_size=%u known_mask=0x%06x hypothesis_mask=0x%06x\n",
	       w->total_mcu, w->unit_num, w->stride_y, w->stride_uv, w->uv_bytes, w->index_size,
	       w->known_mask, w->hypothesis_mask);
	for (i = 0; i < N2JDEC_WORDS; i++) {
		printf("N2JDEC data[%02u]=0x%08x slot_off=0x%03x\n", i, w->data[i],
		       i < N2JDEC_WORDS ? n2jdec_word_table(&nt)[i].off : 0);
	}
	printf("N2JDEC index 0x338=0x%08x 0x36c=0x%08x 0x370=0x%08x 0x374=0x%08x\n",
	       w->index_pa[0], w->index_pa[1], w->index_pa[2], w->index_pa[3]);
	t = n2jdec_word_table(&nt);
	for (i = 0; i < nt; i++)
		printf("N2JDEC verdict slot=%u off=0x%03x %s\n", t[i].slot, t[i].off,
		       t[i].verdict == N2JDEC_KNOWN ? "known" : "hypothesis");
	{
		n2jdec_addr z;
		memset(&z, 0, sizeof(z));
		rc = n2jdec_unknown_words(j, &z, u, 16, &n);
	}
	printf("N2JDEC unknown_words=%u\n", n);
	for (i = 0; rc == N2JDEC_OK && i < n; i++)
		printf("N2JDEC unknown off=0x%03x slot=%u value=0x%08x \"%s\"\n",
		       u[i].off, u[i].slot, u[i].value, u[i].hypothesis);
}

int
main(int argc, char **argv)
{
	uint8_t *b;
	size_t len;
	n2jdec_jpeg j;
	n2jdec_addr a;
	n2jdec_words w;
	const char *file;
	int i, rc;

	if (argc < 3) {
		fprintf(stderr, "usage: %s info FILE | words FILE --src-pa HEX --out-y HEX "
			"--out-c HEX --index-pa HEX [--index-size HEX] [--out-v HEX]\n", argv[0]);
		return 2;
	}
	file = argv[2];
	b = read_file(file, &len);
	if (b == NULL) {
		fprintf(stderr, "cannot read %s\n", file);
		return 2;
	}
	rc = n2jdec_parse(b, len, &j);
	if (rc != N2JDEC_OK) {
		fprintf(stderr, "parse failed: %s (%d)\n", n2jdec_strerror(rc), rc);
		free(b);
		return 1;
	}

	if (strcmp(argv[1], "info") == 0) {
		print_info(&j, len);
		free(b);
		return 0;
	}
	if (strcmp(argv[1], "words") != 0) {
		fprintf(stderr, "unknown command %s\n", argv[1]);
		free(b);
		return 2;
	}

	memset(&a, 0, sizeof(a));
	a.index_size = 0x4000u;
	for (i = 3; i < argc; i += 2) {
		uint32_t v;
		if (i + 1 >= argc) {
			fprintf(stderr, "missing value for %s\n", argv[i]);
			free(b);
			return 2;
		}
		if (parse_hex(argv[i + 1], &v) != 0) {
			fprintf(stderr, "bad value for %s\n", argv[i]);
			free(b);
			return 2;
		}
		if (strcmp(argv[i], "--src-pa") == 0)
			a.src_pa = v;
		else if (strcmp(argv[i], "--out-y") == 0)
			a.out_y = v;
		else if (strcmp(argv[i], "--out-c") == 0)
			a.out_u = v;
		else if (strcmp(argv[i], "--out-v") == 0)
			a.out_v = v;
		else if (strcmp(argv[i], "--index-pa") == 0)
			a.index_pa = v;
		else if (strcmp(argv[i], "--index-size") == 0)
			a.index_size = v;
		else {
			fprintf(stderr, "unknown option %s\n", argv[i]);
			free(b);
			return 2;
		}
	}
	if (a.out_v == 0 && a.out_u != 0 && j.ncomp > 1) {
		uint32_t uv = j.comp[1].plane_bytes;
		if ((uint64_t)a.out_u + uv > 0xffffffffull) {
			fprintf(stderr, "out_c + uv_bytes overflows\n");
			free(b);
			return 1;
		}
		a.out_v = a.out_u + uv;
	}
	rc = n2jdec_fill_words(&j, &a, &w);
	if (rc != N2JDEC_OK) {
		fprintf(stderr, "words failed: %s (%d)\n", n2jdec_strerror(rc), rc);
		free(b);
		return 1;
	}
	print_info(&j, len);
	print_words(&j, &w);
	free(b);
	return 0;
}
