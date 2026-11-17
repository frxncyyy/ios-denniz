# Production SRT and integration evidence review

Independent read-only checkpoint, 25 October 2026. No build or production edit.

## SRT count disposition

Current dispatch_v2 invokes the real aligned-base compute builder, validates the
four-entry SRT using n2mb_compute_srt_pointer, then writes the encoded address|4
at job+96. The real rawJM selector forwards the request to n2c_submit; that path
checks handles, write flags and heads but does not re-run the builder alignment
check on the encoded field. Source and shim JM files are identical. This active
path no longer needs a shared JM API change solely for the resource count.
The wrapper is production code and the host harness links the actual builder;
it is not a fake-kernel fix. Hardware behavior remains untested.

## Reproducible evidence

The existing report source hashes matched the files at the initial review
snapshot (bridge SHA52f00fec364068d8b30668694b1ce6b0698789027cd60653ddae947dd1a9dee4).
All four stored C
executable hashes matched the report. Audit independently executed the existing
release binary (no rebuild), SHA fbc2e24dda01c34da05fc9ce47256dd4eb7cd23bb5aea4c22dde303929fcc55f:
111 checks, three required failures:
- combined sampler_index99 accepts despite declared Metal namespace0..15;
- declared span1023 authorizes a1024-byte layout inside a largerBO;
- explicit wire swizzle0x210 becomes default0x688.

Rust library SHA4bad3821521b1c7744f7e03fd543fa755364bce17d4da6df1cc07d314d6e2ae7
matches the report. Worker Rust source matches productionf83ed62f... exactly.
The source-hash recorded by producer_plan_test is a hash of the current file,
not a binary-embedded source attestation; retain build provenance when replacing
that binary. Later edits invalidate applicability of these checkpoint hashes.

## Independent oracle limitation / new finding

The full job expected envelope is generated through the same production v1
emitters. It guards v2 regression and missing fields, not independent correctness
of those shared emitters. The arch9 texture-plane oracle compares only28bytes:
common packer emits arch10 width/height in word7, while v9.xml declares that word
reserved and v9 generated unpack flags nonzero as invalid. New sharedarch9/v2
planes should emit zero there and compare all32bytes. Preserve old qualified
legacy bytes separately pending root decision. Textures/tests/root were notified.

## Later source hashes (not the tested snapshot)

- gpu/metal/nord2_metal_bridge.c: 50e4468dfcd00a6df70ca9649cee4d09ccb991b1898f3a63dbb01aeff6bc7b6d
- gpu/metal/tegu/pipeline_format.h: dfc0a4104d746b1d911d31097a06f4818edeb86b2801488a4aa55a2ddbabfc97
- gpu/metal/tegu/air_bridge.rs: f83ed62f1e0b3b340dc203887463a0cface223e7cfbf4ec79e1ac5d3d8274624
- gpu/metal/tegu/air_bridge.h: f43736dfb96aaf3f16e19d7d3c4a6e60454c1b4d449b76503ab7158ee33e8993
- gpu/src/nord2_gpu_jm.c: 8035a4381ba569cdc77606c406922208b51520565321b50665ad29cd0c2015e9
- gpu/src/nord2_gpu_desc.c: c852ac02355594ac3b81efbe0c59747cebb7a574218ef3d0928cab7c0eca53ab
- gpu/metal/nord2_texture_resources.h: c61b276ad5a885eed1a13e040db82c5fbf8ce56cc8ef389b07ff33ced6286cb8

## Superseding live-source disposition

Bridge50e4468d fixes the three prior repros in source: checks the combined Metal
sampler slot, constrains layout validation to the declared span, and propagates
nonzero swizzles. The old binary's three failures are not current-source test
results. A fresh matching integration run remains necessary. Explicit zero-valued
RRRR swizzle still lacks a validity bit; current Rust plans admit only zero/default
identity, so do not claim general swizzles.

The textures owner then changed the shared helper to zero arch9 plane word7 in
both packers, leaving legacy qualified gd_texture bytes unchanged. Independent
source inspection confirms the fix at nord2_texture_resources.h:979 and1121.
Owner reports full32-byte Generic/ASTC v9 parity and777 release/ASan checks.
The integration worker's own oracle still needs the same complete comparison.

Fresh cross-owner gates are board1792946052063616048: original-local comparison
still rejects exact-thread edge regions in runtime_pipeline.inc:323; ordinary
exact sampler creation is enabled only after the first compute dispatch;
sampled numeric/depth requirements are not checked; storage numeric checks and
combined sampler restrictions use allocation format instead of the effective
view. Headerdfc0a410 remains structurally ACKed for the compiler implementation.
