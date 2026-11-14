/* SPDX-License-Identifier: MIT */
/*
 * n2a32_stub_classes.c -- weak fallbacks for every instruction-class entry point and every trace hook.  V02.
 *
 * The core always links, even before the class packages exist: each function here returns N2A32_UNDEF, which makes
 * the core stop with N2A32_STOP_UNDEF naming the encoding.  A real class package defines the same symbol strongly
 * and the linker prefers it.  The trace hooks are no-ops until T01 provides them; the core calls them only when
 * c->trace != NULL (and n2a32_trace_mmio unconditionally from the memory hooks).
 */
#include "n2a32.h"

#define WEAK __attribute__((weak))

WEAK int n2a32_arm_dp(struct n2a32_cpu *c, uint32_t insn) { (void)c; (void)insn; return N2A32_UNDEF; }
WEAK int n2a32_arm_media(struct n2a32_cpu *c, uint32_t insn) { (void)c; (void)insn; return N2A32_UNDEF; }
WEAK int n2a32_arm_mem(struct n2a32_cpu *c, uint32_t insn) { (void)c; (void)insn; return N2A32_UNDEF; }
WEAK int n2a32_arm_misc(struct n2a32_cpu *c, uint32_t insn) { (void)c; (void)insn; return N2A32_UNDEF; }
WEAK int n2a32_t16(struct n2a32_cpu *c, uint32_t insn) { (void)c; (void)insn; return N2A32_UNDEF; }
WEAK int n2a32_t32_dp(struct n2a32_cpu *c, uint32_t insn) { (void)c; (void)insn; return N2A32_UNDEF; }
WEAK int n2a32_t32_mem(struct n2a32_cpu *c, uint32_t insn) { (void)c; (void)insn; return N2A32_UNDEF; }
WEAK int n2a32_t32_misc(struct n2a32_cpu *c, uint32_t insn) { (void)c; (void)insn; return N2A32_UNDEF; }
WEAK int n2a32_vfp(struct n2a32_cpu *c, uint32_t insn) { (void)c; (void)insn; return N2A32_UNDEF; }
WEAK int n2a32_neon_3same(struct n2a32_cpu *c, uint32_t insn) { (void)c; (void)insn; return N2A32_UNDEF; }
WEAK int n2a32_neon_shift(struct n2a32_cpu *c, uint32_t insn) { (void)c; (void)insn; return N2A32_UNDEF; }
WEAK int n2a32_neon_misc(struct n2a32_cpu *c, uint32_t insn) { (void)c; (void)insn; return N2A32_UNDEF; }
WEAK int n2a32_neon_ldst(struct n2a32_cpu *c, uint32_t insn) { (void)c; (void)insn; return N2A32_UNDEF; }

WEAK void n2a32_trace_insn(struct n2a32_cpu *c, uint32_t addr, uint32_t insn, int thumb, int size)
{ (void)c; (void)addr; (void)insn; (void)thumb; (void)size; }
WEAK void n2a32_trace_hostcall(struct n2a32_cpu *c, const char *name, int leaving)
{ (void)c; (void)name; (void)leaving; }
WEAK void n2a32_trace_mmio(struct n2a32_mem *m, uint32_t addr, unsigned bytes, uint32_t value, int write)
{ (void)m; (void)addr; (void)bytes; (void)value; (void)write; }
