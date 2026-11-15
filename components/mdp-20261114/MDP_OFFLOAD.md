# MT6893 MDP offload for the iOS port: feasibility, bring-up plan, probe stage (2026-11-14/15, Opus agent "MDP")

Tags: **[V-src]** read in nord2-current-kernel, **[V-dt]** live device tree (ios-sweet/dtb/nord2/live.dts), **[V-dsc]** iOS 20H392
shared cache disassembly/strings (`~/dsc-work`, `ipsw dyld ...`), **[V-phone]** an earlier phone log, **[I]** inferred.
Nothing in this file ran on the phone yet.

## 1. Correction: which block does what on MT6893

| Block | What it is | Generic scale / rotate / CSC? |
|---|---|---|
| **MDP** (mdpsys 0x1f000000) | 4 parallel pipes: RDMA0-3 (read DMA, format unpack, YUV/RGB, crop), RSZ0-3 (scaler), WROT0-3 (write DMA with rotate 90/180/270 + flip, output CSC), plus FG0/1, HDR0/1, AAL0-3, TDSHP0-3 (sharpness), TCC0-3, COLOR0/1, MDP mutex. **No WDMA on this chip.** [V-src mdp/mt6893/mdp_base.h, V-dt] | **Yes: this is Android's generic scaler/rotator/colour converter** (DpBlitStream / DpIspStream in the closed libdpframework.so) |
| Camera ISP P2 (DIP, imgsys 0x15020000 / 0x15820000) | RAW -> YUV (demosaic, NR, LCE...), direct-links into MDP (mdpsys clocks MDP_IMG_DL_ASYNC0-3) | Produces the preview/video YUV that MDP then scales/rotates |
| Display (mmsys 0x14000000) | OVL (blend), DISP_RSZ0/1 (in-line layer scaler, tile 1440 px, height <= 4096, used by HWC "RPO" upscaling), mdp_rdma4/5 + mdp_rsz4/5 + aal4/5 ("MDP in display"), RDMA | Only in-line display-plane scaling, no memory-to-memory jobs [V-src drm/mediatek/mtk_disp_rsz.c:601-612, V-dt] |
| VENC / VDEC (our /dev/redwood-vcodec) | Codec engines | **No.** VDEC writes MT21/MM21 (block NV12), VENC reads NV12/I420 etc. with minimal conversion |
| JPEG enc/dec (/dev/redwood-jpeg) | Codec | No (JPEG decoder has no scaler on this chip that we use) |
| "VPU" / APU (apusys, mtk-vpu) | MediaTek AI processor | **No** — not the scaler. (In the old MT8173 V4L2 mtk-mdp driver the "VPU" firmware computed MDP parameters, which is probably the source of the confusion.) |

How Android drives MDP [V-src drivers/misc/mediatek/mdp/mdp_ioctl_ex.c:707+, mdp_def_ex.h]: the **register programming lives in
userland** (libdpframework.so, present in our vendor dump `gpu/build/cam-vendor-20261109/vfs/lib{,64}/mt6893/libdpframework.so`);
it computes tiles and sends a list of `op_meta` {op, engine, offset, value / fd+offset, mask} with ops WRITE, WRITE_FD (buffer
address), POLL, WAIT/SET/CLEAR event, READBACK to `/dev/mtk_mdp` (CMDQ_IOCTL_ASYNC_EXEC). The kernel turns the metas into a GCE
command buffer on the MDP GCE (gce_mbox_m 0x10318000) and runs it. Exactly the structure of the codec stack we already host
(vendor userland emits GCE lists, our vcodecd executes them as register batches + polls).

## 2. What exists on THIS device [V-dt unless marked]

- mdpsys_config 0x1f000000 (CG_CON0 0x100 / SET 0x104 / CLR 0x108, CG_CON1 0x110, CG_CON2 0x120; topology MOUT/SEL 0xf20-0xf7c),
  mdp_mutex 0x1f001000 (SPI 0x1d2), mdp_smi_common 0x1f002000, smi_larb2 0x1f003000, smi_larb3 0x1f004000, RDMA0-3 0x1f006000-0x1f009000
  (RDMA0 SPI 0x1d3), RSZ0-3 0x1f012000-0x1f015000, WROT0-3 0x1f016000-0x1f019000, TDSHP 0x1f01a000.., COLOR0/1 0x1f02c000/0x1f02d000.
