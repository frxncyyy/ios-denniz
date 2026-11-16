# ios-tegu vs Nord2 comparison (backboardd / CoreAnimation path), 2026-10-26

Scope: read-only analysis. Private ios-tegu source stays in
`overnight-20261024/references/ios-tegu` (local); nothing was published, built,
flashed or run on a phone. Only light local reads were done (git, grep, one
nice/ionice'd streaming `strings` pass over `rootfs-vm/archives/SystemOS.tar`, and
the already-extracted iOS 16.7 dyld cache in `gpu/build/frontend-audit-ui-c2-20261025/dsc`).
Machine-readable companion: `ledger.json` (same directory). "Nord2" below means
this candidate tree (`gpu/metal/tegu` + `gpu/metal/nord2_*`, `gpu/src`, `gpu/build/xnu-stage`).

Evidence class legend: GIT = ios-tegu commit/file; DSC = iOS 16.7 shared cache
inspection (this session); SRC = Nord2 source read this session; DEV = ios-tegu
device log quoted from its NOTES/TEGU.md (not re-verified; its `research/*`
evidence directories are untracked and not present locally).

## 1. Most advanced backboardd state in ios-tegu

Ranking (12 remote branches fetched; local `main` = 7f6f7ca, the Nord2 import base):

| rank | branch / commit | verdict |
|---|---|---|
| 1 | `origin/main` ee8de4c (2026-10-24, 1072 commits, 69 ahead of 7f6f7ca) | Most advanced. Last device baseline R252 kernel + R251 userspace (bundle `research/tgj-mrtskip-r10`): UI renders, `TEGU_COMMAND_FAILED=0`, `TEGU_GENERIC_DRAW_REFUSED=0`, double-buffered scanout, 46 pixel probes PASS, 548-step replay 37.557 ms/command (not 60 fps). Everything after 7f6f7ca is CSF/perf (TGJ2 pair submit, VM/AS1 root work, retained target cache, tile UNORM8). |
| 2 | `origin/metal-single-pass-atop-20261025` b976e92 (+3f6b465) | main + 2 commits: experimental coherent single-pass source-atop compiler entry (`mali_graphics_compiler.c/.h`) and a test alignment. Host only; no device result. |
| - | `origin/tegu-compositor-0133/0141/0146/0149/0151/0154-r1,r2/0155/0157` (tips 846817c 667204c 680944a e0525db a1bd40f 363ab4b a133f80 571b9a5 da71b73) | Pre-7f6f7ca diagnostic CI-gate branches (1-3 commits each: kernel feature switches 0133/0141/0150, OOL import 0146, import trace 0158). Their payload is in main as patches 0141..0158. Superseded. |
| - | `origin/ios26-bringup` 292b628 | iOS 26 (xnu 12377) bring-up tree; deletes most of `tools/tegu/metal`. Not a Metal/backboardd source. Ignore. |

Milestone chain on the way (GIT NOTES.md headings; commit = first NOTES commit):
R74 ROOT111 linear BGRA display surfaces + usage-5 import entry (11edc5b, also adds kernel 0164);
R75 setLabel (7bb18a7); R76/77 setProtectionOptions/enqueue; R78 viewport/scissor/cull (50fc276);
R80 Half4 vertex + resource contract (fbc2b17); R101 UI1/UI2 selector-16 jobs complete serial/fence 1/1..4/4 but target all zero (3539efc);
R103 DCD flags word fix (reg 57) -> first nonzero target; R104 **first visible GPU-rendered iOS pixels** (white Apple logo; 64-byte sampled-row fix) (9a435b5);
R105 Metal viewport Y flip, scissor, UI1 LOAD (patch 0176); R106 userspace-built generic jobs TGJ1 (selector 19, patch 0177, 1f9bc2c);
R108 USB hotfix push verified (14194af); R111-125 reflection-driven generic draws, framebuffer fetch, never-nil pipelines / observer fallback after backboardd SIGABRT "Metal failed to build render pipeline" (25ada2b);
R143-148 first GPU-rendered lock-screen content (3829660); R156-163 chronod widgets (237730d);
R170 LOAD jobs erased untouched tiles (Clean Tile Write Enable) fixed (092b0b1);
R185-191 wallpaper (LaunchServices re-seed, 0197) (082c2f0); R207 user-observed random lines / glitchy blur remain (2b5c124);
R215 tile-fetch UI (7afc8b1); R252 control (199d3d8).
Open at ee8de4c: lavender material tint (host analysis, no cause found), `MTLTextureDescriptor.swizzle` ignored (LA08 as RG8), 4x MSAA SpringBoard effects, 37.6 ms/command.

