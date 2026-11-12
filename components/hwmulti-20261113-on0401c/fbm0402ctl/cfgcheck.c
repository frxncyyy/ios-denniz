/* cfgcheck CONFIG-LINE: exit 0 if n2fbhwctl's own parser accepts it (used by apps-20261104/rehearse-fbhwctl.sh) */
#include <stdio.h>
#define N2FBHWCTL_HOST 1
static void out(const char *s) { puts(s); }
static int sc_get(const char *n, int *v) { (void)n; *v = 0; return 0; }
static int sc_set(const char *n, int v) { (void)n; (void)v; return 0; }
static void nap(unsigned s) { (void)s; }
static int restart_proc(const char *n, int *p) { (void)n; *p = -1; return 0; }
#include "n2fbhwctl.c"
int main(int argc, char **argv)
{
	struct fbcfg c; int bad = argc == 2 ? fbcfg_parse(argv[1], &c) : 99;
	if (bad) { printf("config refused at token %d\n", bad); return 1; }
	(void)fbctl_run; printf("config ok: %d modes, period %d, delay %d, pool %d\n", c.nmodes, c.period, c.delay, c.pool); return 0;
}
