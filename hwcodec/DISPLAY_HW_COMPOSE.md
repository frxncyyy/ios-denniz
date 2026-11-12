# Display hardware composition (OVL layers instead of the CPU copy) — step 1 analysis

2026-11-12 21:20, analysis only. No code was changed and no phone operation was made.

Tags: **[V-code]** = read in source, with the file named. **[V-dev]** = seen in a device log. **[V-notes]** = taken from an
earlier NOTES.md entry and not re-derived here. **[I]** = inferred, still to be proven on the device.

Sources used:
- S = `gpu/build/device-mmu-q10jfh-candidate/k/kernel/stage` (the exact q10jfh tree, 0393 included).
- E = `gpu/build/xnu-stage/osfmk/arm64/sweet_early.c` (startup archive; not recompiled per image).
- DRM = `work/oneplus-mt6893/drivers/gpu/drm/mediatek/mtk_disp_ovl.c` and `work/nord2-current-kernel/...` (same tables).
- PORT = `work/nord2-current-kernel/include/dt-bindings/memory/mt6885-larb-port.h`.
- DTS = `ios-sweet/dtb/nord2/live.dts`.
- Device log = `gpu/build/autocycle/runs/fbh1/ui-usb/xnu-live.log` (q10jfh).

Previous attempt: agent a647a03b (stopped) wrote the NOTES entry "~21:40 HW display composition ... PHASE 1 ANALYSIS" and left
`gpu/build/hwcomp-20261112/` behind. That directory has a pure planner header `b/iokit/Kernel/arm/sweet_fb_hw.h` and a host test
(`test/hwcomp_host_test.out`: ALL OK, 6000 random cases, 0 mismatches). Its a/ and b/ copies of the driver files are identical, so
no driver change exists. This document re-checks that entry and corrects it in 4 places (section 6).

---------------------------------------------------------------------------------------------------------------------------

## 1. (a) Display blocks the driver programs today and what they offer

### 1.1 Pipeline and what the kernel touches

