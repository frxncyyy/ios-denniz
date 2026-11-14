/* SPDX-License-Identifier: MIT */
/*
 * n2a32_core.c -- fetch/decode dispatch, IT state, the run loop, host->guest calls, host-call slots and the
 * SIGSEGV/SIGBUS -> N2A32_STOP_FAULT bridge.  V02.
 *
 * Contract implemented exactly as include/n2a32.h describes:
 *   - for every instruction the core evaluates the condition (A32 bits 31-28, or the T32 IT state), fetches,
 *     sets c->r[15] biased (A32 addr+8, T32 addr+4), c->next_pc = addr of the next instruction and c->in_it;
 *   - a failing condition skips the class call but still advances the IT state;
 *   - on N2A32_OK the PC becomes c->next_pc; a class returning anything else stops the run (N2A32_STOP_UNDEF
 *     unless the class already set c->stop, e.g. SVC/BKPT);
 *   - the host-call page is intercepted before fetch, the registered function is called, and control returns
 *     to c->r[14] with interworking;
 *   - N2A32_RETURN_ADDR stops with N2A32_STOP_RETURN.
 *
 * Routing tables (also posted on the board; one llvm-mc example per row is exercised by test_v02):
 *
 * A32, condition != 0xF:
 *  0x0F000000 SVC                                              -> arm_misc
 *  bits 27-25 == 111: coprocessor; cp 10/11 -> vfp, else      -> arm_misc (SVC is the 1111 sub-case)
 *  bits 27-25 == 110: coprocessor load/store; cp 10/11 -> vfp -> else arm_misc
 *  bits 27-25 == 101: B/BL                                     -> arm_misc
 *  bits 27-25 == 100: LDM/STM                                  -> arm_mem
 *  bits 27-25 == 011: UDF -> arm_misc; bit4==1 media (every media form has it set);
 *                     bit4==0 load/store register, P=0 or P=1  -> arm_mem
 *  bits 27-25 == 010: load/store word/byte immediate           -> arm_mem
 *  bits 27-25 == 001: hint word (0x0320F0xx) -> arm_misc, else -> arm_dp (immediate, MOVW/MOVT)
 *  bits 27-25 == 000 (consolidated T01/V10/V12 decode; the order matters):
 *     (insn & 0x0F0000F0) == 0x90: multiplies                  -> arm_dp (before the LDRH test!)
 *     (insn & 0x90) == 0x90 and bits7-4 in {B,D,F} or (9 with bits11-8==F)
 *                                                              -> arm_mem (LDRH/STRH/LDRSB/LDRSH/
 *                                                                 LDRD/STRD/LDREX/STREX, any P/U)
 *     (insn & 0x0F900FF0) == 0x01000050                       -> arm_media (QADD/QSUB/QDADD/QDSUB)
 *     (insn & 0x0F900090) == 0x01000080                       -> arm_media (SMLAxy/SMULxy/SMLAWy/...)
 *     (insn & 0x0FF000F0) == 0x01200070                       -> arm_misc (BKPT)
 *     bits23-20==2, 19-16==15, 11-8==15, bits7-4 1/3          -> arm_misc (BX/BLX register)
 *     otherwise (dp register, MRS/MSR, CLZ)                   -> arm_dp
 *
 * A32, condition == 0xF (unconditional), by top byte:
 *  F0/F1 CPS/SETEND -> arm_misc
 *  F2/F3 NEON data processing (V19's exact split; bits 22-19 are operands, not an opcode):
 *     bit23==0                                        -> neon_3same
 *     bit4==1 and bits21-19==0 and bit7==0            -> neon_misc (one register, modified immediate)
 *     bit4==1 otherwise                               -> neon_shift (2reg+shift, narrow/widen, VSHLL/VMOVL)
 *     bit4==0 and bits21-20==3                        -> neon_misc (two registers misc, VEXT/VTBL/VDUP/...)
 *     bit4==0 otherwise                               -> neon_shift (3 different lengths, 2reg+scalar)
 *  F4 PLI immediate (mask 0xFF70F000 == 0xF450F000, U=0/1) -> arm_mem, else neon_ldst
 *  F5 PLD/PLDW immediate -> arm_mem; barriers/CLREX (0xF57FF0xx) -> arm_misc; else arm_misc
 *  F6/F7 PLI/PLD/PLDW register -> arm_mem, else arm_misc
 *  F8-FB BLX immediate / unconditional -> arm_misc
 *  FC/FD/FE coprocessor; cp 10/11 -> vfp, else arm_misc
 *  FF SVC -> arm_misc
 *
 * T32 (first halfword hw1; hw1 >= 0xE800 means 32-bit):
 *  0xE800-0xE9FF LDM/STM, LDRD/STRD, exclusive, TBB/TBH -> t32_mem
 *  0xEA00-0xEBFF data-processing shifted/extended register -> t32_dp
 *  0xEC00-0xEEFF coprocessor; cp 10/11 -> vfp, else       -> t32_misc
 *  0xEF00-0xEFFF NEON dp U=0: a32 = 0xF2000000 | (i & 0xFFFFFF) | ((i & 0x10000000) >> 4), then F2/F3 split
 *  0xF000-0xF7FF hw2 bit15==1 -> t32_misc (B/BL/BLX/B<c>, MRS/MSR, hints, barriers, UDF); else t32_dp
 *  0xF800-0xF9FF F8xx or (F9xx with hw1 bit4 set) -> t32_mem (load/store single, PLD/PLI);
 *                else neon_ldst (T32 F9xx converted to A32 F4xx)
 *  0xFA00-0xFBFF data-processing register, mul/div -> t32_dp
 *  0xFC00-0xFDFF coprocessor; cp 10/11 -> vfp, else t32_misc
 *  0xFE00-0xFEFF vfp (word unchanged: A32 and T32 share it)
 *  0xFF00-0xFFFF NEON dp U=1: a32 = 0xF3000000 | (i & 0xFFFFFF)
 *  everything below 0xE800 -> t16
 */
