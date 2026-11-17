#include <mach/mach_types.h>
#include <mach/vm_types.h>
#include <kern/thread.h>
#include <kern/clock.h>
#include <pexpert/pexpert.h>
static vm_offset_t ni_gpio,ni_lt;
static struct { vm_offset_t infra; } nw;
#define NAH_NATIVE_DEFINE_PREPARE 1
#include "audio_hw_native.inc"
/* Build-only exported functions keep bridge symbols visible for review. */
int nah_native_compile_probe(struct nau_hw *hw,struct nah_native_context *ctx)
{ return nah_native_valid(ctx) ? nau_hw_init(hw,&nah_native_io,ctx,0):-1; }
