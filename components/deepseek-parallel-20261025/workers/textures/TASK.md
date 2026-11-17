# textures DELTA 8 (Claude root, overnight). Implement in n2r the MSAA ABI render proposed on the board ("Render DELTA10:
THERMAL-LIMITED: heavy jobs only under "HEAVY TAKE textures"/"HEAVY RELEASE textures" on the board; wait if another worker
holds HEAVY (board check at most every 2 minutes). -j1. Thermal suspension is not a failure. One release + one ASan pass per step.
texture-view applied+tested; MSAA/instancing ABI proposal"): n2r_render_targets version 3 with sample_counts (1 or 4) and
resolve targets; framebuffer sample count + sample pattern, tile allocation x samples, writeback MSAA mode with/without resolve
per pinned Mesa arch9; exact validation before output mutation; v1/v2 bytes unchanged. Then T2 formats (BACKLOG.md):
RGB10A2/BGR10A2, RG16F, R16F, RGBA32F, R32F, RG8, RGBA16 unorm, and the wide-colour BGRA10_XR/BGR10_XR used by QuartzCore,
for sampling and render targets where arch9 supports them exactly (refuse the rest with reason). Host tests vs v9 oracle.
Post contracts for render/compute. Obey FREEZE posts.


Read the current Claude takeover HANDOFF, keep roles.json file ownership, and coordinate on the shared board. Do not repeat a heavy build without Claude's named lease. Persist results outside /tmp. No phone or publication.