#include "n2a32.h"

#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef int (*class_fn)(struct n2a32_cpu *, uint32_t);

#define GUEST_SIZE_U64 (1ull << 32)
#define GUEST_RESERVE_U64 (GUEST_SIZE_U64 + 65536ull)  /* the memory package's host guard page(s) */
#define HOSTCALL_END   (N2A32_HOSTCALL_BASE + 4u * N2A32_HOSTCALL_MAX)

/* ---------------------------------------------------------------------------------------------- hostcall slots */

static pthread_mutex_t hostcall_lock = PTHREAD_MUTEX_INITIALIZER;
static n2a32_hostcall hostcall_fn[N2A32_HOSTCALL_MAX];
static char *hostcall_name_tab[N2A32_HOSTCALL_MAX];

static n2a32_hostcall hostcall_lookup(uint32_t addr, const char **name)
{
	uint32_t off = addr - N2A32_HOSTCALL_BASE;
	unsigned slot;

	/* Lock-free on the hot path (one per guest call of an import): a slot's name and function are published
	 * once, under hostcall_lock, with release stores, and never change or disappear afterwards. */
	*name = NULL;
	if (addr < N2A32_HOSTCALL_BASE || addr >= HOSTCALL_END || (off & 3u) != 0)
		return NULL;
	slot = off >> 2;
	*name = __atomic_load_n(&hostcall_name_tab[slot], __ATOMIC_ACQUIRE);
	return __atomic_load_n(&hostcall_fn[slot], __ATOMIC_ACQUIRE);
}

uint32_t n2a32_hostcall_register(n2a32_hostcall fn, const char *name)
{
	unsigned i, slot = 0;
	char *copy = NULL;

	/* fn == NULL is accepted on purpose: the loader registers one named-but-unimplemented slot per
	 * unresolved import, and entering such a slot stops with N2A32_STOP_NOIMPORT while
	 * n2a32_hostcall_name(stop_addr) still returns the symbol name (contract agreed with V03). */
	if (!name)
		return 0;
	pthread_mutex_lock(&hostcall_lock);
	for (i = 1; i < N2A32_HOSTCALL_MAX; i++) {
		if (hostcall_fn[i] == fn && hostcall_name_tab[i] && strcmp(hostcall_name_tab[i], name) == 0) {
			slot = i;
			pthread_mutex_unlock(&hostcall_lock);
			return N2A32_HOSTCALL_BASE + 4u * slot;
		}
	}
	for (i = 1; i < N2A32_HOSTCALL_MAX; i++) {
		if (hostcall_fn[i] == NULL && hostcall_name_tab[i] == NULL) {
			slot = i;
			break;
		}
	}
	if (slot == 0) {
		pthread_mutex_unlock(&hostcall_lock);
		return 0;
	}
	copy = (char *)malloc(strlen(name) + 1);
	if (!copy) {
		pthread_mutex_unlock(&hostcall_lock);
		return 0;
	}
	strcpy(copy, name);
	__atomic_store_n(&hostcall_name_tab[slot], copy, __ATOMIC_RELEASE);
	__atomic_store_n(&hostcall_fn[slot], fn, __ATOMIC_RELEASE);
	pthread_mutex_unlock(&hostcall_lock);
	return N2A32_HOSTCALL_BASE + 4u * slot;
}

const char *n2a32_hostcall_name(uint32_t guest_addr)
{
	uint32_t off = guest_addr - N2A32_HOSTCALL_BASE;
	unsigned slot;
	const char *n;

	if (guest_addr < N2A32_HOSTCALL_BASE || guest_addr >= HOSTCALL_END || (off & 3u) != 0)
		return NULL;
	slot = off >> 2;
	if (slot == 0)
		return "unresolved";
	n = __atomic_load_n(&hostcall_name_tab[slot], __ATOMIC_ACQUIRE);
	return n;
}

/* ------------------------------------------------------------------------------------- fault -> STOP_FAULT */

struct run_ctx {
	sigjmp_buf jb;
	struct n2a32_cpu *c;
	struct run_ctx *prev;
	volatile uint32_t fault_pc;
};

static _Thread_local struct run_ctx *tls_run;

