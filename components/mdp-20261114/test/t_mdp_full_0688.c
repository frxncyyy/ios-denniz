/* 0687: + IOVA mapping cache (second blit of the same surfaces must hit, no remap)
 * 0686: NV12 destination model (WROT id 12: Y walk with 1 byte, UV walk on the half grid with 2 bytes, from OFST_C) + nv2nv jobs
 * 0683: NV12 multi-tile allowed, RGB scale capability jobs (smooth pattern), caps 0x1fff
 * 0682: + S2 DIAG jobs (all pass in the model, never counted)
 * 0681: scaletile removed, multi-tile / RGB-source scaling refused (expect compile error)
 * 0680: same scenarios on the 0680 sources (TDSHP2 enable)
 * 0679: same scenarios on the 0679 sources (AAL2/TDSHP2 sizes)
 * 0678 copy (RSZ model follows CONTROL_1 algorithm bits: alg 0 = 1.15 input step + 15-bit offsets, alg 2 = 2^20 output-per-input
 * step + output-domain offsets; S2 order nv12v nv12f nv12rot90 scaledown scaleup ...; S2 stop-at-first-failure scenario)
 * 0677 copy (WROT model walks from VIDO_OFST_ADDR like the vendor programs it: first pixel = last byte for mirrored/rotated walks)
 * 0676 copy (vendor RDMA buffer words, WROT INT/FIFO/ROT_EN bits, WROT line ladder: model mode 4 completes only with 8 lines)
 * 0675 copy (OSTDL byte-mirrored model, q10jmdp4 phone larb2 state, larb check non-blocking)
 * 0674 copy of t_mdp_full.c (larb2 bring-up, mutex eof done path, larb sleep stuck)
 * 0673 copy of t_mdp_full.c: same scenarios on the 0673 nord2_mdp.inc (-DNORD2_MDP_INC) + scenario P (bring-up then stages).
 * Host test of 0670+0671 nord2_mdp.inc (probe + stages + service API) against a behavioural MDP model.
 * The model executes a frame when the mutex EN bit of mutex 4 (direct pipe) or 6 (scale pipe) is set, using ONLY the register
 * values the driver wrote (RDMA base/offset/size/pitch/format, RSZ in/out/steps/offsets, WROT base/offset/stride/ctrl/matrix,
 * larb MMU_EN + M4U1 page tables for IOVAs) and then raises the WROT frame-done GCE event.  It encodes the [I] assumptions of
 * nord2_mdp_job.h, so a pass means: the compiler, tiling, offsets, executor, state machine, guards, IOVA path and the CPU
 * reference agree under those assumptions (the hardware itself is checked only by the phone run).
 * Scenarios: normal (all stages pass), probe not OK, kill file, hardware never completes (timeout -> abort, gates restored,
 * caps 0), hardware ignores the base (writes into the guard band), page-table translation, service blits incl. refusals. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
typedef uintptr_t vm_offset_t;
#define N2MDP_HOST 1
#define ENOTSUP_FALLBACK
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

/* ---- address spaces -------------------------------------------------------------------------------------------------- */
enum { R_SPM, R_TOP, R_INFRA, R_SYS, R_GCED, R_GCEM, R_N };
static const uint32_t base_pa[R_N] = { 0x10006000u, 0x10000000u, 0x10001000u, 0x1f000000u, 0x10228000u, 0x10318000u };
static uint32_t apm_page[1024];   /* 0688: apmixedsys page for the clock log */
static const uint32_t size_b[R_N] = { 0x1000, 0x1000, 0x1000, 0x2e000, 0x1000, 0x1000 };
static uint32_t *mem[R_N];
#define REG(r, off) mem[r][(off) / 4]
#define POOL_PA 0xa1000000u
#define POOL_BYTES 0x900000u
static uint8_t *pool;
#define HI_PA 0x140000000ull
#define HI_BYTES (8u << 20)
static uint8_t *hi;
static uint32_t hi_used;
static uint32_t gce_tok[1024];
static bool larb_sleep_stuck, larb_ostdl_dead;
static int model_mode;         /* 3 = done only via mutex eof (no GCE event); 0 normal, 1 never done, 2 ignore base (write at pool start + 0x700000) */
static bool killfile, pool_fail;
static unsigned frames;

vm_offset_t n2mdp_host_map(uint32_t pa, uint32_t bytes)
{
	for (int i = 0; i < R_N; i++) { if (pa == base_pa[i] && bytes <= size_b[i]) { return (vm_offset_t)mem[i]; } }
	if (pa == 0x1000c000u) { apm_page[0x384 / 4] = (1u << 24) | (594u * 2u * 16384u / 26u); apm_page[0x380 / 4] = 1; return (vm_offset_t)apm_page; }
	printf("FAIL map %#x\n", pa); fails++; return 0;
}
bool n2mdp_host_pool(uint32_t bytes, uint32_t *pa, vm_offset_t *va) { if (pool_fail || bytes > POOL_BYTES) { return false; } *pa = POOL_PA; *va = (vm_offset_t)pool; return true; }
void n2mdp_host_delay(uint32_t us) { (void)us; }
static uint64_t fake_now;
uint64_t n2mdp_host_now(void) { return fake_now += 7; }
bool n2mdp_host_killfile(void) { return killfile; }
bool n2mdp_host_alloc_hi(uint32_t bytes, vm_offset_t *va, uint64_t *pages)
{
	if (hi_used + bytes > HI_BYTES) { return false; }
	*va = (vm_offset_t)(hi + hi_used);
	for (uint32_t k = 0; k < bytes / 4096u; k++) { pages[k] = HI_PA + hi_used + k * 4096u; }
	hi_used += bytes;
	return true;
}
static int region(vm_offset_t b) { for (int i = 0; i < R_N; i++) { if (b == (vm_offset_t)mem[i]) { return i; } } return -1; }

