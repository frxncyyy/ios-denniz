# DEC08 abi_tools - how ABI.md is made

`ABI.md` (one directory up) is the decoder half of the VideoToolbox plug-in ABI, read out of the phone's own dyld
shared cache. This directory contains the scripts that read it and the test that checks it. Nothing here is built or
run on the phone, and nothing writes into the work tree except `n2decabi.py --write` (which rewrites `ABI.md`).

## Run the test

    bash gpu/hwcodec/vt/dec/abi_tools/run_tests.sh
    -> N2TEST DEC08 PASS 289/289

The test needs the user's private cache copy, read-only:

    export N2_DYLD_CACHE=$HOME/dsc-work/sys/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64

It is the raw cache directory used by `gpu/video/research`; the `.28`/`.42` LINKEDIT subcaches are not needed (they are
only where export tries live; the named symbols in ABI.md are labelled from the symbol dump and every record and
instruction is re-read from the uncompressed parts on every run). The raw `dyld_shared_cache_arm64.NN` and
`*.dylddata` files are read in place; no copy is made.

## Files

| file | what it is |
|---|---|
| `n2cache.py` | a self-contained reader for the cache: mapping tables of the main file and every non-empty subcache, `rd/q/w`, image list, segments, strings, optional symbols and export tries, and llvm-objdump disassembly. About 250 lines, no third-party code. |
| `n2decabi.py` | the fact table (addresses, vtables, slot values, instruction fingerprints), the loader-list scanner, 289 checks, the JSON dump, and the ABI.md generator. |
| `run_tests.sh` | `n2decabi.py --check` with the cache path. |

The frozen header derived from these facts is `gpu/hwcodec/include/n2vt_dec_abi.h` (hw-lead / opus-dec3) and the
companion narrative is `gpu/hwcodec/facts/VT_DECODER_ABI.md`. `ABI.md` points at both and adds the five complete class
records (including H264SW), the full loader list, and the per-factory call chain.

## What the checks cover

* **The loader**: `_VTLoadVideoDecoders` is decoded by simulating its ADRP/ADD register values to rebuild the 23
  (path, entry) pairs; the Slim entry is #5; `_VTLoadVideoDecoder` is `dlopen(path, RTLD_LOCAL)` + `dlsym` + `br x0`.
* **The records**: five real vtables (H264SW, VCH263, VCPMP4V, AV1SW, `_ParavirtualizedVideoDecoder`) are read as raw
  bytes: reserved slot, base/class pointers, base version and `derivedStorageSize`, class version, every named slot value.
* **The factories**: for each of H264SW, VCH263, VCPMP4V and AV1SW, the checker follows the veneer at the
  `VTVideoDecoderGetClassID` call and at the `CMDerivedObjectCreate` call and asserts both destinations; it also
  asserts that the vtable loaded just before the create call is the record dumped above.
* **The call sites**: 87 instruction fingerprints pin the slot offsets, the version gates (`>= 2` for
  `canAcceptFormatDescription`/`finishDelayedFrames`, `>= 3` for tiles), the factory retry loop, the filter's
  hardware rules, the rating sort, the `IsHardwareAccelerated=true` default in `VTRegisterVideoDecoderWithInfo`, and
  the session callback behaviour.
* **The document**: `ABI.md` is compared byte for byte with the regenerated text, and the proposed header section is
  compiled with `clang -std=c11 -Werror` against `gpu/video/include/n2vt_abi.h`.

## Regenerate

    python3 gpu/hwcodec/vt/dec/abi_tools/n2decabi.py --write
    python3 gpu/hwcodec/vt/dec/abi_tools/n2decabi.py --json      # the same facts, for other packages
    python3 gpu/hwcodec/vt/dec/abi_tools/n2decabi.py --disasm 0x194cf8e98 0x194cf8f20

`--write` does not need the symbol dump; the `syms` labels are part of the fact table and are marked as coming from
the symbol table, while all addresses, record bytes and instructions are read from the cache each time. A different
cache therefore fails `--check` rather than silently rewriting the facts.

## Caveats

* Names of local functions (for example `_VTLoadVideoDecoders`, `_VTVideoDecoderCanAcceptFormatDescription`,
  `_vtSortRegistryItemsByRating`) come from the cache's symbol file; the image export tries were read once with the
  same source and the addresses agree. The scripts do not require the 592 MB symbol file.
* AV1SW and H264SW are stripped images: their vtable and class records are still read, but the function names there are
  ours (the records and addresses are not).
* Tile decode argument names are `[I]`; the register layout is `[V]`. The Slim decoder does not implement tiles.
