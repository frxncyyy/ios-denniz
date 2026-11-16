/* thumbrc host test: nord2_pan.c against a fake kernel. Two Mesa screens (descriptors 5 and 7) import the same IOSurface; each must
 * get its own handle and kernel buffer, a repeated import on one screen must share, and screen 5's close must not release screen 7's. */
#define _GNU_SOURCE
#include "../src/redwood-drm/nord2_pan.c"
#include <assert.h>

static unsigned imports, releases, next_kernel = 100;
static uint64_t released[16];
static int fake_call(void *o, unsigned sel, const uint64_t *in, unsigned ni, const void *b, size_t s, uint64_t *out, unsigned *no,
		     void *ob, size_t *os)
{
	(void)o; (void)ni; (void)b; (void)s;
	if (sel == NORD2_GPU_SEL_IMPORT_SURFACE) {
		imports++; out[0] = next_kernel++; out[1] = 0x100000ull * next_kernel; out[2] = in[2]; *no = 3; return 0;
	}
	if (sel == NORD2_GPU_SEL_RELEASE) { released[releases++] = in[0]; *no = 0; return 0; }
	if (no) *no = 0;
	if (os) *os = 0;
	(void)ob;
	return 1;   /* STATUS etc.: not supported, the layer carries on with defaults */
}
static int fake_map(void *o, unsigned h, uint64_t *a, uint64_t *s) { (void)o; (void)h; *a = 0x1000; *s = 4096; return 0; }
static int fake_unmap(void *o, unsigned h, uint64_t a) { (void)o; (void)h; (void)a; return 0; }
static int fake_surface(void *o, uint32_t id, struct np_surface_info *out)
{
	(void)o; memset(out, 0, sizeof(*out)); out->size = 1032192; out->cpu = 0x7000000; out->width = 375; out->height = 667;
	out->pixelFormat = 0x34323066; out->planes = 2; out->planeOffset[1] = 256512; out->planeRowBytes[0] = out->planeRowBytes[1] = 384;
	return id == 0;
}
static uint32_t import_on(int fd, uint32_t surface)
{
	struct surface_req q; memset(&q, 0, sizeof(q)); q.surface = surface;
	np_set_import_fd(fd);
	int r = np_call(R_SURFACE, &q, sizeof(q), NULL);
	np_set_import_fd(-1);
	assert(r == 0 && q.handle != 0);
	return q.handle;
}
static void close_handle(uint32_t h) { uint32_t c[2] = { h, 0 }; assert(np_call(R_GEM_CLOSE, c, sizeof(c), NULL) == 0); }
int main(void)
{
	struct np_ops ops = { NULL, fake_call, fake_map, fake_unmap, fake_surface };
	if (np_init(&ops) != 0) { printf("np_init failed (errno %d): the layer needs a powered GPU status; test SKIPPED\n", errno); return 77; }
	uint32_t a1 = import_on(5, 88), a2 = import_on(5, 88), b1 = import_on(7, 88);
	assert(a1 == a2);                 /* one screen: shared, as Mesa expects */
	assert(b1 != a1 && imports == 2); /* another screen: its own handle and kernel buffer */
	uint64_t kb = D.bos[b1].kernel;
	close_handle(a1);
	assert(releases == 1 && released[0] != kb && D.bos[b1].kernel == kb);   /* screen 7's buffer survives screen 5's close */
	close_handle(b1);
	assert(releases == 2 && released[1] == kb);
	uint32_t c1 = import_on(-1, 89), c2 = import_on(-1, 89);   /* a caller that names no descriptor: shared, as before */
	assert(c1 == c2 && imports == 3);
	printf("THUMBRC_HOST_TEST_OK: same screen shares (%u=%u), screens apart (%u vs %u), close per screen\n", a1, a2, a1, b1);
	return 0;
}
