# V03 — ELF32/ARM loader (status)

Updated 11 November 2026, ~12:50. **Review: R1 verdict accept** (finding F6 fixed and re-verified by R1 at 12:47). Host-tested only: **nothing here has run on the phone**. It does
compile clean for `arm64-apple-ios16.5` (compile only, both files, `-Werror`, with the SDK V30 uses).

## What is implemented

`gpu/hwcodec/loader/n2elf.c` and `gpu/hwcodec/loader/n2elf_reloc.c` implement the loader half of the frozen
`include/n2elf.h` exactly (no header defects found, no header copied, no extra public API beyond three private
`n2elf__span`/`n2elf__map_lo`/`n2elf__soname` accessors used only by the host test).

- `n2elf_load`: ELF32 little-endian, ET_DYN, EM_ARM only. PT_LOAD segments copied into one page-aligned guest range
  obtained from `n2a32_mem_alloc(span, 4096)`; the load bias is page aligned; bss is zeroed explicitly even when two
  segments share a page; the whole file is bounds-checked before any guest memory is written. PT_DYNAMIC is parsed
  for NEEDED, SONAME, SYMTAB/STRTAB/STRSZ/SYMENT, GNU_HASH, DT_HASH (symbol-count source only), REL/RELSZ/RELENT,
  JMPREL/PLTRELSZ/PLTREL, ANDROID_REL/ANDROID_RELSZ, RELR/RELRSZ/RELRENT and the Android 0x6fffe000 family,
  INIT, FINI, INIT_ARRAY/INIT_ARRAYSZ, FINI_ARRAY/FINI_ARRAYSZ, PREINIT_ARRAY/PREINIT_ARRAYSZ. Every table is
  validated once at load (inside a PT_LOAD, right element size). DT_RELA is rejected (AArch32 uses REL); DT_REL +
  DT_ANDROID_REL together, RELR without size, incomplete SYMTAB/STRTAB and APS2 errors are all named errors.
- `n2elf_reloc.c`: plain REL, the Android packed "APS2" format (SLEB128; all group-flag combinations, per-item
  order offset-delta then r_info as bionic does it; explicit addends parsed but ignored on Elf32_Rel like bionic),
  and RELR bitmaps (both the Android tags and DT_RELR 36/35/37; entry size 4). Relocation types implemented:
  R_ARM_RELATIVE(23), R_ARM_GLOB_DAT(21), R_ARM_JUMP_SLOT(22), R_ARM_ABS32(2). Any other type is an error naming
  the type, e.g. `unsupported relocation R_ARM_PREL31 (42) at 0x...`. An optional `n2elf_reloc_trace` hook (not in
  the frozen header) lets the host test see every applied (address, type, value).
- `n2elf_link`: symbol lookup by GNU hash over the loaded objects in load order (global/weak/GNU-unique,
  default/protected visibility; linear fallback when an object only has DT_HASH), then the function shim tables
  (`n2a32_hostcall_register(fn, name)`), then the data shim tables (one guest allocation per name, initialised from
  the table, shared by all objects). Undefined functions become named unresolved host-call slots and increment
  `*unresolved` once per unique name; undefined data imports are an error. Relocations are applied before page
  protections (text RO, data RW, verified). Calling `n2elf_link` twice is a no-op for already-linked objects.
- `n2elf_sym` (definitions → function shims → data shims, Thumb bit preserved from `st_value`), `n2elf_entry`,
  `n2elf_base`, `n2elf_describe` (`soname:symbol+0x...`, `soname+0x...`, `hostcall:name`, or hex), and
  `n2elf_run_init` (objects in dependency order through DT_NEEDED; PREINIT_ARRAY, DT_INIT, INIT_ARRAY, each object
  once; returns -1 if a guest call stops for anything but RETURN).

## Tests and last lines

Command: `bash gpu/hwcodec/loader/test_v03/run_tests.sh` (builds only under `/tmp/n2hw-V03`, two host builds:
ASan+UBSan -O1 and plain -O2; small ARM objects built with clang `--target=armv7a-linux-androideabi ... -shared
-nostdlib -fuse-ld=lld`). Output of the last run:

```
V03 iOS compile: 2/2 (arm64-apple-ios16.5, compile only)
== V03 oracle under ASan+UBSan
V03 vendor per object (applied/readelf): vpud 91/91, libvpud_vcodec.so 191/191, libvcodecdrv.so 2162/2162, libvcodec_utility.so 498/498, libvcodec_oal.so 178/178
V03 oracle: 12875 checks
V03 vendor: objects 5, relocations 3120, unresolved 146, unique imports 149
V03 synthetic: relocations 35
== V03 oracle under -O2 without sanitizers
V03 vendor per object (applied/readelf): vpud 91/91, libvpud_vcodec.so 191/191, libvcodecdrv.so 2162/2162, libvcodec_utility.so 498/498, libvcodec_oal.so 178/178
V03 oracle: 12875 checks
V03 vendor: objects 5, relocations 3120, unresolved 146, unique imports 149
V03 synthetic: relocations 35
== V03 against V02's real interpreter (load/link/unresolved stop/init/GOT/PLT)
V03 real-emu: 33 checks
V03 real-emu: 33 checks
== V03 relocation-encoding unit tests
V03 reloc unit: 61 checks
V03 reloc unit: 61 checks
== V03 vendor acceptance line
V03 vendor: objects 5, relocations 3120, unresolved 146, unique imports 149
N2TEST V03 PASS 12971/12971
```

What the 12875 oracle checks compare, all with llvm-readelf (`-r -W`, `--dyn-syms`, `-lW`, `-SW`, `-dW`) and the
object bytes as the source of implicit addends — never with the loader's own numbers:

- synthetic set: liba (plain REL, ARM), libb (Thumb, Android packed, imports liba + shims), libc (Android packed +
  RELR, depends on libb), libd (plain REL + RELR), libarm (ARM, built against liba, calls through GOT/PLT + a host
  shim). Manifest order is deliberately reversed (libd, libc, libb, libarm, liba) so lookup order and dependency
  order are not the same. Every applied relocation is compared address by address and value by value (35); every
  exported symbol (including the duplicate `dup_fn`, the weak `weak_fn`, Thumb entries and the init markers), the
  unknown-symbol 0 return, the unresolved weak `weak_missing_fn`, the shim and data-shim addresses, init order (libd,
  liba preinit, liba init array, libb init, libc init; libarm has none), .data contents, .bss zeros, page
  permissions, `n2elf_describe` on a function and on an unresolved slot, second-link no-op.