| Block | PA | Programmed by us | Evidence |
|---|---|---|---|
| DSI0 (command mode, TE) | 0x1400e000 | START 0x00, INTSTA 0x0c (bit4 FRAME_DONE, bit2 TE_RDY, bit1 CMD_DONE, bit31 BUSY, bit12 underrun), CMDQ for DCS | [V-code] S/SweetFramebuffer.cpp:86-95, 881-900, 1297-1332 |
| DISP_MUTEX | 0x14117000 | EN0 (0x20) = 1 before every DSI_START | [V-code] same file:884; E:684 |
| OVL0_2L / OVL1_2L (pipe 0 / pipe 1) | 0x14001000 / 0x14101000 | **layer 0 ADDR (0xf40)** written on every double-buffer flip; INTSTA read/clear after each transfer; PITCH/SIZE read for checks; PITCH written only by the never-run n2scan probe | [V-code] :96-102, 719-764, 1072-1078 |
| OVL0 / OVL1 (4-layer) | 0x14000000 / 0x14100000 | only at boot: LK's warning layer (layer 1) turned off: SRC_CON bit1, L1 RDMA_CTRL 0xe0 = 0, DATAPATH_CON bit5 | [V-code] E:557-600 |
| RDMA0/1, COLOR, CCORR, AAL, GAMMA, DITHER | — | never touched (LK's config is kept) | [V-notes] phase-1 dump 2026-10-19 |
| larb0/larb1, M4U | — | never touched (LK leaves MMU_EN = 0, physical addressing) | [V-notes]; consistent with LK ADDRs being carve-out PAs [V-code] E:574-575 |

Layout that the early code checks before it accepts the LK state [V-code] E:573-578:
- The GUI is on OVL*_2L layer 0: SRC_CON == 1, ADDR == 0x76e10000 + pipe*2160, PITCH 4352, SRC_SIZE 0x0960021c (540x2400).
- The 4-layer OVL had only layer 1 enabled (SRC_CON == 2, the LK warning layer), and it is removed.
- So each pipe runs 2L -> 4L in cascade, with the 4L passing the 2L output through as background.
- Dual pipe: pipe 0 = panel x 0..539, pipe 1 = x 540..1079.

Device proof that per-frame layer-address writes take effect [V-dev] fbh1:
- "NORD2 FB DBL: double buffering on, second buffer at 0x77806000".
- "CADENCE ... dbl=1 preps=260 hits=260".
- 0 "overlay flip refused" (ovlSelect reads ADDR back after writing it).
- OVL INTSTA on both 2L blocks shows b0 (REG_CMT), b1 (FME_CPL), b14 (START), b15-b22, each 260 times. There is **no b2
  (FME_UND)** and no b9-b12 (RDMAn SMI underflow), so there is no underflow with one full-screen layer.
- TE period 11239 us (about 89 Hz panel); presents are paced at n2fbhz=60.

### 1.2 Register map per layer [V-code] DRM lines 58-254, 869-925, 1253-1380, 3530-3537

Stride 0x20 per layer n.

| Reg | Offset | Use |
|---|---|---|
| INTSTA | 0x08 | 0 REG_CMT, 1 FME_CPL, **2 FME_UND**, 5-8 RDMAn EOF abnormal, **9-12 RDMAn SMI underflow**, 13 abnormal SOF, 14 START |
| EN | 0x0c | 22 BYPASS_SHADOW (irrelevant here: MT6885/6893 `support_shadow = false`) |
| ROI_SIZE / ROI_BGCLR | 0x20 / 0x28 | 540x2400 per pipe; background colour |
| DATAPATH_CON | 0x24 | bit2 BGCLR_IN_SEL (cascade input), bit0 LAYER_SMI_ID_EN, bit24 GCLAST_EN |
| SRC_CON | 0x2c | bits 0-3 layer enable, bit8 force relay |
| Ln_CON | 0x30+0x20n | [7:0] plane alpha, 8 AEN, 9/10 flips, [15:12] CFMT, 23 CLRFMT_MAN, 24 BYTE_SWAP, 25 RGB_SWAP, [29:28] source (1 = constant colour / dim) |
| Ln_SRC_SIZE | 0x38+0x20n | h<<16 \| w |
| Ln_OFFSET | 0x3c+0x20n | y<<16 \| x inside the pipe's ROI (destination) |
| Ln_PITCH_MSB | 0x40+0x20n | [3:0] pitch bits 16-19 |
| Ln_PITCH | 0x44+0x20n | [15:0] pitch; the DRM driver writes the pitch only (upper blend-select bits = 0) |
| Ln_CLIP | 0x4c+0x20n | left/right/top/bottom crop, 8 bits each |
| Ln_RDMA_CTRL | 0xc0+0x20n | bit0 RDMA enable |
| Ln_ADDR | **0xf40+0x20n** | **32-bit address; there is no MSB register** (`unsigned int addr = pending->addr`, DRM:1253) |
| Extended layers EL0-2 | CON 0x330, OFFSET 0x33c, SRC_SIZE 0x338, PITCH 0x344, ADDR 0xfb0+4n | 3 more per 4L OVL |

Layers per pipe: OVL_2L has 2, OVL (4L) has 4, plus 3 extended. That gives at least 6 normal layers per pipe, against at most 4
for CoreAnimation (SweetFramebuffer SWEET_FB_LAYERS = 4) [V-code]. The 2/4 split per block follows the DRM naming (`OVL_2L`) and
the RDMA0-3 bits [V-code DRM]. That the 4L block on this board really has 4 usable layers is **[I]**.

There is no scaler in the OVL (RSZ exists, but it is not needed). The 1125x2436 canvas is cropped at offset
(fOffX, fOffY) = ((1125-1080)/2, (2436-2400)/2) = (22, 18) [V-code] SweetFramebuffer.cpp:373-374, sweet_fb_blend.h:65-81.

Source crop: ADDR = surface PA + sy*pitch + sx*4, SRC_SIZE = visible w,h. A layer that crosses x = 540 is split between the two
pipes. The right part uses ADDR + (540 - dx_left)*4 and OFFSET x = 0. This is the same split the DRM driver does
(mtk_drm_crtc.c dual pipe) [V-notes] / [I].

Formats [V-code DRM:869-925]. CoreAnimation 'BGRA' is memory bytes B,G,R,A, which is DRM_FORMAT_ARGB8888.
- Straight alpha: CFMT 2 (0x2000). LK runs the panel buffer with 0x21ff = 0x2000 | AEN | alpha 0xff, which matches this table
  [V-code E + DRM].
- **Premultiplied: 0x03803000** (CFMT 3 | CLRFMT_MAN | BYTE_SWAP | RGB_SWAP). Both local kernel trees agree. The "legacy
  0x00803000" variant named in the earlier entry is in neither tree. Use 0x038031ff (AEN + plane alpha 0xff) for the upper
  layers. Whether the OVL applies true premultiplied source-over with only these CON bits (no PITCH blend-select bits) is **[I]**.
  It is the first thing the device must prove (step H2 below).

Blend semantics of the CPU path that the HW path must reproduce [V-code] sweet_fb_blend.h:
- Layer 0 is the bottom one. The lowest covering layer is copied (its alpha ignored), every higher layer is premultiplied
  source-over, and panel pixels no layer covers are black.
- HW equivalent: ROI_BGCLR = opaque black, bottom HW layer with AEN = 0 (opaque), upper layers premultiplied with AEN = 1.

---------------------------------------------------------------------------------------------------------------------------

## 2. (b) How the OVL can read IOSurface memory

### 2.1 Facts

1. **IOSurface pages are not physically contiguous, and they sit above 4 GiB.** [V-code]
   - S/SweetSurface.cpp:936 allocates with `IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,
     kIODirectionInOut | kIOMemoryKernelUserShared, allocSize, page_size)`.
   - There is no kIOMemoryPhysicallyContiguous flag and no physical mask, so pages are wired kernel memory with page-size
     alignment only.
   - XNU's RAM is 0x1_42000000 + 3776 MiB [V-notes 9760, 10586; image names "ram3776"], all above 4 GiB.
   - The OVL ADDR is 32-bit physical while MMU_EN = 0, so **no buffer IOSurface is reachable by the display today.**
2. **Budget.** Buffer surfaces count against 2048 MiB wired (SWEET_SURFACE_WIRED_MAX) [V-code SweetSurface.cpp:83]. Row
   alignment is 64 B (SWEET_SURFACE_ROW_ALIGN), so a 1125-px BGRA surface has pitch 4544 and takes 11,069,184 B (10.56 MiB).
   The OVL pitch field is 16+4 bits, so that pitch is fine [V-code].
3. **Physical surfaces already exist.** `sweet_surface_create_physical()` uses `IOMemoryDescriptor::withPhysicalAddress` with
   write-combined mappings. It is used for the two panel buffers 0x76e10000 / 0x77806000 [V-code SweetSurface.cpp:254-275,
   SweetFramebuffer.cpp:377, 756].
4. **The GPU cannot import such memory today (correction to the earlier entry).**
   - The import path (Nord2GPU.cpp:1110-1135) takes every 4 KiB page through `getPhysicalSegment` and refuses the surface
     unless `valid_ram(pa)` holds.
   - `valid_ram` requires `gPhysBase <= pa < gPhysBase + gPhysSize`, which is XNU's RAM only (Nord2GPU.cpp:131-134).
   - So a surface in low DRAM would be refused as a Metal render target. `valid_ram` must be widened to accept a registered
     display-pool range, at all 6 call sites or at least 328/1079/1130. This is a required kernel change.
5. **Low DRAM free window.**
   - DTS mblocks: atf-ramdump ends at 0xc0000000, and the next mblock is DPM-reserved at 0xdfe70000.
   - Our own fixed claims inside that gap: CAMSV 0xD0000000 (2 x 16 MiB), vcodec/JPEG up to 0xDFC00000 / 0xDFE70000
     [V-code nord2_camsv.inc:26-33, nord2_hwcodec.inc, redwood_jpegdec.h].
   - A grep of the whole stage for 0xC/0xD addresses found no claim in **0xC0000000..0xD0000000 (256 MiB)**. The 0xc0000000
     constants found are ioctl encodings and bit masks [V-code].
   - Whether nothing else (TEE, modem, SSPM) touches that range at run time is **[I]**. It is supported by the CAMSV pool
     working at 0xD0000000 in the same gap.
6. **Precedent for an MMU-off DMA master in low DRAM:** the CAMSV camera DMA writes 0xD0000000 with its larb MMU off, mapped
   with `ml_io_map_unmappable(pa, len, VM_WIMG_DEFAULT)`, and works on the device (cam9 30 fps and later) [V-code + V-notes].
7. **Display M4U.**
   - The OVL ports are larb0 p5 OVL_RDMA0, larb0 p6 OVL_2L_RDMA1, larb1 p5 OVL_RDMA1, larb1 p6 OVL_2L_RDMA0 [V-code PORT:25-51,
     `MTK_M4U_ID(larb,port)`].
   - Per-port enable is larb + 0x380 + 4*port, bit0 MMU_EN [V-code mtk-smi.c:53-54, 185]. The IOMMU aperture is 32-bit
     (`DMA_BIT_MASK(32)`, mtk_iommu.c:412) [V-code].
   - The M4U base (0x1411a000 per the earlier entry), its page-table format under XNU, and whether bits [15:8] of NONSEC_CON
     extend physical addresses with MMU off are all **[I] / unverified**.
8. **Coherency for GPU-written pixels is already right for a DMA reader.**
   - The GPU runs with COHERENCY_NONE, and every job ends with `JS_CFG_END_FLUSH_CLEAN` [V-code nord2_gpu_hw.c:218, 543-544],
     so writes are in DRAM when the job completes.
   - Risk: CPU-written surfaces and stale dirty CPU lines (section 5).

### 2.2 Options

| Option | What | For | Against |
|---|---|---|---|
| **A. Display pool (recommended for step 1)** | Reserve 0xC0000000..0xD0000000. IOSurface create puts eligible surfaces there (BGRA, planeCount 0, at least a quarter of the screen) as physically contiguous memory (`withPhysicalAddress`, page-aligned, simple first-fit with 16 KiB granules). Anything else, or a full pool, goes through the normal IOBMD path. | No MMU work; reuses the CAMSV and panel-buffer precedents; costs no iOS RAM (outside XNU's 3776 MiB, not in the 2048 MiB wired budget) | 256 MiB = **24** full-screen surfaces (10.56 MiB each) before fragmentation; GPU import must accept the pool (2.1 item 4); eligibility has to be decided at create time |
| B. Display M4U page tables | Map scattered IOSurface pages to a 32-bit IOVA, set MMU_EN on the 4 OVL ports | Any surface, no pool limit | M4U bring-up under XNU is untested; shared with MDP/other display masters; a bad table means bus faults or a hang; TEE may own parts of the M4U [I] |
| C. Copy into the pool | — | — | Rejected: still a full CPU or DMA copy |

Budget for option A [I]:
- backboardd surfaces: 2-3 (it ping-pongs ids 2/3 [V-notes 0393]).
- The foreground app's CAMetalLayer: 3 drawables.
- Keyboard, alerts and similar: about 2.
- That is 7-9 surfaces, about 75-95 MiB, so 256 MiB leaves headroom for one app switch.
- When the pool is full, the next surface goes to normal memory, and any swap that uses it falls back to the CPU path (counted).

---------------------------------------------------------------------------------------------------------------------------

## 3. (c) Per-frame atomic update, completion, SwapWait and GPU fences

How it works now [V-code SweetFramebuffer.cpp:779-1060 and sweet_fb_present.h]:
- Present thread states: 5 = wait for the GPU (`nord2_gpu_surface_busy` on every layer, polled every 100 us, at most
  SWEET_FB_BUSY_WAIT_US = 10 ms, then **forced** = copy anyway); 1 = copy, flip, TE window; then push.
- FRAME_DONE is polled every 250 us, with a 40 ms timeout.
- Double buffering (0156): prepNext copies the next swap into the idle panel buffer during a transfer.
- SwapWait 0393: level 1, and a swap retires when its copy ended or when it was replaced unseen [V-code sweet_fb_swapwait.h].

What changes for a HW frame:
1. **Atomicity.** No shadow registers on this SoC [V-code DRM:3536]. In command mode the OVL fetches DRAM only between DSI_START
   and FRAME_DONE (it is idle otherwise, and the panel GRAM holds the picture). Writing all layer registers on the present thread
   after the previous FRAME_DONE and before MUTEX_EN/DSI_START is therefore atomic per frame. This is exactly how the 0156 flip
   already works [V-dev fbh1]. No vsync-aligned commit is needed. The TE window logic (te_lead 1 ms, push 8.9 ms within the
   11.24 ms TE period [V-notes 0393]) stays unchanged.
2. **Completion = FRAME_DONE.** After FRAME_DONE the OVL never reads the frame's surfaces again, unless the next push re-programs
   them. Under 0392 retain, layers outside a swap's mask are re-read on every push.
3. **SwapWait / retire on a HW frame.**
   - Retire "copy ended" becomes retire "FRAME_DONE of the push that showed it, and the surface is no longer in the current
     scene".
   - A surface kept by retain stays held, because the next push reads it again. Today it is treated as free, which is a latent
     race on the CPU path too.
   - Hold time grows from 1.8-3.4 ms (copy) to TE wait (0-11.2 ms) + 8.9 ms, about 9-20 ms [I].
   - backboardd with 2 surfaces at 60 Hz reuses a surface after about 33 ms, so this still fits. At 89/90 Hz presents it would
     not; keep n2fbhz=60 for the HW path.
   - Empty swaps on a HW frame push nothing: the GRAM already holds the picture.
4. **GPU fences per surface.**
   - Today the "fence" is the polled predicate `surfaceBusy` [V-code Nord2GPU.cpp:616-634]: a queued or running job with this
     surface in its write mask, or a 0112 reservation.
   - The HW path must **never force**. If any HW layer is busy after the 10 ms wait, skip the swap: the panel keeps its picture,
     and the swap retires as dropped. Do not fall back to a forced copy either, since that is the same race.
   - Better fence (proposed, [I]): at SwapEnd, snapshot each surface's last write-job sequence number. The fence is ready when
     the core's completed sequence is at least the snapshot. This stops a newer frame's job on the same surface (app ran ahead)
     from holding back an older swap, and it does not let a later write slip in unseen.
   - At FRAME_DONE, check `surfaceBusy` again on the HW layers. Busy means a GPU write overlapped the scanout: count `hw_race`,
     the source of tearing on a HW frame.
5. **The app-side guard is missing.** IOSurface use-count traps 0/1 are no-ops (`trapUseCount` returns success,
   SweetSurface.cpp). The 0x68 lock result / state page has no known in-use field ([V-notes 10209]: +0x2d "displayable" only).
   An app's CAMetalLayer can therefore reuse a drawable the display still reads, which is the measured src_race / busy_after.
   For the HW path this is the main correctness risk (section 5, R1).

---------------------------------------------------------------------------------------------------------------------------

## 4. (d) Step-by-step bring-up plan

Runtime switch: sysctl `kern.n2fbhw` (boot-arg `n2fbhw` for the default).
- 0 = CPU path only (default until H3 passes).
- 1 = HW for single-layer swaps.
- 2 = HW for 1-2 layers.
- 3 = up to 4.
- Test-pattern selectors as sysctl `kern.n2fbhwtest`.

Per-swap decision (pure function, host-tested; the existing `sweet_fb_hw.h` planner from hwcomp-20261112 is the starting point).
A swap goes to the HW path only if **all** of the following hold; any failure is counted by reason and the swap takes the
current CPU path for that frame:
- the mode allows that many layers;
- every visible layer is BGRA, planeCount 0, and fully inside the pool or the carve-out;
- every visible layer is opaque or premultiplied (CA surfaces are premultiplied);
- the geometry is representable (no scaling);
- no layer is busy (after the bounded wait);
- the display is powered on and not fading.

Automatic fallback to mode 0 for the rest of the boot, with one log line:
- any OVL INTSTA FME_UND / SMI underflow / EOF-abnormal bit after a HW frame;
- a DSI underrun (INTSTA bit12);
- a FRAME_DONE timeout;
- a register read-back mismatch;
- hw_race above a threshold.

Every switch from HW to CPU re-points 2L L0 at the panel buffer and disables the extra layers, writing them between transfers.

| Step | Kernel content | Device test (orchestrator) | Pass criterion |
|---|---|---|---|
| **H0** diagnostics only | Log IOSurface create properties (keys, size, format, pid, cacheMode) for BGRA surfaces of a quarter-screen or more; per swap, which layers would be HW-eligible if pooled. No behaviour change. | Boot, Springboard, Photos scroll | Know which surfaces CA/apps present and what decides pool eligibility |
| **H1** pool + GPU import | Reserve 0xC0000000-0xD0000000; `withPhysicalAddress` allocator for eligible surfaces (`n2fbpool=0/1`), WC kernel map, user map per cacheMode (2.1); Nord2GPU `valid_ram` accepts the pool; pool stats in "NORD2 FB PRESENT". Display still CPU path. | Same scenario + camera + video | No visual change, no GPU faults, `pool_used` / `pool_full` counters, CPU-copy timings unchanged or better (WC source reads may be slower, so measure) |
| **H2** test patterns through HW layers | `n2fbhwtest=1`: pattern surface in the pool → 2L L0 full screen (ADDR/PITCH/SRC_SIZE/OFFSET/CON per pipe). `=2`: plus 2L L1 as a 300x300 premultiplied alpha gradient crossing x=540, with CON variants 0x21ff / 0x038031ff. Restore after 2 s. | Photograph the screen | Crop and pipe seam exact; gradient matches the CPU reference blend (one variant must be exact); no FME_UND |
| **H3** one full-screen layer | `n2fbhw=1`: single-layer swaps go through HW (2L L0 = pool surface, crop (22,18)); retire at FRAME_DONE; never force; empty swap = no push | Springboard idle and swipe, Photos scroll, app switch | `hw_frames` majority, `copy_us` 0 for HW frames, hw_race ≈ 0, no FME_UND, visually identical, fps ≥ CPU path |
| **H4** two layers with alpha | `n2fbhw=2`: bottom layer opaque (AEN 0), top premultiplied; per-pipe split; ROI_BGCLR black for uncovered pixels | Photos grid (Photos drawable + backboardd UI layer, the 2-layer case of 0390) | Grid plus pills/tab bar correct; mixed-alpha edges match the CPU blend |
| **H5** up to 4 layers, retain | 4L layers 0/1 (cascade); 0392 retained layers held until replaced | Keyboard over app, alerts | Same |
| **H6** fences + use count | SwapEnd write-sequence fences; real IOSurface use count (after reversing IOSurfaceIsInUse) | Fast scroll stress | src_race/hw_race 0 |

Each step is a separate patch (0394a…), host tests first:
- the planner;
- the pool allocator;
- retire state machine extensions in `sweet_fb_swapwait.h`;
- the valid_ram range.

Template: `build_q10jfh.sh` plus the new patch dir, `txn_g.sh` for the orchestrator.

---------------------------------------------------------------------------------------------------------------------------

## 5. Risks

| # | Risk | Effect | Mitigation |
|---|---|---|---|
| R1 | No IOSurface in-use protocol (use-count traps are no-ops; IsInUse logic not reversed) | Apps can redraw a drawable the OVL is scanning, which tears within the frame. The hold is longer than with the copy (≈9-20 ms vs 2-3 ms). | hw_race counter at FRAME_DONE; auto-fallback threshold; H6 implements the use count |
| R2 | Pool too small / fragmented (24 full surfaces) | Surfaces outside the pool, so the CPU path runs more often | 16 KiB first-fit, only quarter-screen or larger BGRA surfaces, counters; later: the M4U (option B) |
| R3 | Premultiplied CON encoding not proven (blend-select bits unknown) | Wrong edges / halos on 2-layer frames | H2 gradient test before H4; straight-alpha fallback for opaque/clear-only layers |
| R4 | GPU import of non-XNU-RAM pages (Mali MMU attributes, `valid_ram`) | GPU faults on pool surfaces | H1 runs the GPU on the pool with the CPU display path before any HW frame |
| R5 | CPU-written pool surfaces with cacheable user mappings, or stale dirty lines evicted over GPU data | Old or garbled pixels on the panel (the display does not snoop) | WC user mapping for pool surfaces (cost: slow CPU readback for apps), or DC CVAC at SwapEnd; decide from H0 cacheMode data |
| R6 | Bandwidth with 2-4 full-screen layers (≈20-40 MB per 8.9 ms burst) and LK's GMC/FIFO/ultra settings tuned for one layer | OVL underflow, artifacts | INTSTA underflow bits already collected → automatic fallback; RDMA GMC tuning later |
| R7 | Panic / console drawing goes to panel buffer 0 via sweet_early.c (not rebuilt per image) and pushes whatever OVL state is current | Panic text invisible while a HW frame is shown (today already when buffer B is front) | Logs still reach pmsg; option: restore OVL state from a stage-side panic callout if one exists [I] |
| R8 | Power-off fade halves pixels in panel memory | No effect on a HW frame | On power-off: copy the last scene by CPU once, or show black via ROI_BGCLR with layers off |
| R9 | 0xC0000000-0xD0000000 used by firmware at run time | Corruption / hang | Same gap as the working CAMSV pool; H1 pattern-check the pool at boot and at intervals |
| R10 | Retire at FRAME_DONE plus higher present rates | CA skips more frames | Keep n2fbhz=60 for HW; measure SwapWait not_ready |
| R11 | Display M4U accidentally enabled by firmware for OVL ports | Physical ADDRs misread | Read NONSEC_CON of the 4 ports at H1 and refuse the HW path unless MMU_EN = 0 |

Expected gain [I]: per HW present the CPU writes about 20 registers (well under 50 us) instead of 1.8-3.4 ms of copy (50 ms worst
under load), and 11 MB of WC writes plus 11-22 MB of reads disappear. The panel link (8.9 ms push, TE) remains the ceiling. The
copy-induced late pushes and the source races during the copy go away for HW frames; the race during scanout remains until R1 is
closed.

---------------------------------------------------------------------------------------------------------------------------

## 6. Corrections to the earlier "PHASE 1 ANALYSIS" entry (NOTES ~21:40)

1. "GPU import uses getPhysicalSegment -> works for them" is **wrong**: `valid_ram()` refuses pages outside XNU's RAM, so pool
   surfaces need a Nord2GPU change (Nord2GPU.cpp:131-134, 1130). This also means the panel buffers have never been GPU-importable.
2. The premultiplied CON value conflict does not exist in the local sources: both trees give 0x03803000; 0x00803000 was not found.
3. The pool holds 24 surfaces (pitch 4544), not about 22.
4. OVL port ↔ larb mapping is now verified from the larb-port header (was stated without source).

---------------------------------------------------------------------------------------------------------------------------

## 7. Interfaces (track D keeps this current; last update 2026-11-12 ~22:00)

Paths below are relative to `gpu/build/`. H = `hwcomp-20261112/`.

### 7.1 Patches and their layout

| Patch | Track | Directory | Files (owner) | Status |
|---|---|---|---|---|
| 0394 | A | H/pool-0394/ | SweetSurface.cpp, sweet_fb_hw_pool.h, sweet_fb_hw_pool_alloc.h | delivered; preflight OK on q10jfh and q10jfg+0393; q10jfi+0394 built, IMAGECHECK_OK (WARN_BUDGET=2, see 8.2) |
| 0395 | B | H/<track dir>-0395/ | Nord2GPU.cpp, nord2_gpu_fence.h | not delivered yet |
| 0396 | C | H/<track dir>-0396/ | SweetFramebuffer.cpp (+ sweet_fb_hw.h, sweet_fb_swapwait.h if needed) | not delivered yet |

Each directory has `NNNN-*.patch`, `patched-sources-NNNN/` (every file the patch touches, in its patched form), and a/ b/ make_patch.sh.
- build_q10jfi.sh finds the patches at depth <= 3 under H. There must be exactly one file per number.
- **a/ = the q10jfh stage** (`device-mmu-q10jfh-candidate/k/kernel/stage`). Because the files are disjoint, any subset applies in
  any order.
- A track that has to touch another track's file must make its a/ from base + the lower patches. The preflight then checks this:
  patched-sources-N must equal base + patches <= N.
- build_p1_candidate.py checks SweetSurface.cpp, Nord2GPU.cpp, SweetFramebuffer.cpp, sweet_fb_swapwait.h, sweet_fb_present.h and
  others against the **highest patched-sources-NNNN <= the last series number that contains the file**. A missing patched-sources
  entry therefore fails the kernel build.
- Shared contract headers live at the root of H:
  - `sweet_fb_hw_pool.h` (A): POOL_BASE 0xC0000000, SIZE 256 MiB, GRANULE 64 KiB, cache modes NONE/WC/COPYBACK, contains().
  - `nord2_gpu_fence.h` (B): n2gf_* write-sequence table, 64 slots, waits in 100 us steps up to 100 ms.
  - `b/iokit/Kernel/arm/sweet_fb_hw.h` (C): the planner.

### 7.2 Sysctls (the runtime switch; no reflash needed)

| Name | Patch | Type | Default | Contract |
|---|---|---|---|---|
| kern.n2fbhw_pool | 0394 | int RW (root) | 0 (boot-arg `n2fbhw_pool`) | 1 = eligible BGRA surfaces come from the pool. It only affects surfaces created **after** the switch. Read-only stats: `kern.n2fbhw_pool_live`, `_used_kib`, `_full`, `_foreign`. |
| kern.n2fbhw | 0396 | **int RW (root), 0..3** | 0 | 0 = CPU path, 1 = 1 layer, 2 = 1-2 layers, 3 = up to 4. Switched between frames. **A read returns the effective mode**: 0 after an auto-fallback. A write after a fallback may be refused (EPERM) or re-arm; either way the kernel logs a `HW MODE` line (7.3). |

Boot-arg budget (limit 607 chars):
- q10jfi's line is 595 chars (12 free, measured) and q10jfj's is 603 (4 free). Neither `n2fbhw_pool=1` (14 chars with the space) nor
  `n2fbhw=N` fits in q10jfj, and the pool boot-arg does not fit in q10jfi either.
- So modes are set by the fbhwctl launchd job (7.4). It writes kern.n2fbhw_pool first, at job start, and kern.n2fbhw after its delay.
- **Open item for track A:** backboardd's boot-time surfaces predate the job, so they stay outside the pool unless the pool is on at
  boot. Proposal: compile default 1 in 0394 (H1 says no visual change on the CPU path), or a short alias boot-arg such as `n2fp=1`
  (7 chars with the space; fits q10jfi only).

### 7.3 Console lines that the verdict parser reads (H/phone/fbhw_verdict.py)

Existing:
- `NORD2 FB SYNC:` (0393) is cumulative since boot.
- `NORD2 FB PRESENT:` and `NORD2 FB OVL:` are per stats interval. The OVL bits counted are b2 FME_UND, b5-8 EOF abnormal and
  b9-12 SMI underflow, plus `dsi_underrun`.
- `NORD2 FB TEAR:`, `RACE:` and `LONG:` are counted per mode.

**Required from 0396** (track C). Extra keys are allowed; they go into verdict.json.
- `NORD2 FB HW: mode=%u hw_frames=%llu cpu_frames=%llu hw_race=%llu ovl_und=%llu smi_und=%llu eof_abn=%llu dsi_und=%llu fd_timeout=%llu readback_bad=%llu fallback=%u ref_<reason>=%llu ...`
  - Cumulative since boot.
  - Printed in the stats block **before** `NORD2 FB PRESENT`, so the line sits at the start of the block it describes.
- `NORD2 FB HW MODE: old=%u new=%u why=sysctl|fallback|boot [reason]` at every change.
- `NORD2 FB HW FALLBACK: reason=... ` once, when the kernel drops to mode 0. It may be the same event as a `why=fallback` HW MODE line.

How the parser judges:
- `ovl_und`, `smi_und`, `eof_abn`, `dsi_und`, `fd_timeout` or `readback_bad` > 0 in a mode = FAIL.
- An OVL underflow bit, a fallback, or `hw_frames` == 0 in a mode >= 1 = FAIL.
- `hw_race`, `src_race`, `busy_after`, `copy_mismatch` or `busy_forced` > 0 = WARN.
- A tear rate (tear_none_te + tear_ambig per 1000 pushes) above 1.5 x the mode 0 rate + 2 = WARN.

`N2FBHWCTL` lines from the job:
- `start ...`, `pool set=V rc=E readback=R t=S`, `boot mode=M`
- `phase I/N mode=M set rc=E readback=R t=S`
- `phase_end I/N mode=M readback=R t=S [FALLBACK]`
- `done final=M ...`, `error ...`

The parser cuts the per-mode segments at these lines. Without the job, it cuts at `HW MODE` lines or at changes of the HW line's `mode=`.

**Screenshot caveat (request to track C).** `phone.py screenshot` (AppleCLCD::snap) copies the panel buffer. In modes >= 1 that
buffer is not what the OVL shows. Two options for snap() in HW mode:
- compose the current plan with sweet_fb_blend into the reply; or
- set a flag in the picture status, so the host knows the picture is not the screen.

### 7.4 The mode job (fbhwctl)

- `/usr/local/libexec/n2fbhwctl` is built from H/fbhwctl/n2fbhwctl.c. It is freestanding, ad-hoc signed, links libSystem only, and
  uses sysctlbyname.
- It runs as launchd job `com.nord2.fbhwctl` (RunAtLoad, root) with `--config /private/var/db/nord2-fbhw-mode`.
- The config is one line, e.g. `modes=0,1,2,3 period=120 delay=90 pool=1 final=0`, and is also the gate: no file = the job exits.
  `period=0` means: set the first mode and exit.
- Installed by `apps-20261104/guest-fbhwctl.sh`. Every payload uses this same guest; the config comes from `plan.json`.
- Removed by `guest-fbhwctl-undo.sh`. The catalog check uses `owned-fbhwctl.json` (owned-gnssd2.json + 3 paths).
- Installing over an existing install rewrites only the config. The rehearsal checks this.

---------------------------------------------------------------------------------------------------------------------------

## 8. Build/test (track D)

### 8.1 Host verification

    gpu/build/hwcomp-20261112/run_all_tests.sh [--rehearse] [--preflight] [-k PATTERN]

The runner:
- finds every track test: `{fbblend,fbretain,fbsync,hwcomp}-20261112/test` and `hwcomp-20261112/**/test`. A `run_tests.sh` in a
  directory is that directory's test; otherwise each .c/.cpp is one test;
- compiles C tests with `-O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined`, with leak checks off;
- includes the test's own `cc ...` recipe flags first, then the track b/ dirs, then the q10jfh stage;
- also runs track D's own tests (verdict parser on synthetic logs and on the real fbh1 log, the mode daemon's host test and iOS
  link, a dry run of the scenario, and a syntax check of the build scripts);
- with `--rehearse`, runs the payload rehearsal; with `--preflight`, runs PREFLIGHT_ONLY builds with every 0394-0399 patch present;
- exits 1 with a FAILED list.

2026-11-12 ~22:00: ALL HOST TESTS OK (12 with --rehearse): fbblend, fbscene, fbsync, hwcomp planner, pool (0394), and track D's own.

### 8.2 Kernel images

    PATCHES="0394 0395 0396" PREFLIGHT_ONLY=1 gpu/build/build_q10jfi.sh      # seconds: dry apply + patched-sources check
    PATCHES="0394 0395 0396" gpu/build/build_q10jfi.sh                       # q10jfi = q10jfh + list
    PATCHES="0394 0395 0396" gpu/build/build_q10jfj.sh                       # q10jfj = q10jfg (GNSS) + 0393 + list (final phone image)

Options:
- `PATCHES` may be a subset, e.g. `"0394"`, or `""` (the base recipe).
- Other knobs: `NOPACK=1`, `WARN_BUDGET=0`, `EXTRA_ARGS=` (checked against 607), `SPARE_WARN=8192`.
- The default output is `device-mmu-<name>-candidate`. It must not exist.

Checks:
- **Apply cleanliness:** no fuzz, offset, failed or reversed hunks, both in the preflight and in every real apply-NNNN.log.
- **Patched files:** every file of each patch is reported patched.
- **Sources:** the final stage equals the newest patched-sources holding each file.
- **Warnings:** total and own-source warnings per compile log are no more than the reference build (q10jfh for jfi, q10jfg for jfj)
  plus `WARN_BUDGET`.
- **Image:**
  - check.log `FAILURES: none`;
  - boot_spare > 0;
  - boot-args = the reference's minus keepsyms=1 (the packer strips symbols), plus `EXTRA_ARGS`, and <= 607 chars;
  - AE2 audio-q10jae.pkg 94fb2f9b present;
  - the 0392 and 0393 strings in the kernel, plus each listed patch's first `"NORD2..."` literal (GNSS: the S11 string).
- The script ends with `done IMAGECHECK_OK`.

Series order for q10jfj: base, then GNSS 0350,0351,0353..0359,0362..0364, then 0390..0393, then the HW list **last**.

Warning budget details:
- Exempt, counted and printed: warnings from `sys/sysctl.h` and warnings at stage lines that begin with `SYSCTL_`. A SYSCTL_* macro
  in C++ brings about 4 warnings (reserved `sysctl__` identifiers, the missing `oid_link` initializer, the startup cast). The first
  include of sys/sysctl.h brings about 16. `SYSCTL_EXEMPT=0` counts them again.
- Everything else counts. On failure the build lists only the warnings the reference does not have.
- **First real run (q10jfi with 0394 only, 2026-11-12 ~22:05):** 0394 adds 35 exempt warnings and **2 real ones**:
  `-Wcast-align` from `volatile uint8_t *` to `volatile uint64_t *` at SweetSurface.cpp:242 and :264 (the canary code).
  - **Request to track A:** use aligned `volatile uint64_t *` arithmetic from the start, or memcpy, so the budget is +0 again.
  - Until then, build with `WARN_BUDGET=2`.
  - Track C will see the same SYSCTL exemption for kern.n2fbhw.

### 8.3 boot_spare budget (`hwcomp-20261112/spare_table.py`, from the receipts; +-2048 B per entry, compressed image)

| Patch(es) | Cost B | Patch(es) | Cost B |
|---|---|---|---|
| 0350+0351 GNSS core/bind | 2048 | 0362 datalink v9 | 2048 |
| 0353 safe steps | 2048 | 0363 split ladders **(ladder)** | 2048 |
| 0354 one-access steps (probe ioctl) | 0 | 0364 DSP boot | 4096 |
| 0355 safe3 | 0 | 0390 fb blend | 2048 |
| 0356 power ladder **(ladder)** | 6144 | 0391 fb inval diag (diag) | 2048 |
| 0357 ladder L1 **(ladder)** | 0 | 0392 fb retain | 0 |
| 0358 L1 probe **(probe ioctl)** | 2048 | 0393 fb sync | 4096 |
| 0359 datalink ladder **(ladder)** | 4096 | 0394 display pool | **4096** (q10jfi+0394 spare 69632) |

- GNSS set: 24576 B. Of that, the bring-up ladders and probes (0354, 0356-0359, 0363) are 14336 B.
- q10jfh spare is 73728. q10jfg is 53248. The q10jfj base forecast (q10jfg - 0393) is 49152.
- So 0394-0396 together may cost up to about 65 KB (q10jfi) or 40 KB (q10jfj) before the spare drops under 8 KiB.
- 0394 measured 4096 B. 0395 and 0396 are still to be built: forecast q10jfj spare = 45056 - cost(0395, 0396).
- If q10jfj runs short, trim in this order: the GNSS ladders 0356/0359/0363 and the probe 0358 (up to 14 KB), then 0391.
- The build prints `BUDGET WARNING` with this list when the spare is under `SPARE_WARN`.

### 8.4 Phone test (orchestrator; about 4 min of transaction plus the boot plus the run)

1. `apps-20261104/rehearse-fbhwctl.sh` must print REHEARSAL_OK. Rerun it after `hwcomp-20261112/fbhwctl/make_payloads.sh`.
2. One boot that compares all modes:

       hwcomp-20261112/phone/run_fbhw_txn.sh <q10jfi|q10jfj .img> fbhw1 payload-fbhwcycle

   - This runs `autocycle/txn_g.sh` with PAYLOAD/GUEST/OWNED set and READS=900, then starts
     `phone/fbhw_scenario.py --capture runs/fbhw1/ui-usb/xnu-live.log --cycle` once `NORD2 FB PRESENT` appears.
   - The schedule is a 90 s settle, then 120 s each of modes 0, 1, 2 and 3, then mode 0.
   - The scenario repeats home page swipes and Spotlight, Photos (icon 150,850; Library grid scrolled 4x down and 4x up; Albums),
     Control Center, and the app switcher with home-bar switches, until `N2FBHWCTL done`.
   - It writes `runs/fbhw1/fbhw/{steps.jsonl,verdict.txt,verdict.json}` and `verdict-final.txt`.
   - The table has, per mode: interactions, pushes, presents, hw/cpu frames, hw%, tear/1k, src_race, busy_after, hw_race,
     underflow (b2/smi/eof/dsi), fallbacks, average copy time, and PASS/WARN/FAIL with reasons.
3. Fixed mode for a longer soak: `run_fbhw_txn.sh IMG fbhwm2 payload-fbhwmode2` (DURATION=300). Mode 0 = `payload-fbhwmode0`, with pool 0.
4. Undo: `run_fbhw_txn.sh IMG fbhwu payload-fbhwctl-undo`, or any later transaction with `guest-fbhwctl-undo.sh`.
5. Without a transaction, when the phone is already booted with the job installed and no console reader is running:
   `fbhw_scenario.py OUT --duration 300`. It pulls with `phone.py logs`, which rewinds the buffer, so never run it alongside
   nord2-usb-console.py.
6. Re-judge any log later: `phone/fbhw_verdict.py LOG [--steps steps.jsonl] [--mode N]`.

---------------------------------------------------------------------------------------------------------------------------

## 9. Display pool interface details (TRACK A; summary in section 7, which track D maintains)

Owner of `sweet_fb_hw_pool.h` and `SweetSurface.cpp`. Patch 0394 is made against the q10jfh stage.

Header `gpu/build/hwcomp-20261112/sweet_fb_hw_pool.h` (master). Patch 0394 installs a byte-identical copy as
`iokit/Kernel/arm/sweet_fb_hw_pool.h`, so include it as `#include "sweet_fb_hw_pool.h"` from iokit/Kernel/arm. It is pure C/C++
(stdint/stdbool only), so bsd/dev C files can include it by a relative path.

| Symbol | Meaning |
|---|---|
| `SWEET_FB_HW_POOL_BASE 0xC0000000ull`, `SWEET_FB_HW_POOL_SIZE 0x10000000ull` | pool PA range (256 MiB, below 4 GiB) |
| `SWEET_FB_HW_POOL_GRANULE 0x10000ull` | allocation unit/alignment: every pool surface PA is 64 KiB aligned |
| `static inline bool sweet_fb_hw_pool_contains(uint64_t pa, uint64_t len)` | `[pa, pa+len)` inside the pool and `len > 0`; a pure range check (TRACK B: use it to widen Nord2GPU `valid_ram`) |
| `uint64_t sweet_surface_pool_pa(uint32_t id)` | PA of pixel (0,0) = allocation base (pool surfaces always have IOSurfaceOffset 0); 0 = not a pool surface / no such id |
| `uint32_t sweet_surface_pool_stride(uint32_t id)` | bytesPerRow (multiple of 64, < 1 MiB = OVL pitch range) |
| `uint64_t sweet_surface_pool_size(uint32_t id)` | bytes of the contiguous allocation (round_page(stride*height) or more) |
| `uint32_t sweet_surface_pool_cache(uint32_t id)` | `SWEET_FB_HW_POOL_CACHE_WC` (1, default) / `_COPYBACK` (2) / `_NONE` (0) = attribute of every CPU mapping |
| `..._pa_obj / _stride_obj / _size_obj / _cache_obj(const void *SweetSurface)` | same for a surface object the caller already holds. **Prefer these** when you keep the object: an id can be reused after the surface left the table while a kernel holder still retains the old object |
| `SweetSurface *sweet_surface_create_pool(uint32_t w, uint32_t h)` (C++) | kernel-made zeroed 'BGRA' pool surface (H2 test patterns), published in the id table, returned retained; ignores kern.n2fbhw_pool; NULL when full |

Rules for the other tracks:
- **Lifetime:** a pool range goes back to the pool in `SweetSurface::free()` (last retain dropped). It is not freed if someone
  else still retains the surface's IOMemoryDescriptor (then it is counted as `leaked`). Whoever lets the OVL scan a surface
  (DSI_START..FRAME_DONE) or a GPU job use it must hold a retain on the SweetSurface for that time.
- **Cache (default WC):** the CPU never has dirty lines over pool memory, so neither the OVL nor the GPU needs CPU cache
  maintenance for it. `nord2_gpu_surface_cpu_acquire/release` stay harmless (DC CIVAC on a WC VA is a no-op on the data). With
  n2fbpoolc=1 (copyback) the normal IOSurface rules apply (unlock cleans, readers after a GPU write invalidate) and TRACK C must
  clean before scanout of CPU-written surfaces. The pool code itself always cleans after zeroing/copy-in.
- **Kernel VA:** `SweetSurface::kernelAddress()` returns the pool's own kernel mapping for pool surfaces (same attribute as the user
  mappings), so the GPU import's `pages[i].cpu` and the framebuffer CPU copy work unchanged. With WC, CPU reads of pool surfaces
  (the CPU display path at H1) are uncached: measure copy_us at H1.
