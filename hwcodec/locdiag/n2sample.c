/* SPDX-License-Identifier: MIT
 * n2sample (12 Nov 2026): "where is every thread of process X waiting" for the Nord2 iOS port, printed to /dev/console.
 * For each named process (and each repeat): pid, dyld shared-cache slide, main image load address, then per thread:
 * run state/flags/sleep/cpu, name, dispatch queue address, pc lr sp fp x0..x3, the first 24 bytes at x0 (a mach_msg header
 * when the thread sits in mach_msg: bits size remote local voucher id) and the frame-pointer chain (up to 32 return
 * addresses). Lines start with "N2SAMPLE". The task is suspended while its threads are read (task_suspend/task_resume).
 * Symbolize on the host: cache addresses - slide with scratchpad/tools/img.py; main-image addresses - mh + 0x100000000.
 * usage: n2sample [--gate FILE] [--delay S] [--repeat N] [--interval S] NAME...
 */
typedef unsigned char u8; typedef unsigned int u32; typedef int i32; typedef unsigned long long u64; typedef long long i64;
typedef unsigned long size_t; typedef u32 mach_port_t; typedef int kern_return_t; typedef u32 natural_t;
extern mach_port_t mach_task_self_;
extern kern_return_t task_for_pid(mach_port_t, int, mach_port_t *);
extern kern_return_t task_threads(mach_port_t, mach_port_t **, natural_t *);
extern kern_return_t thread_get_state(mach_port_t, int, natural_t *, natural_t *);
extern kern_return_t thread_info(mach_port_t, int, natural_t *, natural_t *);
extern kern_return_t task_info(mach_port_t, int, natural_t *, natural_t *);
extern kern_return_t task_suspend(mach_port_t);
extern kern_return_t task_resume(mach_port_t);
extern kern_return_t mach_vm_read_overwrite(mach_port_t, u64, u64, u64, u64 *);
extern kern_return_t mach_port_deallocate(mach_port_t, mach_port_t);
extern kern_return_t vm_deallocate(mach_port_t, u64, u64);
extern int proc_listallpids(void *, int);
extern int proc_name(int, void *, u32);
extern int proc_pidinfo(int, int, u64, void *, int);
extern int open(const char *, int, ...);
extern long write(int, const void *, size_t);
extern int close(int);
extern int fsync(int);
extern unsigned int sleep(unsigned int);
extern int access(const char *, int);
extern int snprintf(char *, size_t, const char *, ...);
extern int strcmp(const char *, const char *);
extern int atoi(const char *);
extern size_t strlen(const char *);

#define O_WRONLY 1
#define O_NONBLOCK 4
#define O_APPEND 8
#define O_CREAT 0x200
#define O_NOCTTY 0x20000
#define ARM_THREAD_STATE64 6
#define THREAD_IDENTIFIER_INFO 4
#define THREAD_EXTENDED_INFO 5
#define TASK_DYLD_INFO 17
#define ADDR_MASK 0x0000000fffffffffULL

static int g_log = -1;
static void out(const char *s)
{
	int fd = open("/dev/console", O_WRONLY | O_NONBLOCK | O_NOCTTY);
	size_t n = strlen(s);
	if (fd >= 0) { write(fd, s, n); write(fd, "\n", 1); close(fd); }
	if (g_log >= 0) { write(g_log, s, n); write(g_log, "\n", 1); fsync(g_log); }
}
static int rd(mach_port_t t, u64 a, void *buf, u64 n)
{
	u64 got = 0;
	return mach_vm_read_overwrite(t, a, n, (u64)buf, &got) == 0 && got == n;
}