static struct sigaction old_segv, old_bus;
static pthread_once_t fault_once = PTHREAD_ONCE_INIT;

static void chain_fault(int sig, siginfo_t *si, void *uc, struct sigaction *old)
{
	if ((old->sa_flags & SA_SIGINFO) != 0 && old->sa_sigaction != NULL) {
		old->sa_sigaction(sig, si, uc);
		return;
	}
	if ((old->sa_flags & SA_SIGINFO) == 0 && old->sa_handler != SIG_DFL && old->sa_handler != SIG_IGN) {
		old->sa_handler(sig);
		return;
	}
	/* SIG_DFL or SIG_IGN: deliver with the default action. */
	signal(sig, SIG_DFL);
	raise(sig);
}

/* opus-guestmem: the last host fault the bridge turned into N2A32_STOP_FAULT on this thread (signal, si_code, host address). */
static _Thread_local int tls_fault_sig, tls_fault_code;
static _Thread_local uintptr_t tls_fault_addr;

void n2a32_last_fault(int *sig, int *code, uintptr_t *host_addr)
{
	if (sig) *sig = tls_fault_sig;
	if (code) *code = tls_fault_code;
	if (host_addr) *host_addr = tls_fault_addr;
}

static void fault_handler(int sig, siginfo_t *si, void *uc)
{
	struct run_ctx *ctx = tls_run;

	if (ctx != NULL && ctx->c != NULL && ctx->c->mem != NULL && ctx->c->mem->base != NULL && si != NULL) {
		uintptr_t a = (uintptr_t)si->si_addr;
		uintptr_t b = (uintptr_t)ctx->c->mem->base;
		if (a >= b && (uint64_t)(a - b) < GUEST_RESERVE_U64) {
			uint64_t off = (uint64_t)(a - b);
			tls_fault_sig = sig;
			tls_fault_code = si->si_code;
			tls_fault_addr = a;
			ctx->c->stop = N2A32_STOP_FAULT;
			ctx->c->stop_addr = (off < GUEST_SIZE_U64) ? (uint32_t)off : 0xFFFFFFFFu;
			ctx->c->stop_insn = 0;
			siglongjmp(ctx->jb, 1);
		}
	}
	if (sig == SIGSEGV)
		chain_fault(sig, si, uc, &old_segv);
	else
		chain_fault(sig, si, uc, &old_bus);
}

static void install_fault_handlers(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = fault_handler;
	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	sigaddset(&sa.sa_mask, SIGSEGV);
	sigaddset(&sa.sa_mask, SIGBUS);
	(void)sigaction(SIGSEGV, &sa, &old_segv);
	(void)sigaction(SIGBUS, &sa, &old_bus);
}

/* ---------------------------------------------------------------------------------------------- routing */

static class_fn neon_route(uint32_t insn)
{
	/* Exact split of the F2/F3 (bit23=1) space, from V19's decode (bits 22-19 are operand bits, not an opcode):
	 *   bit4=1 and bits21-19==0 and bit7==0 -> one register, modified immediate (V20);
	 *   bit4=1 otherwise                     -> two regs + shift, narrowing/widening, VSHLL/VMOVL (V19);
	 *   bit4=0 and bits21-20==3              -> two registers, miscellaneous (V20: VMOVN/VREV/VSWP/...);
	 *   bit4=0 otherwise                     -> three different lengths / two regs + scalar (V19). */
	if ((insn & 0x00800000u) == 0)          /* bit 23 clear: three registers of the same length */
		return n2a32_neon_3same;
	if ((insn & 0x10u) != 0) {
		if (((insn >> 19) & 7u) == 0 && ((insn >> 7) & 1u) == 0)
			return n2a32_neon_misc;
		return n2a32_neon_shift;
	}
	if (((insn >> 20) & 3u) == 3u)
		return n2a32_neon_misc;
	return n2a32_neon_shift;
}