- **GPU import (TRACK B):** pages are physically contiguous at `pool_pa`, `pool_size` bytes, `getPhysicalSegment` works on the
  descriptor as for the panel buffers; only `valid_ram()` has to accept `sweet_fb_hw_pool_contains(pa, 4096)`.
- Eligibility at create time (pure function `sweet_fb_pool_eligible`, `sweet_fb_hw_pool_alloc.h`): 'BGRA' (0x42475241), 4-byte
  1x1 elements, planeCount 0, width*height >= 1000*2000, bytesPerRow % 64 == 0 and >= width*4 and < 1 MiB, IOSurfaceOffset 0,
  allocation <= 256 MiB, and `kern.n2fbhw_pool == 1`. Anything else, or a full pool, gets normal IOBMD memory (as in q10jfh).

---------------------------------------------------------------------------------------------------------------------------

## 10. Display pool: memory map, mechanism, patch 0394 (TRACK A, 2026-11-12 ~22:30, host only, NOT device-tested)

### 10.1 Memory map check

| Range | Owner | Evidence | Tag |
|---|---|---|---|
| 0x1_42000000 + 0xEC000000 (3776 MiB) | XNU RAM (gPhysBase/gPhysSize) | loader refuses any other payload window (shim/nord2-xnu/loader.c:593-596, defaults :50-55; loader3776 disasm compares 0x1_42000000 / 0xEC000000); packer ADT dram-size 0xec000000 (pack_with_args_nosyms.py:44-47); arm_vm_init.c:1881/1896; no maxmem boot-arg | V-code |
| 0xBFF80000 + 0x80000 | atf-ramdump (no-map), ends exactly at 0xC0000000 | live.dts:1028-1032 | V-code |
| **0xC0000000 + 0x10000000** | **in the DTS: Linux `ssmr-reserved-cma_memory` (shared-dma-pool, reusable, NOT no-map, `alloc-range` [sic]) for `ssmr { compatible = "mediatek,trusted_mem" }`**; LK's mblock list has it as its own free-RAM entry #16 | live.dts:1160-1166, 8491-8494; mblock_info decode | V-code |
| 0xD0000000..0xD8000000 | camera buffers (CAMSV 2 x 16 MiB, camdev) | nord2_camsv.inc:32-33, nord2_camdev.inc:103 | V-code |
| 0xD8000000..0xDFC00000 | JPEG, jpegdec (+guard) | redwood_vcodec.h:113-114, redwood_jpegdec.h:18-26 | V-code |
| 0xDFE00000 + 64 KiB | GNSS rings, only in a gnss-dl3 tree, not in q10jfh | gnss-dl3-20261112/b/bsd/dev/sweet_touch.c:1043 | V-code |
| 0xDFE70000 + 0x190000 | DPM (no-map) | live.dts:918-922 | V-code |

