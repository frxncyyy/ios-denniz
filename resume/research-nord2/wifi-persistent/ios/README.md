# Private offline persistent phase10 image templates

Planned `shim/out/nord2-xnu/nord2-ios-wifi-persistent-stop23.img`. No build, CI, device operation or approval performed here. Parent integrates reviewed source, verifies new CI/source/kernel identity, prepares and independently reviews exact image before requesting concrete boot-only approval including optional conditional manual recovery.

Exact commandline derives from working8ca8ee39 manifest: remove only `nord2wificommon=1` and `nord2wifiadieid=1`, append `nord2wifipersist=1`. Keep `nord2wifi=2`, `nord2wifiemi=0`, no legacy opt-in, all nonWiFi tokens unchanged. Explicit persistent source branch replaces the restoring common/ID diagnostic; no old restoration transaction is expected. V2 selected PMIC lowbits0->2->3/rail order and build3 require source/link review; templates do not independently prove hardware behavior.

Identity, prepared-image identity and authorization JSON remain blank/false. Offline build/verifier retains exact Linux/both DTs/rescue CPIO/product ADT/header ID/all loaded fixed-up kernel segment checks. Exact persistent marker strings must occur in actual kernel and loaded segments. Verifier pins working8ca8 manifest and exact WiFi-mode-only arg transformation. Exclusive new manifest/payload output; no overwrite.

Fresh once-only runner pins prepared image, current979221654522038778ae470dbbc8d299775360994fbe93626d462f39ab984a85 (33282048bytes), working8ca8ee39 (33282048bytes),3b2e, stock backup and tested rescue before USB, plus installed root readback proof. Prepared new identity and exact human approval required. Persistent path cannot automatically return to Linux or tear down radio. Recovery requires physical reset to fastboot and selected known fallback/rescue restore within separately recorded conditional authorization; runner does not automatically recover. Never boot stock Android over iOS userdata.

Expected exact observations from reviewed native v2/XNU binding:

- `NORD2 WIFI PERSIST: retained=1 phase=10 id=0x66358a11 handoff=2`
- `NORD2 WIFI: power probe stage=3 id=0x20010101 cfg=0x3 success=1`
- `NORD2 WIFI: retained=1 restored=0 persistent source stage10`

Source permits no new CONN MMIO/guard after terminal source HW-control handoff; logs report software result only. Successful phase10 retains common configuration, PMIC/power and ownership. This is selected source initialization, not network readiness, RF operation, firmware CPU execution, persistent MMIO readability or effective DRAM permission proof. Cold subsystem SYSRAM clearing is distinct from CONN/MCIF DRAM access. No MPU changes, firmware placement or WFSYS/BGFSYS CPU release in this scope.

Parser uses exact preserved UFS-only function from prior proven parser (`live-base.py`) and independently validates new persistent/outer retained records, all four known firmware blob verification sizes+successful probe result, ordering, duplicates, truncation, UTF8 and panic/fatal absence. It rejects old COMMON/ADIE/EMI diagnostics and outer restoration in this persistent capture. Firmware RAM verification proves blob integrity only. Synthetic tests do not claim hardware success. Missing baseline or failed/panicked transition cannot become success through old common restoration inference.

`test-template.py` performs only fresh-disposable-copy nullidentity/runner checks, exact args+shell syntax and minimal synthetic success/failure tests. It remains safe after parent later fills actual identity; do not run older suites that expect current identity to remain blank. `test.log` records current PASS. Source-contract identities are in `source-contract.json`.
