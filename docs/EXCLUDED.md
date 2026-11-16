# What was deliberately NOT published, and why

This export was made by an allowlist (text source/script/doc types, <= 2 MB, no binaries), then filtered and scanned with the
port's release sanitizer (`components/release-20261115/tools/sanitize_release.py`) using the owner's private identifier list
(99 needles: IMEI/ICCID/IMSI/serial/MAC/SSID/SSH/calibration digests/owner strings, in ASCII, UTF-16, BCD, raw and hex forms) plus
extra scans (private keys, tokens, e-mail addresses, MAC/IMEI/ICCID patterns, owner account name). Result on the final tree:
**SANITIZE_CLEAN**. Values are never printed by these tools.

## 1. Private data of the author / the author's phone (never published)
- Device dumps and partitions: GPT dumps, persist/nvdata/nvram/protect/proinfo/seccfg/otp images, `oplus_custom`, Android
  cmdline (serial), dmesg/pstore/kallsyms/iomem, `backup/`, `IOS-BACKUP-*`, `scratchpad-backup-*`.
- Modem NVRAM blobs (`N2NVDSK1`, `n2nvdisk*.bin`, `payload-nvfile1*`) and **kernel patches that embedded the author's modem NVRAM
  tree as C arrays** (`0260`-`0276`, `sweet_nvram_blob.h` with contents; the final kernel loads the blob from the phone instead).
- Wi-Fi calibration file (`WIFI`, `WIFI_CUSTOM`) and MAC; Bluetooth address; sensor calibration values.
- **Per-device pins compiled into kernel sources** (Wi-Fi calibration sha256, GPT LBA1..5 sha256, GPT entry-array sha256 + CRC,
  sensor calibration acc/gyro/ps): replaced by zero placeholders in every file of this tree (including files already present in
  the public ios-sweet history, where they were scrubbed by the first commit of this branch).
- Rootfs mirror (`rootfs-mirror.img`) and anything extracted from the phone's iOS root (CommCenter/lockdown/keychain/Wi-Fi
  plists, unified logs, crash reports, app data), SSIDs.
- SSH host keys, the jbts password and `payload-jbts1*` (contain key hashes), `secrets/`, adb keys and rescue images built with
  the author's adb key.
- All logs and captures: `autocycle/runs/`, `cc-txn-*`, `cc-fetch-*`, `ui-usb/`, `*.log`, `fpdiag*`, `fp-boot*`, receipts,
  reports, `ANDROID-IMS-LOG/`, `PARKED-audio-logs-*`.
- Large `owned-*.json` payload ownership lists (> 128 KiB): they enumerate paths of the author's root volume (app container
  UUIDs, user files).
- One host test that contained the author's Bluetooth address (`components/bt-20261115/test/t_addr.c`).
- The engineering log `NOTES.md` of the work tree (1.5 MB): it mentions the private reference sources (see section 3) and is
  held back for the owner's decision; this export's commit messages and docs summarise it.
- Owner host paths were rewritten (`$NORD2_WORK`, `$NORD2_REPO`, `$HOME`, `user`).

## 2. Apple and vendor content (never published; get it yourself)
- iOS IPSW/OTA content, dyld shared caches, anything copied from the iOS root (e.g. `com.apple.*` plists, evidence files),
  iOS SDK private headers (files with "Apple Inc. All rights reserved" and no APSL notice), Apple open-source headers copied into
  a fake root (`fakeroot/`).
- Android/vendor partitions and their extracts (`system-x/`, `vendor-x/`, `vfs/`, `system.img`, `vendor.img`, `super-head.bin`),
  MediaTek firmware (Wi-Fi/BT/GNSS/modem/SCP), GNSS boot data header `n2gpsdl_bootdata.h` (embedded vendor data).
- Kernel images, boot images, ramdisks, ADTs, Mach-O binaries, payload binaries, `.img`/`.qcow2`, ISO/VM images.

## 3. Held back for a licence decision (owner to decide)
- **Files derived from the private `ios-tegu` reference sources**: every file whose name or content mentions `tegu` was withheld,
  including the kernel GPU driver `iokit/Kernel/arm/Nord2GPU.cpp`, `bsd/dev/nord2_gpu_transport.h`,
  `iokit/Kernel/arm/sweet_fb_pending_surfaces.h` and patch `0084-nord2-gpu.patch`. Without them the kernel in
  `xnu/stage-q10jfinal44ts/` does not build.
- **GPU userland** (Nord2Metal Metal driver, `libredwoodair.dylib` with Mesa's Valhall compiler, the gfxall payload sources
  `gfx200-20261114/all*-src`, `gpusched-20261114/userland`, `webxr-20261114/src`, the Mesa/Mali compile tools) and all GPU-userland
  work directories (pipeline packs, shader corpora, compiler experiments, springboard candidates, `y2s*`, `l4-*`, `air-*`,
  `metal2vulkan`, `mesa-*`, ...). Only the gfxall deployment glue (`txn_gfxall.sh`, guest scripts, owned list) is included.