static void sample(int pid, const char *name, int rep)
{
	char l[1024]; mach_port_t task = 0; kern_return_t kr = task_for_pid(mach_task_self_, pid, &task);
	if (kr != 0) {           /* no task port: thread states only, through proc_pidinfo (root) */
		static u64 h[512]; int nb = proc_pidinfo(pid, 6 /* PROC_PIDLISTTHREADS */, 0, h, sizeof h);
		snprintf(l, sizeof l, "N2SAMPLE %s[%d] r%d task_for_pid kr=%d, proc_pidinfo threads=%d", name, pid, rep, kr, nb / 8); out(l);
		for (int i = 0; i < nb / 8 && i < 512; i++) {
			natural_t t[28] = {0}; char nm[65];
			if (proc_pidinfo(pid, 5 /* PROC_PIDTHREADINFO */, h[i], t, sizeof t) <= 0) continue;
			for (int k = 0; k < 64; k++) nm[k] = ((char *)&t[12])[k]; nm[64] = 0;
			snprintf(l, sizeof l, "N2SAMPLE %s t%d run=%d flags=0x%x sleep=%d cpu=%d pri=%d name='%s'", name, i, (int)t[6], t[7], (int)t[8], (int)t[4], (int)t[9], nm); out(l);
		}
		return;
	}
	u64 slide = 0, mh = 0; natural_t dyld[5], dc = 5;
	if (task_info(task, TASK_DYLD_INFO, dyld, &dc) == 0) {
		u64 aii = ((u64)dyld[1] << 32) | dyld[0]; u8 h[160];
		if (rd(task, aii, h, sizeof h)) {
			slide = *(u64 *)(h + 152);
			u64 arr = *(u64 *)(h + 8); u64 e[3];
			if (arr && rd(task, arr, e, sizeof e)) mh = e[0];
		}
	}
	mach_port_t *th = 0; natural_t nth = 0;
	task_suspend(task);
	kr = task_threads(task, &th, &nth);
	snprintf(l, sizeof l, "N2SAMPLE %s[%d] r%d threads=%u kr=%d slide 0x%llx mh 0x%llx", name, pid, rep, nth, kr, slide, mh); out(l);
	for (natural_t i = 0; kr == 0 && i < nth; i++) {
		natural_t ext[28] = {0}, ec = 28, idi[6] = {0}, ic = 6, st[68] = {0}, sc = 68;
		thread_info(th[i], THREAD_EXTENDED_INFO, ext, &ec);
		thread_info(th[i], THREAD_IDENTIFIER_INFO, idi, &ic);
		kern_return_t sk = thread_get_state(th[i], ARM_THREAD_STATE64, st, &sc);
		u64 *x = (u64 *)st; u64 fp = x[29], lr = x[30], sp = x[31], pc = x[32];
		char nm[65]; for (int k = 0; k < 64; k++) nm[k] = ((char *)&ext[12])[k]; nm[64] = 0;
		u64 tid = ((u64)idi[1] << 32) | idi[0], q = ((u64)idi[5] << 32) | idi[4];
		snprintf(l, sizeof l, "N2SAMPLE %s t%u tid=%llu run=%d flags=0x%x sleep=%d cpu=%d pri=%d name='%s' q=0x%llx sk=%d",
		         name, i, tid, (int)ext[6], ext[7], (int)ext[8], (int)ext[4], (int)ext[9], nm, q, sk); out(l);
		if (sk != 0) continue;
		u32 m[6] = {0}; int mok = rd(task, x[0] & ADDR_MASK, m, sizeof m);
		snprintf(l, sizeof l, "N2SAMPLE %s t%u pc 0x%llx lr 0x%llx sp 0x%llx fp 0x%llx x0 0x%llx x1 0x%llx x2 0x%llx x3 0x%llx msg%s %x %x %x %x %x %x",
		         name, i, pc & ADDR_MASK, lr & ADDR_MASK, sp, fp, x[0], x[1], x[2], x[3], mok ? "" : "(-)", m[0], m[1], m[2], m[3], m[4], m[5]); out(l);
		int n = snprintf(l, sizeof l, "N2SAMPLE %s t%u bt", name, i);
		for (int d = 0; d < 32 && fp && n < (int)sizeof l - 24; d++) {
			u64 fr[2]; if (!rd(task, fp & ADDR_MASK, fr, sizeof fr)) break;
			if (!fr[1]) break;
			n += snprintf(l + n, sizeof l - n, " 0x%llx", fr[1] & ADDR_MASK);
			if (fr[0] <= fp) break; fp = fr[0];
		}
		out(l);
		mach_port_deallocate(mach_task_self_, th[i]);
	}
	task_resume(task);
	if (th) vm_deallocate(mach_task_self_, (u64)th, (u64)nth * sizeof(mach_port_t));
	mach_port_deallocate(mach_task_self_, task);
}

int main(int argc, char **argv)
{
	int delay = 120, repeat = 1, interval = 30, first = 1; const char *gate = 0;
	for (; first < argc && argv[first][0] == '-' && argv[first][1] == '-'; first += 2) {
		if (first + 1 >= argc) return 2;
		if (!strcmp(argv[first], "--delay")) delay = atoi(argv[first + 1]);
		else if (!strcmp(argv[first], "--repeat")) repeat = atoi(argv[first + 1]);
		else if (!strcmp(argv[first], "--interval")) interval = atoi(argv[first + 1]);
		else if (!strcmp(argv[first], "--gate")) gate = argv[first + 1];
		else if (!strcmp(argv[first], "--log")) g_log = open(argv[first + 1], O_WRONLY | O_CREAT | O_APPEND, 0644);
		else return 2;
	}
	if (gate && access(gate, 0) != 0) return 0;          /* gate absent: do nothing (exit 0, no relaunch) */
	char l[256]; snprintf(l, sizeof l, "N2SAMPLE start delay=%d repeat=%d interval=%d", delay, repeat, interval); out(l);
	sleep(delay);
	for (int r = 0; r < repeat; r++) {
		if (r) sleep(interval);
		static int pids[4096]; int np = proc_listallpids(pids, sizeof pids);
		for (int a = first; a < argc; a++) {
			int found = 0;
			for (int k = 0; k < np && k < 4096; k++) {
				char pn[64] = {0}; if (pids[k] <= 0 || proc_name(pids[k], pn, sizeof pn) <= 0) continue;
				if (!strcmp(pn, argv[a])) { sample(pids[k], argv[a], r); found = 1; }
			}
			if (!found) { snprintf(l, sizeof l, "N2SAMPLE %s r%d not running", argv[a], r); out(l); }
		}
	}
	out("N2SAMPLE done");
	return 0;
}
