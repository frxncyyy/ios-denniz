#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""facts/imports.py -- the 149 symbols the phone's vendor vcodec objects import from outside their own set.

The set is the five ARM32 objects of the vendor codec stack, read at run time from the extracted copy of the
phone's own /vendor (never copied into the tree):

    bin/vpud, lib/libvpud_vcodec.so, lib/libvcodecdrv.so, lib/libvcodec_utility.so, lib/libvcodec_oal.so

An import is a symbol listed undefined (llvm-nm -D --undefined-only) by at least one object and defined
(llvm-nm -D --defined-only) by none of them.  ELF symbol version suffixes ("malloc@LIBC") are stripped: the
daemon binds by name.  Each import is assigned to the shim table that must provide it, using the table layout
in work_packages.json (V30 string/heap, V32 thread, V33 stdio, V34 android/data).  Every assignment is
explicit: an import with no assignment is a hard error, so the table cannot drift silently.

Usage:
    python3 gpu/hwcodec/facts/imports.py                 # per-table lists, one object-per-line provenance
    python3 gpu/hwcodec/facts/imports.py --per-object    # imports of each object separately
    python3 gpu/hwcodec/facts/imports.py --markdown      # table for FACTS.md
    python3 gpu/hwcodec/facts/imports.py --table string  # one table only
    N2HW_VENDOR_DIR=... python3 .../imports.py           # other extraction of the same files

