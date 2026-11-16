#!/usr/bin/env python3
"""Generates ledger.json (static analysis ledger; no builds, no device)."""
import json
A,M,N,P,U="ALREADY IN NORD2","MISSING","NOT APPLICABLE","PARTIAL","UNVERIFIED"
RE='gpu/metal/tegu/render_encoder.inc'
RP='gpu/metal/tegu/render_pipeline.inc'
SF='gpu/build/xnu-stage/iokit/Kernel/arm/SweetFramebuffer.cpp'
def it(i,area,ref,status,evidence,owner,plan,prio,test):
    return dict(id=i,area=area,ios_tegu=ref,nord2_status=status,nord2_evidence=evidence,owner_file=owner,minimal_port_plan=plan,priority=prio,test=test)
items=[
it("N-01","root/CoreAnimation prefs","NOTES.md '2026-10-22: ROOT111' (11edc5b); keys recovered from QuartzCore DSC (not in git)",M,
 "No CAEnable* seeding in gpu/tools, gpu/docs, work/tools, xnu/patches (grep). Nord2 history shows b3a8 surfaces (NOTES.md ~7818/7891).",
 "work/tools/sweet-root-tweaks.py merge_pref() pattern; install step gpu/tools/stage_metal_root.py (UI-Metal-ON stage only)",
 "Write boolean false for CAEnableDeepFramebuffer, CAEnableFramebufferCompression, CAEnableIOSurfaceCompression into mobile backboardd.plist and .GlobalPreferences.plist (501:501 0600), plus root equivalents; revert with the UI-Metal OFF/rollback path.","P0",
 "host: plist builder/rollback test; device (needs approval): SWEET SURF log shows backboardd fmt BGRA 1125x2436 bpr 4608 alloc 11239424 planes 0, no b3a8"),
it("N-02","IOSurface import usage 4/5 (R23)","render_encoder.inc tegu_iosurface_render_target_usage; 11edc5b",A,
 f"{RE}:151 usage==4||usage==5; {RE}:811 import entry; capability COMPOSITOR_IOSURFACE (bit15) advertised",RE,"none","-","existing render_encoder host tests"),
it("N-03","display-surface import to GPU","0128/0137/0147/0164; selector 17",A,
 "gpu/src/Nord2GPU.cpp:388 importSurface; gpu/metal/nord2_metal_bridge.c:843 n2mb_import_surface; render_encoder.inc:818-850 (stride/alloc/shape checks, shadow fallback)","Nord2GPU.cpp","none (transport is Nord2's own)","-","probe 10 direct IOSurface checks already hardware-qualified (NOTES.md line 48)"),
it("N-04","kernel display-surface GPU backing policy 0164","xnu 0164 (11edc5b) process-name+exact-tuple promotion",N,
 "Nord2GPU::importSurface accepts any SweetSurface with valid_ram pages; no GPU-shared allocator/S2MPU window","-","none","-","-"),
it("N-05","command-buffer/encoder selectors (setLabel, enqueue, setProtectionOptions:0, setViewport/Scissor/Cull/Winding/FillMode)","R24-R27 (7bb18a7, 50fc276)",A,
 "present in compute_frontend.m/blit_encoder.inc (setLabel,enqueue,setProtectionOptions) and render_encoder.inc (viewport etc.)","-","none","-","existing host frontend suites"),
it("N-06","never-nil PSO + inert/observer policy","R125 fbdaac4/25ada2b",P,
 f"{RP}:1952-2106 keeps never-nil for backboardd via TEGU_INERT_PIPELINES; build/native/Nord2Metal.bundle strings contain TEGU_INERT_DRAW_REFUSED; Nord2 deliberately poisons the command instead of silently dropping (nord2_render_pass.inc:1-15). Inert is guarded refusal, NOT support.",RP,"verify the flag is part of the production build recipe (NORD2_FRONTEND_DEFINES is an env hook in build_tegu_frontend_objects.sh:27, default empty)","P1","host: ca_backboardd_host_test.py (exists in gpu/build/c6f-lowering-20261026); require a build-identity record of the defines"),
it("N-07","MRT emulation / color(1) framebuffer fetch (R143-R155, 57e90f0)","one TGJ1 job per BGRA8 target; RT1 texture bound for color(1) fetch in RT0 job",P,
 f"{RP}~2030: 'True-MRT framebuffer fetch unsupported' -> inert in backboardd. Compute DELTA18 admits 1..4 targets but fetch snapshot transport for true MRT is still a guarded refusal (board 10:52).","render/lowering/compute owners","Reuse ios-tegu semantics (per-target jobs, snapshot of both old attachments) only after a red oracle exists for the 763x413 platter shape; do not count refusal as support","P1","integration_tests CoreAnimation oracle (DELTA11) must include color(1) fetch case"),
it("N-08","LOAD semantics / Clean Tile Write Enable (R170)","092b0b1: LOAD jobs must not clean-write untouched tiles",A,
 "gpu/metal/nord2_metal_render.c:439 rt.clean_tile_write_enable = load_action != LOAD (JM descriptors follow Mesa)","nord2_metal_render.c","none","-","pixel probes LOAD cases"),
it("N-09","CPU preclear for framebuffer-fetch first job (R173)","bea248e",U,
 "Nord2 uses snapshot-based fetch/LOAD; equivalence of first-job clear + fetch not re-verified here","render owner","add a case: Clear pass whose first draw fetches the destination","P2","integration_tests"),
it("N-10","IOSurface shadow copies for unimportable surfaces (R154/R197)","render_encoder.inc tegu_iosurface_shadow",A,
 f"{RE}:802,835 gated on GENERIC_JOB cap (advertised, bit18); perf cost 85-300 MB/2 s observed in ios-tegu R197","-","none; watch cost","P3","-"),
it("N-11","MTLTextureDescriptor.swizzle (LA08 as RG8)","ios-tegu R173 open item (also unfixed there)",M,
 f"{RE}:2292 swizzle_valid=0; swizzle words fixed to identity at {RE}:1729,1750,1783,2179","textures owner","plumb descriptor swizzle into n2t view; only if backboardd 230x186 LA08 surfaces appear in refusal trace","P2","textures oracle"),
it("N-12","per-client GPU wired budget vs display surfaces","ios-tegu had 16-24 MiB resource windows, no analogue",P,
 "gpu/include/nord2_gpu_transport.h:99 limit=64 MiB charged per client incl. imported pages (Nord2GPU.cpp:392); advertised cap 256 MiB (nord2_metal_bridge.c capabilities out[3]) and ctx_open limit 256 MiB (Nord2GPU.cpp:529) disagree. Display surface = 686 x 16 KiB; 3 buffers + pipelines may exceed 64 MiB.","gpu/include/nord2_gpu_transport.h, Nord2GPU.cpp","do not charge IMPORTED (already-wired SweetSurface) pages against the allocation limit, or reconcile limits; keep per-BO 4096-page cap","P1","host: 3 display imports + 40 MiB private allocations must succeed; ASan/UBSan transport test"),
it("N-13","AppleCLCD swap retains surfaces until copy (0142)","4445100 sweet_fb_pending_surfaces.h",M,
 f"{SF}:386 swap() memcpy only; present() resolves ids later via sweet_surface_copy","xnu patches (SweetFramebuffer.cpp lives in 0082-nord2-early-probe lineage); kernel/display owner","Port the lifecycle header + capture at SwapEnd + release after copy; keep Nord2 ownership/retry logic (nord2_display_begin)","P1","host model from ios-tegu surface_presentation_host_test.cpp; stop/restart race"),
it("N-14","LaunchServices re-seed, AMFI-by-name, developer_mode sysctl, container path, class D (wallpaper/widgets)","0184/0185/0189/0192/0197",M,
 "no developer_mode_status / sandbox container hook in work/xnu/patches or xnu-stage","xnu patches / root installer","Port only after Metal UI shows no wallpaper: first confirm PosterBoard extensions count; 0197 equals dropping the lsd recovery marker file once","P2","device logs from lsd/PosterBoard (needs approval)"),
it("N-15","multi-layer overlay blend (0187)","66d4962",M,
 f"{SF} present() replaces each layer (no alpha blend)","SweetFramebuffer.cpp","Instrument first (NORD2 FB SWAP mask popcount); port sweet_fb_blend_bgra_row only if >1 layers are swapped with Metal ON","P2","host row-blend test from ios-tegu"),
it("N-16","GPU-completion dirty trigger (0174)","702b55a",U,
 "bridge waits for job completion before returning (NORD2_GPU_SEL_WAIT); no sweet_fb_note_gpu_write equivalent","Nord2GPU.cpp / SweetFramebuffer.cpp","only if asynchronous GPU completion after SwapEnd is observed","P3","-"),
it("N-17","presentation cadence / transfer gate (0211/0212), USB capture 0160/0182","973b986,a263295",N,
 "MTK command-mode panel; sweetfb_hz boot-arg already exists (default 10 Hz, SweetFramebuffer.cpp:230)","-","raise sweetfb_hz via boot-arg when UI is stable","P3","-"),
it("N-18","USB hotfix push 0178 and hotfix destination tables","cd0f555,0186,0190,0191,0197",N,"Nord2 uses offline HFS addon/rollback; phone operations are not authorized","-","none","-","-"),
it("N-19","fixed UI classes UI1/UI2/B3/AC1/TFU, kernel selector 16 packages","0161-0173,0176",N,"CSF kernel packages; Nord2 compiles QuartzCore pipelines through the generic JM path","-","none","-","-"),
it("N-20","TGJ2/TGJ3 pairs, union submit, VM/AS1, retained target cache, work attribution","0204,0223-0226, gpu_vm.c, generic_job3.c, pair_layout.c",N,"CSF / Tensor AS1 specific; Nord2 JM has own MMU/scheduler","-","none","-","-"),
it("N-21","MRT DontCare RT1 store skip","render_encoder.inc mrtStore (post-7f6f7ca)",M,
 "nord2_render_pass.inc:73-76 always stores; DontCare aux work is executed","render owner","skip the emulated RT1 job when store action is DontCare and nothing reads it","P3","oracle: DontCare RT1 output undefined"),
it("N-22","tile UNORM8 specialization / single-pass source-atop","spirv_tile_unorm8.h; b976e92",N,"perf/experimental, opt-in; Nord2 uses snapshot fetch, not the CSF tile entry","-","defer","P3","-"),
it("N-23","CAMetalLayer/nextDrawable, drawable crash fix (R211), events/fences","R211; compute_frontend.m",A,"QuartzCore drives drawables through newTextureWithDescriptor:iosurface (N-03); events/fences present in compute_frontend.m/blit_encoder.inc","-","none","-","-"),
it("N-24","ASTC, memoryless, A8/RG8/RGBA16F sampling","R156-R165",A,"render_encoder.inc tegu_texture_is_astc:147 and texture paths; Nord2 added n2t helpers","-","verify through texture corpus","-","corpus owner"),
]
ledger=dict(
 generated="2026-10-26", analysis="static; no build/test/phone",
 inputs=dict(ios_tegu_local="overnight-20261024/references/ios-tegu",import_base="7f6f7ca9c58ab840cb1a9c306e7cba3bddd2d572",origin_main="ee8de4c",
  nord2_files_identical_to_base=312,nord2_files_modified=27,nord2_files_added=14),
 q1_best_backboardd=dict(branch="origin/main",commit="ee8de4c",note="R252/R251 device baseline; UI renders, 37.557 ms/command, 46 pixel probes PASS",
  second="origin/metal-single-pass-atop-20261025 b976e92 (experimental, host only)",
  superseded=["tegu-compositor-0133..0157 diagnostic CI-gate branches","ios26-bringup (iOS 26 tree)"],
  milestones={"R74/ROOT111 linear display surfaces + 0164":"11edc5b","R75 setLabel":"7bb18a7","R78 viewport":"50fc276","R80":"fbc2b17","R101 UI1/UI2 GPU completion, target zero":"3539efc",
   "R104 first visible GPU pixels / R105":"9a435b5","R106 TGJ1":"1f9bc2c","R108 hotfix":"14194af","R111-125 generic draws/never-nil":"25ada2b","R143-148 lock screen":"3829660",
   "R156-163 widgets":"237730d","R170 LOAD fix":"092b0b1","R185-191 wallpaper":"082c2f0","R207":"2b5c124","R215 tile UI":"7afc8b1","R252":"199d3d8"}),
 q2_ca_preferences=dict(in_git=False,
  evidence="git log --all -S/-G for ROOT111, CAEnable*, CoreAnimation, QuartzCore, Deep, Compress over tools/research and all file types; git grep on every branch tip; root builders and hotfix tables inspected; only prose in NOTES.md/TEGU.md; research/compositor-ui-device-r74 untracked",
  keys=["CAEnableDeepFramebuffer","CAEnableFramebufferCompression","CAEnableIOSurfaceCompression"],value=False,
  mechanism="CFPreferencesGetAppBooleanValue(key, kCFPreferencesCurrentApplication, &valid); valid => flag=(value!=0)",
  quartzcore=dict(image_base="0x188284000",cfstrings=["0x1d5b5f1e0","0x1d5b5f200","0x1d5b5f220"],read_sites=["0x188381f80","0x18837b58c","0x1884cae30"],env_switch="CA_DISABLE_FRAMEBUFFER_COMPRESSION @0x18859df5e"),
  domains_recommended_unverified=["/private/var/mobile/Library/Preferences/backboardd.plist","/private/var/mobile/Library/Preferences/.GlobalPreferences.plist","/private/var/root/Library/Preferences/backboardd.plist","/private/var/root/Library/Preferences/.GlobalPreferences.plist"],
  backboardd_user="mobile (verified in 20H392 launchd.plist)",
  rootfs_domains_used_by_ROOT111="UNKNOWN (prose says 'mobile and root preference domains'; TEGU.md says 'four')"),
 items=items,
 plan=[
  "P0 N-01 seed the three CA keys (false) in the UI-Metal-ON root step; verify BGRA/planes 0 in SWEET SURF log",
  "P1 N-12 stop charging imported SweetSurface pages against the 64 MiB client budget (or reconcile limits)",
  "P1 N-13 port 0142 surface retention into SweetFramebuffer (kernel/display owner)",
  "P1 N-06/N-07 record NORD2_FRONTEND_DEFINES in build identity; keep true-MRT fetch as red oracle, do not count inert as support",
  "P2 N-14 wallpaper/widget root requirements; N-15/N-16 only if observed; N-11 swizzle",
  "P3 N-21 MRT DontCare skip; ignore all CSF-only items (N-17..N-22)"])
json.dump(ledger,open('ledger.json','w'),indent=1)
print(len(items),"items")