static class_fn a32_route(uint32_t insn)
{
	unsigned cond = insn >> 28;
	unsigned group, op, op2, cp;

	if (cond != 0xFu) {
		group = (insn >> 25) & 7u;
		switch (group) {
		case 0:                          /* data processing register and miscellaneous */
			/* Consolidated case-0 decode (V10 exact media masks + V12 extra-ld/st + T01's MUL
			 * collision fix).  The order matters: multiplies first (bit 7 and bit 4 are both set,
			 * like LDRH), then the extra load/store nibble test, then the exact media masks. */
			if ((insn & 0x0F0000F0u) == 0x00000090u)
				return n2a32_arm_dp;    /* MUL/MLA/MLS/UMAAL/UMULL/UMLAL/SMULL/SMLAL */
			if ((insn & 0x90u) == 0x90u) {
				unsigned lo = (insn >> 4) & 0xFu;
				if (lo == 0xBu || lo == 0xDu || lo == 0xFu ||
				    (lo == 0x9u && ((insn >> 8) & 0xFu) == 0xFu))
					return n2a32_arm_mem; /* LDRH/STRH/LDRSB/LDRSH/LDRD/STRD, LDREX/STREX */
			}
			op = (insn >> 20) & 0x1Fu;
			op2 = (insn >> 4) & 0xFu;
			if ((op & 0x10u) != 0) {
				if ((insn & 0x0F900FF0u) == 0x01000050u)
					return n2a32_arm_media; /* QADD/QSUB/QDADD/QDSUB */
				if ((insn & 0x0F900090u) == 0x01000080u)
					return n2a32_arm_media; /* SMLAxy/SMULxy/SMLAWy/SMULWy/SMLALxy */
				if ((insn & 0x0FF000F0u) == 0x01200070u)
					return n2a32_arm_misc;  /* BKPT */
				if (op == 0x12u && ((insn >> 16) & 0xFu) == 0xFu && ((insn >> 8) & 0xFu) == 0xFu &&
				    (op2 == 1u || op2 == 3u))
					return n2a32_arm_misc;  /* BX/BLX (register) */
			}
			return n2a32_arm_dp;            /* everything else: DP register, MRS/MSR, CLZ */
		case 1:                          /* data processing immediate, MOVW/MOVT, hint space */
			if ((insn & 0x0FFFFF00u) == 0x0320F000u)
				return n2a32_arm_misc;  /* NOP/YIELD/WFE/WFI/SEV/SEVL (imm8 in bits 7-0) */
			return n2a32_arm_dp;
		case 2:
			return n2a32_arm_mem;           /* load/store word or byte, immediate */
		case 3:
			if ((insn & 0x0FF000F0u) == 0x07F000F0u)
				return n2a32_arm_misc;  /* UDF */
			/* Every media instruction of this group has bit 4 set (V12 checked all forms);
			 * LDR/STR (register), including the P=0/T forms, has it clear. */
			if ((insn & 0x10u) != 0)
				return n2a32_arm_media;
			return n2a32_arm_mem;
		case 4:
			return n2a32_arm_mem;           /* LDM/STM */
		case 5:
			return n2a32_arm_misc;          /* B/BL */
		case 6:
			cp = (insn >> 8) & 0xFu;
			return (cp == 10u || cp == 11u) ? n2a32_vfp : n2a32_arm_misc;
		default:                         /* group 7 */
			if (((insn >> 24) & 0xFu) == 0xFu)
				return n2a32_arm_misc;  /* SVC */
			cp = (insn >> 8) & 0xFu;
			return (cp == 10u || cp == 11u) ? n2a32_vfp : n2a32_arm_misc;
		}
	}

	switch (insn >> 24) {
	case 0xF0u:
	case 0xF1u:
		return n2a32_arm_misc;              /* CPS, SETEND, unconditional miscellaneous */
	case 0xF2u:
	case 0xF3u:
		return neon_route(insn);
	case 0xF4u:
		if ((insn & 0xFF70F000u) == 0xF450F000u)
			return n2a32_arm_mem;           /* PLI (immediate), U=0 and U=1 */
		return n2a32_neon_ldst;             /* Advanced SIMD element/structure load/store */
	case 0xF5u:
		if ((insn & 0xFF70F000u) == 0xF550F000u)
			return n2a32_arm_mem;           /* PLD (immediate) */
		if ((insn & 0xFF70F000u) == 0xF510F000u)
			return n2a32_arm_mem;           /* PLDW (immediate) */
		return n2a32_arm_misc;              /* barriers, CLREX, other unconditional */
	case 0xF6u:
	case 0xF7u:
		if ((insn & 0xFF70F010u) == 0xF650F000u)
			return n2a32_arm_mem;           /* PLI (register) */
		if ((insn & 0xFF70F010u) == 0xF750F000u)
			return n2a32_arm_mem;           /* PLD (register) */
		if ((insn & 0xFF70F010u) == 0xF710F000u)
			return n2a32_arm_mem;           /* PLDW (register) */
		return n2a32_arm_misc;
	case 0xF8u:
	case 0xF9u:
	case 0xFAu:
	case 0xFBu:
		return n2a32_arm_misc;              /* BLX immediate and the rest of the unconditional space */
	case 0xFCu:
	case 0xFDu:
	case 0xFEu:
		cp = (insn >> 8) & 0xFu;
		return (cp == 10u || cp == 11u) ? n2a32_vfp : n2a32_arm_misc;
	default:                                /* 0xFF */
		return n2a32_arm_misc;              /* SVC with cond=1111 */
	}
}