The exact path lists of section 3 are below.


### 3a. Withheld: files mentioning tegu (paths in the original work tree)

- `gpu/build/HANDOFF-DEEPSEEK-20261106.md`
- `gpu/build/ansuz-port-20261026/0099/analyze_nord2_sched.py`
- `gpu/build/ansuz-port-20261026/0099/tests/test_analyze_nord2_sched.py`
- `gpu/build/ansuz-port-20261026/LEDGER.md`
- `gpu/build/apps-20261104/guest-inspect4.sh`
- `gpu/build/apps-20261104/payload-storage-purge2/plan.json`
- `gpu/build/apps-20261104/rehearse-storage-purge2.sh`
- `gpu/build/autotest/salvage/src/Nord2GPU.cpp`
- `gpu/build/autotest/salvage/src/nord2_gpu_transport.h`
- `gpu/build/cpu-20261114/sched0099/tests/test_analyze_nord2_sched.py`
- `gpu/build/device-mmu-q10jfinal44ts-candidate/k/kernel/stage/bsd/dev/nord2_gpu_transport.h`
- `gpu/build/device-mmu-q10jfinal44ts-candidate/k/kernel/stage/iokit/Kernel/arm/Nord2GPU.cpp`
- `gpu/build/device-mmu-q10jfinal44ts-candidate/k/kernel/stage/iokit/Kernel/arm/sweet_fb_pending_surfaces.h`
- `gpu/build/host-tests/batch-code-reuse/batch_code_reuse_test.c`
- `gpu/build/host-tests/pass-mrt-cross-fetch/nord2_metal_bridge.h`
- `gpu/build/host-tests/pass-mrt-cross-fetch/pass_mrt_cross_fetch_test.m`
- `gpu/build/host-tests/run_render_semantics.sh`
- `gpu/build/hwcolor-20261112/ctest/sweet_fb_pending_surfaces.h`
- `gpu/build/hwfb-20261112/ctest/Nord2GPU.cpp`
- `gpu/build/hwfb-20261112/ctest/sweet_fb_pending_surfaces.h`
- `gpu/build/kernel-p1-20261026/0085-sweet-fb-surface-retention.patch`
- `gpu/build/kernel-p1-20261026/0111-iokit-trap-fast-path.patch`
- `gpu/build/kernel-p1-20261026/0113-transport-trap-block-yields-to-frontend.patch`
- `gpu/build/kernel-p1-20261026/0188-kernel-stress-clients.patch`
- `gpu/build/kernel-p1-20261026/0189-stress-bootarg-n2st.patch`
- `gpu/build/kernel-p1-20261026/README-0100-0103.md`
- `gpu/build/kernel-p1-20261026/README-0110-0111.md`
- `gpu/build/kernel-p1-20261026/README-0112.md`
- `gpu/build/kernel-p1-20261026/candidate-gpu-source-0111.patch`
- `gpu/build/kernel-p1-20261026/candidate-gpu-source-0113.patch`
- `gpu/build/kernel-p1-20261026/tests-0087-bridge-list-closure.patch`
- `gpu/build/release-20261115/LICENSES-AND-CREDITS.md`
- `gpu/build/release-20261115/RELEASE_PLAN.md`
- `gpu/build/release-20261115/tools/release-files.txt`
- `gpu/build/vcodec-20261114/src/gpu/hwcodec/WORKER_RULES.md`
- `gpu/hwcodec/WORKER_RULES.md`
- `gpu/tools/analyze_ab_run.py`
- `gpu/tools/audit_quartzcore.py`
- `gpu/tools/audit_shader_pipeline_inputs.py`
- `gpu/tools/build_callable_compilers.py`
- `gpu/tools/build_candidate.sh`
- `gpu/tools/build_metal_probe.sh`
- `gpu/tools/build_native_compilers.py`
- `gpu/tools/build_pipeline_pack.py`
- `gpu/tools/build_pipeline_replay.py`
- `gpu/tools/build_tegu_frontend_objects.sh`
- `gpu/tools/check_arch9_contract.sh`
- `gpu/tools/check_update_catalog_fast.py`
- `gpu/tools/expand_mali_variants.py`
- `gpu/tools/frame_bench.py`
- `gpu/tools/frame_bench_main.m`
- `gpu/tools/frame_model.py`
- `gpu/tools/gen_xr_decode_table.py`
- `gpu/tools/generate_convert.c`
- `gpu/tools/generate_preload.c`
- `gpu/tools/generate_preload.sh`
- `gpu/tools/link_metal_bundle.py`
- `gpu/tools/package_candidate.sh`
- `gpu/tools/package_metal_addon.py`
- `gpu/tools/pipeline_cache_report.py`
- `gpu/tools/run_host_tests.sh`
- `gpu/tools/stage_metal_root.py`
- `gpu/tools/test_analyze_ab_run.py`
- `gpu/tools/test_fastroot.py`
- `gpu/tools/test_tiler_matrix.sh`
- `gpu/tools/update_metal_offline_vm.py`
- `gpu/video/PLAN.md`
- `shim/nord2-xnu/Nord2GPU.cpp`
- `shim/nord2-xnu/nord2_gpu_transport.h`
- `xnu/patches/0084-nord2-gpu.patch`

