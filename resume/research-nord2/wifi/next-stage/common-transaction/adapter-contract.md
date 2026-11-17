# Concrete current-owner predicate for cold -> common diagnostic

Conclusion: the existing measured transition from whole-CONN OFF, both rails off, bus isolated, through this probe's sole power-up establishes a credible fresh ownership origin. XNU has no BT/GPS/FM/Wi-Fi executing drivers or firmware release in this path. This is materially stronger than a generic software lock acquired over an already-live domain. Add bounded, allowlisted rejection checks for the actual BGF/FM requests and power acknowledgements; do not demand an undefined external “all-master idle” oracle. `NCC_ALL_MASTERS_IDLE` should mean THIS conjunction and provenance, not a generic runtime API.

Linux's equivalent origin is `curr_status==0` under `infra_ctx->core_lock` (`core/conninfra_core.c:213,285–329`; `platform/consys_hw.c:312–370`). That curr_status is built from in-kernel driver state, not a hardware global master-idle register. The selected first-on path configures D-die and SPI BEFORE BT/GPS subsystem functions are enabled. We can reproduce that diagnostic phase without releasing any CPU or executing A-die/RF commands.

## Native adapter allowlist and zero predicate

All reads below are performed ONLY after existing AP power/clock/bus guards and chipID20010101. Require every listed masked value zero; record raw values, validmask and first failing field. These are conservative rejection predicates, not invented reset-value assertions. In particular, source documents BGF ack bits being ONE when powered, so rejecting any asserted ack is conservative; no claim that a retained zero CPU-reset bit alone proves domain off.

| Address | mask | Selected meaning/source |
|---|---:|---|
|18000010|1|existing WFSYS CPU reset-not; soc3_0 CPU reset handling|
|18000000|80|existing WFSYS top power request|
|180602cc|4|existing WFSYS power-state bit|
|18000008|80|BGFSYS top power request, mt6893_pos.c:1790–1798/1837–1844|
|18000014|1|BGF CPU reset-not; exact BT platform_mt6893.h:62–63,982,1151,1305|
|18000114|03000000|BGF ON primary+secondary power acknowledgements; same header:68–69,999–1004; GPS v010 also polls these|
|18000124|01000003|BGF OFF-domain power acknowledgements, NOT an inverted “domain off” flag; same header:71–73,1014–1033|
|18001874|1|BT function enable; exact BT header:94–95,988,1302|
|18001878|1|GPS function enable; GPS v010 conn_infra_cfg.h:14,60–62, gps_dl_hw_dep_bgf.c:29|
|18008040|1|FM oscillator enable; FM conn_infra.c:1076–1078,1777; MT6893 DTS selects family6885, host6893 at6403–6408|
|180601a8|1|BT force wake request; exact BT header:108,279,1330|
|180601ac|1|GPS force wake request; GPS v010 conn_host_csr_top.h:14,38–40|
|180601b0|1|FM force wake request; FM family6885 mapping conn_infra.c:1779–1780|
|18004000|3e|RF SPI busy flags WF/BT/FM/GPS/TOP bits1..5; mt6893_pos.c:1926–1979,1987–2007|

No RF command is issued by reading SPI_STA. Do not read powered-off BGF188xxxxx/GPS18cxxxxx subordinate buses, PC debug, efuse, calibration or firmware memory. No selector writes are necessary. Do not use SYSRAM18052834 RADIO_STATUS as proof: it is retained host software bookkeeping, updated under a semaphore, not physical power/CPU state. Do not use MD coexistence10003318 as a power authority: selected BT source describes it as host-to-modem notification; source does not identify a modem-owned SPI enable there.

MT6893 GPS applicability is explicit in gps/Makefile:105–107 selecting v010, whose power function enables GPS then polls BGF primary+secondary acknowledgements. MT6893 BT applicability is explicit in btmtk_chip_if.h:23–24 selecting platform_mt6893.h. FM applicability is explicit in arch/arm64/boot/dts/mediatek/mt6893.dts:6403–6408 selecting family6885. Do NOT substitute v030/v050 or MT6983 registers.

## Execution contract

1. Outer gate remains nord2wifi=2; new exact optin selects one diagnostic. Require captured original nw.pwr low5=0x12, both SPM CONN ack bits0, protected bus, GALS disabled, PMIC13/18 monitors0, no host radio driver started, and nw.saved armed BEFORE power-up. Store an explicit private `fresh_owned` marker ONLY after those original checks succeed. Do not derive this from post-power `nw.saved` alone.
2. Within existing post-ID window run bounded AP guards and all14 reads. Reject any invalid/nonzero field. Read them a second time after barriers before entering transaction; no timeout/retry/poll necessary. A noncold result simply skips the inner transaction and runs existing outer restore.
3. Map only needed apertures: existing CFG18001000/host18060000; RGU18000000; SPI18004000; WTSLP18005000; FM18008000. Each4KiB. Guard AP access before CONN reads. AP protection clear/readability is the existing operational MMIO-access proof; ambiguous EMI-MPU SMC results are unrelated to these register apertures and must not become a prerequisite.
4. Native `check(NCC_REQUIRED)` rechecks provenance/owner marker, AP guards and all14 zero fields. This is bounded but costs14 CONN reads per check. It introduces no sleep/waitlock and performs no writes. Its own read failures skip mutation or leave dirty result as specified. This diagnostic is serialized in the same sole input/probe thread, never a parallel worker.
5. Pass stage2 to ncc_run. Table writes do not touch any guard field, so the predicate remains valid throughout normal apply/restore. On failure, always run ncc reverse restoration while AP/cold guards remain safe. If AP access disappears, do not force MMIO; dirty remains explicit. Outer restore remains unconditional. A dirty inner transaction must refuse normal boot handoff (a full outer power-down may physically reset some registers, but is not proof that each saved value was restored).
6. Log bounded metadata and masks only; never claim firmware execution/RF/networking. Clear fresh_owned only after outer restoration has verified.

The logical limitation is explicit: local reads cannot prevent an independent secure-world writer appearing after the check. Neither Linux's software curr_status/core_lock nor this XNU diagnostic can prove that against arbitrary hostile firmware. For this measured cold-origin diagnostic with no released CONN CPUs, no XNU radio users, all function/request/power bits zero and SPI busy clear, there is no source-identified live client requiring another semaphore or mystery MMIO read. This is a reviewable bounded predicate; if hardware shows any conflicting bit, skip, capture exact raw evidence, and resolve that particular field rather than blindly expanding writes.

Primary URLs, selected commit04d00c25:
https://github.com/<github-user>/android_kernel_oplus_mt6893/blob/04d00c25/drivers/misc/mediatek/connectivity/bt/mt66xx/include/btif/platform_mt6893.h
https://github.com/<github-user>/android_kernel_oplus_mt6893/blob/04d00c25/drivers/misc/mediatek/connectivity/gps/Makefile
https://github.com/<github-user>/android_kernel_oplus_mt6893/blob/04d00c25/drivers/misc/mediatek/connectivity/gps/data_link/plat/v010/gps_dl_hw_dep_bgf.c
https://github.com/<github-user>/android_kernel_oplus_mt6893/blob/04d00c25/drivers/misc/mediatek/connectivity/fmradio/plat/conn_infra.c
https://github.com/<github-user>/android_kernel_oplus_mt6893/blob/04d00c25/arch/arm64/boot/dts/mediatek/mt6893.dts
https://github.com/<github-user>/android_kernel_oplus_mt6893/blob/04d00c25/drivers/misc/mediatek/connectivity/conninfra/core/conninfra_core.c
