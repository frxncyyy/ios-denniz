/* one-shot register probe module: executes an op blob patched into the .ko, prints via printk, fails init so it unloads */
typedef unsigned long u64; typedef unsigned int u32;
struct op { u64 op, a, b, c; };
#define NOPS 250
struct blob { char magic[8]; u64 printk, ioremap, iounmap, msleep; u64 prot; char strs[1024]; struct op ops[NOPS]; };
__attribute__((section(".data"), used)) volatile struct blob n2d_blob = { .magic = "N2DOPS01" };

typedef int (*printk_t)(const char *, ...);
typedef void *(*ioremap_t)(u64, u64, u64);
typedef void (*iounmap_t)(volatile void *);
typedef void (*msleep_t)(unsigned int);
typedef u64 (*fn_t)(u64, u64, u64, u64);

static u32 rd32(volatile u32 *p) { return *p; }

int init_module(void)
{
	volatile struct blob *B = &n2d_blob;
	printk_t pk = (printk_t)B->printk;
	ioremap_t ior = (ioremap_t)B->ioremap;
	iounmap_t iou = (iounmap_t)B->iounmap;
	msleep_t ms = (msleep_t)B->msleep;
	int skip = 0, i;
	pk("\0016N2D begin\n");
	for (i = 0; i < NOPS; i++) {
		struct op o = { B->ops[i].op, B->ops[i].a, B->ops[i].b, B->ops[i].c };
		if (o.op == 0) break;
		if (o.op == 4) { skip = 0; continue; }
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
				*p = (u32)o.b; pk("\0016N2D W %09lx <- %08x now %08x\n", o.a, (u32)o.b, rd32(p));
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