- CG bits (CG_CON0) [V-src clk-mt6885-mdp.c]: RDMA0 0, RSZ0 4, WROT0 7, RDMA2 8, RSZ2 10, WROT2 14, MUTEX0 15, RDMA1 16, RSZ1 20,
  WROT1 23, RDMA3 24, RSZ3 26, WROT3 30, APB bus 31. CG_CON1: APMCU_GALS 1, SMI0 4, SMI1 8, SMI2 12 (GATE_DUMMY in Linux; written
  open as 0x1112 at power-on, clk-mt6885-pg.c:1147-1149).
- Power: SPM MDP_PWR_CON 0x1000634c, status bit 19 in PWR_STATUS 0x16c/_2ND 0x170; bus protection infracfg STA1 0x228 bits 10/23,
  MM 0x2ec, MM_2 0xdd8 [V-src pg.c:411-460, 2921+]. **The phone boots with the MDP domain ON** (PWR_STATUS 0x10580e03, bit 19 set)
  [V-phone runs/q10hw1/00-step0.log, quoted in gpu/hwcodec/FACTS.md 2.5 and kern/test_dec07]. Clock root mdp_sel = CLK_CFG_1 bits 11:8,
  gate bit 15; MMDVFS steps 594 / 416 / 312 / 273 MHz (live.dts mmdvfs_pmqos mdp_step0-3).
- IOMMU: MDP larbs sit behind **M4U1 = iommu1 0x1f027000** (SPI 0x1f2, clock mdp-smi2), not the display's M4U0. Ports: larb2 p0 RDMA0,
  p1 RDMA2, p2 WROT0, p3 WROT2, p4 FG0; larb3 p0 RDMA1, p1 RDMA3, p2 WROT1, p3 WROT3, p4 FG1 [V-src mt6885-larb-port.h:54-67].
  Android programs them 34-bit IOVA (config_port_34bit, mdp.c:1409). larb NON_SEC_CON(p) = +0x380+4p, bit0 MMU_EN.