- Nothing in the q10jfh stage, sweet_early.c, the loader or hwcodec kernel/kern references 0xC0000000..0xD0000000. The only
  0xC... constants are SMC ids, ioctl encodings and magic values. The early identity map does not cover 32 MiB block 96. [V-code]
- The loader writes only pmsg and the XNU window. [V-code]
- So there is **nothing to take out of XNU's RAM map**: no loader/ADT/boot-arg change.
- SSMR: under Linux the trusted_mem driver hands this CMA region to the TEE on demand. Under XNU nobody issues that request, so no
  EMI MPU protection should be armed on it. **[I]**. Mitigation: the pool init writes and reads back a canary in all 4096 granules
  and disables the pool for the boot on any mismatch.
- The existing comments "live.dts has no carve-out in 0xC0000000..0xDFE70000" (nord2_camsv.inc:26, nord2_camdev.inc:33,
  redwood_vcodec.h:31) miss the SSMR node. They were left unchanged (not track A files, and changing them costs nothing at run time).
- `nord2_dvfs.h` `busy[]` (DVFS share placement check only) does not list the pool or the camera/JPEG claims. It was left as is:
  the share is fixed at 0x70000000.

### 10.2 Mechanism (0394)

- **Reservation** = compile-time constants + `_Static_assert` in SweetSurface.cpp: base >= 0xC0000000 (atf-ramdump), end <=
  0xD0000000 (camera), end <= 4 GiB (OVL 32-bit ADDR), granule alignment.
