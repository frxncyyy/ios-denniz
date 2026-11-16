# Larger render targets and repeated draws

The current render path is qualified for a 16×16 RGBA8 target and one triangle. A larger target needs coordinated changes to the target layout, GPU mappings, tiler heaps, frontend texture storage, and pass semantics. Expanding only the C state builder would leave the kernel mapping too small for the surface.

## Current limits

- `render_client.inc` maps one target backing page at `job->pages[64]`, flushes one target page, and addresses it at `TEGU_RENDER_TARGET_VA + (offset & 4095)`. Its builder address contains no target dimensions or row stride.
- `render_job.h` and `render_submit.inc` provide 256 page aliases total, or 1 MiB. The target occupies alias page 64 and vertex-resource pages start at page 65. The job also carries a fixed 256-entry physical-page array.
- `render_state.c` validates a fixed 1024-byte target range. Its FBD row stride, surface size, viewport, scissor, and bounding-box values are fixed for 16×16.
- The frontend texture allocator stores rows tightly at `width * 4` bytes. It does not currently provide a padded target layout.
- The encoder permits one three-vertex draw, requires clear-and-store pass actions, and submits each recorded job synchronously. A later pass clears the target again.

For scale, a 1080×2400 RGBA8 surface at a 64-byte-aligned row stride of 4352 bytes is 10,444,800 bytes, about 2550 pages. This exceeds both the one-page target mapping and the current 256-page alias window. The phone display's reported stride is 4320 bytes. Prove whether the GPU supports that stride before assuming it can be used; otherwise render into padded storage and copy rows to the display surface.

## Staged qualification

1. **Keep the current 16×16 gate.** The larger-target prototype is host-only. Its header says 16×16 is hardware-qualified, while its builder accepts 32×32 based on static heap accounting. Run an actual GPU test at 32×32 before accepting it in production. Do not use host Mesa packing goldens as hardware qualification.

2. **Define a checked target layout.** Carry width, height, row stride, byte length, and format from the frontend to the kernel. Validate multiplication and alignment without overflow, require the surface span to fit the owned allocation, and derive the target GPU address only from kernel-owned handles and validated offsets. Include the layout in state-builder range and overlap checks.

3. **Redesign target aliases for multiple pages.** Give the target a contiguous GPU virtual range backed by every required physical page. Expand the page list and MMU mapping window beyond 256 entries, reserve nonoverlapping ranges for scratch, target, vertex, texture, and descriptor pages, and assign permissions by resource role. Flush every target page touched by rendering. On unsafe submission failure, quarantine all mapped backing pages and descriptor storage until GPU access is known to have stopped.

4. **Derive tiler requirements from Mesa and device features.** Compute bin-pointer storage, hierarchy selection, geometry storage, and chunk storage for each layout using the pinned Mesa formulas plus the actual GPU's maximum hierarchy levels and relevant features. Allocate and map those spans explicitly. The prototype's fixed 131072-byte chunk, 65536-byte geometry span, and one-level hierarchy do not establish safety for screen dimensions, even when a bin-pointer estimate fits.

5. **Prove the row-stride contract.** Compare hardware alignment requirements with the phone's 4320-byte scanout stride. If direct rendering requires 4352-byte rows, allocate and expose that padded stride consistently through texture creation, read/write methods, render descriptors, cache maintenance, and the kernel request. If scanout requires 4320, add a bounded row-copy/present step from padded offscreen storage and verify it on device.

6. **Define pass and draw behavior before supporting multiple draws.** Specify when a pass clears and when it loads existing pixels, then support repeated draws without clearing between them. Either build a bounded command stream containing multiple draws or submit each draw with explicit load/store state and preserve target contents across submissions. Validate vertex ranges, command counts, and target lifetime across the complete pass. Add tests proving that multiple draws accumulate and that a fresh clear pass resets the target.

## Evidence boundary

The existing prototype tests compare target-dependent state fields against Mesa-generated host goldens, check legacy byte identity, and test malformed-layout refusals. They do not test kernel page mapping, real heap exhaustion, GPU hierarchy behavior, or rendered pixels. Keep those results as derivation evidence and gate larger production layouts on staged device qualification.
