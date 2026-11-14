/* SPDX-License-Identifier: MIT */
/*
 * n2a32.h -- FROZEN interface of the AArch32 user-mode interpreter (gpu/hwcodec track V). Owner: hwcodec-lead.
 * Propose changes on the board; never edit.
 *
 * Purpose: run the phone's 32-bit vendor codec libraries (ARMv8 AArch32: A32 + T32, VFPv4/ARMv8 FP, NEON) inside a 64-bit
 * process, on the PC (x86-64) and on the phone (arm64). Portable C11, no JIT, no assembly, little-endian hosts only.
 *
 * ---- guest memory -------------------------------------------------------------------------------------------------
 * The guest's 4 GiB are one host reservation (mmap PROT_NONE, MAP_NORESERVE); guest address a is host mem->base + a.
 * Pages become usable through n2a32_mem_map(). A megabyte can be marked MMIO: every access to it goes through the hook
 * (register windows of the hardware). Guest code never sees host pointers; shims convert with n2a32_ptr().
 * Unmapped accesses raise a host SIGSEGV; n2a32_run() (core) turns it into N2A32_STOP_FAULT with the fault address.
 *
 * ---- execution contract between the core (dispatcher) and the instruction-class packages -----------------------------
 * For every instruction the core:
 *   1. fetches it; T32: a 16-bit unit, or two units combined as (first << 16) | second;
 *   2. evaluates the condition (A32: bits 31-28, 0xF = unconditional space; T32: the IT state) and skips the
 *      instruction if it fails (IT state still advances). Class functions therefore NEVER test the condition, except
 *      the T16/T32 conditional branches B<c>, which carry their own condition and call n2a32_cond();
 *   3. sets c->r[15] to what the instruction reads as PC (A32: address + 8; T32: address + 4), c->next_pc to the address
 *      of the following instruction, c->in_it = 1 inside an IT block (16-bit data processing then must not set flags);
 *   4. calls the class function chosen by the tables in the core (see "class functions" below);
 *   5. on N2A32_OK sets the PC to c->next_pc. A class function branches ONLY by calling n2a32_branch() (no interworking:
 *      B, BL, CBZ, table branch, T32 "ALU write PC" does not exist) or n2a32_bx() (interworking: BX, BLX, POP {pc},
 *      LDR pc, LDM with pc, and A32 data processing writing r15), which set c->next_pc and c->thumb.
 * A class function returns N2A32_OK, or N2A32_UNDEF when the encoding is not one it implements (the core then stops with
 * N2A32_STOP_UNDEF, c->stop_insn = the encoding): never guess, never abort(), never print.
 * Reading r15 as an operand gives c->r[15] (already biased); "Align(PC, 4)" for T32 literals is c->r[15] & ~3u.
 * Flags live unpacked in c->n, c->z, c->cf, c->v, c->q (each 0 or 1); GE in c->ge (4 bits); use the helpers below.
 */
#ifndef N2A32_H
#define N2A32_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

enum { N2A32_OK = 0, N2A32_UNDEF = 1 };
enum n2a32_stop { N2A32_STOP_NONE = 0, N2A32_STOP_RETURN, /* reached N2A32_RETURN_ADDR: a host->guest call finished */
	N2A32_STOP_UNDEF, N2A32_STOP_FAULT, N2A32_STOP_SVC, N2A32_STOP_BKPT, N2A32_STOP_LIMIT, /* instruction budget used up */
	N2A32_STOP_REQUEST, /* c->stop_request was set (by a shim or another thread) */ N2A32_STOP_NOIMPORT /* unresolved import called */ };

#define N2A32_HOSTCALL_BASE 0xfff00000u   /* guest addresses [BASE, BASE + 4 * N2A32_HOSTCALL_MAX): import n lives at BASE + 4 * n */
#define N2A32_HOSTCALL_MAX  4096u
#define N2A32_RETURN_ADDR   0xffff0000u   /* LR value of a host->guest call */
#define N2A32_MMIO_SHIFT    20            /* MMIO granularity: 1 MiB */

struct n2a32_cpu;
typedef uint32_t (*n2a32_mmio_rd)(void *ctx, uint32_t addr, unsigned bytes);
typedef void (*n2a32_mmio_wr)(void *ctx, uint32_t addr, unsigned bytes, uint32_t value);
struct n2a32_mmio { n2a32_mmio_rd rd; n2a32_mmio_wr wr; void *ctx; };

