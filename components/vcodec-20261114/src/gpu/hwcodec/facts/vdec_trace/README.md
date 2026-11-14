# DEC02 -- register-access trace of H.264 decode, and the exact VDEC page list

Owner: hw-dec02.  Host only: nothing here has run on the phone, nothing is published, no vendor
binary is copied.  The vendor files are read at run time from
`gpu/build/cam-vendor-20261109/vfs`.

Two questions this package answers with measurements instead of inference:

1. **Which 4 KiB pages of `0x16000000..0x1603ffff` does the vendor decoder touch, read or written,
   which power domain must be on, and in which phase of a session?**  -> `PAGES.md`
2. **What does one H.264 access unit cost in register operations and guest work, at 320x240 and
   at 1920x1080?**  -> `COST.md`

## Run it

```sh
bash gpu/hwcodec/facts/vdec_trace/run.sh
# last line: N2TEST DEC02 PASS 14/14
```

Everything is built and run under `/tmp/n2hw-dec02` (override with `N2DEC02_OUT`); the only files
written in the tree are the regenerated `PAGES.md` and `COST.md`.  The run takes ~57 s with `-j2`
and `nice -n 10`.

The runner is deterministic by construction on this host: two independent sessions per resolution
produce the same access **multiset** (the interleaving of the vendor's two guest threads is
scheduling-dependent, each thread's order is not) and a byte-identical `PAGES.md`.  The test
checks both.

## Method

```
ap/n2ap_vdec.c (DEC01's AP client: QUERY_CAP / INIT / SET_PARAM / one DEC_START per AU /
                RESET / DEINIT plus the LOCK/UNLOCK/WAITISR/GET_FB/PUT_FB service)
        |  and, as an independent cross-check, dec1/vdec_ref.c (opus-dec1's reference driver)
        v
vpud + libvcodecdrv.so + libvcodec_utility.so + libvcodec_oal.so   (the phone's own files)
        |  under emu/ (V02 AArch32 interpreter), on T02's fake n2vc_hw
        v
n2tr (this package): transparent n2vc_hw wrapper, one line per register op
        |
        v
PAGES.md / COST.md (tools/classify.py)
```

* `probe/dec01_client.c` -- the driver that produces the documents: it splits the Annex B stream
  into access units, feeds them to DEC01's client (one DEC_START each, first AU again with an
  output buffer after the header pass), harvests the displayed/freed buffers, and prints one
  `DC START au=...` line per AU with the `n2vcu_stats` deltas.  It never talks to a register
  itself.
* `probe/n2tr.[ch]` -- the recorder.  It forwards every callback to T02's fake and records
  power/register/DMA/interrupt events.  It also models the one window the decode path needs and
  the tree does not have yet (`RWVC_WIN_VDEC_ALL` = 12, `0x16000000+0x30000`, per-page VDE/VDE2
  power) and the SMI larb sleep-protect handshake (write SLP_CON bit 0, poll bit 16), which the
  real larb answers.  Every trace line ends with the guest thread that issued it.
* `probe/vdec_probe.c` -- a third, independent AP-side client (my own implementation of the kernel
  protocol from `facts/VDEC_PROTOCOL.md` 2.1/2.3).  It runs a header START and one frame START on
  a one-AU stream and asserts `mmio_bad == 0`; it is the check that the page list does not depend
  on either driver.  (`N2DEC02_SEED=off=value,...` writes words into the vendor's instance area
  for diagnostics only; the normal path does not use it.)
* `tools/stage.py` -- prepares the `/tmp` copies.  It applies four overlays, each with exact text
  and a hard failure if the tree changed, and each skipped once the tree carries it:
  1. `shim/n2shim_props.c`: `vendor.mtk.vdec.frame.layout.mode = "0"` (MM21, not UFO MT21).  This
     is opus-dec1's V34 property (board 1794415185); without it a 1080p session selects MT21.
     Not ours; staged.
  2. `vcu/n2vcu.c`: one window entry, `{0x16000000, 0x30000, 12}` after the ten frozen VDEC
     entries.  This is DEC04/DEC07's `RWVC_WIN_VDEC_ALL` text, staged until the frozen header is
     revised to rev 3.
  3. `tests/hw/n2vc_hw_fake.c`: `VDEC_BASE` (0x16000000) and `VDEC_RACING` (0x16004000) mapped to
     `RWVC_BLK_VDEC_SOC` instead of `RWVC_BLK_VDEC_CORE` (see "Findings" below).  Not ours;
     staged and reported.
  4. `dec1/vdec_ref.c`: include `n2tr.h`, wrap the fake, add `MARK` phases and close the trace at
     exit.  Nothing else is changed.