static class_fn t32_route(uint32_t insn, uint32_t *out)
{
	uint32_t hw1 = (insn >> 16) & 0xFFFFu;

	*out = insn;
	if ((hw1 & 0xF800u) == 0xE800u) {
		switch ((hw1 >> 8) & 7u) {
		case 0:
		case 1:
			return n2a32_t32_mem;
		case 2:
		case 3:
			return n2a32_t32_dp;
		case 4:
		case 5:
		case 6: {
			unsigned cp = (insn >> 8) & 0xFu;
			return (cp == 10u || cp == 11u) ? n2a32_vfp : n2a32_t32_misc;
		}
		default: {                      /* 0xEF: Advanced SIMD data processing, U = 0 */
			uint32_t a32 = (insn & 0x00FFFFFFu) | 0xF2000000u | ((insn & 0x10000000u) >> 4);
			*out = a32;
			return neon_route(a32);
		}
		}
	}
	if ((hw1 & 0xF800u) == 0xF000u) {
		if ((insn & 0x00008000u) != 0)
			return n2a32_t32_misc;      /* branches and miscellaneous control */
		return n2a32_t32_dp;            /* data processing, plain binary immediate */
	}
	if ((hw1 & 0xFE00u) == 0xF800u) {   /* F8/F9 */
		/* Load/store single has hw1 bit 8 set (F8C1 STR, F8D1 LDR, F991 LDRSB, ...).  The F9xx NEON
		 * element/structure group has it clear (F900 VST1, F920 VLD1, F9A0 VLD1 lane).  PLD/PLI are the
		 * exception: hw2 holds 1111:imm12, which no register-list encoding uses. */
		if ((hw1 & 0x0F00u) == 0x0800u) /* F8xx: load/store single, PLD/PLI */
			return n2a32_t32_mem;
		if ((hw1 & 0x0010u) != 0)       /* F9xx with hw1 bit 4 set: LDRSB/LDRSH/PLI, ... */
			return n2a32_t32_mem;
		*out = (insn & 0x00FFFFFFu) | 0xF4000000u;  /* Advanced SIMD structure ld/st in A32 form */
		return n2a32_neon_ldst;
	}
	if ((hw1 & 0xFE00u) == 0xFA00u)     /* FA/FB */
		return n2a32_t32_dp;
	if ((hw1 & 0xFE00u) == 0xFC00u) {   /* FC/FD */
		unsigned cp = (insn >> 8) & 0xFu;
		return (cp == 10u || cp == 11u) ? n2a32_vfp : n2a32_t32_misc;
	}
	if (hw1 < 0xFF00u)                  /* FE */
		return n2a32_vfp;
	/* FF: Advanced SIMD data processing, U = 1 */
	{
		uint32_t a32 = (insn & 0x00FFFFFFu) | 0xF3000000u;
		*out = a32;
		return neon_route(a32);
	}
}

/* ---------------------------------------------------------------------------------------------- CPU state */

void n2a32_cpu_init(struct n2a32_cpu *c, struct n2a32_mem *m)
{
	if (!c)
		return;
	memset(c, 0, sizeof(*c));
	c->mem = m;
	c->stop = N2A32_STOP_NONE;
}

static void it_advance(struct n2a32_cpu *c)
{
	/* ARM ARM ITAdvance(): ITSTATE<4:0> = LSL(ITSTATE<4:0>, 1), or 0 when ITSTATE<2:0> was zero. */
	if ((c->itstate & 7u) == 0)
		c->itstate = 0;
	else
		c->itstate = (uint8_t)((c->itstate & 0xE0u) | ((c->itstate << 1) & 0x1Fu));
}

/* ------------------------------------------------------------------------------------------ decode cache */

#include "n2a32_fast.h"

/* Per host thread, direct mapped, indexed by pc.  Every entry carries the raw encoding and is re-validated
 * against a fresh fetch on every use, so it never needs invalidation (see n2a32_fast.h). */
#define DC_BITS 12u
#define DC_MASK ((1u << DC_BITS) - 1u)

/* Non-ABI test switch (tests/perf/t_fast.c): 0 = every instruction goes through its class function. */
volatile int n2a32_core_fastpath = 1;

static pthread_key_t dc_key;
static pthread_once_t dc_once = PTHREAD_ONCE_INIT;
static _Thread_local struct dent *tls_dc;

static void dc_free(void *p) { free(p); }
static void dc_key_init(void) { (void)pthread_key_create(&dc_key, dc_free); }

static struct dent *dcache_get(void)
{
	struct dent *dc = tls_dc;

	if (dc != NULL)
		return dc;
	pthread_once(&dc_once, dc_key_init);
	dc = (struct dent *)calloc((size_t)DC_MASK + 1u, sizeof(*dc));
	if (dc != NULL) {
		(void)pthread_setspecific(dc_key, dc);   /* freed at thread exit */
		tls_dc = dc;
	}
	return dc;
}

static void dcache_fill(struct dent *e, uint32_t pc, int thumb, uint32_t raw, unsigned size)
{
	memset(e, 0, sizeof(*e));
	e->key = pc | (uint32_t)thumb;
	e->raw = raw;
	e->size = (uint8_t)size;
	if (thumb && size == 4) {
		e->fn = t32_route(raw, &e->routed);
		fast_t32(e, raw);
	} else if (thumb) {
		e->routed = raw;
		e->fn = n2a32_t16;
		fast_t16(e, raw);
	} else {
		e->routed = raw;
		e->fn = a32_route(raw);
	}
}

/* Stop bookkeeping shared by both loops. */
static inline void stop_at(struct n2a32_cpu *c, enum n2a32_stop why, uint32_t pc)
{
	c->stop = why;
	c->stop_addr = pc;
	c->stop_insn = 0;
}

