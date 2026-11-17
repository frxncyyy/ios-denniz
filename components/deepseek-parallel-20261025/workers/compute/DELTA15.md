# Compute DELTA15 — owned selector gaps (host only)

Status: **READY FOR REVIEW**. Host release configurations and one ASan/UBSan
configuration pass. No phone/native/publication.

## Implemented selectors

- `TeguComputeDevice name`: existing stable METAL name retained; a non-METAL
  fallback category returns the same `Tegu Mali GPU`/`Nord2 Mali-G77` string.
- `TeguComputeDevice minimumLinearTextureAlignmentForPixelFormat:` returns 64
  for the supported sized formats and 0 for unsupported formats. This is the
  row/offset alignment a future exact buffer-backed 2D alias would require.
  Exact buffer-backed textures are not implemented in this delta:
  `newLinearTexture...` and `newTextureWithDescriptor:offset:bytesPerRow:` stay
  explicit nil + bounded `TEGU_SELECTOR_REFUSED` (no copy, no fabricated
  layout). `deviceLinearReadOnlyTextureAlignmentBytes` stays 0 because no
  arbitrary CPU-page/read-only import is advertised.
- `TeguQueue commandBufferWithDescriptor:` returns the ordinary
  `commandBuffer`; retainedReferences/errorOptions are safely ignored (the
  bridge does not claim an unimplemented retention/error policy).
- `TeguCommand encodeSignalEvent:value:`: no Tegu MTLSharedEvent/MTLEvent
  object exists, so it explicitly sets sticky invalid + an NSError and never
  forwards to Apple or crashes. No false event ordering.
- `TeguBuffer newTextureWithDescriptor:offset:bytesPerRow:`: selector present,
  explicit nil + bounded trace (exact buffer-backed 2D layout remains a gap).
- `TeguLibrary newFunctionWithName:constantValues:completionHandler:`: calls
  the existing synchronous specialization exactly once, then invokes the
  completion handler exactly once with function-or-nil and NSError. The bridge
  has no asynchronous compiler queue; completion occurs after the synchronous
  result is known. Nil handler is accepted.

## Host test plan

`check_frontend_delta5.py` release configurations plus one ASan/UBSan pass:
name string, alignment values, queue descriptor command creation, event
refusal invalid+error, buffer texture nil, and async library completion-once
on a bare/null AIR library.

## Actual host result

`python3 workers/compute/check_frontend_delta5.py`:
- release: `delta10_default_off`, `delta10_default_on`, `delta11_inert_on`,
  `delta11_compositor_inert` PASS
- one ASan/UBSan: `delta11_inert_asan` PASS
- logs: `workers/compute/runtime/delta10_*.log`, `delta11_*.log`
- selector assertions: name, alignment 64/0, queue descriptor command, event
  sticky invalid+NSError, buffer texture nil, async library completion exactly
  once. `TEGU_SELECTOR_REFUSED` bounded trace count is four (existing no-copy,
  resource-group, linear plus the new buffer-texture selector).

## Hashes

- `compute_frontend.m` `7ea1fb655bb4e071d0f7bc3f857817d19a110eb466233c0a0e83bdcaa479cdda`
- `library_frontend.inc` `22ef99c03c187374ea490372e4a98c5ea70f20a90295d3345cbcdebc123b463c`
- test `check_frontend_delta5.m` `a5fd7b893b1a5acfc3b45505abaf93143f61e78bd46331b08c054f36a750dc33`
- runner `check_frontend_delta5.py` `4f07ad08fc1fd4fbc6da1bc62b00826ce4fa6ec6dee4db2fb26ae8ef4352a197`

## Gaps

- Exact buffer-backed 2D/linear texture alias remains nil; no under/over-copy.
- `deviceLinearReadOnlyTextureAlignmentBytes` remains 0; no zero-copy CPU-page
  import.
- No native/device execution; root owns L4.