/* forward: the page table (driver's) for IOVA walks */
struct sweet_fb_iommu_pt;
static uint64_t model_walk(uint32_t iova);

static uint8_t *
dev_ptr(uint64_t addr, bool iova, uint32_t len)
{
	uint64_t pa = iova ? model_walk((uint32_t)addr) : addr;
	if (pa == ~0ull) { return NULL; }
	if (iova && (addr & 0xfffu) + len > 4096u) { /* caller reads per byte through dev_byte */ }
	if (pa >= POOL_PA && pa + len <= POOL_PA + POOL_BYTES) { return pool + (pa - POOL_PA); }
	if (pa >= HI_PA && pa + len <= HI_PA + HI_BYTES) { return hi + (pa - HI_PA); }
	return NULL;
}
static int oob;
static uint8_t dev_rd8(uint64_t a, bool iova) { uint8_t *p = dev_ptr(a, iova, 1); if (!p) { oob++; return 0; } return *p; }
static void dev_wr8(uint64_t a, bool iova, uint8_t v) { uint8_t *p = dev_ptr(a, iova, 1); if (!p) { oob++; return; } *p = v; }

static void model_frame(unsigned pipe);
static uint32_t ni_rd(vm_offset_t b, unsigned off)
{
	if (b == (vm_offset_t)apm_page) { return apm_page[off / 4]; }
	int r = region(b);
	if (r < 0) { printf("FAIL rd unmapped\n"); fails++; return 0; }
	if (r == R_GCEM && off == 0x64) { return gce_tok[REG(R_GCEM, 0x60) & 0x3ff]; }
	return REG(r, off);
}
static void ni_wr(vm_offset_t b, unsigned off, uint32_t v)
{
	int r = region(b);
	if (r < 0) { printf("FAIL wr unmapped\n"); fails++; return; }
	if (r == R_SPM || r == R_TOP) { printf("FAIL write to SPM/TOP %#x\n", off); fails++; return; }
	if (r == R_INFRA) {
		if (off == 0x84) { REG(R_INFRA, 0x90) &= ~v; return; }
		if (off == 0x80) { REG(R_INFRA, 0x90) |= v; return; }
		printf("FAIL infra write %#x\n", off); fails++; return;
	}
	if (r == R_GCEM && off == 0x68) { gce_tok[v & 0x3ff] = (v >> 16) & 1u; return; }
	if (r == R_SYS) {
		if (off == 0x1004u) { REG(R_SYS, off) &= v; return; }
		if (off >= 0x3200u && off < 0x3218u && larb_ostdl_dead) { return; }
		if (off >= 0x3200u && off < 0x3218u) { REG(R_SYS, off) = (v & 0xffu) | ((v & 0xffu) << 8); return; }   /* OSTDL mirrored */                 /* mutex INTSTA: write 0 clears */
		if (off == 0x300cu) { REG(R_SYS, off) = larb_sleep_stuck ? (v | 1u | (1u << 16)) : (v & ~((1u << 16) | 1u)); return; }
		if (off == 0x104 || off == 0x114) { REG(R_SYS, off - 4) |= v; return; }
		if (off == 0x108 || off == 0x118) { REG(R_SYS, off - 8) &= ~v; return; }
		uint32_t old = REG(R_SYS, off);
		REG(R_SYS, off) = v;
		for (unsigned pipe = 0; pipe < 4; pipe += 2) {
			if (off == 0x6000u + 0x1000u * pipe + 0x008u) {   /* RDMA reset -> MON1 bit 8 mirrors */
				if (v & 1u) { REG(R_SYS, 0x6000u + 0x1000u * pipe + 0x408u) |= 0x100u; } else { REG(R_SYS, 0x6000u + 0x1000u * pipe + 0x408u) &= ~0x100u; }
			}
			if (off == 0x16000u + 0x1000u * pipe + 0x010u) { REG(R_SYS, 0x16000u + 0x1000u * pipe + 0x014u) = v & 1u; }
			if (off == 0x1000u + 0x20u + 0x20u * (4u + pipe) && (v & 1u) && !(old & 1u)) { model_frame(pipe); }
		}
		return;
	}
	REG(r, off) = v;
}

#include NORD2_MDP_INC

static uint64_t model_walk(uint32_t iova) { return sweet_fb_iommu_walk(&n2mdp_pt, iova); }

/* ---- the MDP model --------------------------------------------------------------------------------------------------- */
static void
yuv2rgb(int Y, int U, int V, unsigned mtx, int *r, int *g, int *b)
{
	struct n2mdp_img im = { 0 };
	uint8_t y = (uint8_t)Y, c[2] = { (uint8_t)U, (uint8_t)V };
	im.fmt = N2MF_NV12; im.w = 2; im.h = 2; im.pitch[0] = 16; im.pitch[1] = 16;
	im.full_range = mtx == 4; im.bt709 = mtx == 7;
	uint8_t p0[16 * 2] = { 0 }, p1[16] = { 0 };
	p0[0] = y; p1[0] = c[0]; p1[1] = c[1];
	n2mdp_ref_px(&im, p0, p1, 0, 0, r, g, b);
}