### What the working backboardd path needs end to end (GIT/DEV)
1. Root: Setup skipped; CoreAnimation deep/compression preferences disabled (section 2);
   LaunchServices recovery marker so lsd re-seeds app extensions (hotfix dest 3, 0197) and, for PosterBoard/widgets,
   AMFI-by-name + `security.mac.amfi.developer_mode_status` + sandbox container path + protection class D (0184/0185/0189) and dyld-policy answer (0192, only for os_log).
2. Kernel: GPU user client + IOSurface import into the GPU address space (0128/0137/0147/0156/0157/0181/0183), display-surface backing
   (0164), IOMFB/AppleCLCD swap path with surface retention until the CPU copy (0142), presentation triggers (0174/0187/0211/0212), Metal
   discovery properties (0119), per-job submission (TGJ1/2/3, CSF only).
3. Userspace driver (TeguMetal.bundle in /System/Library/Extensions): MTLDevice discovery in backboardd and SpringBoard; AIR -> SPIR-V ->
   Mali for QuartzCore's generic pipelines; never-nil PSO policy (generic job, else observer; refused draws dropped individually);
   IOSurface texture import usage 4/5 for BGRA 1125x2436 stride 4608; command-buffer surface (setLabel, enqueue, setProtectionOptions:0,
   setViewport/Scissor/Cull/Winding/FillMode); MRT emulation, framebuffer-fetch via tile/memory, LOAD semantics, memoryless/ASTC textures;
   pipeline cache pack (cold compiles take 1.5-28 s).
4. Presentation: CPU copy of the swapped IOSurface layers into a (double-buffered) scanout buffer on a vsync timer (10 Hz default, 60 Hz after R213).
   Direct scanout is research-only (DPUF0 S2MPU/pKVM unresolved).

## 2. The CoreAnimation preferences seeded by ROOT111 (R74)

