/* y2s67-libmerge1 host test: the MERGED nord2_pan.c (thumbrc + GNR1) against a fake kernel. Where the two meet: a surface imported
 * on two Mesa screens (thumbrc: two handles, two kernel buffers) must be told apart by GNR1's handle->range lookup
 * (nord2_gnr1_bo_info, used by air_metal.c's failed-dispatch report) and by its fault decode (gnr1_where), and both log groups
 * ('np import/close', 'np GNR1 FAULT') must reach SEL_LOG with the NORD2_RW prefix. */
#define _GNU_SOURCE
#include "../src/redwood-drm/nord2_pan.c"
#include <assert.h>

static unsigned imports, releases, next_kernel = 100, logs;
static char logbuf[16384];
static int fake_call(void *o, unsigned sel, const uint64_t *in, unsigned ni, const void *b, size_t s, uint64_t *out, unsigned *no,
		     void *ob, size_t *os)
{
	(void)o; (void)ni; (void)ob;
	if (sel == NORD2_GPU_SEL_IMPORT_SURFACE) {
		imports++; out[0] = next_kernel++; out[1] = 0x100000ull * next_kernel; out[2] = in[2]; *no = 3; return 0;
	}
	if (sel == NORD2_GPU_SEL_RELEASE) { releases++; *no = 0; return 0; }
	if (sel == NORD2_GPU_SEL_LOG) {
		size_t l = strlen(logbuf);
		if (l + s + 1 < sizeof(logbuf)) { memcpy(logbuf + l, b, s); logbuf[l + s] = 0; }
		logs++; if (no) *no = 0; return 0;
	}
	if (no) *no = 0;
	if (os) *os = 0;
	return 1;
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
	if (np_init(&ops) != 0) { printf("np_init failed (errno %d): test SKIPPED\n", errno); return 77; }
	D.up = 1;
	uint32_t a = import_on(5, 88), b = import_on(7, 88);
	assert(a != b && imports == 2);
	uint64_t va_a, sz_a, va_b, sz_b, dummy;
	assert(nord2_gnr1_bo_info(a, &va_a, &sz_a) == 0 && nord2_gnr1_bo_info(b, &va_b, &sz_b) == 0);
	assert(va_a == D.bos[a].va && va_b == D.bos[b].va && va_a != va_b && sz_a == 1032192 && sz_b == 1032192);
	assert(nord2_gnr1_bo_info(0, &dummy, &dummy) == -1 && nord2_gnr1_bo_info(NP_BOS + 1, &dummy, &dummy) == -1);
	char where[224];
	gnr1_where(va_b + 64, where, sizeof(where));
	assert(strstr(where, "INSIDE bo") && strstr(where, "+64, imported"));
	char want[32]; snprintf(want, sizeof(want), "INSIDE bo %u ", b); assert(strstr(where, want));
	struct nord2_gpu_fault_info fi = { .job_id_lo = 9, .status = 0xc1, .fault_status = 0x2c1, .fault_address = va_b + 64 };
	uint64_t out[3] = { 4, 0, 9 };
	gnr1_note_submit(9, 0xabc000, 3, 4, 0);
	gnr1_say_fault(9, out, 3, &fi);
	close_handle(a);
	assert(releases == 1 && nord2_gnr1_bo_info(a, &dummy, &dummy) == -1 && nord2_gnr1_bo_info(b, &va_b, &sz_b) == 0);
	close_handle(b);
	assert(releases == 2);
	assert(strstr(logbuf, "NORD2_RW np import: IOSurface 88 fd 5 -> handle"));
	assert(strstr(logbuf, "NORD2_RW np import: IOSurface 88 fd 7 -> handle"));
	assert(strstr(logbuf, "NORD2_RW np close: IOSurface 88 fd 5"));
	assert(strstr(logbuf, "NORD2_RW np GNR1 FAULT pid="));
	assert(strstr(logbuf, "(fragment, chain 0xabc000, 4 bos)"));
	assert(strstr(logbuf, "MMU translation fault") || strstr(logbuf, "translation, read access"));
	fputs(logbuf, stdout);
	printf("LIBMERGE1_PAN_HOST_TEST_OK: thumbrc handles %u/%u, GNR1 ranges 0x%llx/0x%llx, fault located in bo %u, %u log records\n", a, b,
	       (unsigned long long)va_a, (unsigned long long)va_b, b, logs);
	return 0;
}