- GCE: two GCEs, gce_mbox (display/"D") 0x10228000 SPI 0xcb, infracfg gate IFRAO0 bit 8; gce_mbox_m (MDP/"M") 0x10318000 SPI 0xcd,
  gate bit 9; 24 threads each; MDP engine events: rdma0..3 SOF 0x100-0x103, rsz SOF 0x10c.., wrot SOF 0x110.., frame-done
  wrot3..0 0x120-0x123, rsz 0x12c-0x12f, rdma 0x130-0x133 (live.dts:5294-5351). (The brief's "GCE at 0x10228000" is the display GCE.)

What we already touch (repo search for mdp/cmdq/gce/larb/smi/iommu/mmsys):
- **No MDP engine, no GCE has ever been programmed by our port.** No patch maps 0x1f006000.. or 0x10228000/0x10318000.
- The codec work depends on the MDP *domain*: VDEC core larb4 and VENC core-1 larb8 hang off the **MDP SMI common** (M4U1);
  nord2_vcodec.inc refuses those blocks unless PWR_STATUS bit 19 is on and mdp_sel ungated (it never powers MDP itself). VDEC
  decode works on the phone [V-phone vc26/vc27] with IOMMU bypass (NON_SEC_CON bit0 = 0, physical pool below 4 GiB).
- GCE: the codec's vendor GCE lists are executed **in software** (vcu/n2vcu.c gce_run_list, register batches + polled waits);
  the kernel has no GCE client.
- IOMMU: the display M4U0 (0x1411a000, larb0/1 OVL ports) runs v7s tables from our kernel (0401..0411, default on). M4U1 is the
  same IP generation (compatible "mediatek,iommu_v0") -> the 0401 table code (sweet_fb_iommu.h, host-tested) is reusable for MDP.
- 0311 autotest only *reads* mdpsys CG_CON1 for a log line.

## 3. Feasibility per candidate

MDP capabilities [V-src strings of libdpframework.so mt6893 + mdp code; numbers marked I]:
- Formats: NV12/NV21, I420/YV12, NV16/NV61, NV24/NV42, I422, I444, YUYV/UYVY/YVYU/VYUY, RGB565/BGR565, RGB888, RGBA/BGRA/ARGB/
  ABGR8888, GREY, MTK block formats 420_BLKP (MT21, VDEC output) and their UFO-compressed variants (decoder frame compression).
  10-bit (P010 / MTK 10-bit packed) and AFBC: not visible in the strings -> **unknown, treat as unsupported** until proven.
- Rotation 0/90/180/270 + H/V flip in WROT; crop in RDMA; scaling in RSZ (Android uses it roughly 1/32x..32x with taps changing
  by ratio [I]); YUV<->RGB matrices in RDMA/WROT (BT.601/709, full/video range) [I].
- Max size: 4096-wide sources are handled by tiling (each tile limited by line buffers; libdpframework prints "Config tile time"),
  i.e. **frame size is not a hard limit, tile programming is** [I].
- Throughput [I]: ~1 pixel/clock per pipe at 594 MHz -> 4096x3072 (12.6 Mpix) ~21 ms on one pipe, ~11 ms split over 2 pipes
  (Android splits >= 4K over pipes); 2048x1536 ~5.3 ms on one pipe. Latency = that + per-tile programming (CPU writes ~0.1 us each).
  Android records 4K30 with MDP doing scale+rotate, consistent with >= 375 Mpix/s.

| Candidate | MDP can do it? | iOS side today | Route on iOS |
|---|---|---|---|
| (a) VTPixelTransferSession / VTImageRotationSession: scale, rotate, 420f/420v/BGRA conversion | **Yes** for 8-bit NV12/BGRA/420v/420f, rotate, crop, scale. No for 10-bit (unknown), no for Apple compressed formats | VT builds a VTPixelTransferChain; with a hardware scaler it appends **VTPixelTransferNodeScaler** (`figIOSurfaceAcceleratedPixelTransfer_TransferM2`), else Metal (**VTMetalTransferSession**, the `VTMTSComputeFunction` dispatches in perf1a) or software (vImage) [V-dsc VideoToolbox symbols] | Provide the hardware scaler service (below). No VT plug-in mechanism exists for pixel transfer (codec plug-ins only) [I strong] |
| (b) camera preview: PrimaryScaler / SecondaryScaler, YUV->RGB | Scale/rotate/CSC **yes**; RAW->YUV (our GPU "ISP pass") **no** (needs ISP P2/DIP) | CMCapture BWPixelTransferNode -> VTPixelTransferSession; our camera ISP render is our own Metal compute | Same service; the GPU ISP pass stays until DIP is brought up |
| (c) video encoder input conversion | Yes (BGRA -> NV12, scale) | VT encoder input conversion via VTPixelTransfer | Same service, or directly inside vcodecd |
| (d) CoreAnimation / Metal blit-scale | CA: partly (CA itself probes AppleM2ScalerCSCDriver [V-dsc string in QuartzCore]); arbitrary Metal blits: no | GPU | Service is picked up by CA automatically where CA uses it; Metal stays GPU |
| (e) JPEG/HEIC thumbnailing | Scaling + CSC yes (decode itself: JPEG HW / VDEC) | ImageIO, CMPhoto, AppleJPEG probe AppleM2ScalerCSCDriver [V-dsc strings] | Same service |
| (f) display plane scaling | DISP_RSZ0/1: in-line, input tile <= 1440 px, height <= 4096, used by Android HWC for **upscale** (RPO) [V-src + I] | our overlay path (0396..0416) uses no scaler | Possible later inside SweetFramebuffer for scaled video layers; small gain |

**The key iOS fact [V-dsc]:** all of iOS's hardware scaling goes through one IOKit service name. `IOSurfaceAcceleratorCreate`
does `IOServiceMatching("AppleM2ScalerCSCDriver")` (or "AppleSRSDriver" for the sharpener) + `IOServiceOpen(type 0)`;
`IOSurfaceAcceleratorTransformSurface(accel, src, dst, options, rects, ...)` = `IOConnectCallStructMethod(selector 1, 0x170-byte
struct, no output)`; struct: +0 src IOSurfaceID, +4 dst IOSurfaceID, +0x20 flags (low bits = "Transform" flip/rotate value, only one
rotation bit allowed), rectangles in 16.16 fixed point from +0x28, alpha/border/histogram/dither options further on
(prepareTransformBuffersAndOptions 0x1d3c21d14). VideoToolbox's `scalerCapabilities` (0x194f3b628) reads the
`IOSurfaceAcceleratorCapabilitiesDict` property of that service (MaxSourceWidth/Height, MaxDestWidth/Height, scale-factor limits,
taps/phases, ScalerVersion, flags cropping/colour conversion/yuvs, FormatIn*/FormatOut* keys). The same service name is looked up by
VideoToolbox, CMCapture, CMPhoto, ImageIO, AppleJPEG, QuartzCore, VideoProcessing, AppleCVA, Portrait, AirPlaySupport, VectorKit.
Our port has no such service -> **every one of them falls back to Metal (GPU) or vImage (CPU)** — that is why the camera
pixel transfer (VTMTSComputeFunction) runs on the Mali [I strong: the code path is verified, the "no service" branch is the only one
our port can take].
=> The right hook is **not** a VT plug-in but a kernel IOKit service `AppleM2ScalerCSCDriver` (our class, that name) with an
IOSurfaceAccelerator-compatible user client backed by MDP, published with a conservative capabilities dictionary
(8-bit NV12/420v/420f/BGRA only, sizes <= 4096x4096). Kill switch: register the service only on request (sysctl from a payload);
processes started before registration (and every process if it is never registered) keep today's Metal path. Unsupported requests
return an error -> [I] VT falls back to its Metal node only if the chain is rebuilt; must be checked (risk R1).

## 4. Bring-up plan

Stage 0 — probe (**done tonight, 0670, images q10jmdp0 / q10jmdp1**): see section 6.

Stage 1 — DMA self-test (kernel, no iOS involvement): RDMA0 -> (RSZ0 bypass) -> WROT0, BGRA 256x256 copy then 90° rotation, from/to
a buffer carved from the vcodec DMA pool (PA < 4 GiB) with larb2 p0/p2 **MMU_EN off** (the codec/JPEG bypass method), engines
triggered through the MDP mutex (MOD bits + EN) by CPU writes, completion by polling WROT/RDMA status (frame-done is also wired to
GCE events 0x123/0x133 and to SPIs). Result checked by CRC of the output against a CPU reference, logged. Guard pages around the
buffers, timeout -> engine soft reset (RDMA 0x008, WROT 0x008/0x00c handshake as in cmdqMdpResetEng) and gates closed again.
Register layouts: upstream Linux mtk-mdp3 (mdp_reg_rdma.h / rsz.h / wrot.h; offsets 0xf00 SRC_BASE / VIDO_BASE_ADDR match this
chip's mdp.c) cross-checked against libdpframework disassembly. **GCE decision: CPU-programmed first** (one-shot jobs need only
register writes + polls; GCE adds an instruction encoder and a second unknown). GCE-M later to cut CPU wake-ups per tile.
Stage 2 — RSZ scaling + WROT rotation + NV12->BGRA CSC with our own tile split (or: run libdpframework once per configuration on
the host under the codec stack's n2a32 interpreter, record its op_meta list, replay it natively with buffer addresses patched —
the metas already separate addresses (WRITE_FD) from constants). Pipe split for 4096-wide frames.
Stage 3 — M4U1 v7s tables (0401 code with base 0x1f027000, larb2/3 ports) so scattered IOSurfaces above 4 GiB are reachable.
Stage 4 — `AppleM2ScalerCSCDriver` IOKit service + user client (selector 1 struct 0x170; GetID scalar selector; the others
return kIOReturnUnsupported), IOSurface lookup by ID in our SweetSurface, capabilities dictionary; payload switch to register it.
Stage 5 — measure (camprev_check.py: VTMTS dispatches must disappear; new kernel counters for MDP jobs/ms).

ioctl API for userland tests (/dev/redwood-mdp, stage 1+): `RWMDP_INFO`, `RWMDP_BLIT {src{pa|iova|surface_id, fmt, w, h, pitch[3],
crop}, dst{...}, rotation 0/90/180/270, flip h/v, csc matrix id + range, filter}` -> sync completion with hw_us; every error returns
EINVAL/ETIMEDOUT and the caller keeps its existing path.

Test plan (orchestrator): q10jmdp0 boot -> read `NORD2 MDP` lines -> `python3 gpu/build/mdp-20261114/mdp_verdict.py LOG`;
if MDP_CLOCKS_CLOSED -> q10jmdp1 (level 2); MDP_READY -> stage 1 image; MDP_DOMAIN_OFF -> stage 1 must add the SPM sequence
(clk-mt6885-pg.c:2921 spm_mtcmos_ctrl_mdp; it is a parent for VDEC/VENC-C1 too, so it needs a shared reference with nord2_vcodec.inc).

## 5. Expected relief (perf1a numbers; [I] unless marked)

perf1a Magnifier at full size [V-log]: GPU in flight 87-89 %, one 36.6 ms compute job at a time; per frame the GPU ISP pass
(39-42 ms wait), VT converter/rotator 48x128 groups (39.7 ms incl. queueing), VT preview scaler 18x47 (43.2 ms incl. queueing), CPU
SecondaryScaler 20 ms -> 10-14 fps for the whole UI.
- MDP takes the VT converter/rotator, the preview scaler and the CPU SecondaryScaler. GPU job time removed per frame ~roughly half
  of the camera GPU load (the 3072x4096 rotation job is about as large as the ISP job; the preview scaler is smaller); the CPU
  loses ~20 ms per frame. MDP time for the same work ~11-25 ms per frame on 1-2 pipes, in parallel with the GPU.
- With camprev1 (2048x1536 pool, delivered separately) everything is ~4x smaller: ISP ~10 ms, VT ~10 ms, scaler ~3 ms GPU per
  frame; MDP then removes ~13 of ~23 ms GPU per frame and ~5 ms CPU. camprev1 is the cheaper first fix; MDP is the structural one.
- What stays on the GPU: the RAW->YUV ISP pass (Android does it in ISP P2/DIP), and all UI rendering (Maps/Safari/home costs are
  rendering, not scaling: no MDP gain there). So MDP captures Android's scale/rotate/CSC advantage (roughly half of the camera-path
  GPU cost) but not its ISP advantage, and nothing of the general GPU-performance gap.