Last line: "149 imports".  Deterministic; no network, no writes.
"""

import argparse
import os
import subprocess
import sys

# ---------------------------------------------------------------- vendor set
OBJECTS = [
    "bin/vpud",
    "lib/libvpud_vcodec.so",
    "lib/libvcodecdrv.so",
    "lib/libvcodec_utility.so",
    "lib/libvcodec_oal.so",
]

HERE = os.path.dirname(os.path.abspath(__file__))


def default_vendor_dir():
    # gpu/hwcodec/facts/ -> work tree root is three levels up.
    root = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
    return os.path.join(root, "gpu", "build", "cam-vendor-20261109", "vfs")


def vendor_dir():
    d = os.environ.get("N2HW_VENDOR_DIR") or default_vendor_dir()
    if not os.path.isdir(d):
        sys.exit("imports.py: vendor directory not found: %s" % d)
    return d


def nm(path, defined):
    """Symbol names of one object (llvm-nm -D), version suffixes stripped."""
    flags = ["--defined-only"] if defined else ["--undefined-only"]
    try:
        out = subprocess.run(["llvm-nm", "-D", "--format=posix"] + flags + [path],
                             check=True, capture_output=True, text=True).stdout
    except (OSError, subprocess.CalledProcessError) as e:
        sys.exit("imports.py: llvm-nm failed on %s: %s" % (path, e))
    names = set()
    for line in out.splitlines():
        line = line.strip()
        if not line:
            continue
        name = line.split()[0].split("@", 1)[0]
        if name:
            names.add(name)
    return names


# ---------------------------------------------------------------- shim tables
# One table per shim registration array (include/n2elf.h, work_packages.json).  Format functions live in
# n2shim_string[] but are implemented by n2shim_format.c (V30); the stdio entry points fprintf/printf/puts/
# fscanf live in n2shim_stdio[] (V33) and call the same formatter.
TABLES = [
    ("string", "n2shim_string", "V30",
     "string/memory/format entry points of n2shim_string[] (format ones via n2fmt())"),
    ("heap", "n2shim_heap", "V30", "guest heap and anonymous mmap"),
    ("thread", "n2shim_thread", "V32", "pthread/sem/sched/signal and process plumbing"),
    ("stdio", "n2shim_stdio", "V33", "files, devices, stdio, time, process exit"),
    ("android", "n2shim_android", "V34", "Android runtime: log, props, atrace, ion, dl*, math, startup"),
    ("data", "n2shim_data_all", "V34", "imported data symbols"),
]

ASSIGN = {}


def assign(table, names, note=None):
    for n in names.split():
        if n in ASSIGN:
            sys.exit("imports.py: %s assigned twice (%s and %s)" % (n, ASSIGN[n][0], table))
        ASSIGN[n] = (table, note)


# n2shim_string[] -- string/memory.  (V30 deliverable lists these by name.)
assign("string", """
memcpy memmove memset memcmp strlen strnlen strcmp strncpy strstr strtok strerror strsignal
atoi atoll qsort
__aeabi_memcpy __aeabi_memset __memcpy_chk __strlen_chk __strncpy_chk2
""")
# n2shim_string[], implemented by n2shim_format.c / n2fmt().
assign("string", """
snprintf sprintf vsnprintf sscanf
__snprintf_chk __vsnprintf_chk __vsprintf_chk
""", note="formatter (n2fmt)")
# n2shim_heap[].
assign("heap", """
malloc calloc free memalign getpagesize mmap mmap64 munmap
""")
# n2shim_thread[].
assign("thread", """
pthread_attr_destroy pthread_attr_getdetachstate pthread_attr_getschedparam pthread_attr_init
pthread_attr_setdetachstate pthread_attr_setschedparam pthread_attr_setschedpolicy
pthread_cond_broadcast pthread_cond_destroy pthread_cond_init pthread_cond_signal pthread_cond_wait
pthread_condattr_destroy pthread_condattr_init
pthread_create pthread_equal pthread_exit pthread_gettid_np pthread_join pthread_kill
pthread_mutex_destroy pthread_mutex_init pthread_mutex_lock pthread_mutex_timedlock
pthread_mutex_trylock pthread_mutex_unlock pthread_mutexattr_destroy pthread_mutexattr_init
pthread_once pthread_rwlock_destroy pthread_rwlock_init pthread_rwlock_rdlock pthread_rwlock_unlock
pthread_rwlock_wrlock pthread_self pthread_setname_np
sem_destroy sem_getvalue sem_init sem_post sem_wait
sched_getaffinity sched_setaffinity sched_yield gettid prctl
sigaction sigemptyset signal usleep
""")
# n2shim_stdio[] and n2vfs.c.
assign("stdio", """
fopen fclose fread __fread_chk fwrite fgets feof fflush fseek fileno fsync fprintf fscanf printf puts
open __open_2 close read write ioctl
clock_gettime gettimeofday
exit abort __stack_chk_fail __errno
""")
# n2shim_android[].
assign("android", """
__android_log_print __android_log_vprint log property_get
atrace_begin_body atrace_end_body atrace_get_enabled_tags atrace_int_body
perf_lock_acq perf_lock_rel sec_drv_base_check sec_drv_info_update
dladdr dlclose dlerror dlopen dlsym
exp2 __cxa_atexit __cxa_finalize __register_atfork __libc_init
__gnu_Unwind_Find_exidx __aeabi_unwind_cpp_pr0
ion_alloc ion_alloc_camera ion_custom_ioctl ion_free ion_import ion_mmap ion_munmap ion_share ion_share_close
mt_ion_close mt_ion_open
""")
# n2shim_data_all[].
assign("data", """
__stack_chk_guard stderr
""")


def collect(vdir):
    defined, imported_by = set(), {}
    for rel in OBJECTS:
        p = os.path.join(vdir, rel)
        if not os.path.isfile(p):
            sys.exit("imports.py: missing object: %s" % p)
        defined |= nm(p, True)
        for s in nm(p, False):
            imported_by.setdefault(s, []).append(os.path.basename(rel))
    external = sorted(set(imported_by) - defined)
    return external, imported_by


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--per-object", action="store_true", help="list each object's own imports")
    ap.add_argument("--markdown", action="store_true", help="markdown table instead of plain lists")
    ap.add_argument("--table", choices=[t[0] for t in TABLES], help="one table only")
    ap.add_argument("--vendor-dir", default=None, help="override N2HW_VENDOR_DIR")
    args = ap.parse_args()

    vdir = args.vendor_dir or vendor_dir()
    external, imported_by = collect(vdir)

    unknown = [s for s in external if s not in ASSIGN]
    if unknown:
        sys.exit("imports.py: %d import(s) not assigned to a shim table: %s"
                 % (len(unknown), " ".join(unknown)))
    unused = sorted(set(ASSIGN) - set(external))
    if unused:
        sys.exit("imports.py: %d assignment(s) name symbols that are not external imports: %s"
                 % (len(unused), " ".join(unused)))

    if args.per_object:
        for rel in OBJECTS:
            mine = sorted(s for s in external if os.path.basename(rel) in imported_by[s])
            print("%s: %d external imports" % (rel, len(mine)))
            for s in mine:
                print("  %-28s %s" % (s, ASSIGN[s][0]))
        print("%d imports" % len(external))
        return

    if args.markdown:
        print("| import | table | owner | also imported by |")
        print("|---|---|---|---|")
        for s in external:
            t = ASSIGN[s][0]
            print("| `%s` | `n2shim_%s` | %s | %s |"
                  % (s, {"data": "data_all"}.get(t, t), dict((x[0], x[2]) for x in TABLES)[t],
                     ", ".join(sorted(set(imported_by[s])))))
        print("%d imports" % len(external))
        return

    total = 0
    for key, cname, owner, desc in TABLES:
        if args.table and args.table != key:
            continue
        names = [s for s in external if ASSIGN[s][0] == key]
        total += len(names)
        print("== %s -> %s[] (owner %s): %d imports -- %s" % (key, cname, owner, len(names), desc))
        for s in names:
            note = ASSIGN[s][1]
            print("  %-28s [%s]%s" % (s, ", ".join(sorted(set(imported_by[s]))),
                                      (" " + note) if note else ""))
        print()
    if args.table:
        print("%d imports in table %s" % (total, args.table))
    else:
        print("%d imports" % len(external))


if __name__ == "__main__":
    try:
        main()
    except BrokenPipeError:      # piped into head/grep -q: not an error
        sys.exit(0)