- vendor set: bin/vpud + the four libraries. 3120 relocations applied, per object exactly what llvm-readelf lists
  (vpud 91, libvpud_vcodec 191, libvcodecdrv 2162, utility 498, oal 178) and every value computed from readelf;
  2593 exported symbols looked up through the GNU hash compare equal; 149 unique imports; 146 unresolved slots
  (each a distinct named host-call slot with a NULL implementation), 2 data imports; segment contents, text RO /
  data RW, second-link no-op, init (vpud's arrays contain only -1/0, so no calls).
- negatives: ET_EXEC rejected; a relocation type patched to 42 fails with `R_ARM_PREL31 (42)`; APS2 magic patched
  to APS3 fails; `missing_data` (STT_OBJECT, provider .so not loaded) fails with `unresolved data import`; a
  40-byte truncated file fails; a missing file fails; DT_FINI_ARRAY patched out of bounds fails; RELRENT patched to
  8 fails.
- a third vendor run registers a function shim for every one of the 146 referenced imports: unresolved becomes 0,
  all 3120 GOT/PLT values move to the printed shim slot addresses, and the shim count matches.
- soname == NULL uses DT_SONAME (tested with a renamed libd.so), describe outside every image returns hex, and the
  test suite is 12971 checks total.
- 61 relocation-encoding unit checks use hand-built tables: plain REL, APS2 with no flags / grouped info / grouped
  offset delta / both / per-item addend / grouped addend, malformed magic/truncation/overrun/unknown flags/SLEB128
  overflow, RELR address+bitmap words, entry size and bounds errors. Expected values are literals from the format.

Unresolved count, honestly: the five objects have 529 undefined `.dynsym` entries, 2593 defined names, leaving
**149 unique imported symbols**. Two are data (`__stack_chk_guard`, `stderr`) and are provided by the test's data
shim table. `__aeabi_unwind_cpp_pr0` is an undefined FUNC with **no dynamic relocation** (only the .ARM.exidx
personality encoding), so the loader never binds it. That leaves **146 unresolved function slots**; acceptance said
"unresolved <= 149", and the test prints both numbers.

`llvm-readelf` 23.1.4 does not accept `--raw-relr` (the task text names it); `-r` already expands `.relr.dyn` to one
address per line and agrees with a decode of the section bytes.

## Not done / limitations (all deliberate, none silently assumed)

- RELA, symbol versions (ignored per the header), TLS, GNU IFUNC, R_ARM_COPY, GNU_RELRO protection, object
  unloading: not implemented; RELA/TLS/IFUNC produce named errors, versions are simply ignored. The vendor set has
  none of these.
- FINI_ARRAY is parsed and validated but there is no fini entry point in `include/n2elf.h`, so it is not run.
- DT_NEEDED whose soname is not loaded is ignored (no error), which is what lets hw-iv link a partial set. Duplicate
  sonames are allowed (two loads = two images); lookup always returns the first definition in load order.
- Link once, after all loads and all shim tables: a later `n2elf_link` only relocates images loaded since the previous
  call and never rebinds an import already written as an unresolved slot. Shim tables added after link do not affect
  existing GOT/PLT entries. If a link attempt fails after some relocations were written, the image is marked and a
  retry is refused with a named error (`a previous link attempt failed after partially applying relocations`) instead
  of re-applying R_ARM_RELATIVE to already-bias-adjusted words. R1 finding F6, regression-tested.
- Guest memory allocated by a failed `n2elf_load` is not reclaimed (the memory interface has no free).
- `n2elf_sym` uses the GNU hash, so it finds exported (global/weak) definitions, not local symbols; with no GNU hash
  it falls back to a linear scan (objects with neither hash use the DT_STRTAB-DT_SYMTAB gap as an upper bound; no
  vendor object is like that).
- At most 16 shim tables and 16 data tables per set (`n2elf_set_add_shims/add_data` return void, so a 17th table
  would be dropped silently; the architecture uses five). At most 32 images, 128 data imports, 512 unresolved
  names, 128-byte symbol names.
- `n2elf_run_init` needs `c->r[13]` set up exactly like any `n2a32_call` (the test sets sp).
- Unresolved-import stop reason: V02 accepted `n2a32_hostcall_register(NULL, name)` and posted DONE at 12:24; the
  loader therefore registers named NULL-implementation slots, the core stops with N2A32_STOP_NOIMPORT,
  `stop_addr` is the slot and `n2a32_hostcall_name(stop_addr)` is the import name. t_real verifies both names for
  the synthetic set. `n2elf.c` still has a defensive fallback (shared callback) for an implementation that rejects
  a NULL fn; it is not used with V02's current core.
- Threading: load/link/run_init are single-threaded by contract (do them before guest threads start). After link,
  `n2elf_sym`/`n2elf_describe` only read the images, except that `n2elf_sym` may allocate a *data* shim on first use
  (the two vendor data symbols are allocated during link because relocations reference them; a guest dlsym() of a
  data name that was never relocated would allocate lazily). Function-shim registration goes through
  n2a32_hostcall_register(), which V02 makes thread-safe.
- Not run on the phone, no vendor binary copied into the tree. Compiled for arm64-apple-ios16.5 (compile only, both
  loader files, `-Werror`, SDK at gpu/build/apple-toolchain/sdks/iPhoneOS16.5.sdk).

## Manual robustness check (not in run_tests.sh)

120 randomly mutated copies of liba.so (1-4 bit flips in the first 8 KiB, i.e. headers, program headers, dynamic
table, GNU hash, REL table) were each loaded/linked by t_v03 under ASan+UBSan with a 3 s timeout: 120/120 ended with
a named error or a successful load, no crash, no sanitizer report, no hang.

## External use (reported on the board, 12:01)

hw-t01's tests/emu/diffrun.c uses this loader with the real `n2a32_mem`: it loads `libsupport.so` then
`libkernels.so` (lld-built ARM shared objects), `n2elf_link` returns unresolved=0, `n2elf_sym` finds all 97 `k_*`
symbols, and `n2a32_call` drives them (291/291 expected lines, no MISSING/STOP in their first run). That is the first
independent consumer of the loader.

hw-v30's `shim/test_v30/run_tests_e2e.sh` compiles a real ARM shared object, loads it through this loader, links it
against `n2shim_string[]` + `n2shim_heap[]` (unresolved == 0) and runs compiler-generated varargs, qsort with a guest
comparator, malloc/free, strlen/strcmp/memcpy on V02's real core: `N2TEST V30-E2E PASS 9/9`, guest output identical
to host snprintf. Independent end-to-end evidence that the loader's relocations and shim binding work for real
compiled code.

## Integration with V30/V02 (manual, not part of run_tests.sh)

Linked the real loader with V02's core/mem and V30's `shim/n2shim_string.c`, `n2shim_format.c`, `n2shim_heap.c`;
registered `n2shim_string[]`, `n2shim_heap[]` and a two-entry data table (`__stack_chk_guard`, `stderr`); loaded the
five vendor objects and linked. Result: link succeeds, unresolved drops from 146 to **111** (V30 covers 35 imports).
This exercises real shim tables through `n2elf_link` rather than the test dummy.

## V02 real-core integration (t_real, mandatory in run_tests.sh)

`t_real` is built from the loader plus V02's `n2a32_core.c`, `n2a32_mem.c`, `n2a32_excl.c` and the real class files
(`n2a32_arm_dp`, `arm_mem`, `arm_misc`, `arm_media`, `t16`; the remaining classes come from V02's weak stubs). It
uses only the frozen API and checks: the synthetic set loads/links with unresolved=2; entering each unresolved slot
stops with N2A32_STOP_NOIMPORT, `stop_addr` is the slot and `n2a32_hostcall_name(stop_addr)` is the import name;
`n2elf_run_init` executes the full init chain (libd, liba preinit+init, libb/libc Thumb; all 5 markers become 1) and
ends with RETURN; `liba_fn(5)` returns 5+0x11223344; `libarm_fn(5)` returns 0x22446694 through the loader's GOT/PLT
and a host-call shim; the five vendor objects link with unresolved=146. `libb_fn` (Thumb, 32-bit T32 prologue) is
printed but not asserted because the class set linked here still stops on 32-bit T32 (V16 at the time of writing);
this is an interpreter coverage limit, not a loader one.

The main oracle suite still uses the test double for `n2a32_mem_*`/`n2a32_call` because it needs page-permission
and call-log introspection that `n2a32.h` does not expose; all loader semantics are additionally exercised against
the real V02 in t_real.

## Files

- `gpu/hwcodec/loader/n2elf.c` (new)
- `gpu/hwcodec/loader/n2elf_reloc.c` (new)
- `gpu/hwcodec/loader/test_v03/{run_tests.sh,oracle.py,t_v03.c,t_reloc.c,t_real.c,STATUS.md}`
- `gpu/hwcodec/loader/test_v03/stub/n2a32_stub.c` (test-only V02 stand-in: 4 GiB reservation, map/protect/alloc,
  host-call slots, call log, per-page permission tracing)
- `gpu/hwcodec/loader/test_v03/fixtures/{liba,libb,libc,libd,libarm,baddata,provider,exec}.c`

Committed as **c4f80d0** (`hwcodec/V03: ELF32/ARM loader ...`, 18 files: the two loader sources, test_v03/** and
this STATUS.md); `dsh-state/workers/V03/TASK.md` and `runtime.json` are left untracked like the other workers'.
No phone, no vendor-binary copy, no publish.