- **Kernel mapping:** one mapping of the whole pool at first use, `ml_io_map_unmappable(0xC0000000, 256 MiB, VM_WIMG_WCOMB)` or
  `VM_WIMG_COPYBACK` (0x2, == VM_WIMG_DEFAULT, the CAMSV precedent).
  - It must not be `ml_io_map_wcomb`: arm's io_submap is 8 MiB with KMA_NOFAIL (osfmk/arm/io_map.c:65, 120-125), so 256 MiB
    would panic. [V-code]
- **Surface memory:** `IOMemoryDescriptor::withPhysicalAddress(pa, allocSize)`, the same as the panel buffers. User mappings go
  through the unchanged addRef path with `mapCache` = kIOMapWriteCombineCache (or kIOMapCopybackCache), never kIOMapDefaultCache.
  - Reason: for a PA outside XNU's RAM, kIOMapDefaultCache resolves via IODefaultCacheBits -> pmap_cache_attributes to VM_WIMG_IO
    = Device memory (iokit_rpc.c:481, pmap.c:10076-10078, IOMemoryDescriptor.cpp:550-553). [V-code]
  - On Device memory, user memcpy (unaligned accesses / DC ZVA) would fault. [I, standard arm64]
- **IOSurfaceLock** keeps working: lock/unlock only touch the state page and the cpu_acquire/release hooks. The pixel mapping is
  created at create/lookup as for every surface.
- **Cache decision: write-combined by default** (boot-arg n2fbpoolc=1 = write-back). Reasons:
  - The OVL does not snoop and the GPU runs COHERENCY_NONE.
  - WC leaves no dirty CPU line that could be evicted over GPU/OVL data (R5), and needs no maintenance at SwapEnd.
  - All aliases (pool kernel map, user maps) have the same attribute, and XNU's physmap has no alias of the pool (not RAM).
  - Cost: uncached CPU reads (CPU display path and app readbacks of pool surfaces). Measure at H1.
- **Allocator** (`sweet_fb_hw_pool_alloc.h`, pure):
  - 4096-bit bitmap of 64 KiB granules, first fit, under an IOSimpleLock.
  - 24 x 1125x2436 surfaces fit (169 granules each). Other sizes also fit, with no slot rounding.
  - Bad or double frees change nothing and are counted.
- **Zeroing:** every allocation is zeroed (bzero through the pool map, then DSB or clean). An `IOSurfaceAddress` snapshot is
  copied in for pool surfaces too.
- **Canary:** 16 bytes at the head of every free granule, written at init and on free, checked at alloc. A mismatch counts as
  `foreign` and is logged with the PA (R9).
- **Fallback:** pool off, ineligible, full, or a descriptor that cannot be created all use the q10jfh IOBMD path unchanged. Pool
  surfaces are not counted in the 2048 MiB wired budget.
- **Free:** in `SweetSurface::free()`, after the kernel map and descriptor references are released. The device pager keeps only a
  raw `dp.memory` pointer, not a retain (IOMemoryDescriptor.cpp:227, 750, 2360-2362) [V-code], so the retain count is 1 by then.
  If it is above 1, the range is kept out of the pool (`leaked`).