struct n2a32_mem {                         /* one per address space, shared by all guest threads */
	uint8_t *base;                         /* host address of guest address 0 */
	uint8_t mmio_idx[4096];                /* per megabyte: 0 = plain memory, else index into mmio[] */
	struct n2a32_mmio mmio[16];
	void *priv;                            /* core's bookkeeping (mapped ranges, brk for the bump allocators) */
};

union n2a32_dreg { uint64_t u64; uint32_t u32[2]; uint16_t u16[4]; uint8_t u8[8]; double f64; float f32[2];
	int64_t s64; int32_t s32[2]; int16_t s16[4]; int8_t s8[8]; };

struct n2a32_cpu {                         /* one per guest thread */
	uint32_t r[16];                        /* r[15]: see the contract */
	uint32_t next_pc;
	uint8_t n, z, cf, v, q, ge;            /* APSR */
	uint8_t thumb;                         /* 1 = T32 */
	uint8_t itstate;                       /* T32 IT state (ARM ARM ITSTATE<7:0>), maintained by the core */
	uint8_t in_it;                         /* this instruction is inside an IT block */
	union n2a32_dreg d[32];                /* VFP/NEON: Dn; Sn = d[n / 2].f32[n & 1]; Qn = d[2n], d[2n + 1] */
	uint32_t fpscr;                        /* NZCV 31-28, QC 27, DN 25, FZ 24, RMode 23-22; cumulative bits 0-4,7 */
	uint32_t tpidruro;                     /* cp15 c13 c0 3 (TLS pointer), read by MRC */
	struct n2a32_mem *mem;
	uint64_t excl_addr; uint32_t excl_valid;   /* LDREX/STREX monitor (process-local emulation; see n2a32_excl_*) */
	uint64_t icount;                       /* instructions executed */
	uint64_t ilimit;                       /* 0 = none */
	volatile uint32_t stop_request;
	enum n2a32_stop stop; uint32_t stop_insn, stop_addr;   /* why n2a32_run returned */
	void *user;                            /* owner's context (thread record, process) */
	void *trace;                           /* trace/debug package's state, NULL = off */
};

/* ---- memory helpers (all packages) ---- */
static inline void *n2a32_ptr(const struct n2a32_mem *m, uint32_t a) { return a ? m->base + a : NULL; }
static inline int n2a32_is_mmio(const struct n2a32_mem *m, uint32_t a) { return m->mmio_idx[a >> N2A32_MMIO_SHIFT] != 0; }
uint32_t n2a32_mmio_load(struct n2a32_mem *m, uint32_t a, unsigned bytes);             /* core */
void n2a32_mmio_store(struct n2a32_mem *m, uint32_t a, unsigned bytes, uint32_t v);    /* core */
#define N2A32_LD(name, type) static inline type name(struct n2a32_cpu *c, uint32_t a) { \
	if (__builtin_expect(n2a32_is_mmio(c->mem, a), 0)) { return (type)n2a32_mmio_load(c->mem, a, sizeof(type)); } \
	type v; memcpy(&v, c->mem->base + a, sizeof(v)); return v; }
#define N2A32_ST(name, type) static inline void name(struct n2a32_cpu *c, uint32_t a, type v) { \
	if (__builtin_expect(n2a32_is_mmio(c->mem, a), 0)) { n2a32_mmio_store(c->mem, a, sizeof(type), (uint32_t)v); return; } \
	memcpy(c->mem->base + a, &v, sizeof(v)); }
N2A32_LD(n2a32_ld8, uint8_t) N2A32_LD(n2a32_ld16, uint16_t) N2A32_LD(n2a32_ld32, uint32_t)
N2A32_ST(n2a32_st8, uint8_t) N2A32_ST(n2a32_st16, uint16_t) N2A32_ST(n2a32_st32, uint32_t)
static inline uint64_t n2a32_ld64(struct n2a32_cpu *c, uint32_t a) { return n2a32_ld32(c, a) | (uint64_t)n2a32_ld32(c, a + 4) << 32; }
static inline void n2a32_st64(struct n2a32_cpu *c, uint32_t a, uint64_t v) { n2a32_st32(c, a, (uint32_t)v); n2a32_st32(c, a + 4, (uint32_t)(v >> 32)); }

