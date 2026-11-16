# air/: third-party notice

The AIR (Metal shader bitcode) -> Mesa NIR frontend in this directory comes from the `metal` branch of
the hackios userland repository (oosexgang/hackios-xnu-userland @ 29eef72, `tools/mesa/air`), MIT,
"Copyright (c) 2026 OOSEX GANG", files marked "Copyright 2026 Darwin on redwood contributors,
SPDX-License-Identifier: MIT". It reached us through Chlorophyll (github.com/ry-diffusion/Chlorophyll
@ 198d8c3, the Bergamota project), whose additions are marked "Bergamota" and are under the same
licence. The licence text is `LICENSE.hackios-userland`; the per-file notices are kept.

Taken: `air_bitcode.{c,h}`, `air_layout.c`, `air_metallib.c`, `air_module.{c,h}`, `air_print.c`,
`air_private.h`, `air_to_nir.c`, `air_intrinsics.c`, `nir_air.h`. Not taken: `air_metal.*` (Chlorophyll's
Gallium runtime), `air2nir.c`, `meson.build`.

rotten-apple changes (MIT, marked "rotten-apple:" in the source; docs/CHLOROPHYLL-EVAL.md 1.6):
- Mesa 27 API: `nir_builder.exact` -> `fp_math_ctrl`, `gl_shader_stage` -> `mesa_shader_stage`,
  `nir_block_create(impl)`, `nir_shader_create` without the last argument;
- pointer slots: an alloca or program-scope variable holding a pointer (iOS 16.7 QuartzCore's
  `metal::sampler` copies) is kept at translation time;
- a non-plain program-scope initializer with pointer members is kept as slots, not refused;
- a null texture sampled on a reachable path is bound to texture slot 31 (a null descriptor);
- `air_module_function_constant_names()` (MTLFunctionConstantValues come by name);
- the corpus switches (`AIR_FC_FILL`, `AIR_FC_SET`, `AIR_SC_DEBUG`, `AIR_STRICT_INIT`) read the
  environment only when built with `AIR_CORPUS_SWITCHES`.

nord2 changes (MIT, marked "nord2:" in the source): `air_intrinsics.c` sinpi/cospi/tanpi. The adapter to
the Nord2 Mali compiler ABI lives outside this directory (`../mali_graphics_air.inc`).