- Secondary wins once the service exists: Photos/ImageIO thumbnail scaling, VT encoder input conversion, CA YUV paths [I].

## 6. Stage 0 deliverable (0670) — host-built and host-tested only, NOT on the phone

- Patch: `piece-0670/0670-nord2-mdp-probe.patch` (new bsd/dev/nord2_mdp.inc + 2 lines in sweet_touch.c: include after
  nord2_vcodec_at.inc, `nord2_mdp_start()` after nord2_haptic_start()). Own kernel thread, own ml_io_map_unmappable mappings
  (SPM, topckgen, infracfg, mdpsys 0x2e000, GCE-D, GCE-M: kernel_map, no io_submap page), no PMIC/pwrap, no display/codec/power
  domain access. Level 1 = read-only, gated reads (domain on + protection clear + mdp_sel running before any mdpsys read; each
  engine only with its own CG bit open; larb/M4U1 only with the SMI clocks open; GCE only with its infracfg gate open). Level 2 =
  opens RDMA0/RSZ0/WROT0/MUTEX0/APB + SMI gates that were closed, reads, closes exactly those. Boot probe at +45 s (level
  N2MDP_BOOT_LEVEL), read-only again at +180 s; sysctls kern.n2mdp_probe (1/2 run once, -1 never), kern.n2mdp_boot, kern.n2mdp_state.
