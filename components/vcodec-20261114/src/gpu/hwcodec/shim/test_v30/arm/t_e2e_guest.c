/* SPDX-License-Identifier: MIT */
/*
 * t_e2e_guest.c -- a REAL ARM shared object that imports the V30 shims, used by run_tests_e2e.sh.
 *
 * Compiled for armv7a-linux-androideabi (ARM mode, -march=armv8-a, -mfloat-abi=soft, -fPIC -shared -nostdlib,
 * -fno-stack-protector -fno-unwind-tables), loaded by V03's real loader and executed by V02's real core.  It calls
 * snprintf with compiler-generated AAPCS varargs (including a double and a 64-bit integer), qsort with its own ARM
 * comparator, malloc/free and strlen/strcmp/memcpy.  The host driver compares g_out with its own snprintf and
 * checks g_ok.
 */
typedef unsigned int u32;

extern int snprintf(char *, unsigned, const char *, ...);
extern void *malloc(unsigned);
extern void free(void *);
extern int qsort(void *, unsigned, unsigned, int (*)(const void *, const void *));
extern unsigned strlen(const char *);
extern int strcmp(const char *, const char *);
extern void *memcpy(void *, const void *, unsigned);

char g_out[128];
int g_out_len;
u32 g_arr[64];
int g_ok;
void *g_leak;          /* keeps the malloc result alive so the compiler cannot elide malloc/free */

int cmp_u32(const void *a, const void *b)
{
	u32 x = *(const u32 *)a, y = *(const u32 *)b;

	return x < y ? -1 : x > y ? 1 : 0;
}

int run(void)
{
	int n;
	u32 i, j, x;
	void *p;
	char tmp[8];

	n = snprintf(g_out, sizeof g_out, "%d/%s/%llx/%.2f/%c/%%/%08x", -7, "arm", 0xdeadbeefcafeULL, 3.5,
		     'Z', 0x1234u);
	g_out_len = n;

	for (i = 0; i < 64; i++)
		g_arr[i] = (i * 37u + 11u) % 64u;
	for (i = 63; i > 0; i--) {
		j = (i * 13u + 7u) % (i + 1);
		x = g_arr[i];
		g_arr[i] = g_arr[j];
		g_arr[j] = x;
	}
	qsort(g_arr, 64, 4, cmp_u32);

	g_ok = 1;
	for (i = 0; i < 64; i++)
		if (g_arr[i] != i)
			g_ok = 0;

	p = malloc(100);
	g_leak = p;
	if (!p) {
		g_ok = 0;
	} else {
		char *q = (char *)p;

		q[0] = 'A';
		q[99] = 'B';
		if (strlen(q) != 1)
			g_ok = 0;
		free(q);
		g_leak = 0;
	}
	memcpy(tmp, "abc", 4);
	if (strcmp(tmp, "abc") != 0)
		g_ok = 0;
	return g_ok;
}
