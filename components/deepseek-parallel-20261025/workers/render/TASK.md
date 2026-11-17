# render DELTA 16 (Claude root, THERMAL-LIMITED). Base: commit dbff7f5 (C6c built from 70ebab63). HEAVY: you may take it (root
grants now; nobody else runs heavy). -j1; one release + one ASan pass. Work ONLY under #ifdef TEGU_ENABLE_MSAA4 (default builds stay
as C6c). Fix the C6 audit MSAA findings (board "C6 frontend audit: ranked blockers"): (1) multi-draw 4x passes must not re-clear —
implement exact per-pass load for subsequent draws (or merge draws into one job) and resolve for clear-only passes; (2) tile-buffer
budget guard per pinned Mesa pan_select_tile_size (refuse when sum(bpp)*samples exceeds the budget, e.g. >32 B/px at 256-px tiles);
(3) set DRAW multisample_enable when sample_count>1 per panvk. Coordinate n2r (textures) only via board patches. Host tests for each.
Post hashes, end session.


Read the current Claude takeover HANDOFF, keep roles.json file ownership, and coordinate on the shared board. Do not repeat a heavy build without Claude's named lease. Persist results outside /tmp. No phone or publication.