- Host test `test/t_mdp_probe.c` (ASan/UBSan, register model): T_MDP_PROBE ALL OK (fixed cases + 20,000 random gate/power states);
  4/4 mutants caught (engine gate check removed, protection check removed, gate restore removed, SMI check removed).
- Images (recipe = q10jfinal2 = q10jfinal1 + 0660, + 0670; build_q10jmdp.sh, private integ copy mdp-20261114/integ):
  q10jmdp0 (level 1 at boot), q10jmdp1 (level 2 at boot, piece-0670-l2). Details in NOTES.md.

## 7. Functional stages (0671, 0672) — built on the user's decision "fai tutto il lavoro MDP"; host-tested only, NOT on the phone

New evidence used [V-dis = disassembly of the phone's own libdpframework.so, lib64/mt6893]:
- Mutex: RDMA0..3 use mutex 4..7 (IMGI/IMGI2/WPEI/WPEI2 use 0..3); registers EN 0x20+0x20*id (write 1 = frame start), SOF
  0x2c+0x20*id (0 = single, mask 7), MOD0 0x30+0x20*id, MOD1 0x34+0x20*id; MOD0 bits RDMA0-3 0-3, FG0/1 4-5, AAL0-3 6-9, HDR0/1
  10-11, RSZ0-3 12-15, WROT0-3 16-19, TDSHP0-3 20-23, TCC0-3 24-27, CAMIN1-4 28-31; MOD1 COLOR0/1 0-1 (DpEngineMutex::require jump
  table, engine ids = vendor kernel CMDQ_ENG_* numbering).
- Topology (DpPathTopology::s_adjency, DpPathConnection::s_moutMap/s_selInMap/s_selOutMap, MOUT = bitmask of the listed outputs,
  SEL = index): RDMA0 -> {FG0, WROT0}; RDMA2 -> {HDR0, AAL2, WROT2}; FG0 -> PQ0_SOUT -> {HDR0, RSZ0}; AAL2 -> RSZ2 -> TDSHP2 -> TCC2
  -> WROT2; RSZ0 -> TDSHP0 -> COLOR0 -> TCC0 -> {HDR0, WROT0}.  Registers 0x1f000f20.. (RDMA0 MOUT) .. 0x1f000f7c (WROT3 SEL).
- RDMA: GMCIF 0x10371 (mask 0x30071); SRC_CON = format id | swap<<14 | 0x20000 | 0x600 (mask 0x038cfe0f); TRANSFORM_0 =
  en<<16 | matrix<<23 (mask 0x0f810000).  WROT: VIDO_CTRL = format id | swap<<8 | 0x5000 | rotation<<20 | flip<<24 (|uv bits
  28/30 for YUV out) (mask 0xf131510f).  Register offsets = upstream Linux mtk-mdp3 headers (same layout: 0xf00 bases etc.).
- Done detection without a GCE program: the WROT frame-done event (GCE-M 0x123 for WROT0, 0x121 WROT2; RDMA 0x133/0x131) read
  by the CPU through the GCE token registers (0x60 id / 0x64 value / 0x68 update; mtk-cmdq-mailbox.c cmdq_get_event).
Inferred [I] (each one checked by a stage-2 test before use): RSZ control/step encoding (1.15 steps, enable bits 0/1, 20-bit
subpixel), WROT MAT_CTRL (bit0 enable, bits 7:4 matrix), MAIN_BUF fields, rotated-tile destination offsets, PQ relay bits
(AAL/TCC +0x20 bit0, TDSHP +0x100 = 0), matrix ids 4 JPEG->RGB, 6 BT601->RGB, 7 BT709->RGB, 0 RGB->JPEG.

Design (bsd/dev/nord2_mdp_job.h pure compiler + reference; nord2_mdp.inc executor/state machine; sweet_m2scaler.inc service):
- Pipes: direct RDMA0 -> WROT0 (copy, rotation, flip, YUV->RGB in the WROT) and scale RDMA2 -> AAL2 -> RSZ2 -> TDSHP2 -> TCC2 -> WROT2.
- Tiles: output columns in 512 px (configurable) tiles, input span with a 4 px filter margin, RSZ integer/subpixel start per tile.
- Executor: CPU register writes, POLL ops (engine resets), GO (clear event, mutex EN), WAIT (event, ~190 ms timeout) -> on any
  timeout: mutex off, RDMA/WROT reset, RSZ off, MOD cleared, error; never leaves a DMA running.
- Memory: stage 1/2 buffers 9 MiB taken once from the vcodec pool (PA 0xA0000000+, < 4 GiB), larb2 ports forced to bypass;
  1 MiB guard band after every destination checked after each job.  Stage 3 and the service: M4U1 armed with 0401's v7s tables
  (sweet_fb_iommu.h) after checking that no powered M4U1 master (larb 4/8/9/13/16/18) translates; jobs switch larb2 p0-p3 to
  MMU_EN only for their duration; faults (FAULT_ST1) fail the job.
- State machine (one boot): probe OK -> S1 (copy256 + rot90, exact) -> S2 (rot180, rot270, flip, tiled copy, tiled rot90, 420v,
  420f, scale down, scale up, scale+rotate, tiled scale; tolerance compare for CSC/scaling; R/B order self-calibrated) -> S3
  (IOVA copy to 64 scattered pages above 4 GiB, used in reverse order) -> publish hook -> AppleM2ScalerCSCDriver.  A stage runs
  only if the previous passed; S1 failure restores the clock gates exactly; timeouts/guard hits stop the machine.
- Service (0672): registered from the MDP thread only with stage 3 passed (caps bit IOVA) and kern.n2mdp_svc = 1; selector 1
  accepts only synchronous whole-surface transforms (zero rectangles/options, transform 0 or [I] 0x1 flip / 0x4/0x8/0x10 rotation),
  BGRA/RGBA/420v/420f sources, RGB destinations; everything else -> kIOReturnUnsupported (+ first 16 raw structs hex-dumped:
  this is how the rectangle/option layout will be decoded from a phone log).  Cache maintenance by kernel VA (PoC, PIPT).
- /dev/redwood-mdp (0666): RWMDP_INFO (stage, caps, results, counters), RWMDP_BLIT_SURF (surface id -> surface id, via the 0672 hook).
- Kill switches: file /private/var/db/nord2-mdp-off (payload mdpoff), kern.n2mdp_stages (0..3), kern.n2mdp_svc, kern.n2mdp_probe=-1.
Known open risks: (R1) if VideoToolbox does not fall back to Metal when the service refuses/fails a transform, the camera preview
could lose frames in q10jmdp2 (q10jmdp2a has no service); (R2) most [I] fields are untested on hardware: expect S2 partial passes
on the first run, the log carries the numbers to fix them; (R3) the capabilities dictionary keys are a minimal guess (VT may
decide not to use the scaler at all -> no gain, no harm).
Images: q10jmdp2a (probe + S1, no service) sha256 20f4e3e2421fa96022d2396ecb5cccdec40d9d770737dc9a455c10e757f1a22d;
q10jmdp2 (stages 1-3 + service) sha256 55c0c0046d67c3111103b9c81f8514306c61a04a6f1e3b41180c338cf7a7491a (both on q10jperf4: final1 + 0660/0664/0665).
Host tests: test/t_mdp_probe.c, test/t_mdp_full.c (ASan/UBSan, ALL OK; mutants caught). Verdict: mdp_verdict.py LOG.

## 8. 0673 bring-up stage P (2026-11-15, after the phone verdict MDP_DOMAIN_OFF) — host-tested only, NOT on the phone

Phone fact [V-phone q10jmdp0/1/2a]: domain on (PWR_STATUS 0x10580fff, MDP_PWR_CON 0xd), mdp_sel running, but 0670 said
"bus protection set" and never read mdpsys.  Re-reading clk-mt6885-pg.c [V-src]: the protection registers are shared by every MM
domain and MDP's power-on masks include the bits of the domains behind the MDP SMI common.  Breakdown of the phone values:
- PROTECTEN STA1 0xe70c508e: MDP bits 10 (step1:0) and 23 (step2:0) **clear**.
- MM STA1 0x55554815: MDP-mask bits set = 0x50440014 = CAM 2,4 + CAM_RAWA 18 + CAM_RAWC 22 + VEN_CORE1 28,30; MDP/DIS bits 6-9 clear.
- MM_2 STA1 0x141: = VEN 0 + VDE2 6 + ISP 8; MDP-only bits 2,4,11 clear.
- every one of those owners is OFF in PWR_STATUS (bits 12-18, 23-26 clear).  The display's own mask has set bits too (0x5114001)
  while the display works: same pattern.  => MDP's own protection was already released; 0670's rule (any MDP-mask bit) was a false
  positive.  Not read before: SUB_INFRA_VDNR STA1 0xbc4 bit 20 (MDP step2:3) — now read and logged (and added to the 0670 rule).
Stage P (kern.n2mdp_stages bit 0x100; default 0x101 in q10jmdp3 = P + probe + S1, no service):
checks (domain on, MDP_PWR_CON & 0x111f == 0xd, mdp_sel ungated) -> plan (abort with no write if a set MDP-owned bit is shared with an
ON domain or the display) -> release only exclusive MDP bits (PROTECTEN 10/23, MM_2 2/4/11, SUB_INFRA_VDNR 20) that are set, in vendor
order step2:0 2:1 2:2 2:3 1:0 1:1 1:2 through the _CLR registers, poll STA1 then EN <= 200 x 5 us each -> on a stuck ack: put back
(SET, vendor power-down order step1 then step2, ack polled <= 1 ms) and log register/bit -> verify all four STA1 (owned rule) ->
CG_CON1 CLR 0x118 <- 0x1112, CG_CON0 CLR 0x108 <- gated ones of APB31|MUTEX0 15|RDMA0 0|RSZ0 4|WROT0 7, IFRAO0 CLR 0x84 <- bit 9 if
gated (bit 8 display GCE untouched) -> read back, restore everything on failure -> probe re-run with the owned rule.  With the phone's
state the release writes NOTHING (no exclusive bit set): the effective change is the corrected gating + the gate opening.
Serialised against the codec power code with n2vc_lock.  Result kern.n2mdp_pwron (1 OK ... see the .inc), lines "NORD2 MDP PWRON: ...".
Host test test/t_mdp_pwron.c (+ run_t_mdp_pwron.py: 12/12 mutants caught), t_mdp_full_0673.c (old scenarios + scenario P), log
test/host-tests-0673-pwron.log.  Image q10jmdp3 = q10jperf6 recipe + 0670 + 0671a + 0673 (build_q10jmdp3.sh, private integ3).

## 9. 0690 dual pipe + service timing (2026-11-15, host-tested only, NOT on the phone)

Analysis of the q10jmdp19 Magnifier log [V-phone, scratchpad m19c-logs]:
- Two request streams alternate 1:1 from mediaserverd: 2048x1536 -> 1536x2048 (4 tiles, ~2950 us each, hw 11.8 ms) and 1504x1128 ->
  1128x1504 (3 tiles, 2190/2190/2060 us, hw 6.4 ms). "avg hw 9.1 ms" is just the mean of the two classes, not contention.
- Lock wait ~9 ms = each request waits for the other stream's whole job (both issued for the same frame); the MDP lock serialised
  them on ONE pipe although pipe 2 was idle.  The 0689 timing lines read globals after the lock was released ("tiles 0" for the 2048
  stream, a 4th tile value on 3-tile jobs): mixed streams.
- Per-pixel rate is the same for both classes (~265 Mpx/s = 0.45 px/clk @594 MHz) and extremely stable (+-10 us per tile), while the
  physical S2 timing frame ran at ~0.97 px/clk with almost no rotation penalty.  Candidates: M4U1 translation (IOVA only; M4U1 is
  left at reset by the boot chain and Android clears MISC_CTRL IN_ORDER_WR_EN / WR_LEN THROT_DIS at init), or DRAM contention. [I]
  The 0690 S3 TIMING rows decide it on the next boot.
- Cache maintenance (~0.36 ms) is outside the MDP lock; camera NV12 surfaces are normal cacheable XNU memory (the WC heap holds only
  full-screen BGRA / GPU buffers), so the flush is required; not changed.
- The 0688 surface cache was not thread-safe (two client threads): an entry could be evicted (wire hold dropped) while another
  thread's job used its pages.  Fixed (lock + busy pin).

0690 (piece-0690/0690-nord2-mdp-dual-pipe-svc-timing.patch; nord2_mdp.inc, nord2_mdp_job.h, nord2_mdp_api.h, sweet_m2scaler.inc):
- Pipe-2 direct lane RDMA2 -> WROT2 for unscaled jobs [V-dis, read from libdpframework.so mt6893: s_moutMap RDMA2 {AAL2, WROT2, ..}
  -> MOUT bit 1; s_selInMap WROT2 {TCC2, RDMA2} -> SEL 1; mutex 6 MOD0 RDMA2|WROT2].  Scaled jobs keep the scale lane.
- Locks: MDP lock -> rw lock (stages / probe exclusive, service shared) + one mutex per pipe + map mutex (IOVA cache, tables, fault
  state) + GCE token spin lock.  Service: scaled -> pipe 2; unscaled -> pipe 0, or pipe 2 if pipe 0 busy and N2MDP_CAP_DUAL.
  Larb ports switched per pipe only.  IOVA cache entries pinned while used (fault drop defers unmapping, all-busy -> temp mapping).
- Resumable executor (per-run stats, 200 ms wall timeout per tile), n2mdp_run2 drives both pipes from one thread.
- S2: 15 pipe-2 capability jobs (diag 6) + p2tim90_512 timing + DUAL job (1024x768 NV12 rot90 on pipe 0 and rot270 on pipe 2,
  alone then together, exact + guards) -> caps bit 14 N2MDP_CAP_DUAL (caps 0x7fff when all pass; 0x3fff = dual off, same as before).
- S3 TIMING (after S3): phys90_512 vs iova90_512/256/128/64 + iova0_512 on the same pool pages; then the vendor M4U1 write setup
  (clear MISC_CTRL bits 1/17, WR_LEN bits 5/21) only if no other M4U1 master domain is powered, rerun, reverted if slower >10 % or
  failing; the fastest IOVA rotation tile width (>=3 % better than 512) is used for rotated unscaled service requests.
- Sysctls: kern.n2mdp_tune (1 M4U setup, 2 dual pipe, 4 tile choice; default 7), kern.n2mdp_svc_tile (force px), kern.n2mdp_auto_tile,
  kern.n2mdp_m4u (0..5), kern.n2mdp_overlap.  Kill dual pipe at runtime: sysctl kern.n2mdp_tune=5.
- Host: test/t_mdp_full_0690.c ALL OK (+PIPE, PIN, DELAY scenarios, routing checked in the model), test/run_t_mdp_0690_mutants.py
  11/11 mutants caught.