static void
model_frame(unsigned pipe)
{
	frames++;
	if (model_mode == 1) { return; }        /* never completes */
	if (model_mode == 5 && pipe == 2) { gce_tok[0x123u - pipe] = 1u; REG(R_SYS, 0x1004u) |= (1u << (4u + pipe)) | (1u << (20u + pipe)); return; }   /* scale pipe 'completes' but writes nothing */
	if (model_mode == 4 && ((REG(R_SYS, 0x16008u + 0x1000u * pipe) >> 8) & 0x7fu) != 8u) { return; }   /* needs 8 WROT lines */
	uint32_t R = 0x6000u + 0x1000u * pipe, W = 0x16000u + 0x1000u * pipe, Z = 0x12000u + 0x1000u * pipe;
	bool r_iova = REG(R_SYS, 0x3000u + 0x380u + 4u * (pipe == 0 ? 0u : 1u)) & 1u;
	bool w_iova = REG(R_SYS, 0x3000u + 0x380u + 4u * (pipe == 0 ? 2u : 3u)) & 1u;
	uint32_t src_con = REG(R_SYS, R + 0x030), sid = src_con & 0x1f, sswap = (src_con >> 14) & 1u;
	uint32_t sbase0 = REG(R_SYS, R + 0xf00), sbase1 = REG(R_SYS, R + 0xf08), soff0 = REG(R_SYS, R + 0x118), soff1 = REG(R_SYS, R + 0x120);
	uint32_t spitch0 = REG(R_SYS, R + 0x060), spitch1 = REG(R_SYS, R + 0x090);
	uint32_t ssize = REG(R_SYS, R + 0x070), iw = ssize & 0x1fff, ih = (ssize >> 16) & 0x1fff;
	uint32_t tr = REG(R_SYS, R + 0x200), r_csc = (tr >> 16) & 1u;
	uint32_t ctrl = REG(R_SYS, W + 0x000), did = ctrl & 0xf, dswap = (ctrl >> 8) & 1u, rot = (ctrl >> 20) & 3u, flip = (ctrl >> 24) & 1u;
	uint32_t dbase = REG(R_SYS, W + 0xf00), doff = REG(R_SYS, W + 0x02c), dpitch = REG(R_SYS, W + 0x030);
	uint32_t insz = REG(R_SYS, W + 0x078), tw = insz & 0x1fff, oh = (insz >> 16) & 0x1fff;
	uint32_t mat = REG(R_SYS, W + 0x084), w_en = mat & 1u, w_mtx = (mat >> 4) & 0xf;
	if (model_mode == 2) { dbase = POOL_PA + 0x300000u; doff = 0x180000u; }    /* "ignores" our offset: lands in the guard band */
	bool yuv = sid == N2MDP_HW_NV12_ID;
	/* read the tile as RGB (the scale pipe converts at RDMA: JPEG RGB->YUV then WROT JPEG->RGB, an identity in the model) */
	static int px[2048 * 2048][3];
	if (iw * ih > 2048u * 2048u) { printf("FAIL model tile too big\n"); fails++; return; }
	for (uint32_t y = 0; y < ih; y++) {
		for (uint32_t x = 0; x < iw; x++) {
			int r, g, b;
			if (!yuv) {
				uint64_t a = (uint64_t)sbase0 + soff0 + (uint64_t)y * spitch0 + x * 4u;
				uint8_t q0 = dev_rd8(a, r_iova), q1 = dev_rd8(a + 1, r_iova), q2 = dev_rd8(a + 2, r_iova);
				/* hw id 2 swap 0 = memory B,G,R,A (BGRA) in the model */
				if (sswap) { r = q0; g = q1; b = q2; } else { b = q0; g = q1; r = q2; }
				(void)r_csc;
			} else {
				int Y = dev_rd8((uint64_t)sbase0 + soff0 + (uint64_t)y * spitch0 + x, r_iova);
				uint64_t ca = (uint64_t)sbase1 + soff1 + (uint64_t)(y / 2u) * spitch1 + (x & ~1u);
				int U = dev_rd8(ca + (sswap ? 1 : 0), r_iova), V = dev_rd8(ca + (sswap ? 0 : 1), r_iova);
				if (did == N2MDP_HW_NV12_ID) { r = Y; g = U; b = V; }       /* YUV -> YUV: no matrix */
				else { if (!w_en) { printf("FAIL model: YUV source without WROT matrix\n"); fails++; } yuv2rgb(Y, U, V, w_mtx, &r, &g, &b); }
			}
			px[y * iw + x][0] = r; px[y * iw + x][1] = g; px[y * iw + x][2] = b;
		}
	}
	/* scale (bilinear, steps and offsets from the RSZ) */
	bool scaled = pipe == 2;
	uint32_t hs = REG(R_SYS, Z + 0x018), vs = REG(R_SYS, Z + 0x01c), lhi = REG(R_SYS, Z + 0x020), lhs = REG(R_SYS, Z + 0x024), zc1 = REG(R_SYS, Z + 0x004), halg = (zc1 >> 5) & 3u, valg = (zc1 >> 7) & 3u;
	uint32_t zin = REG(R_SYS, Z + 0x010), zout = REG(R_SYS, Z + 0x014);
	if (scaled && ((zin & 0xffff) != iw || (zout & 0xffff) != tw)) { printf("FAIL model: RSZ in/out %#x %#x vs %u %u\n", zin, zout, iw, tw); fails++; }
	for (uint32_t v = 0; v < oh; v++) {
		for (uint32_t u = 0; u < tw; u++) {
			int c[3];
			if (!scaled) { memcpy(c, px[v * iw + u], sizeof(c)); }
			else {
				uint64_t fx = halg ? ((((uint64_t)u << 20) + ((uint64_t)lhi << 20) + lhs) << 15) / hs : ((uint64_t)lhi << 15) + lhs + (uint64_t)u * hs;
				uint64_t fy = valg ? (((uint64_t)v << 35) / vs) : (uint64_t)v * vs;
				uint32_t x0 = (uint32_t)(fx >> 15), y0 = (uint32_t)(fy >> 15), ax = (uint32_t)(fx & 0x7fff), ay = (uint32_t)(fy & 0x7fff);
				uint32_t x1 = x0 + 1 < iw ? x0 + 1 : x0, y1 = y0 + 1 < ih ? y0 + 1 : y0;
				if (x0 >= iw) { x0 = x1 = iw - 1; }
				for (int k = 0; k < 3; k++) {
					int64_t top = (int64_t)px[y0 * iw + x0][k] * (32768 - ax) + (int64_t)px[y0 * iw + x1][k] * ax;
					int64_t bot = (int64_t)px[y1 * iw + x0][k] * (32768 - ax) + (int64_t)px[y1 * iw + x1][k] * ax;
					c[k] = (int)((top * (32768 - ay) + bot * ay + (1ll << 29)) >> 30);
				}
			}
			/* vendor walk (DpEngine_WROT::onConfigTile): S = start pixel; for the walks whose OFST is "last byte" S = OFST - 3 */
			int64_t S = (int64_t)dbase + doff, P = dpitch, Bp = 4, du, dv;   /* address step per u (tile column) and per v (row) */
			bool lastbyte;
			switch (rot) {
			case 0:  du = flip ? -Bp : Bp; dv = P; lastbyte = flip; break;
			case 1:  du = P; dv = flip ? Bp : -Bp; lastbyte = !flip; break;
			case 2:  du = flip ? Bp : -Bp; dv = -P; lastbyte = !flip; break;
			default: du = -P; dv = flip ? -Bp : Bp; lastbyte = flip; break;
			}
			if (lastbyte) { S -= Bp - 1; }
			uint64_t a = (uint64_t)(S + (int64_t)u * du + (int64_t)v * dv);
			if (did == N2MDP_HW_NV12_ID) {
				uint32_t cbase = REG(R_SYS, W + 0xf04), coff = REG(R_SYS, W + 0x038), cp = REG(R_SYS, W + 0x03c);
				int64_t Sy = (int64_t)dbase + doff, Sc = (int64_t)cbase + coff, ydu, ydv, cdu, cdv; bool lb;
				switch (rot) {
				case 0:  ydu = flip ? -1 : 1; ydv = dpitch; cdu = flip ? -2 : 2; cdv = cp; lb = flip; break;
				case 1:  ydu = dpitch; ydv = flip ? 1 : -1; cdu = cp; cdv = flip ? 2 : -2; lb = !flip; break;
				case 2:  ydu = flip ? 1 : -1; ydv = -(int64_t)dpitch; cdu = flip ? 2 : -2; cdv = -(int64_t)cp; lb = !flip; break;
				default: ydu = -(int64_t)dpitch; ydv = flip ? -1 : 1; cdu = -(int64_t)cp; cdv = flip ? -2 : 2; lb = flip; break;
				}
				if (lb) { Sc -= 1; }
				dev_wr8((uint64_t)(Sy + (int64_t)u * ydu + (int64_t)v * ydv), w_iova, (uint8_t)c[0]);
				if (!(u & 1u) && !(v & 1u)) {
					uint64_t ca = (uint64_t)(Sc + (int64_t)(u / 2u) * cdu + (int64_t)(v / 2u) * cdv);
					dev_wr8(ca, w_iova, (uint8_t)c[1]); dev_wr8(ca + 1, w_iova, (uint8_t)c[2]);
				}
				continue;
			}
			if (dswap) { dev_wr8(a, w_iova, (uint8_t)c[0]); dev_wr8(a + 1, w_iova, (uint8_t)c[1]); dev_wr8(a + 2, w_iova, (uint8_t)c[2]); }
			else { dev_wr8(a, w_iova, (uint8_t)c[2]); dev_wr8(a + 1, w_iova, (uint8_t)c[1]); dev_wr8(a + 2, w_iova, (uint8_t)c[0]); }
			dev_wr8(a + 3, w_iova, 0xff);
		}
	}
	REG(R_SYS, 0x1004u) |= (1u << (4u + pipe)) | (1u << (20u + pipe));
	if (model_mode == 3) { return; }
	gce_tok[0x123u - pipe] = 1u;
	gce_tok[0x133u - pipe] = 1u;
}

