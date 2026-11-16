/*
 * y2s98: the .atx files of the phone (PosterBoard's snapshots, read from the mirror) through rw_texlayout_pure.h, on the build
 * machine.  For every file given:
 *   - the container as ImageIO wrote it (ATXWritePlugin::updateHeader / writeASTCData, 20H392): 'AAPL' magic, HEAD chunk
 *     (GL format 0x93B0 = ASTC 4x4 RGBA, size, pre-twiddled flag, LZFSE flag, 16-byte watermark), FILL, then the data chunk:
 *     'astc' = "pre-twiddled" (what the reader hands CoreAnimation as tiled texture memory, in place), 'ASTC' = linear;
 *   - what the fix relies on: pre-twiddled = 1, watermark = 0 (= this layout's), chunk length = the layout's size;
 *   - the window server's side: rwtl_level_bytes of that memory, decoded with Mesa's software ASTC decoder
 *     (_mesa_unpack_astc_2d_ldr, the reference for "what a GPU shows for these blocks") -> alpha and colour statistics;
 *     with a third argument, the RGBA picture is written there (width*height*4 bytes) for a contact sheet.
 * Prints one line a file; exit 1 if any file is not what the fix expects.
 *   rw_texlayout_atx_check FILE [RAW_OUT]
 */
#include "rw_texlayout_pure.h"

#include <stdio.h>
#include <stdlib.h>

typedef int mesa_format;
void _mesa_unpack_astc_2d_ldr(uint8_t *dst_row, unsigned dst_stride, const uint8_t *src_row, unsigned src_stride,
    unsigned src_width, unsigned src_height, mesa_format format);

static uint32_t
le32(const uint8_t *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

int
main(int argc, char **argv)
{
	if (argc < 2) {
		return 2;
	}
	FILE *f = fopen(argv[1], "rb");

	if (f == NULL) {
		perror(argv[1]);
		return 2;
	}
	fseek(f, 0, SEEK_END);
	long n = ftell(f);

	fseek(f, 0, SEEK_SET);
	uint8_t *d = malloc((size_t)n + 16);

	if (d == NULL || fread(d, 1, (size_t)n, f) != (size_t)n) {
		return 2;
	}
	fclose(f);
	if (n < 0x70 || memcmp(d, "AAPL\r\n\x1a\n", 8) != 0 || memcmp(d + 12, "HEAD", 4) != 0 || le32(d + 8) < 0x54) {
		printf("NOT-ATX %s\n", argv[1]);
		return 1;
	}
	const uint8_t *hd = d + 16;
	uint32_t gl = le32(hd + 0x10), width = le32(hd + 0x18), height = le32(hd + 0x1c);
	uint32_t pretwiddled = le32(hd + 0x30), lzfse = le32(hd + 0x34);
	int wm_zero = 1;

	for (int i = 0; i < 16; i++) {
		wm_zero &= hd[0x3c + i] == 0;
	}
	/* the chunks after HEAD: [length][tag][length bytes] */
	size_t at = 16 + le32(d + 8);
	const uint8_t *data = NULL;
	uint32_t data_size = 0;
	char tag[5] = "none";

	while (at + 8 <= (size_t)n) {
		uint32_t len = le32(d + at);

		if (memcmp(d + at + 4, "astc", 4) == 0 || memcmp(d + at + 4, "ASTC", 4) == 0) {
			memcpy(tag, d + at + 4, 4);
			data_size = le32(d + at + 8);
			data = d + at + 12;
			if (len != data_size + 4 || at + 12 + data_size > (size_t)n) {
				printf("BAD-CHUNK %s\n", argv[1]);
				return 1;
			}
			break;
		}
		if (memcmp(d + at + 4, "END ", 4) == 0) {
			break;
		}
		at += 8 + len;
	}
	struct rwtl_layout l;
	const char *why = "";
	uint32_t metal = gl == 0x93b0 ? 204 : gl == 0x93b7 ? 212 : 0;       /* ASTCTextureImp::MetalFormatForGLFormat: 4x4, 8x8 */
	int ok = data != NULL && rwtl_init(&l, metal, width, height, 1, &why) == 0;
	int expected = ok && pretwiddled == 1 && lzfse == 0 && wm_zero && memcmp(tag, "astc", 4) == 0 && data_size == l.size &&
	    (size_t)(data - d) % 16384 == 0;

	if (!ok) {
		printf("UNSERVED %s gl %#x %ux%u (%s)\n", argv[1], gl, width, height, why);
		return 1;
	}
	uint32_t w = 0, h = 0;
	size_t row = 0;
	size_t length = (l.size + 16383) / 16384 * 16384;                  /* CoreAnimation rounds the length up to a page */
	const uint8_t *level0 = rwtl_level_bytes(&l, data, length, 0, &w, &h, &row, &why);

	if (level0 == NULL) {
		printf("NO-LEVEL %s (%s)\n", argv[1], why);
		return 1;
	}
	uint8_t *rgba = calloc((size_t)w * h, 4);

	if (rgba == NULL) {
		return 2;
	}
	_mesa_unpack_astc_2d_ldr(rgba, w * 4, level0, (unsigned)row, w, h, (mesa_format)((l.block_w << 8) | l.block_h));
	unsigned long long sum[4] = { 0, 0, 0, 0 }, clear = 0, opaque = 0, magenta = 0;
	uint8_t amin = 255;

	for (size_t i = 0; i < (size_t)w * h; i++) {
		const uint8_t *p = rgba + i * 4;

		for (int c = 0; c < 4; c++) {
			sum[c] += p[c];
		}
		clear += p[3] == 0;
		opaque += p[3] == 255;
		magenta += p[0] == 255 && p[1] == 0 && p[2] == 255;        /* the decoder's error colour */
		if (p[3] < amin) {
			amin = p[3];
		}
	}
	size_t px = (size_t)w * h;

	printf("%s %ux%u gl %#x tag %s pretwiddled %u lzfse %u watermark %s data %u at %#zx layout %zu | decoded: alpha min %u, "
	    "opaque %.1f%%, clear %.1f%%, error-colour %.2f%%, mean rgba %llu %llu %llu %llu\n", expected ? "ATX-OK" : "ATX-UNEXPECTED",
	    width, height, gl, tag, pretwiddled, lzfse, wm_zero ? "0" : "NONZERO", data_size, (size_t)(data - d), l.size, amin,
	    100.0 * opaque / px, 100.0 * clear / px, 100.0 * magenta / px, sum[0] / px, sum[1] / px, sum[2] / px, sum[3] / px);
	if (argc > 2) {
		FILE *o = fopen(argv[2], "wb");

		if (o == NULL || fwrite(rgba, 4, px, o) != px) {
			return 2;
		}
		fclose(o);
	}
	free(rgba);
	free(d);
	return expected ? 0 : 1;
}
