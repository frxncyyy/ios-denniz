# T01 — emulator test infrastructure

Everything here is host-side.  No code in this directory touches the phone, and the vendor
libraries are never copied.

| File | Purpose |
|---|---|
| `asmrun.c`, `asmrun.sh` | assemble one A32/T32 snippet with `llvm-mc`, extract `.text`, run at `0x10000`, stop at `udf #254`/`bx lr`, print `name=value` lines |
| `kernels/*.c` | 137 self-contained `uint32_t k_name(uint32_t seed)` functions used as the differential corpus (including copies of V10's, V11's, V12's, V14's, V15's, V16's and V18's class kernels, with provenance comments; V16's optional atomic kernel is omitted because it compiles to ARMv8 LDAEX/STLXR, which the ARMv7 vendor set never uses; V15's k_v15_extend is omitted pending the reported vectorised-CLZ mismatch) |
| `support_arm.c` | mem*/division/shift helpers compiled for ARM and loaded as a separate ET_DYN object |
| `diffrun.c` | V03 front end: loads the objects, finds every kernel, calls it for every seed |
| `diffcheck.py` | compares the host reference with every configuration, prints the per-configuration table and routes failures |
| `run_diff.sh` | builds and runs the seven ARM configurations |
| `run_fuzz.sh`, `fuzz.c` | 2 million random A32/T32 trials, ASan+UBSan and `-O2` |
| `t_trace.c`, `test_trace_elf.sh`, `run_tests.sh` | trace ring/file tests, real-V03 symboliser test and the top-level runner |
| `n2a32_trace.h` | configuration API for `emu/n2a32_trace.c` |

## Quick checks with asmrun

    bash gpu/hwcodec/tests/emu/asmrun.sh snippet.s
    bash gpu/hwcodec/tests/emu/asmrun.sh --thumb --reg r0=7 --print r0,r1 --mem 0x30000:16 snippet.s

The snippet is the last argument and must put all code and constants in `.text` (literal pools
and jump tables emitted by the assembler are fine).  The runner maps `0x10000..0x40000`:
code at `0x10000`, stack below `0x30000`, scratch from `0x30000`.  Output is one `name=value`
line per register, flags (`n z c v q ge`), `stop`, `stop_reason`, `icount`, `thumb` and the
requested memory.  `stop=ok` means `udf #254` (A32 `0xe7f00ffe`, T32 `0xdefe`) or `bx lr` with
`lr = N2A32_RETURN_ADDR`.

## Differential kernels

The reference is the same C compiled natively with clang `-O2 -ffp-contract=off`; it is also
built at `-O0` and the two must agree before ARM is compared (otherwise the kernel itself is
undefined).  ARM configurations:

| name | flags |
|---|---|
| `arm-O0` | `-marm -O0` |
| `arm-O2` | `-marm -O2` |
| `arm-O3v` | `-marm -O3 -ftree-vectorize` |
| `thumb-O0` | `-mthumb -O0` |
| `thumb-O2` | `-mthumb -O2` |
| `thumb-O3v` | `-mthumb -O3 -ftree-vectorize` |
| `armv8-O3v` | `-marm -march=armv8-a -mfpu=neon-fp-armv8 -O3 -ftree-vectorize` |

Every object is built with
`--target=armv7a-linux-gnueabi -mfpu=neon-vfpv4 -mfloat-abi=softfp -ffreestanding -fno-builtin
-nostdlib -fPIC -shared -fuse-ld=lld -fno-stack-protector -fno-unwind-tables
-fno-asynchronous-unwind-tables -ffp-contract=off -Wl,--hash-style=gnu`.

Kernel rules (also stated at the top of `kcommon.h`):

* only exact-width types; `char` is never used as a number, all buffers are `uint8_t`/`int8_t`;
* unsigned types for anything that may overflow; signed overflow, shift counts outside
  `0..width-1`, division by zero and `INT_MIN/-1` never occur;
* float -> integer conversions are range checked first; no NaN is produced by arithmetic (a
  NaN is constructed from its bit pattern when needed);
* `-ffp-contract=off` on both targets, so no FMA contraction difference;
* arithmetic right shift of a negative value is used deliberately (it exercises `ASR`); it is
  implementation-defined in C but identical on ARM and x86-64 two's-complement targets;
* the only other implementation-defined operation is the conversion of an out-of-range
  unsigned bit pattern to a signed type, again identical on both targets (two's complement).

`__aeabi_uldivmod` and `__aeabi_ldivmod` return a 128-bit value in `r0-r3`; clang cannot
express that C return convention on ARM32 (GCC's `value_in_regs` is unsupported, and a struct
return uses an sret pointer).  Those two are therefore real **host shims** registered through
`n2elf_set_add_shims()` in `diffrun.c`; the guest still executes the PLT call and the host-call
page transition.  All other compiler runtime helpers (`__aeabi_uidiv/idiv/uidivmod/idivmod`,
`__aeabi_llsl/llsr/lasr`, `__aeabi_lmul`, `mem*`, `strlen`) are plain C in `support_arm.c`,
compiled for ARM and loaded through V03 as its own object.

## Trace

`emu/n2a32_trace.c` implements the three frozen hooks.  `n2a32_trace_attach()` sets
`c->trace`, opens `N2HW_TRACE` if set, and the front end calls `n2a32_trace_dump()` when
`n2a32_run()` returns.  The ring keeps the last 4096 lines; the optional file gets every line
as it happens.  Addresses are symbolised through `n2elf_describe()` when `n2a32_trace_set_elf()`
has installed a loader set; the reference is weak, so linking without V03 still works.
`n2a32_trace_regs(1)` / `n2a32_trace_dregs(1)` add pre-instruction integer / d16-d23 lines;
`diffrun --trace-regs|--trace-dregs` exposes them for a single kernel/seed.

## Fuzz

`fuzz.c` maps one scratch megabyte at `0x20000000`, fills it with deterministic random bytes,
and runs 1,000,000 A32 and 1,000,000 T32 trials with random registers and `ilimit = 24`.
Outcomes are classified as `undef` (class rejected the encoding), `fault` (unmapped guest
page), or `ok` (the core stopped deliberately: RETURN/LIMIT/SVC/BKPT).  Any other stop,
`icount > ilimit`, a host crash or an ASan/UBSan report fails the test.  `run_fuzz.sh` refuses
to run unless every V1x class source is present, so an all-UNDEF run cannot pass as success.