/* ---- flag / branch helpers (instruction classes) ---- */
static inline void n2a32_set_nz(struct n2a32_cpu *c, uint32_t res) { c->n = res >> 31; c->z = res == 0; }
/* AddWithCarry of the ARM ARM: returns the sum, *carry and *overflow */
static inline uint32_t n2a32_adc(uint32_t x, uint32_t y, uint32_t cin, uint8_t *carry, uint8_t *overflow)
{
	uint64_t u = (uint64_t)x + y + cin; uint32_t r = (uint32_t)u;
	*carry = (uint8_t)(u >> 32); *overflow = (uint8_t)((~(x ^ y) & (x ^ r)) >> 31); return r;
}
static inline int n2a32_cond(const struct n2a32_cpu *c, unsigned cond)
{
	int r;
	switch (cond >> 1) {
	case 0: r = c->z; break; case 1: r = c->cf; break; case 2: r = c->n; break; case 3: r = c->v; break;
	case 4: r = c->cf && !c->z; break; case 5: r = c->n == c->v; break; case 6: r = c->n == c->v && !c->z; break;
	default: return 1;
	}
	return (cond & 1) ? !r : r;
}
static inline void n2a32_branch(struct n2a32_cpu *c, uint32_t target) { c->next_pc = c->thumb ? (target & ~1u) : (target & ~3u); }
static inline void n2a32_bx(struct n2a32_cpu *c, uint32_t target)   /* BXWritePC */
{
	if (target & 1) { c->thumb = 1; c->next_pc = target & ~1u; } else { c->thumb = 0; c->next_pc = target & ~3u; }
}
static inline uint32_t n2a32_apsr(const struct n2a32_cpu *c)
{ return (uint32_t)c->n << 31 | (uint32_t)c->z << 30 | (uint32_t)c->cf << 29 | (uint32_t)c->v << 28 | (uint32_t)c->q << 27 | (uint32_t)c->ge << 16; }

/* ---- class functions: one package each (work_packages.json V10..V20; V12 owns arm_mem + arm_misc, V16 t32_mem + t32_misc, V20 neon_misc + neon_ldst). `insn` is the A32 word, or for T32 the 16-bit
 * unit / the combined 32-bit value. The VFP/NEON functions always receive the A32 form: the core converts T32 Advanced
 * SIMD data-processing (111U 1111 ...) to 1111 001U ..., and passes VFP / load-store / coprocessor words unchanged with
 * the top nibble forced to 0xE (they are identical in both sets). ---- */
int n2a32_arm_dp(struct n2a32_cpu *c, uint32_t insn);      /* V10: A32 data processing, MOVW/MOVT, multiplies, MRS/MSR (APSR), CLZ */
int n2a32_arm_media(struct n2a32_cpu *c, uint32_t insn);   /* V11: A32 extend, bit field, REV*, RBIT, SSAT/USAT, PKH, SEL, parallel add/sub, Q* saturating, SDIV/UDIV, SMUL*xy/SMLA*, USAD8 */
int n2a32_arm_mem(struct n2a32_cpu *c, uint32_t insn);     /* V12: A32 LDR/STR all forms and sizes, LDRD/STRD, LDM/STM, LDREX/STREX*, PLD/PLI */
int n2a32_arm_misc(struct n2a32_cpu *c, uint32_t insn);    /* V12: A32 B/BL/BLX/BX, SVC, BKPT, UDF, hints, barriers, MRC/MCR cp15 */
int n2a32_t16(struct n2a32_cpu *c, uint32_t insn);         /* V14: every 16-bit T32 instruction (IT itself: updates c->itstate) */
int n2a32_t32_dp(struct n2a32_cpu *c, uint32_t insn);      /* V15: 32-bit T32 data processing (imm, reg, shifted), MOVW/MOVT, mul/div, bit field, sat, extend, misc, parallel add */
int n2a32_t32_mem(struct n2a32_cpu *c, uint32_t insn);     /* V16: 32-bit T32 load/store single, dual, multiple, exclusive, TBB/TBH, PLD */
int n2a32_t32_misc(struct n2a32_cpu *c, uint32_t insn);    /* V16: 32-bit T32 B/BL/BLX/B<c>, MRS/MSR, hints, barriers, UDF, MRC/MCR cp15 */
int n2a32_vfp(struct n2a32_cpu *c, uint32_t insn);         /* V17: VFP (cp10/cp11): VLDR/VSTR/VLDM/VSTM/VPUSH/VPOP, VMOV core<->fp, VMRS/VMSR, arithmetic, VCVT*, VCMP, VSQRT, VFMA*, ARMv8 VSEL, VMAXNM, VRINTx, VCVT{A,N,P,M} */
int n2a32_neon_3same(struct n2a32_cpu *c, uint32_t insn);  /* V18: NEON three registers same length, integer and float, and logic ops */
int n2a32_neon_shift(struct n2a32_cpu *c, uint32_t insn);  /* V19: NEON two registers + shift amount, three registers different lengths, two registers + scalar */
int n2a32_neon_misc(struct n2a32_cpu *c, uint32_t insn);   /* V20: NEON two-register misc, VEXT, VTBL/VTBX, VDUP, one-register modified immediate */
int n2a32_neon_ldst(struct n2a32_cpu *c, uint32_t insn);   /* V20: NEON element and structure load/store VLD1-4/VST1-4 (multiple, single, all lanes) */
/* shared by V12/V16: exclusive monitor on real atomics (other guest threads run on other host threads) */
void n2a32_excl_mark(struct n2a32_cpu *c, uint32_t addr, unsigned bytes, uint64_t value_read);
int n2a32_excl_store(struct n2a32_cpu *c, uint32_t addr, unsigned bytes, uint64_t value);   /* 0 = stored (STREX result 0), 1 = failed */