* `tools/classify.py` -- turns the traces plus the per-AU driver lines into the two documents and
  runs 46 self-checks (page set, windows, power state per page, AVC_VLD read+write, larb writes
  and their exact values, no access above 0x30000, per-AU counts, VD-g reached, GCE zero).  It
  converts a frozen-window offset to the global offset with the window base table; that
  distinction matters (`VDEC_VLD` is at `0x16020000`, not at `0x16000000 + offset`).

The test vectors are generated with ffmpeg at run time: 320x240 main `-bf 2`, 12 AUs; 1920x1080
high `-bf 0`, 6 AUs (the same parameters `dec1/run_ref.sh` uses); a third 1920x1080 high `-bf 2
-slices 4` stream that checks the page set does not grow with slice count.  Nothing is committed as
a binary.

## Results in one line each

* **18 pages** are touched, all inside `0x16000000..0x1602ffff`; `0x30000..0x3ffff` is untouched.
* **6192 of 21321** register ops on 320x240 (29%) are outside the ten frozen windows and need
  `RWVC_WIN_VDEC_ALL`; the rest are served by the old windows.
* Every page below `0x20000` belongs to **VDE** (`RWVC_BLK_VDEC_SOC`), every page at/above it to
  **VDE2** (`RWVC_BLK_VDEC_CORE`).  Measured, not assumed: the recorder logs each power transition
  and classify.py records the domains that were on at every access.  The one access with CORE off
  is the client's 68-word racing restore at `0x16004100`, issued by the LAT lock.
* larb5 and larb4 are written **only at +0x00c**: `0x00000001` (request) and `0x00010000` (release,
  the ack bit written back).  Anything else on those pages is read-only.
* The **init** and **header** phases issue **no** VDEC register access: SPS/PPS parsing is ARM
  code over the mapped bitstream; the hardware is first programmed at the first frame START.
* **No GCE** is used for H.264 decode (0 flushes, 0 commands); all programming is direct MMIO.
* Per frame: ~1040 reads and ~588 writes at the `n2vc_hw` boundary (n2tr), **~441 distinct read +
  430 distinct write offsets** per AU (436/425 in a steady state); the `/dev/vcu` model (n2vcu_stats) counts
  491 reads and 574 writes per AU because the client's own batches and the OAL's lists take a
  different path.  Both are in `COST.md`, with the reconciliation.

## Findings this package produced (posted to the board)

1. **The client's racing restore happens with CORE off.**  DEC01's client answers LOCK_LAT
   first and does what `mtk_vcodec_dec_clock_on()` does: power SOC, restore the 68 racing words,
   then power CORE.  With the kernel's conservative page map (`VDEC_RACING` -> CORE) that write is
   refused; the fake counted 12 EACCES per 12-AU session at `0x16004100`.  Which MTCMOS domain the
   page physically sits in is **[I]** (the Android stack has both on during a decode).  Two legal
   fixes, both consistent with the kernel source: the client takes the CORE reference for its whole
   active period, or the kernel allows the racing page with SOC only.  Posted to hw-dec01/hw-k02;
   the fake used by run.sh stages the second so the trace is clean.
2. **The larb SLP_CON release is `0x00010000`, not `0`.**  K02's allow-list accepts only values
   with `value & ~1 == 0`, which refuses the second half of the handshake (18 writes per session).
   Posted to hw-k02/hw-dec07/hw-lead.
3. **The vendor's ring/ion budget.**  `n2shim_ion_register_share()` has 64 slots and vpud's own
   buffers use ~34 of them; a client that registers 24 bitstream + 24 frame shares gets `-ENOMEM`
   from share 31 on, and the vendor then stops decoding after 3 access units with
   `ion_import failed`.  The harness uses 2 bitstream buffers and 12 two-plane frame buffers.
   Posted to hw-dec01/hw-dec05/hw-ip.

## Limits

* Host fake: no pixels; the fake schedules the decode-done event and the client's WAITISR service
  waits for it, so the interrupt *protocol* is exercised but not a real GIC line.  A fixed 86 MiB
  DMA pool.  The page set is a property of the vendor's programming, not of the picture; a stream
  with many slices, a different profile or a mid-stream resolution change could touch more pages.
* The vendor's internal `core_index` argument is not visible at the register address; "core" in
  `PAGES.md` is the address region (LAT `0x10000..0x1ffff`, CORE `0x20000..0x2ffff`, else SOC).
* Exact per-thread guest instruction counts are not available (V32's thread table has no icount
  accessor; asked on the board).  `COST.md` gives measured guest CPU time and an `[I]` conversion
  at 35 MIPS.
* `PAGES.md`/`COST.md` are regenerated in place by `run.sh`; they are small text documents, not
  build products.
