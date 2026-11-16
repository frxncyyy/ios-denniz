/*
 * nord2_hang.inc - where is SpringBoard's main thread waiting?
 *
 * q10f2 unified logs: at every answered call SpringBoard's main thread stops logging for good right after
 * "Adding sensor usage data for MICROPHONE" (recording indicator), so prox events are never handled (no
 * blanking) and the UI freezes. This probe samples the user stack of SpringBoard's first (main) thread every
 * 2 s and prints it whenever it changes and stays the same over two samples. Shared-cache frames are printed
 * as offsets from the cache's slid base ("c+0x..."), so they map onto the unslid cache (base 0x180000000).
 * Read only: backtrace_user() with the default copy routine (vm_map_switch to SpringBoard's map).
 */
#include <kern/backtrace.h>
#include <vm/vm_shared_region.h>
#include <sys/proc.h>
#include <sys/proc_internal.h>
#include <sys/user.h>
extern thread_t get_machthread(struct uthread *);

extern thread_t get_firstthread(task_t);
extern void thread_reference(thread_t);

#define N2HANG_FRAMES 40
static uintptr_t n2hang_prev[N2HANG_FRAMES], n2hang_said[N2HANG_FRAMES];
static unsigned n2hang_prev_n, n2hang_said_n, n2hang_prints;

static int
n2hang_find(const char *name)
{
	char nm[32];
	for (int pid = 1; pid < 1500; pid++) {
		nm[0] = 0;
		proc_name(pid, nm, sizeof(nm));
		if (nm[0] && !strcmp(nm, name)) {
			return pid;
		}
	}
	return 0;
}

static void
n2hang_sample(int pid, const char *who)
{
	proc_t p = proc_find(pid);
	if (p == NULL) {
		return;
	}
	task_t task = proc_task(p);
	thread_t th = task ? get_firstthread(task) : THREAD_NULL;
	uintptr_t bt[N2HANG_FRAMES];
	unsigned n = 0;
	mach_vm_offset_t cache = 0;

	if (th != THREAD_NULL) {
		struct backtrace_control ctl = BTCTL_INIT;
		struct backtrace_user_info info = BTUINFO_INIT;
		thread_reference(th);
		ctl.btc_user_thread = th;
		n = backtrace_user(bt, N2HANG_FRAMES, &ctl, &info);
		thread_deallocate(th);
		vm_shared_region_t sr = vm_shared_region_get(task);
		if (sr) {
			(void)vm_shared_region_start_address(sr, &cache, task);
			vm_shared_region_deallocate(sr);
		}
	}
	proc_rele(p);
	if (n == 0) {
		return;
	}
	bool same_prev = n == n2hang_prev_n && !memcmp(bt, n2hang_prev, n * sizeof(bt[0]));
	bool same_said = n == n2hang_said_n && !memcmp(bt, n2hang_said, n * sizeof(bt[0]));
	memcpy(n2hang_prev, bt, n * sizeof(bt[0]));
	n2hang_prev_n = n;
	if (!same_prev || same_said || n2hang_prints >= 60) {
		return;
	}
	memcpy(n2hang_said, bt, n * sizeof(bt[0]));
	n2hang_said_n = n;
	n2hang_prints++;
	printf("NORD2 HANG: %s[%d] main thread stack #%u (%u frames, cache base 0x%llx):\n", who, pid, n2hang_prints, n,
	    (unsigned long long)cache);
	for (unsigned i = 0; i < n; i += 4) {
		char line[200];
		int o = 0;
		for (unsigned k = i; k < n && k < i + 4 && o < (int)sizeof(line) - 40; k++) {
			uint64_t a = bt[k];
			if (cache && a >= cache && a < cache + 0x100000000ULL) {
				o += snprintf(line + o, sizeof(line) - (size_t)o, " c+0x%llx", (unsigned long long)(a - cache));
			} else {
				o += snprintf(line + o, sizeof(line) - (size_t)o, " 0x%llx", (unsigned long long)a);
			}
		}
		printf("NORD2 HANG:  %u:%s\n", i, line);
	}
}

/* q10gk: every thread of a process, once (where is mediaserverd while Camera shows black?) */
static void
n2hang_all(int pid, const char *who)
{
	proc_t p = proc_find(pid);
	if (p == NULL) { return; }
	task_t task = proc_task(p);
	mach_vm_offset_t cache = 0;
	vm_shared_region_t sr = task ? vm_shared_region_get(task) : NULL;
	if (sr) { (void)vm_shared_region_start_address(sr, &cache, task); vm_shared_region_deallocate(sr); }
	thread_t ths[48]; unsigned nth = 0;
	struct uthread *uth;
	proc_lock(p);
	TAILQ_FOREACH(uth, &p->p_uthlist, uu_list) { if (nth < 48) { ths[nth] = get_machthread(uth); thread_reference(ths[nth]); nth++; } }
	proc_unlock(p);
	printf("NORD2 HANG: ALL %s[%d]: %u threads, cache base 0x%llx\n", who, pid, nth, (unsigned long long)cache);
	for (unsigned t = 0; t < nth; t++) {
		uintptr_t bt[28]; char line[220];
		struct backtrace_control ctl = BTCTL_INIT; struct backtrace_user_info info = BTUINFO_INIT;
		ctl.btc_user_thread = ths[t];
		unsigned n = backtrace_user(bt, 28, &ctl, &info);
		thread_deallocate(ths[t]);
		for (unsigned i = 0; i < n; i += 5) {
			int o = 0;
			for (unsigned k = i; k < n && k < i + 5; k++) {
				uint64_t a = bt[k];
				if (cache && a >= cache && a < cache + 0x100000000ULL) { o += snprintf(line + o, sizeof(line) - (size_t)o, " c+0x%llx", (unsigned long long)(a - cache)); }
				else { o += snprintf(line + o, sizeof(line) - (size_t)o, " 0x%llx", (unsigned long long)a); }
			}
			printf("NORD2 HANG: T%u.%u:%s\n", t, i, line);
		}
	}
	proc_rele(p);
}

static int n2hang_chan;
static void
n2hang_thread(void *arg __unused, wait_result_t wr __unused)
{
	int sb = 0;
	for (unsigned t = 0;; t++) {
		uint64_t deadline;
		clock_interval_to_deadline(2000, NSEC_PER_MSEC, &deadline);
		(void)assert_wait_deadline(&n2hang_chan, THREAD_UNINT, deadline);
		thread_block(THREAD_CONTINUE_NULL);
		if (t < 30) {                   /* from 60 s after start: SpringBoard is up */
			continue;
		}
		if (!sb || t % 15 == 0) {
			sb = n2hang_find("SpringBoard");
		}
		if (sb) {
			n2hang_sample(sb, "SpringBoard");
		}
		{
			static unsigned cam_seen, dumps;
			int cam = n2hang_find("Camera");
			cam_seen = cam ? cam_seen + 1 : 0;
			if (cam && (cam_seen == 4 || cam_seen == 12) && dumps < 4) {     /* 8 s and 24 s after Camera shows up */
				dumps++;
				n2hang_all(cam, "Camera");
				int ms = n2hang_find("mediaserverd");
				if (ms) { n2hang_all(ms, "mediaserverd"); }
			}
		}
	}
}

static void
nord2_hang_start(void)
{
	thread_t thread;
	if (kernel_thread_start(n2hang_thread, NULL, &thread) == KERN_SUCCESS) {
		thread_deallocate(thread);
		printf("NORD2 HANG: SpringBoard main-thread stack probe on\n");
	}
}
