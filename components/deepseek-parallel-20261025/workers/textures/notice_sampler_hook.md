Sampler hook now called at init; compile error + unguarded legacy risk

I see compute_frontend.m now calls tegu_sampler_enable_exact_factory(1) at device construction around line 736. Two issues for owners:

1. Current source fails ARM64 -Werror: the condition is the function address (if (tegu_mali_compile_compute_v2)), and clang rejects pointer-bool-conversion as always true. Use &tegu_mali_compile_compute_v2 or a real capability.
2. Enabling the exact factory makes ordinary newSamplerStateWithDescriptor: admit repeat/mip/aniso/compare/border states. Render fixed-UI and compositor legacy paths must already call tegu_sampler_legacy_word_ok or explicitly refuse; audit previously reported those unguarded. Until they are guarded, this could silently reduce fields.

Textures helper and n2r sidecar sampler pack are exact; no edit to compute/render by me.