Not in git. Evidence: `git log --all -S` for ROOT111/CAEnable/CoreAnimation/QuartzCore/Deep/Compress over `tools research`, `-G` over all file types,
`git grep` on every branch tip, and the root builders (`tools/mkiosroot.sh`, `tools/sweet-root-tweaks.py`, `tools/tegu/{build,prepare_gui,skip_setup,repair_cryptex}_root_vm.py`,
hotfix tables 0186/0190/0191/0197) contain no such key; the only statements are prose in NOTES.md `### 2026-10-22: ROOT111 ...` ("three CoreAnimation
deep/compression preferences for both the mobile and root preference domains") and TEGU.md "Current BackBoard import checkpoint" ("through the four root/mobile preference
domains"; ROOT133 "preserves ... disabled CoreAnimation surface compression preferences"). The R74 evidence dir `research/compositor-ui-device-r74/` is untracked.
The ROOT111 file list and written values are therefore UNKNOWN from ios-tegu.

Recovered from the actual iOS 16.7 QuartzCore (DSC, image base 0x188284000, `__cfstring` 0x1d5b5f1e0/0x1d5b5f200/0x1d5b5f220), which fills the gap:

| key | cfstring | read at | semantics |
|---|---|---|---|
| `CAEnableDeepFramebuffer` | 0x1d5b5f1e0 | 0x188381f80 | `CFPreferencesGetAppBooleanValue(key, kCFPreferencesCurrentApplication, &valid)`; if valid, flag = (value != 0), overriding the hardware default (obfuscated MobileGestalt key) |
| `CAEnableIOSurfaceCompression` | 0x1d5b5f200 | 0x18837b58c | same pattern, flag at +712 |
| `CAEnableFramebufferCompression` | 0x1d5b5f220 | 0x1884cae30 | same pattern; only consulted when the deep-framebuffer flag (+792) is set |

Also an env switch `CA_DISABLE_FRAMEBUFFER_COMPRESSION` (0x18859df5e). Value to disable: boolean `false`.
App ID is `kCFPreferencesCurrentApplication` (verified by dereferencing the GOT symbol, value CFString "kCFPreferencesCurrentApplication"), so the lookup uses the
current process's own domain then the user's global domain. `com.apple.backboardd` runs as `mobile` (UserName verified in 20H392 `launchd.plist`, program
`/usr/libexec/backboardd`), SpringBoard also `mobile`. Effective seed (inferred, to be verified on the next boot, not read from ROOT111):
`/private/var/mobile/Library/Preferences/backboardd.plist` and/or `/private/var/mobile/Library/Preferences/.GlobalPreferences.plist`
(owner 501:501, mode 0600), plus the same names under `/private/var/root/Library/Preferences/` for root-run CA clients (ROOT111's "root" domain), each with
`{CAEnableDeepFramebuffer: false, CAEnableFramebufferCompression: false, CAEnableIOSurfaceCompression: false}`. `work/tools/sweet-root-tweaks.py:merge_pref()` already
implements the mobile/0600 merge.
Nord2 relevance: grep of `gpu/tools`, `gpu/docs`, `tools`, patches finds no such seeding. Nord2's own hardware history shows the same failure mode
(`NOTES.md` ~7818/7891: backboardd created `b3a8` two-plane surfaces; "b3a8 is uncompressed RGB10 + alpha plane, not BGRA"); the Nord2 kernel present has no planar `b3a8`
interpretation (such layers are only counted in `diag.non_bgra`, SweetFramebuffer.cpp present()). lowering-opus' C6f board note had no selector for this; this is it.

## 3. Kernel-side requirements ios-tegu added vs Nord2

| ios-tegu | purpose | Nord2 |
|---|---|---|
| 0164 (11edc5b) auto GPU backing for exact BackBoard tuple (1125x2436 BGRA, stride 4608, alloc 11239424, process `backboardd`) | surfaces had to come from the GPU-visible scatter allocator (S2MPU GB34 window on Tensor) | NOT APPLICABLE as a gate. `Nord2GPU::importSurface` (gpu/src/Nord2GPU.cpp:388) maps any `SweetSurface` by id (page-aligned base/len, `valid_ram`, retains the surface). 11239424 B = 2744 pages = 686 x 16 KiB units. Risk: per-client wired budget (see ledger N-12). |
| 0128/0137/0147/0156/0157/0158/0181/0183/0203/0207 IOSurface import, spans, flush, GB34 | CSF TGJ spans and Tensor S2MPU | Equivalent already: selector IMPORT_SURFACE + `n2mb_import_surface` (nord2_metal_bridge.c:843) + per-page cache sync (Nord2GPU.cpp `sync_page`). GB34/span items NOT APPLICABLE. |
| 0142 (4445100) retain surfaces captured at SwapEnd until the panel copy; stop-safe lifecycle | recycled/destroyed id between SwapEnd and the vsync flush | MISSING. `AppleCLCD::swap` stores the raw record; `present` calls `sweet_surface_copy(id)` up to 100 ms later (SweetFramebuffer.cpp:386/408). |
| 0174 (702b55a) GPU completion marks the framebuffer dirty | GPU write after last SwapEnd never re-presented | PARTIAL/UNVERIFIED: Nord2's bridge submits then waits (`NORD2_GPU_SEL_WAIT`), so the ordering hazard is smaller; no hook exists. |
| 0187 (66d4962) premultiplied source-over for layers above the lowest | CA may swap several layers | MISSING. Nord2 `present` memcpys each layer (replaces). Only matters if `NORD2 FB SWAP` mask popcount > 1 with Metal ON. |
| 0211/0212/0213/0160/0182 transfer gate, cadence, gesture replay, USB capture | 60 Hz presentation and test tooling | Mostly NOT APPLICABLE (MTK command-mode panel, own USB log); `sweetfb_hz` boot-arg already exists (default 10 Hz). |
| 0178/0186/0190/0191/0197 USB hotfix push + destinations | iteration speed | NOT APPLICABLE (Nord2 uses offline HFS addon, phone ops not authorized). The 0197 *idea* (LaunchServices recovery marker) is a root requirement, see ledger N-14. |
| 0184/0185/0189/0192 AMFI-by-name, developer_mode sysctl, sandbox container path, class D, dyld policy | ExtensionKit/PosterBoard (wallpaper, widgets), os_log | MISSING in Nord2 patches (no match for `developer_mode_status`/`sweet_sandbox_container`). Not GPU; matters for wallpaper/widgets once UI is accelerated. |
| 0119 Metal discovery properties | `MTLCreateSystemDefaultDevice` finds the accelerator | ALREADY (own implementation: `nord2_metal=1`, separate `--system-metal` image; UI Metal OFF by default). |

## 4. Frontend/userspace differences (full table in ledger.json)

Key fact: 312 of 339 imported files are byte-identical to 7f6f7ca; 27 Nord2-modified, 14 Nord2-added. All backboardd-specific frontend work up to 7f6f7ca
(R23 usage 4/5, R24-R27 selectors, R125 never-nil PSOs, TGJ1 generic draws, MRT emulation, ASTC, memoryless, shadow IOSurface, observer drops) is therefore *source-present*.
Whether it is *enabled* in Nord2 depends on Nord2 overrides: `nord2_render_pass.inc` (every attachment rendered, refused pass => inert encoder + poisoned command),
`render_pipeline.inc` (`TEGU_INERT_PIPELINES`, backboardd-only inert PSO; the current `build/native/Nord2Metal.bundle` strings contain `TEGU_INERT_DRAW_REFUSED`, so source and binary agree), and capability bits
(Nord2 advertises 3,4,6,8,13,14,15,18,20 = COMPOSITOR_LOAD/COMPOSITOR/COMPOSITOR_IOSURFACE/GENERIC_JOB/SURFACE_SPANS; not 12 LOAD_PASS, 16 V7, 17 ATTACHMENT_CLEAR, 19 BATCH, 21/22 JOB2).

Post-7f6f7ca ios-tegu userspace delta (59 files): TGJ2 pair submission, `generic_job3`, `gpu_vm`, `pair_layout`, retained-target cache kernel patches 0223/0224, 0225 work attribution,
0226 union submit, `FS_MAX_RESOURCE_PAGES` 24 MiB, `compositor_load_*` aperture_va fixes, summarize_*.py tooling: all CSF/AS1 specific => NOT APPLICABLE.
Candidates with real value: (a) `mrtStore` skip of DontCare RT1 emulation jobs (`render_encoder.inc`, `TEGU_MRT_SKIP`) - perf only, Nord2 always stores (nord2_render_pass.inc:76) so a
DontCare RT1 job is wasted work; (b) tile UNORM8 SPIR-V rewrite (`spirv_tile_unorm8.h`, opt-in) - perf/static code only, requires Nord2 tile-fetch entry, defer;
(c) single-pass source-atop (b976e92) - experimental, defer.

## 5. Ranked integration plan

See the final message / ledger `plan`. Summary: P0 CA prefs seeding (root, UI-Metal-ON step only); P1 SweetFramebuffer surface retention (0142 model) and instrument for multi-layer swaps
before porting 0187; P1 wired-budget policy for imported surfaces; P2 AMFI/ExtensionKit/class-D/LaunchServices root requirements for wallpaper/widgets; P3 MRT DontCare skip.
Nothing in the post-7f6f7ca CSF work should be ported.

## Not verified / limits
- ROOT111's exact plist paths/values and the R74 device-result.json are not recoverable from git (section 2).
- No ios-tegu device evidence was re-run; DEV items are quoted from its notes.
- The inferred seeding domains are derived from QuartzCore's lookup call, not from ROOT111.
- No Nord2 build/test/replay was run (thermal policy); Nord2 statements are source reads, plus a `strings` check of the existing `build/native/Nord2Metal.bundle`.