/* ---- core (V02) ---- */
struct n2a32_mem *n2a32_mem_create(void);                                   /* reserves the 4 GiB; NULL on failure */
void n2a32_mem_destroy(struct n2a32_mem *m);
int n2a32_mem_map(struct n2a32_mem *m, uint32_t addr, uint32_t len, int writable);   /* page aligned; zero filled; 0 or -1 */
int n2a32_mem_protect(struct n2a32_mem *m, uint32_t addr, uint32_t len, int writable);
uint32_t n2a32_mem_alloc(struct n2a32_mem *m, uint32_t len, uint32_t align); /* bump allocation of fresh pages (stacks, heaps, mmap); 0 = full */
int n2a32_mem_mmio(struct n2a32_mem *m, uint32_t addr, uint32_t len, const struct n2a32_mmio *hook);   /* megabyte aligned */
/* opus-guestmem: diagnostics of the guest memory package.  level 1 = mprotect failures (default; stderr without a hook),
 * 2 = also maps >= 256 KiB or that zero old pages, 3 = also every mprotect with its result.  Env N2A32_MEM_LOG=<level>
 * sets the level when this is never called.  The hook gets one line without a newline; it may be called under the lock. */
typedef void (*n2a32_mem_logfn)(const char *line);
void n2a32_mem_set_log(n2a32_mem_logfn fn, int level);
/* opus-guestmem: signal number (SIGSEGV or SIGBUS), si_code and host address of the last guest fault on this thread. */
void n2a32_last_fault(int *sig, int *code, uintptr_t *host_addr);
void n2a32_cpu_init(struct n2a32_cpu *c, struct n2a32_mem *m);
enum n2a32_stop n2a32_run(struct n2a32_cpu *c);                             /* until a stop reason */
/* Host -> guest call, AAPCS: args in r0-r3 then the stack; `sp` must be set up by the caller once. Returns r0 (r1 in *r1 if
 * not NULL). Nested use from inside a shim is allowed (callbacks). On a stop other than RETURN, c->stop tells why and 0 returns. */
uint32_t n2a32_call(struct n2a32_cpu *c, uint32_t fn, const uint32_t *args, unsigned nargs, uint32_t *r1);
/* Host calls: the core, when the PC enters the host-call page, calls the function registered for that slot, then returns
 * to c->r[14] with interworking. Slot 0 is reserved for "unresolved". */
typedef void (*n2a32_hostcall)(struct n2a32_cpu *c);
uint32_t n2a32_hostcall_register(n2a32_hostcall fn, const char *name);     /* -> guest address of the slot; same fn+name twice gives the same slot */
const char *n2a32_hostcall_name(uint32_t guest_addr);

/* ---- trace (T01), all optional: the core calls them when c->trace != NULL ---- */
void n2a32_trace_insn(struct n2a32_cpu *c, uint32_t addr, uint32_t insn, int thumb, int size);
void n2a32_trace_hostcall(struct n2a32_cpu *c, const char *name, int leaving);
void n2a32_trace_mmio(struct n2a32_mem *m, uint32_t addr, unsigned bytes, uint32_t value, int write);
#endif
