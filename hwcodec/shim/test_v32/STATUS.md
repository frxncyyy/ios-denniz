# V32 -- n2shim_thread: threads, mutexes, condition variables, semaphores, rwlocks, scheduling

Status: **implemented and host-tested; not on the phone, not linked into `n2vcodecd` yet** (that link is
`n2-iv`/`n2-ip`).  Nothing here has run on the device; every statement below is about host builds.

    bash gpu/hwcodec/shim/test_v32/run_tests.sh
    N2TEST V32 PASS 13149/13149

## Files (all mine)

| File | Lines | What |
|---|---|---|
| `shim/n2shim_thread.c` | 2308 | `n2shim_thread[]`: all 50 D01 thread-family imports + 16 extras |
| `shim/n2shim_thread.h` | 67 | first-failure accessors, introspection, stop request |
| `shim/test_v32/run_tests.sh` | 107 | the acceptance test: ASan+UBSan -O1, -O2, TSan -O1 |
| `shim/test_v32/arm/t_v32_guest.c` | 1009 | the guest side: a real ARM `.so` with no C library |
| `shim/test_v32/t_e2e.c` | 310 | host driver: V03 loader + V02 core + the guest `.so` |
| `shim/test_v32/t_direct.c` | 382 | host driver: fake cpu, table coverage, errno paths, side table |
| `shim/test_v32/compile_ios.sh` | 23 | extra: arm64-apple-ios16.5 compile check (needs the SDK) |
| `dsh-state/workers/V32/STATUS.md` | | worker handoff |

## What is implemented

`n2shim_thread[]` has 66 entries: the 50 imports D01 counted for this family (encoded verbatim in
`t_direct.c` and re-checked on every run) and 16 extras that the vendor code or the tests need
(`pthread_detach`, `pthread_cond_timedwait`, `pthread_condattr_setclock/getclock`,
`pthread_mutexattr_settype/gettype/setpshared/getpshared`, `pthread_attr_getschedpolicy`,
`pthread_rwlock_tryrdlock/trywrlock/timedrdlock/timedwrlock`, `sem_trywait`, `sem_timedwait`,
`__register_atfork`).

**Guest threads are host threads.**  `pthread_create` takes a table slot, allocates a 1 MiB guest stack with
`n2a32_mem_alloc`, initialises a private `struct n2a32_cpu` on the caller's `struct n2a32_mem`, and starts a host
thread that runs the guest start routine with `n2a32_call`.  The guest `pthread_t` is `slot + 1`, so 0 is never a
valid id.  `pthread_join` waits on a condition variable (100 ms slices so a teardown request is noticed),
collects `r0`, reclaims the host thread with `pthread_join`, and frees the slot; `pthread_detach` detaches the
host thread (or reclaims it if it already exited); `pthread_exit` records the value and stops the calling cpu;
detached threads and joined threads recycle their guest stack through a 32-entry pool (the interpreter has no
unmap, so the address space never shrinks).

**Guest synchronisation objects are side-table entries keyed by guest address.**  `pthread_mutex_t`,
`pthread_cond_t` and `sem_t` are 4 guest bytes and `pthread_rwlock_t` is 40, so the host objects live in a
1024-bucket hash over a 512-entry pool, created on first use under one global lock.  That is what makes the
all-zero static initialisers work (`PTHREAD_MUTEX_INITIALIZER`, `PTHREAD_COND_INITIALIZER`,
`PTHREAD_ONCE_INIT`): the guest bytes are never read or written.  Entries are reference counted while an
operation is in flight, `*_destroy` unlinks them immediately and the last user frees them, and a second object
kind at the same guest address retires the first (counter `n2shim_thread_kind_clashes()`).

