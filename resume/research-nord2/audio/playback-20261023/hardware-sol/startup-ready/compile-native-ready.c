#include <mach/mach_types.h>
#include <mach/vm_types.h>
#include <kern/thread.h>
#include <kern/clock.h>
#include <pexpert/pexpert.h>
static vm_offset_t ni_gpio,ni_lt;
static struct {vm_offset_t infra;} nw;
#define NAH_NATIVE_DEFINE_PREPARE 1
#include "audio_hw_native.inc"
