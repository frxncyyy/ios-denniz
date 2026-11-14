/* SPDX-License-Identifier: MIT */
/*
 * n2a32_trace.h -- configuration API for emu/n2a32_trace.c (T01).
 *
 * The three hooks called by the interpreter core are frozen in include/n2a32.h and are not
 * repeated here.  A front end that wants a trace does:
 *
 *     n2a32_trace_attach(&cpu);          // cpu.trace = the ring; opens N2HW_TRACE if set
 *     cpu.ilimit = 100000;
 *     n2a32_run(&cpu);
 *     n2a32_trace_dump(&cpu, stdout);    // last 4096 events, then a stop line
 *
 * n2a32_trace_set_elf() installs the loader set used to symbolise addresses.  When the loader
 * is not linked, n2elf_describe() is a weak undefined reference and lines show "-" instead.
 */
#ifndef N2A32_TRACE_H
#define N2A32_TRACE_H

#include <stdint.h>
#include <stdio.h>

struct n2a32_cpu;
struct n2elf_set;

void n2a32_trace_attach(struct n2a32_cpu *c);
void n2a32_trace_detach(struct n2a32_cpu *c);
/* Install/clear the symbol table used for address -> "soname+sym+0x..." descriptions. */
void n2a32_trace_set_elf(struct n2elf_set *s);
/* Debug aids: also record r0-r6/r12/sp/lr/flags, or d16-d23, before each instruction. */
void n2a32_trace_regs(int on);
void n2a32_trace_dregs(int on);
/* Forget the ring contents and counters (the file, if open, stays open). */
void n2a32_trace_clear(void);
/* Write the last min(4096, events) lines plus counters and, when c != NULL, the stop reason.
 * `out` may be NULL (stderr).  Nothing is written unless this function is called. */
void n2a32_trace_dump(struct n2a32_cpu *c, FILE *out);
/* Total events recorded since the last clear (may exceed 4096). */
uint64_t n2a32_trace_events(void);
/* Open a full-trace file explicitly (same as setting N2HW_TRACE before the first attach). */
int n2a32_trace_open(const char *path);
void n2a32_trace_close(void);

#endif
