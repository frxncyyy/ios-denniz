# Parent promotion prerequisites

All listed work remains parent-owned; this file is a checklist, not a granted gate.

1. Freeze final canonical source and reviewed live protocol/owner binder, boot-lifetime PID allocation and actual clean rejoin lifetime/cancellation/retirement. Add exact final hashes/reviews to source-proposals.json and native-schema-required.json. Keep booleans false until actual review; current357 source pins match committed c35ce62 exactly and saved reviews are referenced; final CI/artifact/linked promotion remains pending. Do not use the old DHCP candidate manifest or linked PASS.
2. Generator now appends wifi_live_control to MAIN_FILES and copies live_control_bind.inc. After merging rejoin, include wifi_station_rejoin.h in the generated file list. Regenerate0082 once; generated source equality audit must require all four additions and compare every +++b path dynamically. Keep0083 AKS unchanged. No regeneration was performed here.
3. Add wifi_live_control.c to selected MakeInc.def's actual generic-special-source rule. Inspect successful CI build.log for -mcpu=generic/-march=armv8.2-a+crypto on this TU and all existing relevant units, plus the actual owning sweet_touch.c after including the binder. artifact-review/audit-artifact.py includes the new TU and required generated additions; no historical file count substitutes for equality. Root runs ISA guard on the complete actual kernel.
4. Pin exact source commit/CI run+attempt/kernel path/bytes/SHA/SHA256SUMS/run.json. New identities must be filled by root after successful build. Perform bounded actual linked proof of USB queue-only decoder, mailbox ownership/idempotence, owner capture/profile/disconnect, final rejoin ledger and sampled PID reserve/publish/receive branches. Store new linked-review/review.json and manifest.json, plus exact independent reviews; linked_reviews is currently empty.
5. Pin current source/schema parser dependencies and final required markers: old startup/security/WPA/RX plus nord2wifilive, LIVE, LIVEFRAME, TXPUB/STATUS/FINAL/NOTE/LIMIT and any final rejoin markers. Confirm canonical connection/data readers support actual new generations/row schemas. Current eight reader byte pins are observations only. Live binary host replies are validated by canonical nord2-wifi-live.py, whose time field is milliseconds; final code/tests/pins must include that correction. No LIVEFRAME completeness parser is presently claimed.
6. Update connection_diagnostics with exact final canonical readers (including RXERROR and TX correlation), host CLI/tests, and any stage-dependent supplemental readers; update source_pins for generated patch/generator/MakeInc and final live/rejoin files. Pin parent source/generic/ISA/linked receipts. Never set parent_verified merely to enable an offline builder with incomplete metadata.
7. Build once with the tested path only after identity/schema gates open. Keep prepared/human/RF/recovery false. Supply complete image/payload identity to independent-image-review/verify-existing.py; it uses --check-existing under1GiB and compares critical files before/after. Then root may promote prepared identity and request exact new boot-only approval with d9a/RAM-rescue recovery identities.
8. Live CLI and log collector may coexist only under root's USB scheduling policy; do not open a second console collector. No user serial appears in this template. Actual authorization must supply the exact device locally after the image is reviewable. This template sends no control or USB request.

## Metadata finalizer

`python3 research/nord2/wifi-ios-live/ios/finalize-metadata.py --prepare-current`
refreshes current working-source/review/local-receipt hashes with every approval
closed. It does not assert a coherent final commit merely from a working copy.

After the actual final commit, root may use `--source FULL40SHA`; every source
member must byte-match `git show SHA:path`, including new formerly untracked
live/rejoin sources and fixtures. Once the exact successful artifact is local,
add `--ci-run RUN --ci-attempt ATTEMPT --kernel-sha256 SHA64 --kernel-bytes N`.
The helper checks completed/success/headSHA/run/attempt, kernel bytes/hash and
artifact SHA256SUMS. It invokes no CI/download/build/device tool. Optional
`--linked-review REPO_RELATIVE_PATH` adds exact actual saved linked receipts;
file presence is not automatically interpreted as independent PASS.

The helper always leaves parent_verified=false/schema_pending=true and all
review-approval flags closed. Root must inspect final composed source, tests,
actual generated-source/generic/ISA and linked reviews before promoting those
existing gates. Prepared and human files are protected byte-for-byte. There is
no additional approval loop introduced by the helper.

Current rejoin metadata intentionally reads wifi_rejoin_supported=false with
wifi_experimental_rejoin_requested=true and wifi_rejoin_hardware_proven=false.
This labels an experimental requested feature without claiming a working
on-device rejoin. It still requests the reviewed bounded implementation and
removes the old first-connection-only intent. General roaming/SAE/required-PMF
and private-address claims remain false.
