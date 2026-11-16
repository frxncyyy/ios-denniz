/* gpupipe1 host test of nord2_pan.c's ABI v3 submit: the access set handed by pan_jm.c (redwood_submit_access) becomes touch/write masks of the
 * picked kernel handles (duplicates OR-ed, head buffer always touched, executable buffers never written), no set -> v2, a full list -> v2,
 * a kernel that refuses v3 -> that submit and every later one go out as v2 and redwood_pipe_enabled() turns 0.  SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include "nord2_pan.c"

static struct { unsigned next, refuse_v3, v2, v3, refused; struct nord2_gpu_submit_request3 last3; struct nord2_gpu_submit_request last2; uint64_t job; void *mem[64]; } K;
static int kcall_m(void *o, unsigned sel, const uint64_t *in, unsigned ni, const void *bytes, size_t size, uint64_t *out, unsigned *no, void *ob, size_t *os)
{
	(void)o; (void)ni;
	switch (sel) {
	case NORD2_GPU_SEL_STATUS: { struct nord2_gpu_status s = { .abi_version = 2, .enabled = 1, .powered = 1, .arch_ok = 1, .gpu_id = 0x90910011, .js_present = 7 };
		memcpy(ob, &s, sizeof(s)); *os = sizeof(s); return 0; }
	case NORD2_GPU_SEL_HWINFO: { struct nord2_gpu_hwinfo w = { .version = 1 }; memcpy(ob, &w, sizeof(w)); *os = sizeof(w); return 0; }
	case NORD2_GPU_SEL_ALLOC: { unsigned h = ++K.next; K.mem[h] = calloc(1, (in[0] + 16383) & ~16383ull);
		out[0] = h; out[1] = 0x1000000000ull + (uint64_t)h * 0x2000000ull; out[2] = (in[0] + 16383) & ~16383ull; *no = 3; return 0; }
	case NORD2_GPU_SEL_RELEASE: return 0;
	case NORD2_GPU_SEL_SUBMIT_JM:
		if (size == sizeof(struct nord2_gpu_submit_request3)) {
			const struct nord2_gpu_submit_request3 *r = bytes;
			assert(r->version == 3 && r->flags == NORD2_GPU_SUBMIT_PRECISE && !r->reserved[0] && !r->reserved[1]);
			if (K.refuse_v3) { K.refused++; return (int)0xe00002c2u; }
			K.last3 = *r; K.v3++;
		} else {
			assert(size == sizeof(struct nord2_gpu_submit_request));
			const struct nord2_gpu_submit_request *r = bytes; assert(r->version == 2); K.last2 = *r; K.v2++;
		}
		out[0] = ++K.job; *no = 1; return 0;
	case NORD2_GPU_SEL_WAIT_SEQ: out[0] = 4; out[1] = in[0]; out[2] = 0; *no = 3; return 0;
	}
	return (int)0xe00002c7u;
}
static int kmap_m(void *o, unsigned h, uint64_t *a, uint64_t *s) { (void)o; *a = (uint64_t)(uintptr_t)K.mem[h]; *s = 16384; return 0; }
static int kunmap_m(void *o, unsigned h, uint64_t a) { (void)o; (void)h; (void)a; return 0; }
struct create_bo_t { uint32_t size, flags, handle, pad; uint64_t offset; };
static uint32_t mk(unsigned size, unsigned flags, uint64_t *va) { struct create_bo_t q = { .size = size, .flags = flags };
	assert(np_call(0x42, &q, sizeof(q), NULL) == 0 && q.handle); if (va) *va = q.offset; return q.handle; }
static int bitof(const struct nord2_gpu_submit_request3 *r, uint32_t mask, unsigned kh) { for (unsigned i = 0; i < r->nr_handles; i++) if (r->handles[i] == kh) return (mask >> i) & 1u; return -1; }
int main(void)
{
	struct np_ops ops = { .call = kcall_m, .map = kmap_m, .unmap = kunmap_m };
	assert(np_init(&ops) == 0 && np_ready()); np_pool_enable(0);
	gp_off = 0;                               /* no kill file on the host */
	gp_no_v4 = 1;                             /* this test covers v3/v2 (gpupipe2_host_test.c covers v4) */
	uint64_t vp, vc, vh, vt;
	uint32_t pool = mk(65536, 1, &vp), code = mk(4096, 0, &vc), heap = mk(1 << 20, 3, &vh), tex = mk(65536, 1, &vt);
	uint32_t list[5] = { code, tex, pool, heap, heap };
	struct submit s = { .jc = vp + 64, .bo_handles = (uint64_t)(uintptr_t)list, .bo_handle_count = 5 };
	/* vertex chain: code read, tex not touched (fragment only), pool rw, heap written (2nd entry only) */
	uint8_t acc[5] = { 1, 0, 3, 0, 3 };
	assert(redwood_pipe_enabled());
	redwood_submit_access(acc, 5); assert(np_call(0x40, &s, sizeof(s), NULL) == 0); redwood_submit_access(NULL, 0);
	assert(K.v3 == 1 && K.last3.nr_handles == 4 && K.last3.handles[0] == pool);   /* head buffer first, heap once */
	assert(bitof(&K.last3, K.last3.touch_mask, pool) == 1 && bitof(&K.last3, K.last3.write_mask, pool) == 1);
	assert(bitof(&K.last3, K.last3.touch_mask, code) == 1 && bitof(&K.last3, K.last3.write_mask, code) == 0);
	assert(bitof(&K.last3, K.last3.touch_mask, tex) == 0 && bitof(&K.last3, K.last3.write_mask, tex) == 0);
	assert(bitof(&K.last3, K.last3.touch_mask, heap) == 1 && bitof(&K.last3, K.last3.write_mask, heap) == 1);
	/* executable buffers are never declared written, the head's buffer is always touched */
	uint8_t acc2[5] = { 3, 3, 0, 1, 0 };
	redwood_submit_access(acc2, 5); assert(np_call(0x40, &s, sizeof(s), NULL) == 0); redwood_submit_access(NULL, 0);
	assert(bitof(&K.last3, K.last3.write_mask, code) == 0 && bitof(&K.last3, K.last3.touch_mask, pool) == 1 && bitof(&K.last3, K.last3.write_mask, tex) == 1);
	/* no access set (or a count that does not match the list): v2 as before */
	assert(np_call(0x40, &s, sizeof(s), NULL) == 0 && K.v2 == 1 && K.v3 == 2);
	redwood_submit_access(acc, 4); assert(np_call(0x40, &s, sizeof(s), NULL) == 0 && K.v2 == 2); redwood_submit_access(NULL, 0);
	/* a full list (32 distinct buffers + the head): v2, counted */
	uint32_t big[40]; uint8_t bacc[40]; for (unsigned i = 0; i < 40; i++) { big[i] = i ? mk(4096, 1, NULL) : pool; bacc[i] = 1; }
	struct submit sb = { .jc = vp + 64, .bo_handles = (uint64_t)(uintptr_t)big, .bo_handle_count = 40 };
	redwood_submit_access(bacc, 40); assert(np_call(0x40, &sb, sizeof(sb), NULL) == 0 && K.v2 == 3 && gp_full == 1); redwood_submit_access(NULL, 0);
	printf("gpupipe v3 masks / v2 fallbacks PASS\n");
	/* a kernel without 0632, fresh process state: the first v3 is refused, sent again as v2, pipe off for good */
	gp_v3 = 0; gp_no_v3 = 0; K.refuse_v3 = 1;
	redwood_submit_access(acc, 5); assert(np_call(0x40, &s, sizeof(s), NULL) == 0); redwood_submit_access(NULL, 0);
	assert(K.refused == 1 && K.v2 == 4 && gp_no_v3 == 1 && !redwood_pipe_enabled());
	redwood_submit_access(acc, 5); assert(np_call(0x40, &s, sizeof(s), NULL) == 0 && K.refused == 1 && K.v2 == 5); redwood_submit_access(NULL, 0);
	printf("GPUPIPE_HOST_TEST_OK\n");
	return 0;
}
