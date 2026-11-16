# Metal/QuartzCore gap

## What exists

`gpu/metal/nord2_metal.c` is the tested transport-independent compute core;
`gpu/metal/Nord2Metal.m` is a source-only ObjC shim over it and the private
`Nord2GPU` user client.  It implements device discovery by
`IOServiceMatching("Nord2GPU")`, `IOConnectCallMethod/ScalarMethod`, buffer
allocation/mapping, a pipeline holding a precompiled arch9 Mali binary,
queue/command-buffer/compute-encoder, `setBuffer:offset:atIndex:`,
`dispatchThreadgroups:threadsPerThreadgroup:`, `commit`, `waitUntilCompleted`
and error reporting.

It is host syntax-checked with freestanding Objective-C declarations.  It has
**not** been built against the iOS 16 SDK, loaded by Metal.framework or run on
the phone.

## What is missing before QuartzCore can use it

1. A real `.bundle` built for arm64 iOS16 with Foundation/Metal linkage and
   `MetalPluginName`/`MetalPluginClassName` matching the class.  The current
   file deliberately does not claim the Apple accelerator ABI.
2. `MTLDevice` method coverage required by QuartzCore: library/pipeline
   creation from AIR, render pipeline descriptors, textures, samplers,
   depth/stencil, heaps, events, shared events, indirect command buffers,
   drawables and the private CAMetalLayer/IOSurface selectors.
3. An in-process AIR -> SPIR-V -> arch9 Mali compiler path.  The host compiler
   is proven; the iOS dylib build/ABI and the AIR frontend are not.
4. IOSurface import: map the retained surface backing into a BO, honor row
   stride/format/usage, fence completion and cache visibility.
5. Presentation: submit a render pass to the Nord 2 framebuffer/IOSurface and
   use the existing SwapEnd/command-mode path or a bounded CPU scanout copy.
   Rendered pixels must come from Mali; CPU drawing must not be labelled
   Metal.
6. A renderer probe that passes before any global CoreAnimation switch.  The
   CPU presentation path and the calibrated audio plugin stay installed until
   then.

## Honest capability advertisement

`supportsFamily:` returns 0, no texture/render support is advertised, and
`newComputePipelineStateWithBinary:` is a private test entry point rather than
an Apple `MTLLibrary` API.  This avoids the donor's trap of advertising caps
the backend cannot honor.