static inline struct dent *dc_lookup(struct dent *dc, uint32_t mask, uint32_t pc, int thumb, uint32_t raw,
				     unsigned size)
{
	struct dent *e = &dc[(pc >> 1) & mask];
	/* (pc, thumb, raw) determine the entry completely: T16 raws are < 0xE800, T32 raws >= 0xE8000000. */
	if (__builtin_expect(e->key != (pc | (uint32_t)thumb) || e->raw != raw, 0))
		dcache_fill(e, pc, thumb, raw, size);
	return e;
}

/* The class path failed: record the stop exactly as the contract says. */
static inline void stop_undef(struct n2a32_cpu *c, uint32_t pc, uint32_t raw)
{
	if (c->stop == N2A32_STOP_NONE)
		c->stop = N2A32_STOP_UNDEF;
	if (c->stop_insn == 0)
		c->stop_insn = raw;
	c->stop_addr = pc;
	c->r[15] = pc;
}

/*
 * Lean T32 loop: used while the guest is in Thumb state, outside an IT block, with no trace attached.  It
 * does exactly what the generic step below does for that case (same checks every instruction, same state
 * updates, same order), minus the work that cannot apply: condition evaluation, IT bookkeeping, tracing.
 * Returns 1 when the run must stop (c->stop set), 0 to continue in the generic loop at c->r[15].
 */
static int thumb_loop(struct n2a32_cpu *c, struct run_ctx *ctx, struct dent *dc, uint32_t mask, uint32_t pc)
{
	const uint8_t *base = c->mem->base;
	const int fast_on = n2a32_core_fastpath;

	c->in_it = 0;
	for (;;) {
		struct dent *e, cur;
		uint32_t raw;
		unsigned size;
		uint16_t hw1;

		if (__builtin_expect(__atomic_load_n(&c->stop_request, __ATOMIC_RELAXED) != 0, 0) ||
		    __builtin_expect(c->ilimit != 0 && c->icount >= c->ilimit, 0) ||
		    __builtin_expect(pc >= N2A32_HOSTCALL_BASE, 0))
			break;                              /* the generic loop handles all of these */
		ctx->fault_pc = pc;
		memcpy(&hw1, base + pc, 2);
		if (hw1 >= 0xE800u) {
			uint16_t hw2;
			memcpy(&hw2, base + pc + 2, 2);
			raw = ((uint32_t)hw1 << 16) | hw2;
			size = 4;
		} else {
			raw = hw1;
			size = 2;
		}
		e = dc_lookup(dc, mask, pc, 1, raw, size);
		cur = *e;    /* a local copy: guest code run re-entrantly (MMIO hook, shim) may refill the slot */
		c->r[15] = pc + 4u;
		c->next_pc = pc + size;
		if (cur.op != F_NONE && fast_on) {
			(void)fast_exec(c, &cur);
		} else if (cur.fn(c, cur.routed) != N2A32_OK) {
			stop_undef(c, pc, raw);
			return 1;
		}
		c->icount++;
		pc = c->next_pc;
		if (__builtin_expect(!c->thumb, 0)) {
			pc &= ~3u;
			c->itstate = 0;                     /* ITSTATE does not exist in ARM state */
			break;
		}
		pc &= ~1u;
		if (__builtin_expect(c->itstate != 0, 0))
			break;                              /* an IT instruction: the generic loop runs the block */
	}
	c->r[15] = pc;
	return 0;
}