- **Controls:**
  - sysctl `kern.n2fbhw_pool` (rw, default 0; boot-arg `n2fbhw_pool` or the short alias **`n2fp=1`** (7 chars). This answers
    track D's open item 7.2: backboardd's boot-time surfaces can be pooled where the command line has room. The compile
    default stays 0, because without track B's valid_ram change the GPU refuses pool render targets).
  - Read-only `kern.n2fbhw_pool_live`, `_used_kib`, `_full`, `_foreign`.
  - boot-arg `n2fbpoolc` (0 WC / 1 write-back, fixed once per boot).
- **Logs** `NORD2 FB POOL:`
  - root start (state of the switches);
  - first use (pa, size, mapping attribute, canary readback "N bad of 4096", ready/disabled);
  - `create`/`kernel` allocations (first 32, then every 64th): id, geometry, pa, live, used;
  - `free` (same rate, REFUSED if not allocated);
  - `full ... -> normal memory` (first 8, then every 100th);
  - `foreign write`, `kept out of the pool`;
  - one stats line in every SWEET SURF refusal report: per-reason creation counts, where `eligible-but-off` = would have been
    pooled, which is H0 data even with the pool off.

### 10.3 Verification

- Host test `gpu/build/hwcomp-20261112/test/pool_host_test.c` runs the exact kernel header. Result: **ALL OK**, also under
  ASan/UBSan, and the header compiles as C++. It checks:
  - contract constants and contains() edges;
  - 17 eligibility cases;
  - 24 full-screen surfaces fit and the 25th fails; first-fit placement and reuse;
  - 5 kinds of bad free and a double free;
  - 200k random alloc/free steps against a per-granule reference model (placement, no overlap, < 4 GiB, counters);
  - canaries;
  - a 40-surface create life cycle: 24 pooled, 16 fall back.
- Kernel build (`pool-0394/build_q10jfpa.sh` = build_q10jfh.sh + 0394):
  - KERNEL OK, SweetSurface.cpp compiles; the new warnings are only SYSCTL-macro noise and cast-align on the aligned canary
    pointers.
  - The sysctl entries are in `__BOOTDATA,__init_entry_set` like kern.nbuf.
  - First full build (0394 without the `n2fp` alias line): image sha256
    e37e2317df489dd483eb23dd57667366dd4d12d96e436393fc5b1fc1ec897beb, FAILURES none, command line == q10jfh.
    **boot_spare 71680 (q10jfh 73728): 0394 costs 2048 bytes** (unstripped kernel +4520 bytes). That image was deleted
    when rebuilding with the final patch.
  - Final patch: KERNEL OK, kernel `device-mmu-q10jfpa-candidate/k/kernel/kernel.development.nord2-metal-q10jfpa` sha256
    b9cb877b2d0994b3cee7d915c8dc30d9dd567b5365b2fefc401d600132da0842, the same size as the first build (22,795,392 bytes).
    The image pack failed: zopfli's gzip output was truncated while another agent packed q10jfi-0394 at the same time, with
    the CPU hot (`ui-sched-PACKFAILED-zopfli/`). It was not retried, because of the thermal limits. Track D's q10jfi-0394 image
    gives the final spare number.
- The accessor functions are in SweetSurface.cpo (T, C linkage) but dropped from the linked kernel while unreferenced (the link
  localizes globals). They will be kept once TRACK B/C reference them. Check that in the combined build.
- Patch: `gpu/build/hwcomp-20261112/pool-0394/0394-nord2-fb-hw-pool.patch`, sha256
  321cc0371d60d1ee9006ca088f422387d7816a2aa4e009c2fd54cf695273347c. Files: SweetSurface.cpp, new sweet_fb_hw_pool.h, new
  sweet_fb_hw_pool_alloc.h; `patched-sources-0394/`; `make_patch.sh` (self-check: applies to a/ and equals b/, header ==
  master).
- **Not verified, device only (H1):**
  - that 0xC0000000..0xD0000000 is writable and unused at run time (canary line at first use);
  - that WC/copyback user mappings of a non-RAM PA behave in apps;
  - the speed of the CPU path on WC sources;
  - GPU import (TRACK B).
- Device test idea (orchestrator): q10jfpa with boot-arg `n2fbhw_pool=1`, or `sysctl -w kern.n2fbhw_pool=1` early. Run
  Springboard, Photos, app switch. grep `NORD2 FB POOL`. Pass criteria:
  - "canary readback 0 bad ... ready";
  - allocations at 0xc...;
  - no `foreign`, no `REFUSED`, no `kept out`;
  - live/used return to the baseline after app exit;
  - no visual change, as long as the GPU accepts the pool surfaces. **Until TRACK B's valid_ram change is in, Metal render
    targets in the pool are refused by the GPU import, so keep n2fbhw_pool=0 on images without it.**

---------------------------------------------------------------------------------------------------------------------------

## 7. Implementation, Track C: patch 0396 (OVL programming, planner, modes, fallback), 2026-11-12 ~21:55

Host-only. It compiles with the CI flags and the host test passes. **It has never run on the device.** Work is split into 0394 (A:
pool, SweetSurface.cpp), 0395 (B: GPU import of pool pages + write fences, Nord2GPU.cpp), 0396 (C, this section) and D (build
q10jfi = q10jfh + 0394 + 0395 + 0396, plus the phone harness). Directory: `gpu/build/hwfb-20261112`. Patch
`0396-nord2-fb-hw-overlay-compose.patch` (sha256 1a43aa6a01beb726..., `make_patch.sh` re-applies it to a/ and compares with b/).

Files:
- `iokit/Kernel/arm/sweet_fb_hw.h` (new, pure). It reviews and extends the a647a03b planner:
  - plan per pipe (540 columns, layer split at x = 540, ADDR = pa + offset + sy*pitch + sx*4);
  - reachability (pool 0xC0000000+256 MiB or the carve-out, everything below 4 GiB);
  - register lists for apply and restore-to-base, with read-back masks;
  - H2 pattern and reference strips;
  - fault / race / fence policy.
- `SweetFramebuffer.cpp`: everything else (below). Not touched: SweetSurface.cpp and Nord2GPU.cpp.
- Interfaces used: `sweet_surface_pool_pa_obj/_size_obj/_cache_obj` (0394) and `nord2_gpu_surface_write_seq/_write_pending` (0395).
  The real headers are included through `__has_include` (both are installed in iokit/Kernel/arm). Without them, matching
  declarations are used. The object needs those 5 symbols at link time, so 0396 never links without 0394 and 0395.

### 7.1 Runtime control (sysctl only; the 607-character boot-args are untouched)

| sysctl | values |
|---|---|
| `kern.n2fbhw` | **0** (default) CPU path. 1 = dry run: every swap is planned and counted, the first 8 plans are logged, no register is written. 2 = **H3**, one full-screen layer through the OVL. 3 = **H4**, up to two layers (bottom opaque, upper with per-pixel alpha). |
| `kern.n2fbhwpm` | upper-layer CON: 0 = straight 0x21ff, 1 = "legacy" 0x008031ff, **2** (default) = DRM premultiplied 0x038031ff |
| `kern.n2fbhwtest` | write 1..5. 1 = pattern only. 2/3/4 = pattern + gradient tile across the seam with pm 0/2/1. 5 = 1,2,3,4 in a row. Each step is held, then the previous picture is put back. |
| `kern.n2fbhwhold` | hold per test step, ms (default 3000, 100..20000) |
| `kern.n2fbhwstat` | read only: the counters of the "NORD2 FB HW: mode=..." line |

Pool surfaces only exist after `sysctl -w kern.n2fbhw_pool=1` (0394). Surfaces made before that stay in ordinary memory: why[memory].
So restart backboardd (or the app) after enabling the pool. That is Track A/D's procedure.

### 7.2 Per frame (present thread)

- **Every frame, any mode**: the planner counts would_single / would_dual and the reasons (H0 data at no cost).
- **H3/H4 frame**:
  1. All visible layers must be pool surfaces with write-combined CPU mappings, looked up by the object held and not by id.
     The format must be BGRA single plane, the geometry representable, and every read inside the surface and the window.
  2. The id must still name the same object (the 0395 fences work by id).
  3. **Fence**: no GPU write job in flight (`nord2_gpu_surface_busy` incl. 0112 reservations, or `write_pending`).
  4. Then about 25 registers per pipe are written between FRAME_DONE and DSI_START and every one is read back.
  5. No copy, no flip, no noteDistinct.
  6. The swap retires at FRAME_DONE (presentDone), not at copy end.
  7. The surfaces stay held (fHwHeld) until the overlay is re-programmed.
- **Fence rule**: a HW-eligible frame that is still busy after the 10 ms wait is **requeued, never forced** (a newer swap replaces it).
  It is checked again right before DSI_START, because the TE wait can last up to about 11 ms (late_write). At FRAME_DONE, a write_seq
  change or busy counts as hw_race.
- **Fallback per frame**: any reason sends the frame down the CPU path, counted in why[mode/none/count/cover/format/memory/pitch/range/
  missing/fence/state].
  - The first CPU frame after HW frames restores the 2L registers (layer 0 = panel buffer, layer 1 off, LK background) and then flips
    as before.
  - An empty swap while a HW frame is shown pushes nothing (the panel GRAM keeps the picture).
- **Auto-disable to kern.n2fbhw = 0, with the CPU path restored at once**, on any of:
  - OVL INTSTA FME_UND (b2), RDMA EOF-abnormal (b5-8), SMI underflow (b9-12) or abnormal SOF (b13);
  - DSI underrun (b12);
  - FRAME_DONE timeout (40 ms);
  - register read-back mismatch;
  - 16 consecutive HW frames with hw_race;
  - a failed init check.
  The normal per-frame bits b0, b1, b14-b22 (fbh1) are not faults.
- **Init** (once, at the first HW frame or test). Both pipes' 2L state is read and logged and must be the verified CPU layout
  (layer 0 = shown panel buffer, pitch 4352, 540x2400, SRC_CON layer 0 only). larb0/larb1 NON_SEC_CON p5/p6 must have MMU_EN = 0
  (R11). Otherwise HW composition is refused for this boot.
- Layer 1 RDMA GMC/FIFO: when LK left them 0, layer 0's values are used. **[I]** If the OVL underflows with two layers, the
  auto-disable fires and the init line shows the values.
- In modes 2/3, 0156 prepNext (copying the next swap ahead) is off, so CPU fallback frames lose that overlap.
- Power off while a HW frame is shown: the registers are restored and the screen goes straight to black (no fade of a stale buffer).

### 7.3 H2 test pattern (no pool needed)

- **Where the pattern lives**: in the free third slot of the framebuffer carve-out, 0x781FC000 (behind the two 0156 buffers, pitch
  4352, ends 0x78BF2000 < 0x78C00000). The CPU draws it once; it is the reference, not UI.
- **Layout** (panel pixels):

| Rows | Content |
|---|---|
| all edges | white 4 px frame |
| 0-199 | grey ramp |
| 200-599 | 8 colour bars |
| outside the blue band | seam markers: red x = 539, green x = 540 |
| 900-1699 | blue band (0x2060c0) |
| 1000-1299, x 390-689 | the gradient tile, placed there by layer 1 (crosses the pipe seam: 150 px per pipe) |
| 1320-1469 | **REF_PM**: CPU-computed correct premultiplied source-over of the tile's gradient over the blue |
| 1480-1629 | **REF_ST**: what straight-alpha treatment of the premultiplied tile gives (darker: mid gradient 0x8f7060 vs 0x4f5060) |
| 2000-2299, x 0-299 | the raw tile, which layer 1 reads |

- **Tile**: upper half = orange premultiplied with alpha 0..255 across x; lower half = 10x10 checkers, opaque white / fully clear,
  which look identical under every CON.
- **How to judge the photo**: for each step, the HW gradient (rows 1000-1149) must look like REF_PM for a CON that is usable in H4.
  pm 0 (step 2) must look like REF_ST if the OVL does straight alpha. The checkers and the seam must be continuous across x = 540.

### 7.4 Log lines that prove each step

- **H0**:
  - `NORD2 FB HW: surface id N WxH fmt 'BGRA' ... mem buffer|physical len segs pa lo..hi contiguous below_4g pool pool_pa
    pool_cache -> HW eligible (full screen) | not HW: format | not HW: memory not in the display pool` (first sight of each
    presented surface, at most 160 lines);
  - every 5 s, `NORD2 FB HW: mode=0 ... would_single=.. would_dual=.. why[...]`.
- **H1** (with 0394/0395): after the pool is on and backboardd has restarted, the surface lines show `physical ... pool 1 ->
  HW eligible`. `kern.n2fbhw=1` gives `NORD2 FB HW: plan dry swap ...` (exact register values) and would_single grows. The CPU
  path keeps showing everything (GPU faults on pool pages are 0395's lines).
- **H2**:
  - `NORD2 FB HW: init pipe0/1 base ...`, `init larb0 p5 .. (bit0 MMU_EN must be 0)`, `init ok`;
  - per step: `test T step S (...) regs written and read back ok`, `plan test ...`, `test T step S frame done in X us, ovl0 int
    .. ovl1 int .. dsi_underrun 0 -> none; holding 3000 ms (photo now)`;
  - end: `test T finished, previous picture back (pushed 1, overlay CPU path)`.
  Plus the photo.
- **H3**: `kern.n2fbhw=2` gives `plan single swap ...` and `frame 1 shown by the overlay (1 layers, no CPU copy), ovl_int ...`.
  In the 5 s line hw_frames grows, faults=0 and race stays low, and "NORD2 FB PRESENT" copy_us drops toward 0.
- **H4**: `kern.n2fbhw=3` with `kern.n2fbhwpm` set to the variant H2 proved gives `plan dual ...` on Photos-grid frames.
- **Fault**: `NORD2 FB HW: AUTO-DISABLE <ovl-underflow|dsi-underrun|frame-done-timeout|readback-mismatch|hw-race> ... kern.n2fbhw
  N -> 0` + `restored the CPU path (auto-disable)`.

### 7.5 Host test (`test/hwfb_host_test.c`, `test/hwfb_host_test.out`: ALL OK)

- **Pixel model vs CPU path**: a model of the two OVL pipes reads simulated pool memory with the planned registers, BGCLR black,
  layer 0 opaque, layer 1 per CON. It is compared with the CPU path's own `sweet_fb_layer_clip` + `sweet_fb_compose_row`.
  Result: 2,924 random 1-2 layer scenes, 2,910 planned and **pixel-exact**, 14 correctly rejected, 0 reads outside a surface.
- **H3 full-screen case**: exact addresses (crop 22,18; pitch 4544; pipe 1 +540 px).
- **Rejections**: IOBMD / pool end / above 4 GiB / length / pitch / format / count / cover / mode.
- **Register lists**: upper bits of SRC_CON / PITCH / PITCH_MSB are kept; layer-1 GMC/FIFO fall back to layer 0's; apply then
  restore gives exactly the base registers.
- **H2**: the plans cross the seam (150 + 150 px); the model gives exactly REF_PM for pm 2 and REF_ST for pm 0.
- **Blend math**: over_pm equals the CPU blend `sweet_fb_blend_px` exhaustively.
- **Fault, race and fence policy**.

### 7.6 Not done / limits (and why)

- **No OVL output read-back.** There is no write-back path (WDMA) in use, so "readback mismatch" means register read-back. The picture
  itself is judged by photo (H2) and by the model (host).
- **H5** (more than 2 layers through the 4L OVL, 0392 retained layers held) and **H6** (SwapEnd write-seq fences through
  `nord2_gpu_surface_writes_done`, the IOSurface use count) are not implemented. Frames with 3-4 layers take the CPU path.
- **R1**: an app may still redraw a drawable during the about 9 ms scanout. That is detected (race) and limited (auto-disable after
  16 in a row), not prevented.
- "NORD2 FB SEEN/CADENCE" distinct-frame stats only see CPU frames (they hash panel memory).
- The empty-swap-kept frames count as "legacy" in NORD2 FB PRESENT.

### 7.7 0399 colour-order fix (hwcolor-20261112), 2026-11-12 ~23:00 -- host only

- **Bug [V-dev, coordinator's photos, q10jfi/j/k mode 3 Photos]**: layer 1 (backboardd's UI, premultiplied CON 0x038031ff = 0396's
  pm 2) showed R and B swapped (iOS blue 0,122,255 as 255,122,0). Layer 0 (0x20ff) was right. The colours came back when frames
  took the CPU path.
- **Derivation**: CON bits come from mtk_disp_ovl.c 296-313 (the same in both kernel trees): [15:12] CFMT, 23 CLRFMT_MAN, 24
  BYTE_SWAP, 25 RGB_SWAP, 8 AEN.
  - ovl_fmt_convert() (869-925) gives, for ARGB8888 (memory B,G,R,A = CA 'BGRA'): straight = CFMT 2 = 0x2000 (LK, correct on the
    phone); premultiplied = CFMT 3 | MAN | BYTE_SWAP | RGB_SWAP = 0x03803000. That is the value that showed R/B swapped.
  - Within the same table BYTE_SWAP is the R<->B toggle, so on this OVL the premultiplied B,G,R,A value is CFMT 3 | MAN | RGB_SWAP
    = **0x02803000**. **[I]** until the photo below confirms it.
  - Layer-1 RDMA GMC/FIFO hold no colour bits and are not the cause.
- **Why the 0396 host test missed it**: its OVL model ignored CFMT/BYTE_SWAP. The model now decodes every pixel through
  `sweet_fb_hw_con_decode(final CON)`:
  - it reproduces the photo bug (pm 3 differs in 12129 sampled px);
  - pm 2 is pixel-exact again.
- **Changes**:
  - pm 2 = 0x028031ff (default); the old 0x038031ff is now pm 3.
  - `kern.n2fbhwcsw` 0..3: bit 0 flips BYTE_SWAP on layer 1, bit 1 flips it on layer 0, for HW frames and tests.
  - `NORD2 FB HW: CON written pipe0 L0 .. L1 .. | pipe1 ... (pm, csw)` is logged on every change, and `kern.n2fbhwstat` gains
    csw / con_l0 / con_l1.
  - `kern.n2fbhwtest=6` colour test: an opaque R|G|B strip (100 px each, x 390-689, across the seam) from layer 0 at rows 620-699,
    and the same strip from layer 1 at rows 710-789. **Both rows must read red, green, blue from left to right.** Test 5 runs 1,2,3,4,6.
  - `kern.n2fbhwselftest` (default 1): the first switch to mode 2/3 of a boot runs test 6 first, and logs the CONs.
- **One-boot experiment** (q10jfi + 0399; a photo during each 3 s hold):
  1. `sysctl -w kern.n2fbhwcsw=0; sysctl -w kern.n2fbhwtest=6`: expected both strips R,G,B.
  2. `sysctl -w kern.n2fbhwcsw=1; sysctl -w kern.n2fbhwtest=6`: layer 1 must now read B,G,R. That proves the switch works on
     layer 1.
  3. (only if step 1 showed layer 1 swapped) `kern.n2fbhwpm=3` with csw 0 and 1, test 6 each.
  4. Keep the csw/pm pair whose photo shows both strips R,G,B (also check `kern.n2fbhwcsw=2`: layer 0 must turn B,G,R).
  5. Then `kern.n2fbhwtest=3` (gradient with pm 2) against REF_PM, and `kern.n2fbhw=3` in Photos (Select button blue).
  Log check: each test prints `CON written ... L1 028031ff` (or 038031ff with csw 1), then `test 6 step 6 COLOUR TEST ... regs
  written and read back ok` and `frame done ... -> none`.

---------------------------------------------------------------------------------------------------------------------------

## 0402. CPU-free composition: more overlay layers, H6 fences, CPU-frame verdict (hwmulti-20261113), 2026-11-13 ~00:30

Host tests, compile checks and an image build only. **Nothing here has run on the phone.** Directory `gpu/build/hwmulti-20261113`
(applies on the chain 0394..0400) and the variant `gpu/build/hwmulti-20261113-on0401` (applies after 0401, the display IOMMU).
Files: `iokit/Kernel/arm/SweetFramebuffer.cpp`, `iokit/Kernel/arm/sweet_fb_hw.h`.

### 0402.1 What causes CPU frames, and what 0402 does about each (mode-3 run: 178 fallbacks)

| Cause (why[...]) | 0402 | Remaining CPU frames |
|---|---|---|
| more than 2 layers (count, 101) | each pipe gets the cascaded 4L OVL as well: slots 0..5 = 2L L0, 2L L1, 4L L0..L3 (`kern.n2fbhwlayers` 2..6, default 4) | only if the 4L is refused at init or is in its underflow backoff |
| surface outside the pool (memory, 79) | not 0402: 0401 maps it through the display IOMMU. 0402 keeps IOVA layers in 2L slots (the 4L ports read physical addresses) | IOVA layer at index >= 2 = why[memory] |
| GPU still writing (fence, 18) | H6: never a CPU frame. Hold the old picture until the SwapEnd writes finish, at most 2 refreshes, then show anyway | none |
| offsets / crop / destination position | already planned in 0396: src crop, dst offset, split at x = 540, any 4-byte-aligned address, pitch multiple of 4 up to 0xffff (now host-tested with 6 layers) | range: only reads outside the surface |
| scaling / transform | `IOMFBScalingLimits` = 1/1/1/1: CoreAnimation scales on the GPU instead of handing a scaled layer | counted as `scaled` (shown 1:1 like the CPU path) |
| formats | the OVL has no semi-planar YUV input (only packed UYVY/YUYV) [V-code ovl_fmt_convert]. BGRA only | format: a detached 420v layer (rare: needs an unscaled video) |
| empty swap (none) | no longer counted as a CPU frame (no pixel is touched) | none |

### 0402.2 Hardware basis

- [V-code] MT6885/6893 main path: `OVL0_2L -> OVL0 (4L) -> RDMA0`, dual pipe `OVL1_2L -> OVL1 -> RDMA1` (mtk_drm_drv.c
  mt6885_mtk_ddp_main / _dual_main).
- [V-code] An OVL whose previous component is an OVL takes that OVL's output as its background (DATAPATH_CON bit 2 BGCLR_IN_SEL,
  set in mtk_ovl_connect). So the 4L layers stack above both 2L layers.
- [V-code] DATAPATH_CON bit 4+n = layer n FBDC (compression). 0402 clears it for the 4L layers it uses; the early code already
  cleared bit 5 for LK's layer.
- [V-code] The 4L block has the same per-layer register map (stride 0x20, ADDR 0xf40+0x20n) and SRC_CON bits 0..3.
- [V-code] Its RDMA ports, larb0 p5 and larb1 p5, are already in hwInit's MMU_EN = 0 check.
- [V-code] The early code verified the 4L at boot (LK's warning layer on L1) and removed L1. hwInit (0402) logs both 4L blocks and
  accepts them only as a pass-through stage. The `init 4L` line requires all of:
  - no layer on, not in relay mode;
  - BGCLR_IN_SEL set;
  - the same ROI as the 2L.
- [I] Three things are still open:
  - that 4L layers 0, 2 and 3 fetch correctly (only L1 was used by LK);
  - that GMC/FIFO from LK's L1 is enough for them;
  - that bandwidth suffices for 3-4 full-screen layers per pipe.
- Plane alpha: CON[7:0] together with AEN, as the DRM driver does for PLANE_PROP_PLANE_ALPHA [V-code]. The formula with
  premultiplied pixels is [I] (out = s*pa + d*(1 - sa*pa), as in HWC2). [V-disasm, QuartzCore]: per-layer opacity never reaches
  the display, so the driver passes 255. The planner and model support it.

### 0402.3 Planner (sweet_fb_hw.h, pure)

- `sweet_fb_hw_plan_makec(p, in, n, mode, pm, fw, fh, cap)`. Layer i (bottom first) uses slot i in every pipe it reaches; a pipe
  it does not reach keeps that slot off. The constraint is i < cap.
  - With cap 2 the registers are exactly 0399's: the 0399 host test passes unchanged on this header.
  - `sweet_fb_hw_plan_make` = cap 2.
- Slot 0: opaque (AEN 0) over the black 2L background. Every other slot: premultiplied source-over (0399 CON pm). A layer with
  plane alpha < 255 gets AEN plus the alpha byte, slot 0 included.
- 4L slots take only physical addresses (pool / carve-out), unless the caller passes `SWEET_FB_HW_CAP_L4_IOVA` (to use when 0401
  also translates the 4L ports). Otherwise the result is why[memory].
- `sweet_fb_hw_regs4` / `_regs4_base` give the 4L register lists:
  - layer registers, pitch upper bits 0, GMC/FIFO from base or LK's L1;
  - DATAPATH FBDC cleared with BGCLR_IN_SEL checked;
  - SRC_CON bits 0..3;
  - restore = enables, RDMA bits and DATAPATH as read.
- `sweet_fb_hw_h6()` is the H6 fence decision, written with GPU callbacks so the host can test it.
- Test 7 (`kern.n2fbhwtest=7`) has four layers: test 3 (pattern, gradient tile), plus the layer-1 R|G|B strip at rows 710-789
  (4L L0), plus the tile again at 390,1750 (4L L1).

### 0402.4 Driver (SweetFramebuffer.cpp)

- **Layers:** `kern.n2fbhwlayers` (2..6, default 4). The cap is 2 if the 4L is refused or in backoff. hwInit runs before the first
  plan of a HW mode, so the 4L state decides the cap.
- **4L faults:** an OVL underflow (2L or 4L INTSTA) or a DSI underrun on a frame that used the 4L does NOT disable HW composition.
  - It logs `NORD2 FB HW: 4L OFF (...)` and caps at 2 slots for 2, 4, 8 then 16 s, then for the rest of the boot; `4L back on` is
    logged when a backoff ends.
  - A 4L register read-back mismatch disables the 4L for the boot.
  - Underflows on frames of 2 slots or fewer keep the 0397 hwFault/backoff behaviour.
- **H6 fence (modes 2/3).** State 5 runs `hwFence5`:
  - At SwapEnd: snapshot `nord2_gpu_surface_write_seq` for each layer, plus the layers whose surface has an outstanding 0112
    reservation.
  - A layer is ready when `nord2_gpu_surface_writes_done(id, snapshot)` holds.
  - An outstanding reservation holds the layer. When it retires, the snapshot is taken again, so the promised job is waited for.
  - A newer swap replaces a waiting one.
  - After 2 TE periods from the swap's arrival, the frame is shown anyway (`fence_forced`).
  - No CPU fallback and no requeue for a fence any more: the 0396 hwCandidate requeue and hwFrameTry's `-1` path are removed.
  - At the TE wait, `late_write` uses the same snapshot. A forced frame is never held there.
  - At FRAME_DONE, `race` = a write *submitted* during the scanout. A reservation for the next frame is no longer counted.
  - The display hold (0398) of a HW frame still lasts until the next HW frame's FRAME_DONE or the CPU-path restore (fHwHeld).
- **IOMFBScalingLimits:** `{RGBLayer_MinScaleFraction, RGBLayer_MaxScale, YUVLayer_MinScaleFraction, YUVLayer_MaxScale} = 1`.
  - [V-disasm] QuartzCore `DisplayLimits::update_display_scaling` (0x1883ac1f4) reads these four keys.
  - When the key is absent, CA allows RGB 1..2x and YUV 0.25..4x on the display.
  - [V-disasm] CA never hands more than 3 swap layers (indices 0..2; at most one detached surface). No property or selector sets
    the layer count.
  - So 4 slots per pipe already cover everything CA can send. [I]: >2-layer frames in the logs come from 0392's retained scene.
- **CPU frames** (emergency path, already reading WC pool surfaces through the 0397 cacheable alias):
  - counted by reason in why[...];
  - `cpu_fault` = CPU frames while a fault backoff holds HW off;
  - every >= 60 s: `NORD2 FB CPUFREE: FB HW: CPU-FREE PASS|FAIL|IDLE|OFF window_s= hw= cpu= cpu_per_min= cpu_fault= hw_mode= cap= why[count/cover/format/memory/pitch/range/missing/state]= fence_wait= fence_forced=`;
  - PASS = HW frames and 0 CPU frames in the window;
  - `kern.n2fbhwstat` / the `NORD2 FB HW:` line gain `cap l4 l4_frames l4_faults cpu_fault fence_wait fence_forced fence_resv scaled cpu_per_min`.
- **0401 interplay** (variant on0401): hwWriteRegs passes only 2L lists through 0401's `sweet_fb_iommu_gate_list`. 4L lists reuse
  offsets 0xf40/0xf60 and are physical-only.

### 0402.5 Verification

- **Host test** `test/hwmulti_host_test.c`: ALL OK, also under ASan/UBSan (`test/hwmulti_host_test.out`).
  - The planner's register lists are written into a simulated register file of both blocks of both pipes.
  - A cascade pixel model reads the panel back from those registers and compares it with the CPU path (sweet_fb_compose_row) and
    with an independent reference.
  - 6139 random scenes of 1..6 layers, with crop, offsets, seam crossing, nonzero IOSurface offsets, cap 2..6 and plane alpha:
    - 4125 planned, pixel-exact, 0 out-of-surface reads;
    - 2025 used the 4L;
    - 2014 correctly refused for the cap.
  - Restore puts the 2L back to base exactly.
  - Rejections and test 7 on the real pattern are covered.
  - H6 simulation: 40000 runs, 0 frames shown with SwapEnd writes unfinished. A mutant that ignores SwapEnd reservations is
    caught (563 violations).
  - The on0401 variant also checks that an IOVA layer is refused in a 4L slot and accepted in slot 0.
- **Regression:** the 0399 host test passes on the 0402 header.
- **Compile:** SweetFramebuffer.cpp with the CI flags, rc 0. The only new warnings are 4 SYSCTL-macro ones on the
  kern.n2fbhwlayers line, which the build exempts. __TEXT grows by 8.3 KB uncompressed.
- **Chain:** PREFLIGHT_OK for 0394..0400+0402 and 0394..0401+0402, both on the q10jfi and the q10jfj line (no fuzz, no offset).
- **Image:** q10jhm = q10jfj line + 0394..0400 + 0402 (`GNSS=1 WARN_BUDGET=4`; the budget is needed for the 0394/0395/0398
  warnings, as for q10jfn).
  - KERNEL OK, IMAGECHECK_OK.
  - **boot_spare 12288 vs q10jfn 18432: 0402 costs 6144 B.**
  - image sha256 d1ab6a95ab1fdcd92f982ccf16a78cad37d11f115b2861d4bba4edbd51468246
    (`device-mmu-q10jhm-candidate/ui-sched/`, not device-tested, not approved).
  - With 0401 on top, check the spare. If it drops under 8 KiB, trim per 8.3 (the GNSS ladders first).
- **Patches:**
  - `hwmulti-20261113/0402-nord2-fb-hw-multi-layer.patch` sha256 3e0a5d93110125f6dbd36e74cfdeaec39c7d486c89948f7b65235bcacfb64d21 (on 0400);
  - `hwmulti-20261113-on0401/0402-nord2-fb-hw-multi-layer.patch` sha256 7cb2d90ec1c259168cbe85bee157e3a766df9fb2e3e51080421d40ecbd2f41af (on 0401 sha 38d17cee...).
  - If 0401 changes again: `BASE=<0401 patched-sources> ./make_patch.sh` in the on0401 directory, then resolve any reject by hand
    as done for hwTestPoll and hwFrameEnd.
- **Parsers:** stress_verdict.py and fbhw_verdict.py were checked on synthetic logs and are unaffected by the new lines.
  `cpufree_verdict.py LOG` gives PASS/FAIL/NO DATA with reasons.

### 0402.6 Phone test (orchestrator)

1. Image with 0402. One mode-3 transaction with the pool on (and 0401 if built in), as in 8.4.
2. Settle the 4L first. `sysctl -w kern.n2fbhwtest=7` and take a photo during the 3 s hold. Expected:
   - test 3's picture (the gradient equals REF_PM);
   - a R|G|B strip at rows 710-789;
   - a second tile at 390,1750 across the seam;
   - `init 4L ok`, `test 7 step 7 ... frame done ... -> none`.
3. If the init line says REFUSED, or test 7 underflows: `sysctl -w kern.n2fbhwlayers=2`. That is 0399 behaviour; report the init
   line.
4. Normal use (Photos grid, switcher, keyboard, Control Center, a game) for >= 5 minutes in mode 3.
5. Judge with `hwmulti-20261113/cpufree_verdict.py ui-usb/xnu-live.log`:
   - PASS needs `FB HW: CPU-FREE PASS` windows and no FAIL window;
   - the reasons line names any remaining cause;
   - WARN lines list 4L OFF / AUTO-DISABLE.
6. Also look at:
   - `fence_forced` (should stay a small fraction of `fence_wait`);
   - `race`;
   - `scaled` = 0 (IOMFBScalingLimits honoured);
   - `l4_frames` > 0 in scenes with 3 layers.
