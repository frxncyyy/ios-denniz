# The camera's ISP on the GPU: `rwa_isp_*` (API version 1)

Header: `src/rwa_isp_api.h` (the only thing to compile against). Implementation: `libredwoodair.dylib` y2s39
(`gpu/build/y2s39-gpuisp/src/air_metal.c`), in `/System/Library/Extensions/Nord2Metal.bundle/`.
Status (10 November 2026): implemented and checked on the build machine (softpipe, and Panfrost's shader compiler); **not yet
run on the phone's GPU, and not installed** -- the library with these calls is `gpu/build/y2s39-gpuisp/payload/`, sha256
`a4fa9b4b...`, and goes onto the phone with the transaction `apps-20261104/guest-gpuisp.sh`. Until then `RWA_ISP_LOAD` gives 0
on the phone, which is the case the bundle must handle anyway. The section "What was checked" says what is verified.

## What it is

`isp_render_nv12()` and `isp_render_full_nv12()` of `camera_isp.c`, done by the GPU: a RAW10 Bayer frame as
`/dev/redwood-camera` hands it over is made into the two planes of an NV12 picture, written by the GPU **in place** into the
IOSurface of a CVPixelBuffer (the graph's pool's, or the bundle's own). The measurements (`isp_measure*`), AE, AWB and AF stay
on the processor: they read a sixteenth of the frame and are not what costs.

Three passes on the GPU per picture:

1. the ISP proper, RAW to 8-bit R, G, B, into an intermediate picture of the output's size that the context keeps;
2. R, G, B to the luma plane;
3. R, G, B to the chroma plane (each 2x2's Cb and Cr from the sum of its four pixels).

The arithmetic is the processor's own, step for step, in the same whole numbers where `camera_isp.c` uses whole numbers
(`pixel_q`, `highlights_q`, `shade_at`, `nv12_pair`), with the same tone curve table (uploaded as a texture) and the same
sampling positions (the library computes, per column and per row of the picture, the sensor pixel `render_line` /
`render_line_window` would read, with their own fixed-point arithmetic, and hands the shader two small tables).

| | processor | GPU |
|---|---|---|
| `RWA_ISP_MODE_CELLS`, no lens shading table | `isp_render_nv12` -> `render_line[_window]` -> `pixel_q` | the same integers: **the processor's picture bit for bit** (host test: 0 bytes differ in 130 pictures) |
| `RWA_ISP_MODE_FULL` (every pixel, 3x3 bilinear demosaic) | `isp_render_full_nv12` -> `full_line` -> `pixel` (floating point) | the same formula in 32-bit floats: bit for bit on softpipe (10 pictures); on the Mali a fused multiply-add may move a value by one step of the tone curve's table, so expect a difference of 0 to 2 in a few bytes (not measured) |
| lens shading table (`isp->lsc.on`) | `lsc_at` + `pixel` | **not done**: `RWA_ISP_E_UNSUPPORTED`, use the processor |
| BGRA output (`isp_render_bgra`) | yes | not in version 1 |

Not looked at in `RWA_ISP_MODE_FULL`, as on the processor: `zoom`, `flip`.

## Finding it

Nothing is linked. `dlopen(RWA_ISP_LIBRARY, RTLD_NOW)` -- the same path the Metal driver opens, so in a process that already
uses Metal (mediaserverd does: VideoToolbox and CMCapture make Metal devices) it is the same image, and in one that does not
it loads the library, which costs its 10 MB of text and nothing else until `rwa_isp_create`. Then `RWA_ISP_LOAD` (the header's
macro) looks the seven calls up; it gives 0 when any is missing or `rwa_isp_api_version()` is lower than asked. A library
older than y2s39 has none of them: that is how "not available" is told, and the bundle then keeps to the processor.

`rwa_isp_create` returning NULL (with a sentence) is the second way: the library is there but this process cannot have the GPU
(no Nord2GPU service, the kernel booted without the GPU).

## The calls

```c
unsigned        rwa_isp_api_version(void);
struct rwa_isp *rwa_isp_create(char *why, size_t why_size);
void            rwa_isp_destroy(struct rwa_isp *isp);
void           *rwa_isp_frame_memory(struct rwa_isp *isp, unsigned slot, size_t header_bytes, size_t frame_bytes,
                                     char *why, size_t why_size);
int             rwa_isp_render(struct rwa_isp *isp, const struct rwa_isp_frame *frame, const struct rwa_isp_params *params,
                               const struct rwa_isp_output *output, struct rwa_isp_result *result, char *why, size_t why_size);
int             rwa_isp_wait(struct rwa_isp *isp, char *why, size_t why_size);
void            rwa_isp_forget_surfaces(struct rwa_isp *isp);
```

`why` is a buffer for one sentence in English (256 bytes is enough; NULL is allowed): what failed and which number said so.
Every struct begins with `size`, which the caller sets to its `sizeof`: a later version may add fields at the end.

### `struct rwa_isp_frame` -- the input

Straight from `struct redwood_camera_frame`: `width`, `height`, `stride`, `format`, `bayer`, `black`, and `raw`, the first
byte after the header.

* `format` 4 (`REDWOOD_CAMERA_FORMAT_LE40_RAW10`: four pixels as one little-endian 40-bit word, pixel k in bits 10k..10k+9)
  and 1 (`REDWOOD_CAMERA_FORMAT_MIPI_RAW10`: four high bytes, then the four pairs of low bits). Anything else:
  `RWA_ISP_E_UNSUPPORTED`.
* 4x4 to 8192x8192, `stride >= (width + 3) / 4 * 5`. All three sensors' modes fit (IMX766 4096x3072 stride 5120, IMX355
  3264x2448, IMX615 3264x2464).
* The frame reaches the GPU in one of two ways:
  * **uploaded** (any `raw`): the library copies the frame into a GPU buffer of its own during the call, line by line at a
    row length the GPU takes (15.7 MB for the IMX766: a few milliseconds), and `raw` is not needed after the call returns;
  * **zero copy**: `raw` lies in memory `rwa_isp_frame_memory` gave, at `P + header_bytes`, and `stride` is a multiple of 64.
    Then the GPU reads it where it is, and it must stay untouched until the render has been waited for. `header_bytes` is the
    kernel header's size (`sizeof(struct redwood_camera_frame)`, 120): the bundle `read()`s header and frame into `P` in one
    call, exactly as now into its own buffer. Four slots per context, so that the reader can go on filling another while one
    is being rendered. Which of the two happened is in `result->flags` (`RWA_ISP_DID_ZERO_COPY`).

### `struct rwa_isp_params` -- what `struct isp` holds for a picture

`wb`, `digital`, `ccm` (row by row), `falloff`, `zoom`, `flip`, `gamma` (the 4096-byte table, `isp->gamma`), copied from the
stream's `struct isp` (the one with the stream's zoom and flip, `view` in `rwcam_device.c`); `video_range` as
`isp_render_nv12`'s last argument; `lens_shading` = `isp->lsc.on`, which must be 0; `mode`.

`gamma` is read during the call and uploaded only when its bytes differ from the last call's.

### `struct rwa_isp_output` -- where the picture goes

`iosurface = CVPixelBufferGetIOSurface(pb)`, `width`, `height`.

Requirements, each checked, each `RWA_ISP_E_SURFACE` with the number in `why`:

* pixel format `'420f'` or `'420v'` (and `params->video_range` saying which), two planes;
* the surface's width and height are `output->width` x `output->height`; at most 8192x8192; the chroma plane is
  `(width + 1) / 2` x `(height + 1) / 2`;
* each plane's bytes per row is a multiple of 64 and each plane's offset from the surface's base address is a multiple of 64
  (CoreVideo's buffers on this phone are: 1536 for 1504, 4032 for 4032, 1152 for 1128; offsets 64 and a multiple of 64);
* the surface has an ID (it is an IOSurface the kernel knows, not a malloc'ed CVPixelBuffer).

The GPU writes the planes itself: **do not** hold `CVPixelBufferLockBaseAddress` for the GPU's sake (it is not needed), and
do not read or write the planes on the processor between `rwa_isp_render` and the end of the wait. The GPU's writes do not
move the IOSurface's seed; a consumer that copies on a changed seed (none of ours needs it: the compositor converts these
surfaces on the GPU and also looks at their bytes) is served by one `CVPixelBufferLockBaseAddress(pb, 0)` /
`CVPixelBufferUnlockBaseAddress(pb, 0)` after the render, which costs nothing.

The context keeps the imports of the last 8 surfaces it was given (by IOSurface ID, checked against the surface's geometry and
base address), so a pool's surfaces are imported once each and not once a frame. `rwa_isp_forget_surfaces` drops them: call it
when the pool is released or the stream stops (a kept import keeps that surface's memory mapped for the GPU).

`RWA_ISP_OUT_ASYNC`: the call returns when the passes are submitted; `rwa_isp_wait` then waits for all of the context's.
Without the flag `rwa_isp_render` waits itself, and when it returns `RWA_ISP_OK` the buffer can be handed to the graph.

### `struct rwa_isp_result`

`prepare_ns` (upload, tables, imports), `queue_ns` (the three passes built and submitted), `wait_ns` (the GPU finishing them),
`flags`. Log them for the first frames: they are the measurement.

## Errors

| code | meaning | what to do |
|---|---|---|
| `RWA_ISP_OK` 0 | written (or queued, with `ASYNC`) | hand the buffer on |
| `RWA_ISP_E_ARGUMENT` -1 | NULL, wrong `size`, impossible geometry | a bug in the caller |
| `RWA_ISP_E_UNSUPPORTED` -2 | format, mode, lens shading table | render on the processor; stays so for this configuration |
| `RWA_ISP_E_SURFACE` -3 | this output surface cannot be written in place | render this buffer on the processor; another surface may be fine |
| `RWA_ISP_E_GPU` -4 | the driver refused, or two seconds without the GPU finishing | render on the processor, and stop asking the GPU for some seconds (or for good after a few in a row) |
| `RWA_ISP_E_MEMORY` -5 | | as -4 |

After any failure the output surface may be partly written: render the whole picture on the processor.
No call aborts or raises; a failure of the GPU inside the kernel (a job fault) surfaces as `RWA_ISP_E_GPU` from the wait.

## Threads

* Every call may be made from any thread. The library has one lock for all of its calls in the process (the Metal driver's
  calls in the same process take the same lock), held while a call builds and submits its work (the upload's copy included,
  when there is one) and **not** while it waits for the GPU (`rwa_isp_render`'s own wait and `rwa_isp_wait` wait outside it).
* **One context, one thread at a time**: `rwa_isp_render`, `rwa_isp_wait`, `rwa_isp_forget_surfaces` and `rwa_isp_destroy` of
  the same context must not run concurrently (nothing breaks inside the library's lock, but a render's wait and a concurrent
  forget or destroy are the caller's to order). One context per stream is the intended use. Two sensors: two contexts, each
  rendered from its own thread; their GPU work overlaps, their calls are serialised by the lock for the moment each takes.
* The reader thread may `read()` into another `rwa_isp_frame_memory` slot while a render of a different slot is in progress
  (that is memory, not a call); `rwa_isp_frame_memory` itself may be called from any thread at any time.
* The contexts of a process share one GPU device of their own (not the Metal driver's, if the process has one: a second
  device over the process's one connection to the GPU).
* `dispatch_apply` pieces (the processor's way of splitting a picture) have no use here: one call renders the whole picture.

## Cost (expected, not measured on the phone)

* Preview 1504x1128 from 4096x3072: no processor time but the call itself; with upload, one copy of 15.7 MB (a few ms); with
  zero copy, none. GPU: 1.7 M pixels x about 13 texel reads for the ISP pass, plus the two small passes: a few milliseconds
  of GPU time. The wait is that plus the queue in front of it.
* Still 4032x3024 (`RWA_ISP_MODE_FULL`): 12 M pixels x about 21 texel reads; tens of milliseconds of GPU time, against
  130-230 ms of four processor cores.
* Memory per context: the intermediate picture (width x height x 4: 6.8 MB for the preview; the still's 49 MB one is released
  as soon as that render has finished), the upload texture when `raw` is uploaded (`stride * height`), each
  `rwa_isp_frame_memory` slot asked for (`header + stride * height`), three small tables.

## What was checked, and where

On the build machine (`gpu/build/y2s39-gpuisp/src/host-tests.sh`, run from scratch on 10 November 2026; a native build of the
same Mesa tree with assertions and NIR validation on):

* `isp_gpu_host_test`: 141 cases through `camera_isp.c` (this directory's, as it was at 18:51) and through the library on
  softpipe, compared byte by byte, both planes: both frame formats, the four Bayer orders, full and video range, pictures of
  odd sizes and other proportions, zoom 1 to 40 with every flip, three white balances, falloff on and off, the identity matrix,
  three black levels, frames with burnt-out, black and one-colour-clipped patches, the still path (160x120), and the IMX766's
  4096x3072 frame (stride 5120) into 1504x1128 plain and zoomed and flipped. **0 bytes differ, in both modes.** The refusals
  (lens shading, format 2, `FULL` at another size, no planes, no IOSurface framework) answer their codes.
* `pan_compile_host_test`: the six shaders compile with Panfrost's Valhall compiler for the Mali-G77 (3.3 kB of code for the
  cells shader, 5.0 kB for the full one). Compiled, not executed.

Not checked anywhere yet, because it needs the phone: the shaders running on the Mali (integer textures fetched by place,
integer division, the block as a pushed constant buffer); the frame read out of a GPU buffer (`rwa_texture_from_buffer`),
with and without zero copy; the two planes of an IOSurface imported as R8 and RG8 render targets and written in place; a
second GPU device in a process that already has the Metal driver's; every timing. The first frames' `rwa_isp_result` and the
picture itself are the test: compare one GPU-rendered buffer with the processor's render of the same frame (the bundle has
both) before trusting it -- in `RWA_ISP_MODE_CELLS` they must be identical.

Two calls beside the seven, exported for tests and not part of this interface: `rwa_isp_render_textures` (the planes as two
textures instead of an IOSurface) and `rwa_isp_device`.
