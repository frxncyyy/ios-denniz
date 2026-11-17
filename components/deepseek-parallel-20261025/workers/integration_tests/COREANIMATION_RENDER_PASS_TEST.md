# CoreAnimation-shaped render-pass test (DELTA11)

Independent from render's generated `vs_texture_host_test.m`.  The harness
compiles the real `gpu/metal/nord2_render_pass.inc` against a small fake
Objective-C frontend and a fake JM executor.  The fake JM returns status 4
(completed) and writes target bytes from an independent format oracle; it does
not claim GPU execution.

## Cases

- `1080x2400` BGRA8 (format 80), natural stride `4320`
- `1170x2532` BGRA8, natural stride `4680`
- `1170x2532` IOSurface-imported BGRA8 (surface target version 1)
- CAMetalLayer-shaped drawable usage (`textureUsage == 5`) on display targets
- format 115 (`RGBA16F`) clear and bytes
- two colour attachments, formats `80/80` and `80/115`, pipeline descriptor
  shape with slot 0 writeMask 15 and slot 1 writeMask 0
- load actions `Load`, `Clear`, `DontCare`
- store action `Unknown` (4) followed by `setColorStoreAction:atIndex:`
- checks non-inert encoder, command not poisoned, clear/draw jobs, fake JM
  completion, exact pixels at first/mid/last row, imported padding untouched,
  and slot1-mask0 target unchanged by the draw

## Run

From the candidate root:

```sh
python3 gpu/build/deepseek-parallel-20261025/workers/integration_tests/coreanimation_render_pass_test.py
```

The runner builds and runs two binaries (release and ASan/UBSan), writes logs
under `workers/integration_tests/runs/delta11_coreanimation_*.log`, and exits
nonzero while the production fix is absent.  Expected pre-fix signal:

```text
TEGU_PASS_REFUSED line=70 reason=color attachment outside exact JM class ... row=4320/4680 (diagnostic line moved; c6ec was 58)
...
COREANIMATION_PASS_RESULT FAIL
```

After the production admission fix, require:

```text
COREANIMATION_PASS checks=... failures=0 ... jm_status=4
COREANIMATION_PASS_RESULT PASS
```

The main integration suite calls the same runner and records the required
`CoreAnimation render pass acceptance` gate.

## Explicit limitation

The test models the CoreAnimation render pipeline shape (`2 attachments`,
slot1 mask 0, formats 80/115) but does not compile the production
`render_pipeline.inc` builder.  It therefore proves pass admission and target
job/byte behavior, not the full frontend PSO draw path.  Full-frontend coverage
remains with the render-owned harness.
