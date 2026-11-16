/* gpupipe2 host test of nord2_pan.c's ABI v4 submit (kernel 0661): up to 128 handles, 128-bit masks from the access set, more than 128 ->
 * TRUNCATED with the conservative write rule and no PRECISE, a kernel that refuses v4 -> v3 for good (and still precise).  SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include "nord2_pan.c"
static struct { unsigned next, refuse_v4, v2, v3, v4, refused; struct nord2_gpu_submit_request4 last4; struct nord2_gpu_submit_request3 last3; uint64_t job; void *mem[400]; } K;
static int kcall_m(void *o, unsigned sel, const uint64_t *in, unsigned ni, const void *bytes, size_t size, uint64_t *out, unsigned *no, void *ob, size_t *os)
{
	(void)o; (void)ni;
	switch (sel) {
	case NORD2_GPU_SEL_STATUS: { struct nord2_gpu_status s = { .abi_version = 2, .enabled = 1, .powered = 1, .arch_ok = 1, .gpu_id = 0x90910011, .js_present = 7 };
		memcpy(ob, &s, sizeof(s)); *os = sizeof(s); return 0; }
	case NORD2_GPU_SEL_HWINFO: { struct nord2_gpu_hwinfo w = { .version = 1 }; memcpy(ob, &w, sizeof(w)); *os = sizeof(w); return 0; }
	case NORD2_GPU_SEL_ALLOC: { unsigned h = ++K.next; K.mem[h] = calloc(1, 16384);
		out[0] = h; out[1] = 0x1000000000ull + (uint64_t)h * 0x2000000ull; out[2] = 16384; *no = 3; return 0; }
	case NORD2_GPU_SEL_RELEASE: return 0;
	case NORD2_GPU_SEL_SUBMIT_JM:
		if (size == sizeof(struct nord2_gpu_submit_request4)) {
			const struct nord2_gpu_submit_request4 *r = bytes;
			assert(r->version == 4 && r->nr_handles >= 1 && r->nr_handles <= 128 && !r->reserved[0] && !r->reserved[1]);
			for (unsigned i = r->nr_handles; i < 128; i++) assert(!((r->write_mask[i >> 5] >> (i & 31)) & 1u));
			if (K.refuse_v4) { K.refused++; return (int)0xe00002c2u; }
			K.last4 = *r; K.v4++;
		} else if (size == sizeof(struct nord2_gpu_submit_request3)) { K.last3 = *(const struct nord2_gpu_submit_request3 *)bytes; K.v3++; }
		else { assert(size == sizeof(struct nord2_gpu_submit_request)); K.v2++; }
		out[0] = ++K.job; *no = 1; return 0;
	case NORD2_GPU_SEL_WAIT_SEQ: out[0] = 4; out[1] = in[0]; out[2] = 0; *no = 3; return 0;
	}
	return (int)0xe00002c7u;
}
static int kmap_m(void *o, unsigned h, uint64_t *a, uint64_t *s) { (void)o; *a = (uint64_t)(uintptr_t)K.mem[h]; *s = 16384; return 0; }
static int kunmap_m(void *o, unsigned h, uint64_t a) { (void)o; (void)h; (void)a; return 0; }
struct create_bo_t { uint32_t size, flags, handle, pad; uint64_t offset; };
static uint32_t mk(unsigned flags, uint64_t *va) { struct create_bo_t q = { .size = 4096, .flags = flags };
	assert(np_call(0x42, &q, sizeof(q), NULL) == 0 && q.handle); if (va) *va = q.offset; return q.handle; }
static int bit(const uint32_t *m, unsigned k) { return (m[k >> 5] >> (k & 31)) & 1u; }
int main(void)
{
	struct np_ops ops = { .call = kcall_m, .map = kmap_m, .unmap = kunmap_m };
	assert(np_init(&ops) == 0 && np_ready()); np_pool_enable(0);
	gp_off = 0; gp_no_v4 = 0;
	static uint32_t list[200]; static uint8_t acc[200]; uint64_t va0;
	for (unsigned i = 0; i < 200; i++) { list[i] = mk(i == 3 ? 0 : 1, i ? NULL : &va0); acc[i] = (i % 3 == 0) ? 3 : (i % 3 == 1) ? 1 : 0; }
	/* 40 buffers with an access set: v4, precise, masks beyond bit 31 */
	struct submit s = { .jc = va0 + 64, .bo_handles = (uint64_t)(uintptr_t)list, .bo_handle_count = 40 };
	redwood_submit_access(acc, 40); assert(np_call(0x40, &s, sizeof(s), NULL) == 0); redwood_submit_access(NULL, 0);
	assert(K.v4 == 1 && K.last4.nr_handles == 40 && (K.last4.flags & NORD2_GPU_SUBMIT_PRECISE) && !(K.last4.flags & NORD2_GPU_SUBMIT_TRUNCATED));
	assert(bit(K.last4.write_mask, 33) && bit(K.last4.touch_mask, 34) && !bit(K.last4.touch_mask, 35) && !bit(K.last4.write_mask, 3));   /* 3 = executable */
	/* no access set: v4 with the conservative write rule, not precise */
	assert(np_call(0x40, &s, sizeof(s), NULL) == 0 && K.v4 == 2 && !(K.last4.flags & NORD2_GPU_SUBMIT_PRECISE) && bit(K.last4.write_mask, 35) && !bit(K.last4.write_mask, 3));
	/* 200 buffers: 128 listed, TRUNCATED, not precise, conservative writes */
	s.bo_handle_count = 200; redwood_submit_access(acc, 200); assert(np_call(0x40, &s, sizeof(s), NULL) == 0); redwood_submit_access(NULL, 0);
	assert(K.v4 == 3 && K.last4.nr_handles == 128 && (K.last4.flags & NORD2_GPU_SUBMIT_TRUNCATED) && !(K.last4.flags & NORD2_GPU_SUBMIT_PRECISE) && bit(K.last4.write_mask, 127) && gp_v4_trunc == 1);
	/* a kernel without 0661, fresh process state: the first v4 is refused, sent again as v3 (precise), v3 from then on */
	gp_v4 = 0; gp_no_v4 = 0; K.refuse_v4 = 1; s.bo_handle_count = 20;
	redwood_submit_access(acc, 20); assert(np_call(0x40, &s, sizeof(s), NULL) == 0); redwood_submit_access(NULL, 0);
	assert(K.refused == 1 && K.v3 == 1 && gp_no_v4 == 1 && (K.last3.flags & NORD2_GPU_SUBMIT_PRECISE));
	redwood_submit_access(acc, 20); assert(np_call(0x40, &s, sizeof(s), NULL) == 0 && K.refused == 1 && K.v3 == 2); redwood_submit_access(NULL, 0);
	printf("GPUPIPE2_HOST_TEST_OK\n");
	return 0;
}
