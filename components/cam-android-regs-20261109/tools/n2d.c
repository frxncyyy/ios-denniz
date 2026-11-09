/* one-shot register probe module: executes an op blob patched into the .ko, prints via printk, fails init so it unloads */
typedef unsigned long u64; typedef unsigned int u32; typedef unsigned short u16;
struct op { u64 op, a, b, c; };
#define NOPS 250
struct blob { char magic[8]; u64 printk, ioremap, iounmap, msleep; u64 prot; char strs[1024]; struct op ops[NOPS]; };
__attribute__((section(".data"), used)) volatile struct blob n2d_blob = { .magic = "N2DOPS01" };

typedef int (*printk_t)(const char *, ...);
typedef void *(*ioremap_t)(u64, u64, u64);
typedef void (*iounmap_t)(volatile void *);
typedef void (*msleep_t)(unsigned int);
typedef u64 (*fn_t)(u64, u64, u64, u64);
typedef u64 (*fn6_t)(u64, u64, u64, u64, u64, u64);
static u64 R[16];
static const unsigned short W_OFF[] = {0x10,0x14,0x18,0x20,0x2c,0x30,0x4c,0x50,0x58,0x5c,0x130,0x134,0x138,0x13c,0x140,0x194,0x1a4,0x2d0,0x2e0,0x400,0x418,0x420,0x424,0x428,0x430,0x434,0x438,0x43c,0x440,0x444,0x448,0x800,0x804,0xa00,0xa04,0x48,0x0,0x3c,0x38};
#define NW (sizeof(W_OFF) / sizeof(W_OFF[0]))
struct ev { u32 t; u16 off; u32 v; };
static struct ev EV[6000]; static u32 W_LAST[NW], W_CNT[NW];
static inline u64 cntvct(void) { u64 v; __asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(v)); return v; }

static u32 rd32(volatile u32 *p) { return *p; }

