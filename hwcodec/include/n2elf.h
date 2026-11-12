/* SPDX-License-Identifier: MIT */
/*
 * n2elf.h -- FROZEN interface of the ELF32/ARM loader and the import shims (gpu/hwcodec track V). Owner: hwcodec-lead.
 *
 * The vendor set (verified 2026-11-11 with llvm-readelf on gpu/build/cam-vendor-20261109/vfs): bin/vpud (PIE, ET_DYN) and
 * lib/libvpud_vcodec.so, libvcodecdrv.so, libvcodec_utility.so, libvcodec_oal.so. Together they import 149 symbols from
 * outside the set (list: gpu/hwcodec/FACTS.md, section "imports"); no C++ runtime, no TLS segment. Relocation types:
 * R_ARM_RELATIVE, R_ARM_GLOB_DAT, R_ARM_JUMP_SLOT, R_ARM_ABS32; tables in THREE encodings that all must be handled:
 * DT_REL/DT_JMPREL (plain), DT_ANDROID_REL (0x6000000f, "APS2" packed, SLEB128), DT_RELR/DT_ANDROID_RELR (0x6fffe000 family,
 * bitmap). Hash: DT_GNU_HASH only. BIND_NOW. Symbol versions (VERSYM/VERNEED) are ignored.
 */
#ifndef N2ELF_H
#define N2ELF_H
#include "n2a32.h"

struct n2elf_image;                         /* one loaded object */
struct n2elf_set;                           /* a link namespace: the loaded objects + the shim tables */

/* A shim: host implementation of one imported function. It reads its arguments with n2shim_arg(), writes the result with
 * n2shim_ret() and returns; the core then returns to the guest caller. A shim may call back into the guest (n2a32_call). */
struct n2shim_entry { const char *name; n2a32_hostcall fn; };
/* A data import (__stack_chk_guard, stderr, ...): the loader allocates `size` guest bytes, copies init (may be NULL = zero) */
struct n2shim_data { const char *name; uint32_t size; const void *init; };

static inline uint32_t n2shim_arg(struct n2a32_cpu *c, unsigned i)          /* 32-bit argument i (0-based), AAPCS, no 64-bit args before it */
{ return i < 4 ? c->r[i] : n2a32_ld32(c, c->r[13] + 4 * (i - 4)); }
static inline void n2shim_ret(struct n2a32_cpu *c, uint32_t v) { c->r[0] = v; }
static inline void n2shim_ret64(struct n2a32_cpu *c, uint64_t v) { c->r[0] = (uint32_t)v; c->r[1] = (uint32_t)(v >> 32); }
/* floating point follows the soft-float-ABI variant "aapcs" (Android armeabi-v7a): double in r0:r1 / on the stack, NOT in d0 */

struct n2elf_set *n2elf_set_create(struct n2a32_mem *mem);
void n2elf_set_destroy(struct n2elf_set *s);
/* Shim tables are searched after the loaded objects, in the order added; arrays end with {NULL, NULL} / {NULL, 0, NULL}. */
void n2elf_set_add_shims(struct n2elf_set *s, const struct n2shim_entry *table);
void n2elf_set_add_data(struct n2elf_set *s, const struct n2shim_data *table);
/* Load one object from a host file (not yet relocated). `soname` is what DT_NEEDED entries of others call it. NULL + *err on failure. */
struct n2elf_image *n2elf_load(struct n2elf_set *s, const char *host_path, const char *soname, char err[160]);
/* Relocate everything loaded so far. Imports found nowhere are bound to the "unresolved" host call (the run stops with
 * N2A32_STOP_NOIMPORT and the symbol's name when one is called) and counted in *unresolved; data imports found nowhere are an error. */
int n2elf_link(struct n2elf_set *s, unsigned *unresolved, char err[160]);
/* Run DT_PREINIT_ARRAY/DT_INIT/DT_INIT_ARRAY of every object in dependency order on `c`. */
int n2elf_run_init(struct n2elf_set *s, struct n2a32_cpu *c);
uint32_t n2elf_sym(struct n2elf_set *s, const char *name);                  /* guest address (bit 0 = Thumb for functions), 0 = none */
uint32_t n2elf_entry(const struct n2elf_image *im);
uint32_t n2elf_base(const struct n2elf_image *im);
/* address -> "soname+0xoffset" or "soname:symbol+0x..." for traces; returns buf */
const char *n2elf_describe(struct n2elf_set *s, uint32_t addr, char *buf, size_t len);

/* ---- shim tables, one package each (work_packages.json V30, V32, V33, V34). Every table is `const struct n2shim_entry NAME[]`. ---- */
extern const struct n2shim_entry n2shim_string[];   /* V30: mem*, str*, __*_chk, __aeabi_mem*, atoi/atoll, qsort, sscanf/fscanf subset, snprintf family (guest varargs) */
extern const struct n2shim_entry n2shim_heap[];     /* V30: malloc/calloc/free/memalign over a guest heap; mmap/munmap/mmap64 (anonymous); getpagesize */
extern const struct n2shim_entry n2shim_thread[];   /* V32: pthread_*, sem_*, sched_*, usleep, gettid, prctl, signal stubs: guest threads = host threads with their own n2a32_cpu */
extern const struct n2shim_entry n2shim_stdio[];    /* V33: open/close/read/write/ioctl/mmap(fd)/fopen.. over the n2vfs device table; printf family -> log; clock_gettime, gettimeofday; exit/abort */
extern const struct n2shim_entry n2shim_android[];  /* V34: __android_log_*, property_get (table), atrace_*, ion_* / mt_ion_* (-> n2vc_hw dma), perf_lock_*, sec_drv_*, dl* (within the set), math (log, exp2), __libc_init, __cxa_* */
extern const struct n2shim_data n2shim_data_all[];  /* V34: __stack_chk_guard, stderr (a fake FILE the stdio shims recognise) */

/* ---- guest-visible devices (V33 owns the table, V40 the /dev/vcu implementation) ---- */
struct n2vfs_dev {
	const char *path;                                                       /* exact path, e.g. "/dev/vcu" */
	int (*open)(void *ctx, int flags);                                      /* 0 or -errno */
	int (*ioctl)(void *ctx, struct n2a32_cpu *c, uint32_t request, uint32_t guest_arg);   /* >= 0 or -errno (Linux numbers) */
	/* mmap: returns the guest address (megabyte-aligned MMIO or plain memory the device mapped) or 0 with *err = errno */
	uint32_t (*mmap)(void *ctx, struct n2a32_cpu *c, uint32_t len, int prot, uint64_t offset, int *err);
	int (*close)(void *ctx);
	void *ctx;
};
int n2vfs_register(const struct n2vfs_dev *dev);                            /* V33 */
#endif
