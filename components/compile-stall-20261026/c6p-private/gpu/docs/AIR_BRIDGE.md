# AIR -> SPIR-V -> Mali arch9 host oracle

This is a **host code-generation test**, not GPU execution.  It proves that a
real Apple AIR render pipeline can be translated with the pinned public
`metal2vulkan` translator and then lowered by our pinned Mesa arch9 backend.
It does not prove that Explorers/QuartzCore selectors, Metal descriptors,
IOSurface access, or the Mali command stream work on the phone.

## Pinned inputs

* metal2vulkan: `720ef1ce9ae2a2068999a8675717418b9416283e`
  (`refs/metal2vulkan-tegu-pin`), public validation fixtures.
* Mesa: `81daea66703eeea1a232446e3a36902353fc3045`.
* GPU identity for the compiler: `0x90000000`, architecture 9 (G77), not the
  donor's arch11 `0xb8a21030`.
* Test: `gpu/tests/air_render_test.py`.

## Translator build

The CLI is behind the translator crate's `cli` feature.  With a source copy
outside the repository's tracked tree:

```sh
cd <metal2vulkan-source>
CARGO_TARGET_DIR=<build-dir>/cargo-target \
  cargo build --release -j1 --features cli --bin metal2vulkan
```

On this host that took 7m43s.  The smaller `metal2vulkan-air` frontend builds
separately but is not the translator.

The public `.ll` fixtures use IR syntax newer than LLVM 15.  The test makes
runtime copies under `gpu/build/air-test`, removes only the newer `captures`/
`memory(none)` spelling and expands `splat`, then assembles with LLVM 15's
`llvm-as`/`llvm-link`.  No fixture is committed.

## Run

```sh
python3 gpu/tests/air_render_test.py \
  --translator gpu/build/cargo-target/release/metal2vulkan \
  --llvm-bin /path/to/llvm-15/bin
```

The script links `vertex_amplification_id_layer` with
`fragment_constant_color`, translates with the public
`nvidia-rtx5080-laptop.json` profile (the profile only supplies a Vulkan
feature ceiling), and invokes `nord2_mali_compile`.

Observed 2026-10-24:

```
translator: PASS; spirv-val vulkan1.2, vulkan1.4: PASS
vertex:   1572 B SPIR-V -> 1920 B Mali, idvs=true, fau_words=9
fragment: 5320 B SPIR-V -> 384 B Mali, outputs_written=0x10, fau_words=17
vertex binary sha256   3e2ebfac057b9aa4282309e9d8524ba6171e02f8eef9b9adb794f07a2ae016cc
fragment binary sha256 1c6c413495a4644c26b289a2dbb90cf0bcad38df4b2eccfc57d8e44fd8505914
```

The AIR fixture pair is deliberately **resource-free**.  Fixtures that use
`[[stage_in]]`, texture arrays, or buffer resource indexing currently hit
translator or Mesa-refusal/assertion paths; those are compiler coverage gaps,
not hardware results.  The kernel transport's compute path still uses the
separately compiled token shader.

## Missing for a native Metal frontend

There is still no iPhoneOS SDK on this host, so the ObjC shim has not been
compiled or linked into a bundle.  The kernel/userland render request,
IOSurface mapping/fencing, QuartzCore selectors and panel presentation remain
the next implementation layer.  Do not treat this test as a device or UI
success.