### 3b. Withheld: GPU userland files inside otherwise published directories

- `gpu/build/gpusched-20261114/userland/gpupipe2-nord2_pan.diff`
- `gpu/build/gpusched-20261114/userland/gpupipe2-transport.diff`
- `gpu/build/gpusched-20261114/userland/ship/guest-gpupipe1-undo.sh`
- `gpu/build/gpusched-20261114/userland/ship/guest-gpupipe1.sh`
- `gpu/build/gpusched-20261114/userland/ship/owned-gpupipe1.json`
- `gpu/build/gpusched-20261114/userland/ship/payload-gpupipe1-off/plan.json`
- `gpu/build/gpusched-20261114/userland/ship/payload-gpupipe1-undo/plan.json`
- `gpu/build/gpusched-20261114/userland/ship/payload-gpupipe1/plan.json`
- `gpu/build/gpusched-20261114/userland/ship/rehearse-gpupipe1.sh`
- `gpu/build/gpusched-20261114/userland/ship/rehearse-vm.sh`
- `gpu/build/gpusched-20261114/userland/ship/txn_gpupipe.sh`
- `gpu/build/gpusched-20261114/userland/ship/vm-gpupipe1-off/inputs/plan.json`
- `gpu/build/gpusched-20261114/userland/ship/vm-gpupipe1-undo/inputs/plan.json`
- `gpu/build/gpusched-20261114/userland/ship/vm-gpupipe1/inputs/plan.json`
- `gpu/build/gpusched-20261114/userland/src/air_intrinsics.c`
- `gpu/build/gpusched-20261114/userland/src/air_isp.c`
- `gpu/build/gpusched-20261114/userland/src/air_metal.c`
- `gpu/build/gpusched-20261114/userland/src/air_metal.h`
- `gpu/build/gpusched-20261114/userland/src/air_to_nir.c`
- `gpu/build/gpusched-20261114/userland/src/bench-host-test.sh`
- `gpu/build/gpusched-20261114/userland/src/blur_chain_host_test.c`
- `gpu/build/gpusched-20261114/userland/src/blur_nir_host_test.c`
- `gpu/build/gpusched-20261114/userland/src/build.sh`
- `gpu/build/gpusched-20261114/userland/src/half_store_host_test.c`
- `gpu/build/gpusched-20261114/userland/src/host-tests.sh`
- `gpu/build/gpusched-20261114/userland/src/image_slots_host_test.c`
- `gpu/build/gpusched-20261114/userland/src/isp_bench_host_test.c`
- `gpu/build/gpusched-20261114/userland/src/isp_gpu_host_test.c`
- `gpu/build/gpusched-20261114/userland/src/isp_nr_host_test.c`
- `gpu/build/gpusched-20261114/userland/src/isp_nrg_host_test.c`
- `gpu/build/gpusched-20261114/userland/src/isp_stats_host_test.c`
- `gpu/build/gpusched-20261114/userland/src/isp_warm_host_test.c`
- `gpu/build/gpusched-20261114/userland/src/m2m.h`
- `gpu/build/gpusched-20261114/userland/src/metallib_compile_host_test.c`
- `gpu/build/gpusched-20261114/userland/src/nr-host-test.sh`
- `gpu/build/gpusched-20261114/userland/src/nr_reference.inc`
- `gpu/build/gpusched-20261114/userland/src/pan-new-host-test.sh`
- `gpu/build/gpusched-20261114/userland/src/pan_compile_host_test.c`
- `gpu/build/gpusched-20261114/userland/src/pan_compile_new_host_test.c`
- `gpu/build/gpusched-20261114/userland/src/pan_jm.c`
- `gpu/build/gpusched-20261114/userland/src/plane_fit_host_test.sh`
- `gpu/build/gpusched-20261114/userland/src/redwood-drm/gpupipe_host_test.c`
- `gpu/build/gpusched-20261114/userland/src/redwood-drm/nord2_gpu_transport.h`
- `gpu/build/gpusched-20261114/userland/src/redwood-drm/nord2_pan.c`
- `gpu/build/gpusched-20261114/userland/src/redwood-drm/nord2_pan.h`
- `gpu/build/gpusched-20261114/userland/src/redwood-drm/nord2_pan_ios.c`
- `gpu/build/gpusched-20261114/userland/src/redwood-drm/nord2_pan_pressure_host_test.c`
- `gpu/build/gpusched-20261114/userland/src/redwood-drm/pressure-host-test.sh`
- `gpu/build/gpusched-20261114/userland/src/redwood-drm/redwood_drm.c`
- `gpu/build/gpusched-20261114/userland/src/redwood-drm/xf86drm.h`
- `gpu/build/gpusched-20261114/userland/src/redwooddrawable.m`
- `gpu/build/gpusched-20261114/userland/src/redwoodmetal.m`
- `gpu/build/gpusched-20261114/userland/src/rw-api99a-host-test.sh`
- `gpu/build/gpusched-20261114/userland/src/rw-texlayout-host-test.sh`
- `gpu/build/gpusched-20261114/userland/src/rw_api99a_host_test.c`
- `gpu/build/gpusched-20261114/userland/src/rw_api99a_pure.h`
- `gpu/build/gpusched-20261114/userland/src/rw_texlayout_atx_check.c`
- `gpu/build/gpusched-20261114/userland/src/rw_texlayout_host_test.c`
- `gpu/build/gpusched-20261114/userland/src/rw_texlayout_pure.h`
- `gpu/build/gpusched-20261114/userland/src/rw_thumbgrid_host_test.c`
- `gpu/build/gpusched-20261114/userland/src/rw_thumbgrid_pure.h`
- `gpu/build/gpusched-20261114/userland/src/rwa-pan-fb-host-test.sh`
- `gpu/build/gpusched-20261114/userland/src/rwa_host_widen16.c`
- `gpu/build/gpusched-20261114/userland/src/rwa_isp_api.h`
- `gpu/build/gpusched-20261114/userland/src/rwa_pan_fb.c`
- `gpu/build/gpusched-20261114/userland/src/rwa_pan_fb_host_test.c`
- `gpu/build/gpusched-20261114/userland/src/rwa_pressure_pan.c`
- `gpu/build/gpusched-20261114/userland/src/shader99b_numeric_host_test.c`
- `gpu/build/gpusched-20261114/userland/src/stats-host-test.sh`
- `gpu/build/gpusched-20261114/userland/src/texture_choice_host_test.c`
- `gpu/build/gpusched-20261114/userland/src/thumbgrid-host-test.sh`
- `gpu/build/gpusched-20261114/userland/src/translator_numeric_host_test.c`
- `gpu/build/gpusched-20261114/userland/src/ycbcr_gpu_host_test.c`
- `gpu/build/gpusched-20261114/userland/src4/air_intrinsics.c`
- `gpu/build/gpusched-20261114/userland/src4/air_isp.c`
- `gpu/build/gpusched-20261114/userland/src4/air_metal.c`
- `gpu/build/gpusched-20261114/userland/src4/air_metal.h`
- `gpu/build/gpusched-20261114/userland/src4/air_to_nir.c`
- `gpu/build/gpusched-20261114/userland/src4/bench-host-test.sh`
- `gpu/build/gpusched-20261114/userland/src4/blur_chain_host_test.c`
- `gpu/build/gpusched-20261114/userland/src4/blur_nir_host_test.c`
- `gpu/build/gpusched-20261114/userland/src4/build.sh`
- `gpu/build/gpusched-20261114/userland/src4/half_store_host_test.c`
- `gpu/build/gpusched-20261114/userland/src4/host-tests.sh`
- `gpu/build/gpusched-20261114/userland/src4/image_slots_host_test.c`
- `gpu/build/gpusched-20261114/userland/src4/isp_bench_host_test.c`
- `gpu/build/gpusched-20261114/userland/src4/isp_gpu_host_test.c`
- `gpu/build/gpusched-20261114/userland/src4/isp_nr_host_test.c`
- `gpu/build/gpusched-20261114/userland/src4/isp_nrg_host_test.c`
- `gpu/build/gpusched-20261114/userland/src4/isp_stats_host_test.c`
- `gpu/build/gpusched-20261114/userland/src4/isp_warm_host_test.c`
- `gpu/build/gpusched-20261114/userland/src4/m2m.h`
- `gpu/build/gpusched-20261114/userland/src4/metallib_compile_host_test.c`
- `gpu/build/gpusched-20261114/userland/src4/nr-host-test.sh`
- `gpu/build/gpusched-20261114/userland/src4/nr_reference.inc`
- `gpu/build/gpusched-20261114/userland/src4/pan-new-host-test.sh`
- `gpu/build/gpusched-20261114/userland/src4/pan_compile_host_test.c`
- `gpu/build/gpusched-20261114/userland/src4/pan_compile_new_host_test.c`
- `gpu/build/gpusched-20261114/userland/src4/pan_jm.c`
- `gpu/build/gpusched-20261114/userland/src4/plane_fit_host_test.sh`
- `gpu/build/gpusched-20261114/userland/src4/redwood-drm/gpupipe2_host_test.c`
- `gpu/build/gpusched-20261114/userland/src4/redwood-drm/gpupipe_host_test.c`
- `gpu/build/gpusched-20261114/userland/src4/redwood-drm/nord2_gpu_transport.h`
- `gpu/build/gpusched-20261114/userland/src4/redwood-drm/nord2_pan.c`
- `gpu/build/gpusched-20261114/userland/src4/redwood-drm/nord2_pan.h`
- `gpu/build/gpusched-20261114/userland/src4/redwood-drm/nord2_pan_ios.c`
- `gpu/build/gpusched-20261114/userland/src4/redwood-drm/nord2_pan_pressure_host_test.c`
- `gpu/build/gpusched-20261114/userland/src4/redwood-drm/pressure-host-test.sh`
- `gpu/build/gpusched-20261114/userland/src4/redwood-drm/redwood_drm.c`
- `gpu/build/gpusched-20261114/userland/src4/redwood-drm/xf86drm.h`
- `gpu/build/gpusched-20261114/userland/src4/redwooddrawable.m`
- `gpu/build/gpusched-20261114/userland/src4/redwoodmetal.m`
- `gpu/build/gpusched-20261114/userland/src4/rw-api99a-host-test.sh`
- `gpu/build/gpusched-20261114/userland/src4/rw-texlayout-host-test.sh`
- `gpu/build/gpusched-20261114/userland/src4/rw_api99a_host_test.c`
- `gpu/build/gpusched-20261114/userland/src4/rw_api99a_pure.h`
- `gpu/build/gpusched-20261114/userland/src4/rw_texlayout_atx_check.c`
- `gpu/build/gpusched-20261114/userland/src4/rw_texlayout_host_test.c`
- `gpu/build/gpusched-20261114/userland/src4/rw_texlayout_pure.h`
- `gpu/build/gpusched-20261114/userland/src4/rw_thumbgrid_host_test.c`
- `gpu/build/gpusched-20261114/userland/src4/rw_thumbgrid_pure.h`
- `gpu/build/gpusched-20261114/userland/src4/rwa-pan-fb-host-test.sh`
- `gpu/build/gpusched-20261114/userland/src4/rwa_host_widen16.c`
- `gpu/build/gpusched-20261114/userland/src4/rwa_isp_api.h`
- `gpu/build/gpusched-20261114/userland/src4/rwa_pan_fb.c`
- `gpu/build/gpusched-20261114/userland/src4/rwa_pan_fb_host_test.c`
- `gpu/build/gpusched-20261114/userland/src4/rwa_pressure_pan.c`
- `gpu/build/gpusched-20261114/userland/src4/shader99b_numeric_host_test.c`
- `gpu/build/gpusched-20261114/userland/src4/stats-host-test.sh`
- `gpu/build/gpusched-20261114/userland/src4/texture_choice_host_test.c`
- `gpu/build/gpusched-20261114/userland/src4/thumbgrid-host-test.sh`
- `gpu/build/gpusched-20261114/userland/src4/translator_numeric_host_test.c`
- `gpu/build/gpusched-20261114/userland/src4/ycbcr_gpu_host_test.c`
- `gpu/build/webxr-20261114/base-src/air_intrinsics.c`
- `gpu/build/webxr-20261114/base-src/air_isp.c`
- `gpu/build/webxr-20261114/base-src/air_metal.c`
- `gpu/build/webxr-20261114/base-src/air_metal.h`
- `gpu/build/webxr-20261114/base-src/air_to_nir.c`
- `gpu/build/webxr-20261114/base-src/bench-host-test.sh`
- `gpu/build/webxr-20261114/base-src/blur_chain_host_test.c`
- `gpu/build/webxr-20261114/base-src/blur_nir_host_test.c`
- `gpu/build/webxr-20261114/base-src/build.sh`
- `gpu/build/webxr-20261114/base-src/half_store_host_test.c`
- `gpu/build/webxr-20261114/base-src/host-tests.sh`
- `gpu/build/webxr-20261114/base-src/image_slots_host_test.c`
- `gpu/build/webxr-20261114/base-src/isp_bench_host_test.c`
- `gpu/build/webxr-20261114/base-src/isp_gpu_host_test.c`
- `gpu/build/webxr-20261114/base-src/isp_nr_host_test.c`
- `gpu/build/webxr-20261114/base-src/isp_nrg_host_test.c`
- `gpu/build/webxr-20261114/base-src/isp_stats_host_test.c`
- `gpu/build/webxr-20261114/base-src/isp_warm_host_test.c`
- `gpu/build/webxr-20261114/base-src/m2m.h`
- `gpu/build/webxr-20261114/base-src/metallib_compile_host_test.c`
- `gpu/build/webxr-20261114/base-src/nr-host-test.sh`
- `gpu/build/webxr-20261114/base-src/nr_reference.inc`
- `gpu/build/webxr-20261114/base-src/pan-new-host-test.sh`
- `gpu/build/webxr-20261114/base-src/pan_compile_host_test.c`
- `gpu/build/webxr-20261114/base-src/pan_compile_new_host_test.c`
- `gpu/build/webxr-20261114/base-src/plane_fit_host_test.sh`
- `gpu/build/webxr-20261114/base-src/redwood-drm/nord2_gpu_transport.h`
- `gpu/build/webxr-20261114/base-src/redwood-drm/nord2_pan.c`
- `gpu/build/webxr-20261114/base-src/redwood-drm/nord2_pan.h`
- `gpu/build/webxr-20261114/base-src/redwood-drm/nord2_pan_ios.c`
- `gpu/build/webxr-20261114/base-src/redwood-drm/nord2_pan_pressure_host_test.c`
- `gpu/build/webxr-20261114/base-src/redwood-drm/pressure-host-test.sh`
- `gpu/build/webxr-20261114/base-src/redwood-drm/redwood_drm.c`
- `gpu/build/webxr-20261114/base-src/redwood-drm/xf86drm.h`
- `gpu/build/webxr-20261114/base-src/redwooddrawable.m`
- `gpu/build/webxr-20261114/base-src/redwoodmetal.m`
- `gpu/build/webxr-20261114/base-src/rw-api99a-host-test.sh`
- `gpu/build/webxr-20261114/base-src/rw-texlayout-host-test.sh`
- `gpu/build/webxr-20261114/base-src/rw_api99a_host_test.c`
- `gpu/build/webxr-20261114/base-src/rw_api99a_pure.h`
- `gpu/build/webxr-20261114/base-src/rw_texlayout_atx_check.c`
- `gpu/build/webxr-20261114/base-src/rw_texlayout_host_test.c`
- `gpu/build/webxr-20261114/base-src/rw_texlayout_pure.h`
- `gpu/build/webxr-20261114/base-src/rw_thumbgrid_host_test.c`
- `gpu/build/webxr-20261114/base-src/rw_thumbgrid_pure.h`
- `gpu/build/webxr-20261114/base-src/rwa-pan-fb-host-test.sh`
- `gpu/build/webxr-20261114/base-src/rwa_host_widen16.c`
- `gpu/build/webxr-20261114/base-src/rwa_isp_api.h`
- `gpu/build/webxr-20261114/base-src/rwa_pan_fb.c`
- `gpu/build/webxr-20261114/base-src/rwa_pan_fb_host_test.c`
- `gpu/build/webxr-20261114/base-src/rwa_pressure_pan.c`
- `gpu/build/webxr-20261114/base-src/shader99b_numeric_host_test.c`
- `gpu/build/webxr-20261114/base-src/stats-host-test.sh`
- `gpu/build/webxr-20261114/base-src/texture_choice_host_test.c`
- `gpu/build/webxr-20261114/base-src/thumbgrid-host-test.sh`
- `gpu/build/webxr-20261114/base-src/translator_numeric_host_test.c`
- `gpu/build/webxr-20261114/base-src/ycbcr_gpu_host_test.c`
- `gpu/build/webxr-20261114/host/build-webxr-test.sh`
- `gpu/build/webxr-20261114/src/air_intrinsics.c`
- `gpu/build/webxr-20261114/src/air_isp.c`
- `gpu/build/webxr-20261114/src/air_metal.c`
- `gpu/build/webxr-20261114/src/air_metal.h`
- `gpu/build/webxr-20261114/src/air_to_nir.c`
- `gpu/build/webxr-20261114/src/bench-host-test.sh`
- `gpu/build/webxr-20261114/src/blur_chain_host_test.c`
- `gpu/build/webxr-20261114/src/blur_nir_host_test.c`
- `gpu/build/webxr-20261114/src/build.sh`
- `gpu/build/webxr-20261114/src/half_store_host_test.c`
- `gpu/build/webxr-20261114/src/host-tests.sh`
- `gpu/build/webxr-20261114/src/image_slots_host_test.c`
- `gpu/build/webxr-20261114/src/isp_bench_host_test.c`
- `gpu/build/webxr-20261114/src/isp_gpu_host_test.c`
- `gpu/build/webxr-20261114/src/isp_nr_host_test.c`
- `gpu/build/webxr-20261114/src/isp_nrg_host_test.c`
- `gpu/build/webxr-20261114/src/isp_stats_host_test.c`
- `gpu/build/webxr-20261114/src/isp_warm_host_test.c`
- `gpu/build/webxr-20261114/src/m2m.h`
- `gpu/build/webxr-20261114/src/metallib_compile_host_test.c`
- `gpu/build/webxr-20261114/src/nr-host-test.sh`
- `gpu/build/webxr-20261114/src/nr_reference.inc`
- `gpu/build/webxr-20261114/src/pan-new-host-test.sh`
- `gpu/build/webxr-20261114/src/pan_compile_host_test.c`
- `gpu/build/webxr-20261114/src/pan_compile_new_host_test.c`
- `gpu/build/webxr-20261114/src/plane_fit_host_test.sh`
- `gpu/build/webxr-20261114/src/redwood-drm/nord2_gpu_transport.h`
- `gpu/build/webxr-20261114/src/redwood-drm/nord2_pan.c`
- `gpu/build/webxr-20261114/src/redwood-drm/nord2_pan.h`
- `gpu/build/webxr-20261114/src/redwood-drm/nord2_pan_ios.c`
- `gpu/build/webxr-20261114/src/redwood-drm/nord2_pan_pressure_host_test.c`
- `gpu/build/webxr-20261114/src/redwood-drm/pressure-host-test.sh`
- `gpu/build/webxr-20261114/src/redwood-drm/redwood_drm.c`
- `gpu/build/webxr-20261114/src/redwood-drm/xf86drm.h`
- `gpu/build/webxr-20261114/src/redwooddrawable.m`
- `gpu/build/webxr-20261114/src/redwoodmetal.m`
- `gpu/build/webxr-20261114/src/rw-api99a-host-test.sh`
- `gpu/build/webxr-20261114/src/rw-texlayout-host-test.sh`
- `gpu/build/webxr-20261114/src/rw_api99a_host_test.c`
- `gpu/build/webxr-20261114/src/rw_api99a_pure.h`
- `gpu/build/webxr-20261114/src/rw_texlayout_atx_check.c`
- `gpu/build/webxr-20261114/src/rw_texlayout_host_test.c`
- `gpu/build/webxr-20261114/src/rw_texlayout_pure.h`
- `gpu/build/webxr-20261114/src/rw_thumbgrid_host_test.c`
- `gpu/build/webxr-20261114/src/rw_thumbgrid_pure.h`
- `gpu/build/webxr-20261114/src/rwa-pan-fb-host-test.sh`
- `gpu/build/webxr-20261114/src/rwa_host_widen16.c`
- `gpu/build/webxr-20261114/src/rwa_isp_api.h`
- `gpu/build/webxr-20261114/src/rwa_pan_fb.c`
- `gpu/build/webxr-20261114/src/rwa_pan_fb_host_test.c`
- `gpu/build/webxr-20261114/src/rwa_pressure_pan.c`
- `gpu/build/webxr-20261114/src/shader99b_numeric_host_test.c`
- `gpu/build/webxr-20261114/src/stats-host-test.sh`
- `gpu/build/webxr-20261114/src/texture_choice_host_test.c`
- `gpu/build/webxr-20261114/src/thumbgrid-host-test.sh`
- `gpu/build/webxr-20261114/src/translator_numeric_host_test.c`
- `gpu/build/webxr-20261114/src/webxr_host_test.c`
- `gpu/build/webxr-20261114/src/ycbcr_gpu_host_test.c`
- `gpu/tools/build_mesa_arch9.sh`
- `gpu/tools/generate_mesa_pack_subset.py`
- `gpu/tools/nord2_mali_compile.c`
- `gpu/video/gpu/air_video.c`

