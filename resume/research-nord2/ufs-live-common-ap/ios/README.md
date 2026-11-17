# OFFLINE templates only — no image built, no hardware success claimed

Planned image: `shim/out/nord2-xnu/nord2-ios-ufs2-wifi-ap-stop23.img`.
Exact args in build-image.sh/verify-image.py/template.json: current8core/stop23 RWroot display/input+verifiedfirmware/commonobserver, EMI0, PLUS nord2ufs2live=1. No nord2ufs2lane token, even0. Same preserved Linux+bothAndroidDTs, exact rescueCPIO/Zopfli15container and productADT. No wlanaddition, A-dieID/RF/firmwareexecution.

`identity-required.json` intentionally leaves CIrun/sourceSHA/kernelpath/kernelSHA/CIJSON null and parent_verifiedfalse. Builddriver asks verifier for kernelpath; verifier hardfails before any build until parent supplies and verifies ALL fields. This is not an instruction to start a build when fields are filled. Parent owns final CIartifact checks, build invocation and hardware authorization. No device runner exists here. No finalmanifest/payload/image was generated.

`verify-image.py` preserves independent container checks: pinned originalLinux/DTs, exact decompressedCPIO/SHA, AndroidheaderID, payload commandline byteequality, CIstatus/source/SHA256SUMS+parentpinnedkernelhash and bytecomparison of every stripped/fixedup MachOsegment. Adds actualpayloadAppleADT equality against Nord2generator+exactproductADT. Future verifier refuses overwriting evidence and only then may save its result when parent runs it; nothing has been run past blockedpreflight here.

## Live-mode semantics (not the old stopdiagnostic)

- Success means retained x2 with phase5/60commands/18SETs/2reads, activeverified/rootallowed1, rollback0/original0/handoff0. RootRW is allowed ONLY after that proof. There is NO negotiatedx1rollback on success.
- Linuxreturn is permanently blocked from arming, including later panic/runtimeIOfailure. A knowncompleted failedtransition may negotiate back tox1 but root and handoff remain0. Ambiguousfailure retainsDMA/ownership andhalts.
- Tested rescueCPIO contents are present for containerintegrity, NOT an automatic fallback for livemode. Recovery requires physicalreset, fastboot and manually flashing knownrescue. No protocol here resets/flashes automatically.
- LINK/LIMITS emitted before transition describe baseline x1. Do not cite them as posttransitionmeasurement. No GUI/networking/RF/performance outcome follows from prooflines alone.
- Wi-Ficommon observer still snapshots/applies/restores40digitalregisters; dirtyrestoration retainsownership+halts. No CPUrelease/calibration/ROMexecution.

## Frozen evidence parser

`parse-evidence.py LOG` parses exact livePOLICY/RESULT/PROOF/PROFILE/REFUSED/runtimepanic and failureSTOP/STATE/LAST. Format confirmed by ufs_link from frozen `research/nord2/ufs-two-lane/live-candidate`, latestwrapperSHA1d3e7211e9cb1501123d058dbf99636009a91a1c0d624830f358c3ef462909bb. This sourceidentity is ONLY a formatreference, NOT the asyet-unfilled finalCI/source/kernelidentity.

It reuses a PRIVATE copy of the previous strictcommonparser, extends AP/APRAW pairvalidation: ownerprefixbitmap; onebasedownerfail; zerobasedAPfailed; contiguousvalidreadprefix; actualfirstfailure must disagree with exactmask/want, earlierreadfields mustpass; unreadraw mustzero. Legacy owner-or-AP skip with no newpair is insufficient, not silentlytreated as observedAPstate. Duplicate/malformed/truncated/invalidUTF8/reportorder contradictions fail. Cleancommon success and APfailure cannotcoexist. Dirtycommon cannot claim outercontinuation.

Live phase5 success requires exactcounters/flags and rejects rollback/original/handoff. Failure cannotclaim rootpublication; rootlogs beforeproof or afterfailedproof fail. Runtimepanic invalidates overallcombinedsuccess evenafterinitialproof. Missingcommon afterlivesuccess is incomplete. Complete parseroutput classifies observations only; `observed_combined_success` requires both strictlive and cleancommon success, and does NOT imply functionalWiFi, GUI or speed.

`test-template.py` verifies placeholderhardfailure; exactshell/commandline/metadata; all7APfirstfailurefields/all9ownerfailureprefixes; syntheticclean/skipped/dirty/liveresult/runtimecases and mutations; knownrollback/uncertainfailurecontradictions. The ONLY realhardwareinput is older skippedcapture with no newAPpair/liveproof, which correctly remains insufficient. All success-shaped fixtures are syntheticparser tests, never saved as claimedhardwareevidence. FulltestPASS in test.log, shellsyntaxPASS. No build/device/runner/finalmanifest.


## Pre-root I/O panic correction

Confirmed with ufs_link: both exactmessages may occur after validarmedPOLICY and before ANY result/proof/profile/root/common, during initialTUR or defensiveentryguard:

- `NORD2 UFS X2 LIVE: runtime I/O failed; retain DMA/ownership; manual reset required`
- `NORD2 UFS X2 LIVE: uncertain before I/O; retain DMA/ownership; manual reset required`

That exactpolicy+panicshape is accepted as `pre_root_io_failure_manual_reset`, complete observation of failure, never success. Missing/invalidpolicy, partialproof, root/common continuation or duplicatepanic remains contradictory. The same messages AFTER fullphase5proof classify `runtime_failure_manual_reset`; no networking/GUI/recovery claim follows. Tests cover both placements/messages and contradictions.

ExactexistingproductADT verifiedpresent: research/nord2/ipsw/d22ap.adt, SHA2569e670987c80493bd69aba7be62e3179c04663324d9249b0973675baac175dbf1. Buildscript uses that samepath and explicitly checksitexists; verifier pins itsbytes and compares actualpayloadADT/product. No build was run.