**Failure propagation.**  A guest thread started by `pthread_create` that stops with anything other than
`N2A32_STOP_RETURN` records its stop reason in a process-wide first-failure slot (`n2shim_thread_failure()`,
`_failure_thread()`, `_failure_detail()`, `_failure_count()`) and sets `stop_request` on every other live guest
cpu.  `N2A32_STOP_REQUEST` is *not* a failure: it is the stop this package asks for (teardown, failure
propagation, or the thread's own `pthread_exit`), so a requested stop never turns a clean shutdown into a
"failure".  `n2shim_thread_request_stop()` asks every live cpu to stop; `n2shim_thread_note_stop(c)` lets the
daemon record an abnormal stop of the cpu it drives itself.

## Deliberate differences from bionic (all tested or documented)

1. Guest object bytes are left untouched (bionic stores the owner tid in the mutex word).  Copying such an
   object to another address creates a second, unrelated host object.
2. `pthread_exit` on the initial thread stops that cpu, publishes the record as exited and asks every other cpu
   to stop, but the **host process survives**; the daemon sees `N2A32_STOP_REQUEST` from its `n2a32_call`.  If the
   daemon later gives the same cpu guest code, the shims register it again.
3. Every blocking wait (`pthread_join`, `pthread_mutex_lock`/`timedlock`, `pthread_cond_wait`/`timedwait`,
   `sem_wait`/`timedwait`, `pthread_rwlock_*lock`, `pthread_once`'s wait) is sliced at 100 ms and returns
   `EINTR` when the calling cpu has been asked to stop (`n2shim_thread_request_stop()` or a failing sibling).
   That is what makes a daemon teardown complete: a guest thread blocked behind a lock held by a thread that has
   itself stopped cannot hang the join.  A timed wait keeps the guest's absolute deadline (an already-expired
   deadline still succeeds on an uncontended lock or returns `ETIMEDOUT` when contended).
   Deviation from POSIX: `pthread_cond_wait` cannot re-acquire the mutex if its holder was stopped while holding
   it, so on `EINTR` the mutex is not held; the caller is stopping anyway.
4. Destroy: `pthread_mutex_destroy`/`pthread_rwlock_destroy` return `EBUSY` while the object is held,
   `pthread_cond_destroy` returns `EBUSY` while an operation is in flight, and `sem_destroy` always succeeds
   (a `sem_post` that is still in flight completes on the retired entry; a thread still blocked in `sem_wait`
   at destroy time is POSIX-undefined and stays blocked).
5. `sem_init` on an initialised semaphore returns `EBUSY` (bionic too); `pthread_mutex_init`/`cond_init`/
   `rwlock_init` on an initialised object return 0 and leave it alone (safer than bionic's overwrite of a live
   object).
6. Signals are recorded and never delivered: `sigaction`/`signal` store the act/handler, `signal` returns the
   previous handler, `sigaction` writes `SIG_DFL` into `oldact->sa_handler`, `sigemptyset` zeroes one 32-bit
   word (bionic's 32-bit `sigset_t`), `__register_atfork` returns 0 and does nothing.
7. `sched_getaffinity` reports 8 CPUs (bits 0-7) regardless of the host; `sched_setaffinity` is accepted and not
   applied (the guest does not own host cpus).
8. `pthread_setname_np` truncates to 15 characters; `pthread_gettid_np` and `gettid` return the host kernel tid
   (on non-Linux hosts the thread pointer).  `prctl(PR_SET_NAME/PR_GET_NAME)` round-trips the stored name.
9. Limits: 128 thread records, 512 side-table objects, 32 pooled stacks.  Exhaustion returns `EAGAIN`
   (`pthread_create`) or `ENOMEM` (objects).  Thread ids are reused after join/detach (bionic does the same).
10. `sem_init(pshared != 0)` is accepted and ignored (there is one address space).
11. `pthread_attr_t`/`pthread_mutexattr_t`/`pthread_condattr_t` are 4-byte guest objects, so their contents also
    live in the side table; an attr that was never initialised reads as "all defaults".
12. No cancellation, no real signal delivery, no atfork handlers, no TLS, no `pthread_attr_setstacksize`
    (the 1 MiB stack is fixed), no `PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP` statics (only `pthread_mutex_init`
    with an attr type selects a recursive/errorcheck host mutex).

## What the tests do

`run_tests.sh` builds the guest `.so` once, then three host configurations (ASan+UBSan -O1, plain -O2,
TSan -O1) of two binaries, with at most two compiler jobs at a time (~25 s total).

`t_direct` (2386 checks per build): all 50 D01 names resolve and no name appears twice in the table; NULL,
zero and out-of-range ids return the documented errno instead of crashing; attr/sched-param/policy round trips
through guest memory; sched affinity bit pattern; `sigemptyset`/`signal`/`sigaction` recording; mutex/sem/cond/
rwlock first-use creation and destroy; a second object kind at one address; the failure slot reasons and
`request_stop`.

`t_e2e` (10319 checks per full build): a real ARM shared object (`arm/t_v32_guest.c`, no libc, ARM mode,
armv8-a) is loaded by **V03's real loader** and executed by **V02's real interpreter**, so every guest check is
real compiler output with real AAPCS calls:

* 8 threads x 100000 increments of a `.bss` mutex that is never `pthread_mutex_init`ed (all-zero static
  initialiser), result exactly 800000, every worker reporting its own failure line as 0;
* producer/consumer, ring of 8, 2000 items, mutex + condition variable, four consumers, sums equal;
* semaphore ping-pong, 2 threads x 5000 exchanges, `sem_getvalue` 1/0 at the end, second `sem_init` `EBUSY`,
  `sem_trywait` `EAGAIN` when empty;
* `pthread_once` from 8 threads on a zero-initialised control: the init routine runs exactly once and all eight
  threads get through; a later call does not run it again; a re-entrant call from the init routine returns
  `EDEADLK` (counted in `n2shim_thread_nested_once()`);
* rwlock: 4 readers check two words for equality while 2 writers update both under the write lock (40000
  increments, no torn read);
* thread churn: 100 sequential create/join pairs (record and guest-stack-pool reuse) and 40 threads live at
  once, each returning its own index through a different record;
* lifecycle: `pthread_self`/`pthread_equal`/`pthread_gettid_np`/`gettid`/`pthread_kill` (signal 0 and a real
  signal, both accepted), `pthread_setname_np` + `prctl(PR_GET_NAME)` round trip, attrs (detach state, sched
  param, policy), a thread detached through its attr and one through `pthread_detach`, both joined with
  `EINVAL` while alive; `pthread_exit(0x1234)` visible through `pthread_join`, and the slot gone afterwards;
* scheduling/time/signals/error paths: affinity, `sched_yield`, `usleep`, `sigemptyset`/`sigaction`/`signal`,
  `__register_atfork`, `trylock` `EBUSY`, timed-lock `ETIMEDOUT`, `EDEADLK`/`EPERM` on an errorcheck mutex,
  cond timedwait `ETIMEDOUT`, sem reuse after destroy;
* `pthread_exit` from the **initial** thread: the guest call stops with `N2A32_STOP_REQUEST`, the marker after
  the call was never written, the record is published exited and `pthread_self` re-registers the cpu;
* a `pthread_once` init routine whose address is an unmapped page: the fault is recorded as the first failure and
  the calling cpu is asked to stop (`N2A32_STOP_REQUEST`), so the caller cannot continue past a failed one-time
  initialisation;
* a thread whose start address is an unmapped page: `N2A32_STOP_FAULT` is recorded as the first failure, its id
  and `stop_insn`/`stop_addr` are visible, `stop_request` was set on the initial cpu, and the initial call stops
  with `N2A32_STOP_REQUEST`;
* three threads blocked in `sem_wait` while a fourth faults: the failing thread asks every other cpu to stop
  (4 stop requests) and the victims stop **on their own** -- the blocking waits are sliced at 100 ms so a
  teardown request interrupts them -- with `N2A32_STOP_REQUEST` from their `n2a32_call`, no additional failure is
  recorded, and no guest thread is left running.

The TSan build runs the same binaries with `--no-fault`: V02's fault handler calls `siglongjmp` out of a SIGSEGV
handler and TSan aborts with "can't find longjmp buf", so the deliberately faulting entries are skipped there
(only there; every other check, including all timed waits, runs unchanged).  All other checks, including the thread, mutex, cond, sem, once and rwlock work, run under
TSan; TSan reported **no data races** (the final version).  One earlier TSan finding was in the *test*: several
worker functions called the shared `ck()` counter, which is now per-thread failure slots.

`bash shim/test_v32/compile_ios.sh` (extra, needs the SDK):
`N2TEST V32-IOS PASS 1/1 (arm64-apple-ios16.5, compile only)`.

Last full run (from the work-tree root):

    $ bash gpu/hwcodec/shim/test_v32/run_tests.sh
    == guest: real ARM shared object importing the pthread/sem/sched/signal shims
    == build: asan
    V32DIRECTRESULT 2390/2390
    V32RESULT 10759/10759
    == build: o2
    V32DIRECTRESULT 2390/2390
    V32RESULT 10759/10759
    == build: tsan
    V32DIRECTRESULT 2390/2390
    V32RESULT 10730/10730
      checks: direct=2390/2390 (asan==o2, tsan also 2390 )
      checks: e2e full=10759/10759, e2e TSan --no-fault=10730/10730
    N2TEST V32 PASS 13149/13149

e2e per-entry summary from the -O2 run (ASan and TSan report the same numbers for every entry they run):

      v32_increments         stop=1  guest checks 33, failures 0, live 1
      v32_prodcons           stop=1  guest checks 19, failures 0, live 1
      v32_sem_pingpong       stop=1  guest checks 10012, failures 0, live 1
      v32_once               stop=1  guest checks 20, failures 0, live 1
      v32_rwlock             stop=1  guest checks 17, failures 0, live 1
      v32_lifecycle          stop=1  guest checks 49, failures 0, live 1
      v32_misc               stop=1  guest checks 111, failures 0, live 1
      v32_zeroinit           stop=1  guest checks 16, failures 0, live 1
      v32_churn              stop=1  guest checks 420, failures 0, live 1
      v32_once_recurse       stop=1  guest checks 3, failures 0, live 1
      v32_once_fail          stop=7  guest checks 0, failures 0, live 1
      v32_main_exit          stop=7  guest checks 0, failures 0, live 0
      v32_fail_single        stop=7  guest checks 1, failures 0, live 1
      v32_fail_multi         stop=7  guest checks 10, failures 0, live 4
      side table: live 9, peak 9, kind clashes 1, nested once 1, created 177
    V32RESULT 10759/10759

(stop 1 = `N2A32_STOP_RETURN`, stop 7 = `N2A32_STOP_REQUEST`.  The 9 live side-table objects at the end are the
ones the guest never destroys: 4 `pthread_once` controls (including the failing one), the 2 stable semaphores of
the failure tests, the increment mutex and the 2 release semaphores of the detached-thread test.  The 4 live
threads printed after `v32_fail_multi` are the 3 victims still inside `sem_wait` plus the initial thread; they stop
by themselves within one 100 ms slice of the teardown request, which the driver checks before releasing anything.)

## External evidence

* **R1 accepted V32 rev 2** (board: "V32 accepted (13126/13126; thread/object tables and stop propagation read)";
  `review/r1/FINDINGS.md`): no finding; the only note is V02's side of F8.
* **The real loader binds this table against the phone's own libraries.**  hw-v34 and hw-v33 each linked the five
  vendor objects (bin/vpud + libvpud_vcodec/libvcodecdrv/libvcodec_utility/libvcodec_oal) with V03's loader,
  V02's core and the combined shim tables (V30 + V32 + V33 + V34 + data), reporting **unresolved = 0**; V32's
  `n2shim_thread.c` is in both build lists.  So every one of the 50 thread-family imports binds through the real
  loader, not only through this package's own table probe.
* **V33 calls into this package.**  `shim/n2shim_stdio.c` declares
  `extern unsigned n2shim_thread_request_stop(void) __attribute__((weak))` and calls it from `exit`/`abort`/
  `__stack_chk_fail` so a fatal libc call stops every guest cpu; the signature matches this package exactly and
  V33's rev 6 test covers the call.  No change is needed when both are linked; without V32 the weak symbol is
  NULL and V33 skips it.

## How to integrate

    n2elf_set_add_shims(set, n2shim_thread);        /* before n2elf_link */
    /* data imports still need V34's n2shim_data_all or a local two-entry table */

* The shim needs the real `n2a32_call`/`n2a32_mem_alloc` (V02).  It does not need the loader.
* Call `n2shim_thread_request_stop()` before tearing a process down, and join guest threads before
  `n2a32_mem_destroy()`.
* `n2shim_thread_failure()` after a `n2a32_call` that did not return `N2A32_STOP_RETURN` tells the daemon whether
  a worker died; `n2shim_thread_note_stop(c)` records the daemon's own call in the same slot.
* The shim never prints, never allocates from the host heap and never aborts.

## Lock order (invariant to keep when editing)

`g_obj_lock` and `g_thr_lock` are never held at the same time; neither is held while calling guest code, touching
guest memory, or waiting on a host mutex/condvar (`n2s_self()` takes `g_thr_lock` itself, so it must be called
*before* taking that lock - this was wrong in two places during development and is now fixed: `pthread_once`'s
failure path and `n2shim_thread_note_stop`).  Operations that block (`pthread_mutex_lock`, `pthread_cond_wait`,
`sem_wait`, rwlock waits) run with no package lock held; the per-object reference count keeps the entry alive.

## Process-global tables (design limit, not a bug)

The thread and object tables are process-global, which matches the architecture (one daemon process = one
guest address space, PLAN section 4).  Consequences an integrator must know:

* Two `n2elf_set`s in one host process would share thread ids and side-table entries, because the object
  table is keyed by guest address and two address spaces can use the same addresses.  Run one guest address
  space per process.
* The tables live for the life of the process: after `n2a32_mem_destroy()` the initial thread record still
  names its `struct n2a32_cpu`, so creating a *second* `n2a32_mem` in the same process is not supported.
  (This package has no reset entry point; adding one would need a "no guest thread is running" contract.)

## Not done / open

1. **No phone run, no arm64 runtime test** (compile only: `N2TEST V32-IOS PASS 1/1`).  No link into `n2vcodecd`.
2. **V02 side of the stop handshake is a plain read.**  V32 writes `stop_request` with
   `__atomic_store_n(..., __ATOMIC_RELEASE)` and reads its own with `__atomic_load_n(...)`, but
   `n2a32_run()` reads the field with a plain `volatile` load (`n2a32_core.c:468`), which is still a C11 data
   race (R1 finding F8, and my post to hw-v02).  The one-line fix proposed: use
   `__atomic_load_n(&c->stop_request, __ATOMIC_ACQUIRE)` there.  My TSan suite is clean because the propagation
   test orders the write and the victim's read through the thread-table lock and the semaphore, not because the
   core side is atomic.
3. A guest that never joins and never detaches a thread leaves a host thread record (like bionic's zombies);
   a guest that never destroys a static object leaves a side-table entry (bounded at 512).
4. No thread cancellation, no real signal delivery, no atfork handlers, no TLS, no pshared semaphores, no
   `pthread_attr_setstacksize`, no `PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP` statics.
5. Timeouts use the host `CLOCK_REALTIME` (a monotonic condattr deadline is converted through "now");
   a clock jump can therefore stretch or shorten a timed wait.
6. Writer starvation for `pthread_rwlock_t` depends on the host implementation (glibc is reader-preferring).
7. `__register_atfork`: implemented in V32 because TASK V32 lists it; D01's census assigns it to V34.  Both
   returning 0 is mechanically harmless (the loader binds the first table that has the name); a question to
   hw-lead is on the board and unanswered.
8. The vendor libraries themselves have not been run through this shim (that is the `n2vcodecd`/V40 line).
   Their real thread count, mutex count and any use of `pthread_mutexattr_settype` are unmeasured; the census
   only shows which functions are imported.
