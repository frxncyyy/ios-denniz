# IOSurface render and presentation bridge

This plan describes the smallest next step from the current bounded Metal draw path to a GPU-generated tile visible through the existing iOS presentation path. It is a design proposal; it does not claim GPU-to-IOSurface DMA or on-device presentation has been qualified.

## Current path and constraints

`SweetSurface::create` in `xnu/patches/0046-sweet-iosurface-iomfb.patch` allocates normal IOSurface backing without a GPU physical-address constraint. The Metal frontend's `newTextureWithDescriptor:` in `tools/tegu/metal/render_encoder.inc` instead creates a `TeguBuffer`; its target handle is passed through the render client. These are separate allocation and ownership paths today.

IOMobileFramebuffer `SwapEnd` (selector 5) already looks up IOSurfaces by ID and copies bounded four-byte rows into the panel framebuffer. That existing CPU copy is the presentation transport. Tegu GPU buffers are currently accepted only when every 4 KiB page meets the GB34 physical-window predicate in `xnu/patches/0115-tegu-gpu-owned-buffer-pages.patch`. This is a current mapping/allocation constraint; it does not establish that all other GPU mappings are impossible.

Tegu GPU selector 12 is occupied by sampled-texture drawing (`xnu/patches/0126-tegu-gpu-sampled-texture-resources.patch`). Selector 13 is the next available candidate for a surface-target operation; reserve it only after agreeing the larger-target request layout. IOSurface selector 13 belongs to a different user client and currently serves IOSurface limits.

## Proposed first increment

Add an explicit opt-in GPU-shared backing mode to `SweetSurface::create`, such as an `IOSurfaceTeguGPUShared` property. Preserve the ordinary IOSurface allocation path when the property is absent or false. The opt-in allocation must use the same GB34-constrained physical-mask allocation and per-page validation as the Tegu GPU buffer allocator. Reject allocation if any page fails; do not silently fall back to ordinary memory. Track GPU-shared state and its wired-memory charge until the final surface reference is released.

The opt-in property alone will not change allocations made by Backboard or CoreAnimation. A caller must explicitly create or receive an eligible surface and pass that IOSurface to the GPU frontend. Add the standard `newTextureWithDescriptor:iosurface:plane:` device entry point (or an equivalent narrow import API) so an IOSurface-backed `TeguTexture` retains its surface identity and can be distinguished from a buffer-backed texture. Verify the donor API's argument and ownership conventions before implementing the selector.

Propose Tegu GPU selector 13 for the first render-to-surface operation. It should resolve the surface ID with `sweet_surface_copy`, require the GPU-shared flag, validate the target page with the same GB34 predicate, then reuse the bounded render builder, cache maintenance, fence, and failure quarantine used by current rendering. Retain the surface for the whole submission; keep it alive in quarantine whenever completion is uncertain. In the frontend, preserve the surface-backed target kind and route that target to selector 13 while selector 12 continues to handle sampled-texture draws.

After a successful fence, present the surface using the existing SwapBegin/SwapEnd sequence. The render path already performs PoC cache maintenance around GPU work; the IOSurface mapping and SwapEnd CPU read must still be checked for coherent visibility on device. Keep the first proof to a 16x16 RGBA8 tile. SwapEnd copies bytes without scaling or channel swizzling, so this proves a small tile, with the expected channel-order caveat, rather than a full-screen UI. Full-screen geometry and panel-native BGRA handling remain later work.

## Likely files

- SweetSurface implementation introduced by `xnu/patches/0046-sweet-iosurface-iomfb.patch`: opt-in allocation, backing state, and final-release accounting, carried by a new ordered patch.
- A new ordered XNU patch after `0126-tegu-gpu-sampled-texture-resources.patch`: selector 13 surface lookup, validation, render submission, and retained failure lifetime.
- Shared GPU request definitions/source used by that selector, following the existing render transport layout.
- `tools/tegu/metal/render_encoder.inc`: IOSurface texture import and retained surface-backed target identity.
- `tools/tegu/metal/compute_frontend.m`: route recorded surface targets to the new selector.
- `tools/tegu/metal/render_client.inc`: resolve and retain eligible kernel surface backing, preserving fence/lifetime handling.
- A small frontend IOSurface helper/include and host checks: property and import refusals, page-span policy, retained lifetime, and selector transport.

## Qualification gates

Before claiming a visible UI result, verify on device that the constrained IOSurface backing passes the GPU page predicate and can be written by the GPU; verify cache visibility through the kernel IOSurface mapping; then verify SwapEnd presents the fenced output cleanly. The panel framebuffer is currently outside the GB34 range used by Tegu GPU buffers, so this proposal keeps the existing CPU presentation copy and does not target panel memory directly. Check tearing during the copy. A successful host test or GPU buffer readback does not establish any of these device presentation properties.