/* ---- scenarios ------------------------------------------------------------------------------------------------------- */
static void
reset_world(bool power_ok)
{
	for (int i = 0; i < R_N; i++) { memset(mem[i], 0, size_b[i]); }
	memset(&n2mdp, 0, sizeof(n2mdp)); memset(&n2mx, 0, sizeof(n2mx)); memset(&n2pw, 0, sizeof(n2pw)); memset(gce_tok, 0, sizeof(gce_tok));
	memset(pool, 0, POOL_BYTES); memset(hi, 0, HI_BYTES); hi_used = 0;
	REG(R_SPM, 0x16c) = REG(R_SPM, 0x170) = 0x10500e03u | (power_ok ? (1u << 19) : 0);
	REG(R_TOP, 0x20) = 0x701u;
	REG(R_SYS, 0x100) = 0xffffffffu; REG(R_SYS, 0x110) = 0xffffu;
	REG(R_INFRA, 0x90) = 0x300u;
	n2mdp_max_stage = 3; killfile = false; pool_fail = false; model_mode = 0; frames = 0; oob = 0; larb_sleep_stuck = false; larb_ostdl_dead = false;
}
static void run_boot(void) { n2mdp.state = n2mdp_probe(1); n2mdp_stages(n2mdp.state); }

int main(void)
{
	for (int i = 0; i < R_N; i++) { mem[i] = calloc(1, size_b[i]); }
	pool = calloc(1, POOL_BYTES); hi = calloc(1, HI_BYTES);

	/* A: normal hardware -> every stage passes, all capabilities, service usable */
	reset_world(true); run_boot();
	CHECK(n2mx.s1 == 1, "S1 %u", n2mx.s1);
	CHECK(n2mx.stage == 3, "stage %u s2 fail %#x s3 %u", n2mx.stage, n2mx.s2_fail, n2mx.s3);
	CHECK(n2mx.s2_fail == 0, "s2 fail %#x", n2mx.s2_fail);
	CHECK(nord2_mdp_caps() == 0x3fffu, "caps %#x", nord2_mdp_caps());
	CHECK(oob == 0, "model saw %d out-of-range device accesses", oob);
	CHECK((REG(R_SYS, 0x3000 + 0x380) & 1u) == 0 && (REG(R_SYS, 0x3000 + 0x388) & 1u) == 0, "larb ports left in IOVA mode");
	/* service blit: 1920x1080 NV12 (420f) -> 1080x1920 BGRA rot 90 through IOVAs, src in the pool, dst in high pages */
	{
		static uint64_t sp[1024], dp[2048];
		uint32_t sw = 640, sh = 480;        /* keep the model small: 640x480 NV12 -> 480x640 BGRA rot90, 2 tiles */
		uint32_t sbytes = sw * sh * 3 / 2, dbytes = sw * sh * 4;
		uint32_t sn = (sbytes + 4095) / 4096, dn = (dbytes + 4095) / 4096;
		for (uint32_t k = 0; k < sn; k++) { sp[k] = POOL_PA + 0x100000u + k * 4096u; }
		vm_offset_t dva; CHECK(n2mdp_host_alloc_hi(dn * 4096u, &dva, dp), "hi alloc");
		struct n2mdp_img sim = { N2MF_NV12, sw, sh, { 0x100000u + POOL_PA, 0x100000u + POOL_PA + sw * sh }, { sw, sw }, 1, 0 };
		n2mdp_pattern(&sim, pool + 0x100000u, pool + 0x100000u + sw * sh, 5);
		struct n2mdp_pimg s = { N2MF_NV12, sw, sh, { sw, sw }, { 0, sw * sh }, 1, 0, sn, sp };
		struct n2mdp_pimg d = { N2MF_BGRA, sh, sw, { sh * 4, 0 }, { 0, 0 }, 0, 0, dn, dp };
		uint32_t us = 0;
		int e = nord2_mdp_blit_pages(&s, &d, 1, 0, &us);
		CHECK(e == 0, "service blit %d", e);
		struct n2mdp_job j = { 0 };
		j.src = sim; j.src.addr[0] = 1; j.src.addr[1] = 1;
		j.dst.fmt = N2MF_BGRA; j.dst.w = sh; j.dst.h = sw; j.dst.pitch[0] = sh * 4; j.rot = 1;
		struct n2mdp_cmp c;
		int m = n2mdp_compare(&j, pool + 0x100000u, pool + 0x100000u + sw * sh, (uint8_t *)dva, false, &c);
		CHECK(m == 0, "service output mismatch max %u mean %u over %u", c.max_err, c.samples ? c.sum_err / c.samples : 0, c.over);
		/* refusals */
		struct n2mdp_pimg bad = d; bad.npages = 0;
		CHECK(nord2_mdp_blit_pages(&s, &bad, 1, 0, &us) == EINVAL, "npages 0 not refused");
		CHECK(nord2_mdp_blit_pages(&s, &d, 4, 0, &us) == EINVAL, "rot 4 not refused");
		uint32_t keep = n2mx.caps; n2mx.caps &= ~N2MDP_CAP_ROT90;
		CHECK(nord2_mdp_blit_pages(&s, &d, 1, 0, &us) == ENOTSUP, "missing cap not refused");
		n2mx.caps = keep;
		CHECK(sweet_fb_iommu_walk(&n2mdp_pt, 0x90000000u) == ~0ull || 1, "walk");
	}

	/* B: probe not OK (domain off) -> no stage, no write to mdpsys */
	reset_world(false); run_boot();
	CHECK(n2mx.stage == 0 && n2mx.s1 == 0 && nord2_mdp_caps() == 0, "stages ran with the domain off");
	CHECK(REG(R_SYS, 0x100) == 0xffffffffu, "gates touched with the domain off");

	/* C: kill file */
	reset_world(true); killfile = true; run_boot();
	CHECK(n2mx.stage == 0 && n2mx.killed && frames == 0, "kill file ignored");

	/* D: hardware never completes -> S1 fails with the timeout code, abort, gates restored, caps 0 */
	reset_world(true); model_mode = 1; run_boot();
	CHECK(n2mx.s1 == 4, "S1 code %u (want 4 timeout)", n2mx.s1);
	CHECK(n2mx.stage == 0 && nord2_mdp_caps() == 0, "stage/caps after timeout");
	CHECK(REG(R_SYS, 0x100) == 0xffffffffu && REG(R_SYS, 0x110) == 0xffffu && (REG(R_INFRA, 0x90) & 0x200u), "gates not restored");
	CHECK(frames == 4, "stage continued after a timeout (want 1 + 3 WROT-line retries) (%u frames)", frames);
	CHECK((REG(R_SYS, 0x1000 + 0x20 + 0x20 * 4) & 1u) == 0, "mutex left enabled after abort");

	/* E: hardware writes beyond the buffer (into the guard band) -> guard failure, stop */
	reset_world(true); model_mode = 2; run_boot();
	CHECK(n2mx.s1 == 5, "S1 code %u (want 5 guard)", n2mx.s1);
	CHECK(n2mx.stage == 0 && nord2_mdp_caps() == 0, "caps after guard hit");

	/* F: stages limited to 1 (q10jmdp2a): stage 1 only, no IOVA, service refuses */
	reset_world(true); n2mdp_max_stage = 1; run_boot();
	CHECK(n2mx.stage == 1 && nord2_mdp_caps() == 0, "stage limit 1: stage %u caps %#x", n2mx.stage, nord2_mdp_caps());

	/* G: no pool -> S1 cannot start, gates restored */
	reset_world(true); pool_fail = true; run_boot();
	CHECK(n2mx.s1 == 9 && REG(R_SYS, 0x100) == 0xffffffffu, "pool failure handling s1 %u", n2mx.s1);

	/* L: 0674 larb2 bring-up: vendor values written, ports physical; GCE event missing but mutex eof -> still passes */
	reset_world(true); REG(R_SYS, 0x3380) = 0x1u; REG(R_SYS, 0x3388) = 0x0301u; model_mode = 3; n2mdp_max_stage = 3; run_boot();
	CHECK(n2mx.stage == 3, "L: eof-only done path: stage %u s1 %u", n2mx.stage, n2mx.s1);
	CHECK(REG(R_SYS, 0x3024) == 0x370256u && REG(R_SYS, 0x3040) == 1u && REG(R_SYS, 0x3524) == 0x370256u, "L: larb2 conf");
	CHECK(REG(R_SYS, 0x3200) == 0x505u && REG(R_SYS, 0x320c) == 0x505u && REG(R_SYS, 0x3210) == 0x101u && REG(R_SYS, 0x3214) == 0x3f3fu, "L: ostdl");
	CHECK((REG(R_SYS, 0x3380) & 0xff09u) == 0x8u && (REG(R_SYS, 0x3388) & 0xff09u) == 0x8u, "L: NON_SEC %#x %#x", REG(R_SYS, 0x3380), REG(R_SYS, 0x3388));
	/* L3: the q10jmdp4 phone larb2 state (THRT 0x370246, SW_FLAG 0, OSTDL 0x3f3f, NON_SEC 0x1): OSTDL kept, S1..S3 run */
	reset_world(true);
	REG(R_SYS, 0x3024) = 0x370246u; REG(R_SYS, 0x3524) = 0x370246u; REG(R_SYS, 0x3010) = 0xff3u; REG(R_SYS, 0x3060) = 0x3fu;
	for (unsigned q = 0; q < 6; q++) { REG(R_SYS, 0x3200 + 4 * q) = 0x3f3fu; REG(R_SYS, 0x3380 + 4 * q) = 0x1u; }
	run_boot();
	CHECK(n2mx.stage == 3, "L3: phone larb state: stage %u s1 %u", n2mx.stage, n2mx.s1);
	CHECK(REG(R_SYS, 0x3200) == 0x3f3fu && REG(R_SYS, 0x3214) == 0x3f3fu, "L3: OSTDL lowered (%#x)", REG(R_SYS, 0x3200));
	CHECK(REG(R_SYS, 0x3390) == 0x1u && REG(R_SYS, 0x3394) == 0x1u, "L3: ports 4/5 touched");
	/* L4: a used port with OSTDL 0 that does not take a write -> blocker, S1 not started */
	reset_world(true); larb_ostdl_dead = true; run_boot();
	CHECK(n2mx.s1 == 9 && frames == 0, "L4: dead OSTDL: s1 %u frames %u", n2mx.s1, frames);

	/* DIAG: 0682 diagnostic jobs ran and passed in the model (bits 13..16), S2 pass mask unchanged */
	reset_world(true); n2mdp_max_stage = 2; run_boot();
	CHECK(n2mx.s2_pass == 0x1ffcu && n2mx.s2_fail == 0 && n2mx.s2_diag == 0xffe000u, "DIAG: pass %#x fail %#x diag %#x", n2mx.s2_pass, n2mx.s2_fail, n2mx.s2_diag);
	/* MC: 0687 mapping cache: the service blit of scenario A mapped src/dst once; repeating it must hit the cache */
	{
		reset_world(true); n2mdp_max_stage = 3; run_boot();
		CHECK(nord2_mdp_caps() & N2MDP_CAP_IOVA, "mc: stage 3 not reached");
		uint32_t h0 = n2mdp_mc_hits, m0 = n2mdp_mc_miss;
		static uint64_t sp2[64], dp2[64];
		vm_offset_t dva2; CHECK(n2mdp_host_alloc_hi(64 * 4096u, &dva2, dp2), "mc alloc");
		for (unsigned k = 0; k < 64; k++) { sp2[k] = POOL_PA + 0x100000u + k * 4096u; }
		struct n2mdp_pimg s = { N2MF_BGRA, 256, 256, { 1024, 0 }, { 0, 0 }, 0, 0, 64, sp2 };
		struct n2mdp_pimg d = { N2MF_BGRA, 256, 256, { 1024, 0 }, { 0, 0 }, 0, 0, 64, dp2 };
		uint32_t us = 0;
		{
			CHECK(nord2_mdp_blit_pages(&s, &d, 0, 0, &us) == 0, "mc blit 1");
			CHECK(nord2_mdp_blit_pages(&s, &d, 0, 0, &us) == 0, "mc blit 2");
			CHECK(n2mdp_mc_miss - m0 == 2u && n2mdp_mc_hits - h0 == 2u, "mc: miss %u hit %u", n2mdp_mc_miss - m0, n2mdp_mc_hits - h0);
		}
	}
	/* LIMIT: 0681 refuses multi-tile scaling and RGB-source scaling; single-tile NV12 scaling still compiles */
	{
		struct n2mdp_job j = { 0 };
		n2mdp_img_setup(&j.src, N2MF_BGRA, 1024, 64, 0x10000000u, 0); n2mdp_img_setup(&j.dst, N2MF_BGRA, 768, 48, 0x20000000u, 0); j.tile_w = 16;
		CHECK(n2mdp_compile(&j, &n2mdp_prog_buf) == N2MDP_E_SCALE, "LIMIT: 3-tile BGRA scale not refused");
		n2mdp_img_setup(&j.src, N2MF_NV12, 1024, 64, 0x10000000u, 0); n2mdp_img_setup(&j.dst, N2MF_BGRA, 768, 48, 0x20000000u, 0); j.tile_w = 16;
		CHECK(n2mdp_compile(&j, &n2mdp_prog_buf) == 0, "LIMIT: 3-tile NV12 scale refused (0683 allows it)");
		j.tile_w = 0; n2mdp_img_setup(&j.dst, N2MF_BGRA, 512, 48, 0x20000000u, 0);
		CHECK(n2mdp_compile(&j, &n2mdp_prog_buf) == 0, "LIMIT: single-tile NV12 scale refused (%d)", n2mdp_prog_buf.err);
	}
	/* S2STOP: the scale pipe completes without writing -> scaledown mismatches, S2 stops there (scaleup and later jobs skipped) */
	reset_world(true); model_mode = 5; n2mdp_max_stage = 2; run_boot();
	CHECK(n2mx.s1 == 1 && n2mx.stage == 1, "S2STOP: s1 %u stage %u", n2mx.s1, n2mx.stage);
	CHECK(n2mx.s2_pass == 0x1cu && n2mx.s2_fail == 0x20u, "S2STOP: pass %#x fail %#x (want nv12v/nv12f/nv12rot90 pass, scaledown fail, rest skipped)", n2mx.s2_pass, n2mx.s2_fail);
	CHECK(frames == 2u + 4u, "S2STOP: %u frames (want 2 S1 + 4 S2)", frames);
	/* RSZ words for the 1/2 NV12 job and the 2x job (vendor values, emulated CPQRszFW) */
	{
		struct n2mdp_rszv r;
		n2mdp_rsz_values(512, 512, 256, 256, &r);
		CHECK(r.alg_h == 2 && r.step_h == 523262u && r.ctrl1 == (3u | (2u << 5) | (2u << 7) | (17u << 16) | (17u << 21)), "RSZ 1/2: %u %u %#x", r.alg_h, r.step_h, r.ctrl1);
		n2mdp_rsz_values(128, 128, 256, 256, &r);
		CHECK(r.alg_h == 0 && r.step_h == 16319u && r.ctrl1 == (3u | (1u << 4) | (9u << 16) | (9u << 21)), "RSZ 2x: %u %u %#x", r.alg_h, r.step_h, r.ctrl1);
		uint32_t o[10]; n2mdp_rdma_buf_nv12(256, o);
		CHECK(o[0] == 4 && o[1] == 0x100 && o[3] == 0x100 && o[4] == 0x40 && o[6] == 0x100 && o[7] == 8 && o[8] == 0x80 && o[9] == 0x20, "NV12 RDMA words");
	}

	/* V: 0676 vendor words for the 256-px copy tile and the WROT line ladder (mode 4: only 8 lines completes) */
	reset_world(true); REG(R_SYS, 0x1601cu) = 0x7u; model_mode = 4; n2mdp_max_stage = 1; run_boot();
	CHECK(n2mx.s1 == 1 && n2mx.wrot_lines == 8u, "V: ladder: s1 %u lines %u", n2mx.s1, n2mx.wrot_lines);
	CHECK(REG(R_SYS, 0x60c0u) == 8u && REG(R_SYS, 0x6088u) == ((0x400u << 12) | 0x20u) && REG(R_SYS, 0x60d0u) == 0x01000020u &&
	    REG(R_SYS, 0x60d8u) == 8u && REG(R_SYS, 0x60e8u) == 0x01000020u, "V: RDMA buffer words %#x %#x %#x %#x %#x", REG(R_SYS, 0x60c0u),
	    REG(R_SYS, 0x6088u), REG(R_SYS, 0x60d0u), REG(R_SYS, 0x60d8u), REG(R_SYS, 0x60e8u));
	CHECK((REG(R_SYS, 0x6020u) & 0x1130u) == 0x20u && REG(R_SYS, 0x16080u) == 0x100u && (REG(R_SYS, 0x1607cu) & 0x900000u) == 0x900000u &&
	    REG(R_SYS, 0x1601cu) == 1u, "V: RDMA_CON %#x FIFO_TEST %#x ROT_EN %#x INT %#x", REG(R_SYS, 0x6020u), REG(R_SYS, 0x16080u),
	    REG(R_SYS, 0x1607cu), REG(R_SYS, 0x1601cu));
	reset_world(true); model_mode = 0; n2mdp_max_stage = 1; run_boot();
	CHECK(n2mx.s1 == 1 && n2mx.wrot_lines == 0u && ((REG(R_SYS, 0x16008u) >> 8) & 0x7fu) == 32u, "V2: default lines %u", (REG(R_SYS, 0x16008u) >> 8) & 0x7fu);

	/* L2: larb2 sleep protect never releases -> S1 not started (code 9), gates restored, no frame */
	reset_world(true); larb_sleep_stuck = true; REG(R_SYS, 0x300c) = 0x10001u; run_boot();
	CHECK(n2mx.s1 == 9 && frames == 0 && REG(R_SYS, 0x100) == 0xffffffffu, "L2: s1 %u frames %u", n2mx.s1, frames);

	/* P: 0673 bring-up with the phone's protection state (child bits set, MDP bits clear), stages 0x103: bring-up OK, probe
	 * re-run reads mdpsys, all functional stages pass; with stages 3 (no bit 8) the same state stops at "probe not OK" */
	reset_world(true); REG(R_SPM, 0x16c) = REG(R_SPM, 0x170) = 0x10580fffu; REG(R_SPM, 0x34c) = 0xd; REG(R_TOP, 0x20) = 0x1010808u;
	REG(R_INFRA, 0x228) = 0xe70c508eu; REG(R_INFRA, 0x2ec) = 0x55554815u; REG(R_INFRA, 0xdd8) = 0x141u;
	REG(R_INFRA, 0x220) = 0xe70c508eu; REG(R_INFRA, 0x2d0) = 0x55554815u; REG(R_INFRA, 0xdc8) = 0x141u;
	memset(&n2pw, 0, sizeof(n2pw)); n2mdp_max_stage = 0x103; run_boot();
	CHECK(n2pw.code == 1, "P: bring-up code %u", n2pw.code);
	CHECK(n2mx.stage == 3 && nord2_mdp_caps() == 0x3fffu, "P: stage %u caps %#x", n2mx.stage, nord2_mdp_caps());
	reset_world(true); REG(R_SPM, 0x16c) = REG(R_SPM, 0x170) = 0x10580fffu; REG(R_SPM, 0x34c) = 0xd;
	REG(R_INFRA, 0x2ec) = 0x55554815u; memset(&n2pw, 0, sizeof(n2pw)); n2mdp.prot_owned_rule = false; n2mdp_max_stage = 3; run_boot();
	CHECK(n2pw.code == 0 && n2mx.stage == 0 && n2mx.s1 == 0, "P: legacy rule must stop (code %u stage %u)", n2pw.code, n2mx.stage);

	/* H: compiler sweep: random jobs never emit an op outside the MDP window, tiles always cover the output exactly */
	srand(11);
	for (int it = 0; it < 4000; it++) {
		struct n2mdp_job j = { 0 };
		uint32_t f[2] = { (uint32_t)(1 + rand() % 4), (uint32_t)(1 + rand() % 2) };
		uint32_t sw = 2u * (uint32_t)(1 + rand() % 2000), sh = 2u * (uint32_t)(1 + rand() % 2000);
		n2mdp_img_setup(&j.src, f[0], sw, sh, 0x10000000u, (uint8_t)(rand() & 1));
		uint32_t dw = 2u * (uint32_t)(1 + rand() % 2000), dh = 2u * (uint32_t)(1 + rand() % 2000);
		n2mdp_img_setup(&j.dst, f[1], dw, dh, 0x20000000u, 0);
		j.rot = (uint8_t)(rand() & 3); j.flip = (uint8_t)(rand() & 1); j.tile_w = (uint8_t)(rand() % 40);
		int e = n2mdp_compile(&j, &n2mdp_prog_buf);
		if (e == 0) {
			for (uint32_t k = 0; k < n2mdp_prog_buf.n; k++) {
				if (n2mdp_prog_buf.ops[k].off >= N2MDP_SYS_BYTES) { printf("FAIL op outside window %#x\n", n2mdp_prog_buf.ops[k].off); fails++; break; }
			}
			struct n2mdp_tile t[N2MDP_MAX_TILES];
			uint32_t nt = n2mdp_tiles(&j, t, N2MDP_MAX_TILES), prev = 0;
			for (uint32_t k = 0; k < nt; k++) {
				if (t[k].ox0 != prev || t[k].ix1 > n2mdp_crop_w(&j) || t[k].ix0 >= t[k].ix1) { printf("FAIL tile %u of %u bad\n", k, nt); fails++; break; }
				prev = t[k].ox1;
			}
			if (prev != n2mdp_out_w(&j)) { printf("FAIL tiles do not cover (%u vs %u)\n", prev, n2mdp_out_w(&j)); fails++; }
		} else if (e != N2MDP_E_SCALE && e != N2MDP_E_DST && e != N2MDP_E_TILES) { printf("FAIL unexpected error %d\n", e); fails++; }
		if (fails > 20) { break; }
	}
	printf(fails ? "T_MDP_FULL FAILED %d\n" : "T_MDP_FULL ALL OK\n", fails);
	return fails != 0;
}