### 3c. Withheld: GPU userland / compiler work directories (`gpu/build/<dir>`, whole directory)

`air-compute-20261028`, `air-nir-20261028`, `air-plan-20261028`, `api-surface-20261027`, `c6f-lowering-20261026`, `c6t-rca-20261028`, `compile-speed-20261026`, `compile-stall-20261026`, `encode-cpu-20261026`, `frame-bench-20261026`, `gfx100a-20261114`, `gfx200-20261114`, `ios-tegu-compare-20261026`, `l4-c5-20261026`, `l4-c6-20261026`, `l4-c6b-20261026`, `l4-c6c-20261026`, `l4-c6d-20261026`, `l4-c6e-20261026`, `l4-c6f-20261026`, `l4-c6g-20261026`, `l4-c6h-20261026`, `l4-c6i-20261026`, `l4-c6j-20261026`, `l4-c6k-20261026`, `l4-c6l-20261026`, `l4-c6m-20261027`, `l4-c6n-20261027`, `l4-c6o-20261028`, `l4-c6p-20261028`, `l4-c6q-20261028`, `l4-c6r-20261028`, `l4-c6s-20261028`, `l4-c6t-20261028`, `l4-c6u-20261028`, `l4-c6v-20261029`, `l4-c6v2-20261029`, `lag-analysis-20261028`, `mesa-darwin-eval-20261026`, `patch-backup-20261108`, `pipeline-cache-20261026`, `pipeline-pack-20261026`, `regression-hunt-20261028`, `render-lead-a3-20261028`, `shader-coverage-20261027`, `springboard-candidate-c4-compositor-20261026`, `springboard-candidate-c4-safe-20261026`, `springboard-candidate-c5-compositor-20261026`, `springboard-candidate-c5-safe-20261026`, `springboard-candidate-c6-compositor-20261026`, `springboard-candidate-c6-safe-20261026`, `springboard-candidate-c6b-compositor-20261026`, `springboard-candidate-c6b-safe-20261026`, `springboard-candidate-c6c-compositor-20261026`, `springboard-candidate-c6c-safe-20261026`, `springboard-candidate-c6d-compositor-20261026`, `springboard-candidate-c6d-safe-20261026`, `springboard-candidate-c6e-compositor-20261026`, `springboard-candidate-c6e-safe-20261026`, `springboard-candidate-c6f-compositor-20261026`, `springboard-candidate-c6f-safe-20261026`, `springboard-candidate-c6g-compositor-20261026`, `springboard-candidate-c6g-safe-20261026`, `springboard-candidate-c6h-compositor-20261026`, `springboard-candidate-c6h-safe-20261026`, `springboard-candidate-c6i-compositor-20261026`, `springboard-candidate-c6i-safe-20261026`, `springboard-candidate-c6j-batch-20261026`, `springboard-candidate-c6j-compositor-20261026`, `springboard-candidate-c6k-ab-20261026`, `springboard-candidate-c6k-ab2-20261026`, `springboard-candidate-c6l-20261026`, `springboard-candidate-c6l2-20261026`, `springboard-candidate-c6m-20261027`, `springboard-candidate-c6n-20261027`, `springboard-candidate-c6o-20261028`, `springboard-candidate-c6p-20261028`, `springboard-candidate-c6q-20261028`, `springboard-candidate-c6r-20261028`, `springboard-candidate-c6r-gpu-20261028`, `springboard-candidate-c6r-notmpl-20261028`, `springboard-candidate-c6r-safe-20261028`, `springboard-candidate-c6t-20261028`, `springboard-candidate-c6t-nocuts-20261028`, `springboard-candidate-c6u-20261028`, `springboard-candidate-c6v-20261029`, `springboard-candidate-c6v2-20261029`, `texture-coverage-20261026`, `transaction-speed-20261026`, `xnu-stage-backup-pre-p15-20261028`