/* The generic loop (every state); kept out of n2a32_run so the sigsetjmp there does not pessimise it. */
static __attribute__((noinline)) void run_loop(struct n2a32_cpu *c, struct run_ctx *ctx, struct dent *dc,
						uint32_t mask)
{
	int thumb = c->thumb != 0;
	uint32_t pc = c->r[15] & (thumb ? ~1u : ~3u);

	for (;;) {
		const uint8_t *base = c->mem->base;
		struct dent *e, cur;
		uint32_t raw;
		unsigned size;
		int in_it;

		if (__atomic_load_n(&c->stop_request, __ATOMIC_RELAXED) != 0) {
			stop_at(c, N2A32_STOP_REQUEST, pc);
			return;
		}
		if (c->ilimit != 0 && c->icount >= c->ilimit) {
			stop_at(c, N2A32_STOP_LIMIT, pc);
			return;
		}
		if (__builtin_expect(pc >= N2A32_HOSTCALL_BASE, 0)) {   /* one test covers both special pages */
			if (pc == N2A32_RETURN_ADDR) {
				stop_at(c, N2A32_STOP_RETURN, pc);
				return;
			}
			if (pc < HOSTCALL_END) {
				const char *name = NULL;
				n2a32_hostcall hf = hostcall_lookup(pc, &name);

				if (hf == NULL) {
					stop_at(c, N2A32_STOP_NOIMPORT, pc);
					return;
				}
				if (c->trace != NULL)
					n2a32_trace_hostcall(c, name ? name : "?", 1);
				hf(c);
				if (c->trace != NULL)
					n2a32_trace_hostcall(c, name ? name : "?", 0);
				if (c->stop != N2A32_STOP_NONE && c->stop != N2A32_STOP_RETURN &&
				    __atomic_load_n(&c->stop_request, __ATOMIC_RELAXED) == 0) {
					/* A nested n2a32_call made by the shim failed and the shim did not handle it:
					 * stop here with the inner reason/address instead of running on with a
					 * meaningless result.  (A shim that handles the failure itself sets
					 * stop_request, e.g. V32's pthread_once; that stays STOP_REQUEST below.) */
					if (c->stop_addr == 0)
						c->stop_addr = pc;
					return;
				}
				c->stop = N2A32_STOP_NONE;
				c->itstate = 0;             /* a call is a branch; no IT block survives it */
				n2a32_bx(c, c->r[14]);
				pc = c->next_pc & (c->thumb ? ~1u : ~3u);
				c->r[15] = pc;
				thumb = c->thumb != 0;
				continue;
			}
		}

		if (thumb && c->itstate == 0 && c->trace == NULL && pc < N2A32_HOSTCALL_BASE) {
			/* (pc below the special pages also keeps pc + 2 of a 32-bit fetch inside the 4 GiB) */
			if (thumb_loop(c, ctx, dc, mask, pc))
				return;
			thumb = c->thumb != 0;
			pc = c->r[15];
			continue;
		}

		ctx->fault_pc = pc;
		if (thumb) {
			uint16_t hw1;
			memcpy(&hw1, base + pc, 2);
			if (hw1 >= 0xE800u) {
				uint16_t hw2;
				if ((uint64_t)pc + 4u > GUEST_SIZE_U64) {
					stop_at(c, N2A32_STOP_FAULT, pc);
					c->r[15] = pc;
					return;
				}
				memcpy(&hw2, base + pc + 2, 2);
				raw = ((uint32_t)hw1 << 16) | hw2;
				size = 4;
			} else {
				raw = hw1;
				size = 2;
			}
		} else {
			memcpy(&raw, base + pc, 4);
			size = 4;
		}
		e = dc_lookup(dc, mask, pc, thumb, raw, size);
		cur = *e;

		if (thumb) {
			in_it = (c->itstate & 0xFu) != 0;
			if (in_it && !n2a32_cond(c, c->itstate >> 4u)) {
				/* Skipped: no class call, but ITSTATE advances as for an executed instruction. */
				it_advance(c);
				pc += size;
				c->r[15] = pc;
				c->next_pc = pc;
				c->icount++;
				continue;
			}
		} else {
			unsigned cond = raw >> 28;
			in_it = 0;
			if (cond < 0xEu && !n2a32_cond(c, cond)) {
				pc += size;
				c->r[15] = pc;
				c->next_pc = pc;
				c->icount++;
				continue;
			}
		}

		c->r[15] = pc + (thumb ? 4u : 8u);
		c->next_pc = pc + size;
		c->in_it = (uint8_t)in_it;
		if (c->trace != NULL)
			n2a32_trace_insn(c, pc, raw, thumb, (int)size);

		if (cur.op != F_NONE && !in_it && n2a32_core_fastpath) {
			(void)fast_exec(c, &cur);
		} else if (cur.fn(c, cur.routed) != N2A32_OK) {
			stop_undef(c, pc, raw);
			return;
		}

		if (in_it)
			it_advance(c);
		pc = c->next_pc;
		thumb = c->thumb != 0;
		pc &= thumb ? ~1u : ~3u;
		if (!thumb)
			c->itstate = 0;             /* ITSTATE does not exist in ARM state */
		c->r[15] = pc;
		c->icount++;
	}
}

enum n2a32_stop n2a32_run(struct n2a32_cpu *c)
{
	struct run_ctx ctx;
	struct dent one;
	struct dent *dc;
	uint32_t mask;

	if (!c) {
		return N2A32_STOP_FAULT;
	}
	c->stop = N2A32_STOP_NONE;
	c->stop_insn = 0;
	c->stop_addr = 0;
	if (c->mem == NULL || c->mem->base == NULL) {
		c->stop = N2A32_STOP_FAULT;
		return c->stop;
	}

	pthread_once(&fault_once, install_fault_handlers);
	dc = dcache_get();
	mask = DC_MASK;
	if (dc == NULL) {                    /* no memory for the cache: a single always-refilled entry */
		memset(&one, 0, sizeof(one));
		dc = &one;
		mask = 0;
	}

	ctx.c = c;
	ctx.prev = tls_run;
	ctx.fault_pc = c->r[15];
	if (sigsetjmp(ctx.jb, 1) != 0) {
		/* fault_handler ran: c->stop and c->stop_addr are set, the instruction did not complete. */
		tls_run = ctx.prev;
		c->stop = N2A32_STOP_FAULT;
		c->r[15] = ctx.fault_pc;
		return c->stop;
	}
	tls_run = &ctx;      /* only after the jump buffer is live, so an early signal cannot longjmp into garbage */

	run_loop(c, &ctx, dc, mask);

	tls_run = ctx.prev;
	return c->stop;
}

/* ---------------------------------------------------------------------------------------------- host -> guest */

