/* SPDX-License-Identifier: MIT */
/*
 * n2fmt.h -- guest printf/scanf formatting for the shim tables (gpu/hwcodec/shim, package V30).
 *
 * The formatter is split from its call sites on purpose: the stdio/log shims (V33, V34) know where the output
 * goes, this file knows how a guest format string and a guest AAPCS argument list turn into bytes.
 *
 * ---- argument lists ---------------------------------------------------------------------------------------------
 * A guest variadic call arrives in one of two shapes, and both are modelled by struct n2fmt_args:
 *
 *   N2FMT_ARGS_REGS   the shim was entered directly: argument slot i is r[i] for i < 4 and the word at
 *                     [sp + 4 * (i - 4)] above that (n2shim_arg() in n2elf.h).  n2fmt_args_regs(&a, c, first)
 *                     starts at slot `first`, the index of the first variadic argument (snprintf: 3,
 *                     sprintf: 2, printf: 1, __android_log_print: 3, fprintf: 2, ...).
 *
 *   N2FMT_ARGS_AREA   the shim received a guest va_list: on 32-bit ARM bionic a va_list is a pointer to the
 *                     saved argument block, so the guest object at `guest_va` holds one 32-bit word, the address
 *                     of the first argument word.  n2fmt_args_va(&a, c, guest_va) reads it.
 *
 * 64-bit integers and doubles occupy two consecutive 32-bit slots, aligned to an even slot (AAPCS: skip r1,
 * skip r3 to the stack; in an argument area the address is rounded up to 8).  Long is 32 bits in the guest;
 * ll/j are 64, z/t are 32.
 *
 * ---- output ---------------------------------------------------------------------------------------------------
 * n2fmt() writes into a HOST buffer (`out`, `cap` includes the terminating NUL; out may be NULL when cap is 0,
 * which only measures).  The return value is the number of characters the conversion produced, like snprintf:
 * it can exceed cap.  Truncation never writes past out[cap - 1] and always NUL-terminates when cap > 0.
 * A NULL guest format (guest_fmt == 0) returns -1.  Nothing is printed by this file.
 *
 * Supported: %d %i %u %o %x %X %c %s %p %n %% with flags - + space 0 #, width and precision (including *),
 * lengths hh h l ll z j t, and %f %F %e %E %g %G for doubles (converted from the binary value with exact
 * decimal arithmetic and round-half-to-even, so the result matches a C library on IEEE-754 hosts; there is no
 * use of the host's printf and no host floating point formatting assumption).  Precision above 400 is clamped,
 * width/precision literals above 999999999 are clamped, and formats above 2047 bytes are truncated; every limit
 * is far outside anything the vendor code does.
 *
 * ---- scanf ----------------------------------------------------------------------------------------------------
 * n2fmt_vscan() is the scanner behind sscanf() and the fscanf() forward of n2shim_stdio[].  It reads from a
 * caller-supplied byte source, which lets V33 scan a guest FILE* without V30 knowing what a FILE is.  It
 * supports %d %i %u %o %x %X %c %s %p %n with flags * and widths, and literal text / whitespace matching.
 *
 * ---- heap / mmap hooks (V33) ----------------------------------------------------------------------------------
 * The heap shims forward a descriptor-backed mmap to V33 through n2vfs_mmap().  The symbol is weak here, so
 * linking V33's strong definition replaces it without any link-time dependency; without it, such an mmap fails
 * with MAP_FAILED and EINVAL.  These prototypes are repeated here only so both sides agree on the signature.
 */
#ifndef N2FMT_H
#define N2FMT_H

#include <stddef.h>
#include <stdint.h>

#include "n2a32.h"

#ifdef __cplusplus
extern "C" {
#endif

enum n2fmt_args_mode { N2FMT_ARGS_REGS = 0, N2FMT_ARGS_AREA = 1 };

struct n2fmt_args {
	struct n2a32_cpu *c;   /* guest cpu (same as the one passed to n2fmt) */
	uint32_t area;         /* AREA mode: guest address of the first argument word */
	uint32_t next;         /* next 32-bit argument slot */
	int mode;              /* enum n2fmt_args_mode */
	int fault;             /* set when an argument pointer was unreadable/NULL (result is still defined) */
};

void n2fmt_args_regs(struct n2fmt_args *a, struct n2a32_cpu *c, unsigned first_slot);
/* Returns 0 on success, -1 when guest_va is 0 (a NULL va_list). */
int n2fmt_args_va(struct n2fmt_args *a, struct n2a32_cpu *c, uint32_t guest_va);

/* Raw readers, in case a caller needs an argument outside a conversion (rare). */
uint32_t n2fmt_arg32(struct n2fmt_args *a);
uint64_t n2fmt_arg64(struct n2fmt_args *a);       /* aligns to an even slot first */
double n2fmt_arg_double(struct n2fmt_args *a);

/* The formatter.  guest_fmt is a GUEST address.  Returns the untruncated length, or -1 on a NULL format. */
int n2fmt(struct n2a32_cpu *c, char *out, size_t cap, uint32_t guest_fmt, struct n2fmt_args *a);

/* The scanner.  get() returns the next byte (0..255) or -1 at end of input; unget() may be NULL, in which case
 * a byte pushed back is dropped (only affects streams, never sscanf).  Returns the number of successful
 * assignments, or -1 (EOF) if the input ended before the first conversion could be attempted. */
struct n2fmt_input {
	int (*get)(void *ctx);
	void (*unget)(void *ctx, int byte);
	void *ctx;
};
int n2fmt_vscan(struct n2a32_cpu *c, const struct n2fmt_input *in, uint32_t guest_fmt, struct n2fmt_args *a);

/* ---- weak hooks implemented by V33 (see include/n2elf.h and V33's n2vfs.c) ---------------------------------- */
/* Descriptor-backed mmap: returns the guest address, or 0 on failure.  Only called for fd != -1. */
uint32_t n2vfs_mmap(struct n2a32_cpu *c, uint32_t addr, uint32_t len, int prot, int flags, int fd, uint64_t off);
/* Guest address of the calling thread's errno int, or 0 when V33 is not linked.  Used only to report errors. */
uint32_t n2vfs_errno_location(struct n2a32_cpu *c);

#ifdef __cplusplus
}
#endif
#endif /* N2FMT_H */
