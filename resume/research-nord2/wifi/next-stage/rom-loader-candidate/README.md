# Private current-boot CONN owner handoff

The candidate is NOT integrated and does not affect the current WFSYS image.
`identity.json` pins original active loader and ADT generator. The intended
new option is exactly `nord2wifirom=1`; absent option records no claim and
performs no additional FDT scan. Malformed/duplicate option fails closed.

After the existing live-FDT/cache and payload checks, the new path bounds
FDT size to 1 MiB, rejects overlap between the original FDT blob and CONN,
and calls the hardened fixed-selection scanner. XNU allocator remains the
same high-RAM window. The original FDT is untouched. The only new output is
`/chosen/nord2-conn-claim`, 24 little-endian bytes:
base:u64, size:u64, source:u32, valid:u32. Current supported source is 3
(explicit iOS fixed policy); valid=1 only after successful current-FDT scan.
The loader writes no bytes into the CONN carveout and performs no SMC.
This is an ownership policy record, not proof of hidden-master exclusion or
MPU permission. Native mapping and source SET remain separately reviewed.

Independent source review: `independent-review.json`. The actual newly
modified opt-in function and ADT patch function pass host ASan/UBSan tests
(`python3 test-loader.py`), including absent/malformed/duplicate opt-in,
missing/duplicate/wrong-length claim property, wire fields and zero default.
The candidate ADT emits one zero-filled 24-byte property in first-child chosen.
The initial test fixture had incorrect hardcoded string capacities; these
were corrected to `sizeof` before it passed. Loader code did not change.

Focused ARM64 compile and ELF link against hardened `rom_fdt.c` passed.
The private binary is 20,228 bytes, emitted using LLVM objcopy from the linked
ELF (this older bundled lld does not support `--oformat=binary`). It has not
been packed into an image or run on the phone. Promotion must update the
normal loader build script to compile/link the new FDT helper and regenerate
loader-Image, rather than silently using the existing accepted loader binary.