uint32_t n2a32_call(struct n2a32_cpu *c, uint32_t fn, const uint32_t *args, unsigned nargs, uint32_t *r1)
{
	uint32_t saved_r[4], saved_r12, saved_r14, saved_sp, saved_pc, saved_next_pc;
	uint8_t saved_n, saved_z, saved_cf, saved_v, saved_q, saved_ge;
	uint8_t saved_thumb, saved_it, saved_in_it;
	enum n2a32_stop saved_stop;
	uint32_t saved_insn, saved_addr;
	uint32_t new_sp, stack_bytes, res0 = 0, res1 = 0;
	uint64_t need;
	unsigned i;
	int nested, failed = 0;
	enum n2a32_stop st;

	if (!c || !c->mem)
		return 0;

	nested = tls_run != NULL && tls_run->c == c;

	saved_r[0] = c->r[0]; saved_r[1] = c->r[1]; saved_r[2] = c->r[2]; saved_r[3] = c->r[3];
	saved_r12 = c->r[12];
	saved_r14 = c->r[14];
	saved_sp = c->r[13];
	saved_pc = c->r[15];
	saved_next_pc = c->next_pc;
	saved_n = c->n; saved_z = c->z; saved_cf = c->cf; saved_v = c->v; saved_q = c->q; saved_ge = c->ge;
	saved_thumb = c->thumb; saved_it = c->itstate; saved_in_it = c->in_it;
	saved_stop = c->stop; saved_insn = c->stop_insn; saved_addr = c->stop_addr;

	need = (nargs > 4u) ? ((uint64_t)(nargs - 4u) * 4u + 7u) & ~(uint64_t)7u : 0u;
	stack_bytes = (uint32_t)need;
	if (nargs > 1024u || need > saved_sp || saved_sp - stack_bytes < 0x1000u) {
		c->stop = N2A32_STOP_FAULT;
		c->stop_addr = saved_sp;
		c->stop_insn = 0;
		return 0;
	}
	new_sp = (saved_sp - stack_bytes) & ~7u;      /* AAPCS: SP 8-aligned at a public interface */
	if (stack_bytes != 0) {
		/* A caller can hand us a bad SP; a fault while writing the arguments must still become a stop. */
		struct run_ctx actx;
		pthread_once(&fault_once, install_fault_handlers);
		actx.c = c;
		actx.prev = tls_run;
		actx.fault_pc = saved_pc;
		if (sigsetjmp(actx.jb, 1) != 0) {
			tls_run = actx.prev;
			c->stop = N2A32_STOP_FAULT;
			c->stop_insn = 0;
			return 0;
		}
		tls_run = &actx;
		for (i = 4; i < nargs; i++)
			n2a32_st32(c, new_sp + 4u * (i - 4u), args ? args[i] : 0u);
		tls_run = actx.prev;
	}

	c->r[0] = (nargs > 0 && args) ? args[0] : 0u;
	c->r[1] = (nargs > 1 && args) ? args[1] : 0u;
	c->r[2] = (nargs > 2 && args) ? args[2] : 0u;
	c->r[3] = (nargs > 3 && args) ? args[3] : 0u;
	c->r[12] = 0;
	c->r[13] = new_sp;
	c->r[14] = N2A32_RETURN_ADDR;
	c->thumb = (uint8_t)(fn & 1u);
	c->r[15] = fn & ~1u;
	c->next_pc = fn & ~1u;
	c->itstate = 0;
	c->in_it = 0;
	c->stop = N2A32_STOP_NONE;
	c->stop_insn = 0;
	c->stop_addr = 0;

	st = n2a32_run(c);
	res0 = c->r[0];
	res1 = c->r[1];
	if (st != N2A32_STOP_RETURN)
		failed = 1;

	/* Restore the caller's state; only the result registers survive a successful call. */
	c->r[0] = saved_r[0]; c->r[1] = saved_r[1]; c->r[2] = saved_r[2]; c->r[3] = saved_r[3];
	c->r[12] = saved_r12;
	c->r[13] = saved_sp;
	c->r[14] = saved_r14;
	c->r[15] = saved_pc;
	c->n = saved_n; c->z = saved_z; c->cf = saved_cf; c->v = saved_v; c->q = saved_q; c->ge = saved_ge;
	c->thumb = saved_thumb;
	c->itstate = saved_it;
	c->in_it = saved_in_it;
	c->next_pc = saved_next_pc;

	if (failed) {
		/* Leave c->stop/stop_insn/stop_addr describing the failure. */
		c->stop = st;
		c->r[0] = 0;
		if (r1)
			*r1 = 0;
		return 0;
	}
	if (nested) {
		c->stop = saved_stop;
		c->stop_insn = saved_insn;
		c->stop_addr = saved_addr;
	} else {
		c->stop = N2A32_STOP_RETURN;
		c->stop_insn = 0;
		c->stop_addr = N2A32_RETURN_ADDR;
		c->r[15] = N2A32_RETURN_ADDR;
	}
	c->r[0] = res0;
	c->r[1] = res1;
	if (r1)
		*r1 = res1;
	return res0;
}