### 1a. Withheld: files with embedded data blobs (modem NVRAM tree, vendor boot data)

- `gpu/build/kernel-p1-20261026/0260-nord2-modem-fs-server.patch`
- `gpu/build/kernel-p1-20261026/0261-nord2-modem-device-maps-early-rings.patch`
- `gpu/build/kernel-p1-20261026/0262-nord2-modem-nvdisk-fallback.patch`
- `gpu/build/kernel-p1-20261026/0263-nord2-modem-at-client.patch`
- `gpu/build/kernel-p1-20261026/0265-nord2-modem-full-start3s.patch`
- `gpu/build/kernel-p1-20261026/0266-nord2-modem-full-start3s-nopause.patch`
- `gpu/build/kernel-p1-20261026/0267-nord2-modem-full-start10s.patch`
- `gpu/build/kernel-p1-20261026/0268-nord2-modem-one-ring-mapping.patch`
- `gpu/build/kernel-p1-20261026/0269-nord2-modem-ccb-ctrl.patch`
- `gpu/build/kernel-p1-20261026/0270-nord2-modem-at-registration-poll.patch`
- `gpu/build/kernel-p1-20261026/0271-nord2-modem-at-data.patch`
- `gpu/build/kernel-p1-20261026/0272-nord2-modem-persistent-p1-test.patch`
- `gpu/build/kernel-p1-20261026/0273-nord2-modem-daily.patch`
- `gpu/build/kernel-p1-20261026/0274-nord2-modem-dpmaif-datapath.patch`
- `gpu/build/kernel-p1-20261026/0275-nord2-modem-p2t.patch`
- `gpu/build/kernel-p1-20261026/0276-nord2-modem-p2f.patch`
- `gpu/hwcodec/gnss/probe/n2gpsdl_bootdata.h`

### Other exclusion counts (by rule)

```
excluded {'ext': 18054, 'prunedir': 2177, 'denyname': 7787, 'json-not-config': 1329, 'txt-not-doc': 331, 'prune-component': 2, 'gpu-userland-dir': 90, 'apple-plist': 26, 'notfile': 653, 'empty': 5, 'toobig': 2, 'kernel-source-copy': 7989, 'owned-json-large': 23, 'binary': 13, 'vendor-extract': 138, 'skip-explicit': 155}
```

