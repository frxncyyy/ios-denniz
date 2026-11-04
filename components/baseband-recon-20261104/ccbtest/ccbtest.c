/* Linux-side oracle: can the AP (Linux, NS EL1) map and write the CCB shared memory (0x8b500000, 2 MiB cacheable bank) right after boot? raw syscalls, aarch64 */
typedef unsigned long u64; typedef long i64; typedef unsigned int u32;
static i64 sc(i64 n, i64 a, i64 b, i64 c, i64 d, i64 e, i64 f) {
	register i64 x8 __asm__("x8") = n; register i64 x0 __asm__("x0") = a; register i64 x1 __asm__("x1") = b;
	register i64 x2 __asm__("x2") = c; register i64 x3 __asm__("x3") = d; register i64 x4 __asm__("x4") = e; register i64 x5 __asm__("x5") = f;
	__asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5) : "memory", "cc");
	return x0;
}
static void say(const char *s) { u64 n = 0; while (s[n]) n++; sc(64, 1, (i64)s, n, 0, 0, 0); }
static void hex(u64 v) { char b[19]; int i; b[0] = '0'; b[1] = 'x'; for (i = 0; i < 16; i++) b[2 + i] = "0123456789abcdef"[(v >> (60 - 4 * i)) & 15]; b[18] = 0; say(b); }
void _start(void) {
	const char *dev[] = { "/dev/ccci_ccb_dhl", "/dev/ccci_ccb_md_mon", "/dev/ccci_ccb_meta", "/dev/ccci_ccb_ctrl" };
	int d;
	for (d = 0; d < 4; d++) {
		i64 fd = sc(56, -100, (i64)dev[d], 2, 0, 0, 0);
		say(dev[d]); say(": open -> "); hex((u64)fd); say("\n");
		if (fd < 0) continue;
		for (u64 len = 0x1000; len <= 0x200000; len <<= 1) {
			i64 p = sc(222, 0, len, 3, 1, fd, 0);          /* mmap(NULL,len,PROT_READ|WRITE,MAP_SHARED,fd,0) */
			say("  mmap "); hex(len); say(" -> "); hex((u64)p); say("\n");
			if (p < 0 && p > -4096) break;
			volatile u32 *w = (volatile u32 *)p;
			say("   first word "); hex(w[0]); say("\n");
			if (len == 0x1000 || len == 0x200000) {
				u32 old = w[1]; w[1] = 0x12345678; say("   wrote w[1], readback "); hex(w[1]); say("\n"); w[1] = old;
			}
			if (len == 0x200000) { say("   zeroing the whole mapping...\n"); for (u64 i = 0; i < len / 4; i++) w[i] = 0; say("   zeroed OK\n"); }
		}
	}
	sc(94, 0, 0, 0, 0, 0, 0);
	for (;;) { }
}
