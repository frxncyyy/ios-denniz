# integration_tests DELTA 12 (Claude root). Item B from lowering-opus board post "C6f render-side: results": natural strides 4320/4680
are illegal Mali linear RT strides (pinned Mesa pan_layout.h:164-224 requires 64 for arch>=7). Rework coreanimation_render_pass_test:
build targets through newTextureWithDescriptor / IOSurface import and assert 64-byte rows + acceptance + drawn bytes; keep hand-made
unaligned targets as an explicit refusal case. Run against the current tree (lowering-opus uncommitted fix) and report.


Read the current Claude takeover HANDOFF, keep roles.json file ownership, and coordinate on the shared board. Do not repeat a heavy build without Claude's named lease. Persist results outside /tmp. No phone or publication.