int init_module(void)
{
	volatile struct blob *B = &n2d_blob;
	printk_t pk = (printk_t)B->printk;
	ioremap_t ior = (ioremap_t)B->ioremap;
	iounmap_t iou = (iounmap_t)B->iounmap;
	msleep_t ms = (msleep_t)B->msleep;
	int skip = 0, i;
#define VAL(x) ((((x) >> 56) == 0xee) ? R[(x) & 0xf] : (((x) >> 56) == 0xef) ? (u64)&B->strs[(x) & 0x3ff] : (x))
	pk("\0016N2D begin\n");
	for (i = 0; i < NOPS; i++) {
		struct op o = { B->ops[i].op, B->ops[i].a, B->ops[i].b, B->ops[i].c };
		if (o.op == 0) break;
		if (o.op == 4) { if (skip != 2) skip = 0; continue; }
		if (o.op == 19) { if (!skip && VAL(o.a) == 0) { skip = 2; pk("\0016N2D ABORT zero\n"); } continue; }
		if (o.op == 20) { if (!skip && VAL(o.a) != 0) { skip = 2; pk("\0016N2D ABORT nonzero\n"); } continue; }
		if (skip) continue;
		if (o.op == 1 || o.op == 2 || o.op == 3 || o.op == 9) {	/* phys: 1 R a n, 2 W a v, 3 G a mask val, 9 M a mask val (rmw) */
			u64 base = o.a & ~0xfffUL, off = o.a & 0xfff;
			u64 n = o.op == 1 ? o.b : 1, sz = (off + n * 4 + 0xfff) & ~0xfffUL;
			volatile char *v = ior(base, sz, B->prot);
			if (!v) { pk("\0016N2D ioremap fail %lx\n", o.a); continue; }
			volatile u32 *p = (volatile u32 *)(v + off);
			if (o.op == 1) {
				u64 k;
				for (k = 0; k < n; k += 4) {
					u32 w[4] = {0,0,0,0}; u64 j;
					for (j = 0; j < 4 && k + j < n; j++) w[j] = rd32(p + k + j);
					pk("\0016N2D %09lx: %08x %08x %08x %08x\n", o.a + k * 4, w[0], w[1], w[2], w[3]);
				}
			} else if (o.op == 2) {
				*p = (u32)VAL(o.b); pk("\0016N2D W %09lx <- %08x now %08x\n", o.a, (u32)VAL(o.b), rd32(p));
			} else if (o.op == 9) {
				u32 old = rd32(p); *p = (old & ~(u32)o.b) | (u32)o.c;
				pk("\0016N2D M %09lx %08x -> %08x\n", o.a, old, rd32(p));
			} else {
				u32 x = rd32(p);
				if ((x & (u32)o.b) != (u32)o.c) { skip = 1; pk("\0016N2D GUARD %09lx=%08x fail, skip\n", o.a, x); }
				else pk("\0016N2D GUARD %09lx=%08x ok\n", o.a, x);
			}
			iou(v);
		} else if (o.op == 5) {	/* call fn a with args from ops c? : b = arg0, c = arg1 */
			u64 r = ((fn_t)o.a)(o.b, o.c, 0, 0);
			pk("\0016N2D CALL %lx(%lx,%lx) = %lx\n", o.a, o.b, o.c, r);
		} else if (o.op == 6) {	/* kernel virtual read: a addr, b count u64 */
			u64 k; volatile u64 *p = (volatile u64 *)o.a;
			for (k = 0; k < o.b; k += 2) pk("\0016N2D K %lx: %016lx %016lx\n", o.a + k * 8, p[k], p[k + 1]);
		} else if (o.op == 7) {	/* deref: ptr at a, then read b u32 at ptr+c */
			volatile u32 *p = (volatile u32 *)(*(volatile u64 *)o.a + o.c); u64 k;
			pk("\0016N2D P %lx -> %lx\n", o.a, (u64)p);
			if ((u64)p > 0xffffff0000000000UL)
				for (k = 0; k < o.b; k += 4) pk("\0016N2D P+%lx: %08x %08x %08x %08x\n", o.c + k * 4, p[k], p[k+1], p[k+2], p[k+3]);
		} else if (o.op == 8) {
			ms((u32)o.a);
		} else if (o.op == 11) {	/* fill kernel va a with u32 b, c bytes */
			volatile u32 *p = (volatile u32 *)VAL(o.a); u64 k;
			for (k = 0; k < o.c / 4; k++) p[k] = (u32)o.b;
			pk("\0016N2D FILL %lx %x %lx\n", o.a, (u32)o.b, o.c);
		} else if (o.op == 12) {	/* check: count u32 != b in c bytes at a */
			volatile u32 *p = (volatile u32 *)VAL(o.a); u64 k, n = 0, first = ~0UL, last = 0;
			for (k = 0; k < o.c / 4; k++) if (p[k] != (u32)o.b) { n++; if (first == ~0UL) first = k; last = k; }
			pk("\0016N2D CHECK %lx: %lu words changed, first %lx last %lx\n", o.a, n, first * 4, last * 4);
			if (n) { u64 f = first & ~3UL; pk("\0016N2D CHECK data@%lx: %08x %08x %08x %08x\n", f * 4, p[f], p[f+1], p[f+2], p[f+3]); }
		} else if (o.op == 13) {
			R[o.a & 0xf] = ((fn6_t)VAL(o.b))(R[8], R[9], R[10], R[11], R[12], R[13]);
			pk("\0016N2D R%lu = %lx\n", o.a & 0xf, R[o.a & 0xf]);
		} else if (o.op == 14) {
			R[o.a & 0xf] = VAL(o.b) + o.c;
		} else if (o.op == 16) {
			R[o.a & 0xf] = *(volatile u64 *)(VAL(o.b) + o.c);
			pk("\0016N2D R%lu = [%lx] = %lx\n", o.a & 0xf, VAL(o.b) + o.c, R[o.a & 0xf]);
		} else if (o.op == 17) {	/* watch camsv at phys a for b ticks (13 MHz); c = camsys CG bit */
			volatile char *spm = ior(0x10006000, 0x1000, B->prot), *cs = ior(0x1a000000, 0x1000, B->prot), *sv = ior(o.a, 0x1000, B->prot);
			volatile char *ifr = ior(0x10001000, 0x1000, B->prot);
			u64 t0 = cntvct(), n = 0, k, ok_since = 0; int on = 0, prev_on = -1;
			for (k = 0; k < NW; k++) { W_LAST[k] = 0xdeadbeef; W_CNT[k] = 0; }
			while (cntvct() - t0 < o.b && n < 6000) {
				{ u32 st = *(volatile u32 *)(spm + 0x16c), st2 = *(volatile u32 *)(spm + 0x170);
				  u32 mm = *(volatile u32 *)(ifr + 0x2ec), p2 = *(volatile u32 *)(ifr + 0x724), p1 = *(volatile u32 *)(ifr + 0x258);
				  int pw = ((st & st2 & 0x880000) == 0x880000) && !(mm & 0x3f) && !(p2 & 2) && !(p1 & (1 << 22));
				  if (!pw) { ok_since = 0; on = 0; }
				  else { if (!ok_since) ok_since = cntvct(); on = (cntvct() - ok_since) > 26000; }
				}
				if (on) on = !((*(volatile u32 *)(cs + 0) >> o.c) & 1);
				if (on != prev_on) { EV[n].t = (u32)(cntvct() - t0); EV[n].off = 0xffff; EV[n].v = on; n++; prev_on = on; }
				if (!on) continue;
				for (k = 0; k < NW && n < 6000; k++) {
					u32 v = *(volatile u32 *)(sv + W_OFF[k]);
					if (v != W_LAST[k] && (W_OFF[k] > 0x3c && W_OFF[k] != 0x48 || ++W_CNT[k] < 6)) { EV[n].t = (u32)(cntvct() - t0); EV[n].off = W_OFF[k]; EV[n].v = v; n++; W_LAST[k] = v; }
				}
			}
			iou(spm); iou(cs); iou(sv); iou(ifr);
			pk("\0016N2D WATCH %lu events\n", n);
			for (k = 0; k < n; k++) pk("\0016N2D EV %u %x %08x\n", EV[k].t, EV[k].off, EV[k].v);
		} else if (o.op == 18) {
			R[o.a & 0xf] = VAL(o.b) + VAL(o.c);
			pk("\0016N2D R%lu = %lx\n", o.a & 0xf, R[o.a & 0xf]);
		} else if (o.op == 21) {	/* watch seninf: a = phys page, offsets [b, c) step 4, for 8 s */
			volatile char *spm = ior(0x10006000, 0x1000, B->prot), *cs = ior(0x1a000000, 0x1000, B->prot), *sv = ior(o.a, 0x1000, B->prot);
			volatile char *ifr = ior(0x10001000, 0x1000, B->prot);
			static u32 LAST[1024];
			u64 t0 = cntvct(), n = 0, k, ok_since = 0; int on = 0, prev_on = -1;
			for (k = 0; k < 1024; k++) LAST[k] = 0xdeadbeef;
			while (cntvct() - t0 < 8 * 13000000UL && n < 6000) {
				{ u32 st = *(volatile u32 *)(spm + 0x16c), st2 = *(volatile u32 *)(spm + 0x170);
				  u32 mm = *(volatile u32 *)(ifr + 0x2ec), p2 = *(volatile u32 *)(ifr + 0x724), p1 = *(volatile u32 *)(ifr + 0x258);
				  int pw = ((st & st2 & 0x880000) == 0x880000) && !(mm & 0x3f) && !(p2 & 2) && !(p1 & (1 << 22));
				  if (!pw) { ok_since = 0; on = 0; } else { if (!ok_since) ok_since = cntvct(); on = (cntvct() - ok_since) > 26000; } }
				if (on) on = !((*(volatile u32 *)(cs + 0) >> 8) & 1);
				if (on != prev_on) { EV[n].t = (u32)(cntvct() - t0); EV[n].off = 0xffff; EV[n].v = on; n++; prev_on = on; }
				if (!on) continue;
				for (k = o.b; k < o.c && n < 6000; k += 4) {
					u32 v = *(volatile u32 *)(sv + k);
					if (v != LAST[k / 4]) { EV[n].t = (u32)(cntvct() - t0); EV[n].off = k; EV[n].v = v; n++; LAST[k / 4] = v; }
				}
			}
			iou(spm); iou(cs); iou(sv); iou(ifr);
			pk("\0016N2D WATCH %lu events\n", n);
			for (k = 0; k < n; k++) pk("\0016N2D EV %u %x %08x\n", EV[k].t, EV[k].off, EV[k].v);
		} else if (o.op == 22) {	/* wait until CAM+MDP on, prot released, camsys CG bits b clear, for a ticks */
			volatile char *spm = ior(0x10006000, 0x1000, B->prot), *ifr = ior(0x10001000, 0x1000, B->prot), *cs = ior(0x1a000000, 0x1000, B->prot);
			u64 t0 = cntvct(), ok_since = 0; int done = 0;
			while (cntvct() - t0 < o.a) {
				u32 st = *(volatile u32 *)(spm + 0x16c), st2 = *(volatile u32 *)(spm + 0x170);
				u32 mm = *(volatile u32 *)(ifr + 0x2ec), p2 = *(volatile u32 *)(ifr + 0x724), p1 = *(volatile u32 *)(ifr + 0x258);
				int pw = ((st & st2 & 0x880000) == 0x880000) && !(mm & 0x3f) && !(p2 & 2) && !(p1 & (1 << 22));
				if (!pw) { ok_since = 0; continue; }
				if (!ok_since) ok_since = cntvct();
				if (cntvct() - ok_since < 26000) continue;
				if (*(volatile u32 *)(cs + 0) & (u32)o.b) continue;
				done = 1; break;
			}
			pk("\0016N2D WAITCAM %s after %lu us\n", done ? "ok" : "TIMEOUT", (cntvct() - t0) / 13);
			if (!done) skip = 2;
			iou(spm); iou(ifr); iou(cs);
		} else if (o.op == 10) {
			pk("\0016N2D MARK %s\n", (const char *)&B->strs[o.a]);
		}
	}
	pk("\0016N2D end\n");
	return -11;
}
void cleanup_module(void) {}

struct mod_hack { char a[0x18]; char name[56]; char b[0x178 - 0x18 - 56]; int (*init)(void); char c[0x360 - 0x180]; void (*exit)(void); char d[0x3c0 - 0x368]; };
__attribute__((section(".gnu.linkonce.this_module"), aligned(64), used)) struct mod_hack __this_module = { .name = "n2d", .init = init_module };
__attribute__((section(".modinfo"), used, aligned(1))) const char n2d_modinfo[] = "license=GPL\0name=n2d\0";
__asm__(".section .plt,\"ax\",@nobits\n.p2align 4\n.section .init.plt,\"ax\",@nobits\n.p2align 4\n.section .text.ftrace_trampoline,\"ax\",@nobits\n.p2align 4\n.text\n");
