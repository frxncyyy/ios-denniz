# Bounded DHCP linked delta — pending artifact

Preparation only. No CI identity, kernel access, PASS verdict or gate is supplied by this directory yet. `requirements.json` pins the final six source/header inputs, regenerated0082 and successful parent16-unit compile report. Only three focused object identities are relevant; differing compiler optimization does not automatically imply a bug or require byte-identical local objects.

After parent CI_READY with actual source/run/attempt/kernel SHA/length, invoke:

```sh
python3 -B research/nord2/wifi-ios-dhcp/ios/linked-review/extract.py --artifact-ready \
 --ci ACTUAL_RUN --attempt ACTUAL_ATTEMPT --source ACTUAL_FULL_SOURCE \
 --kernel-sha ACTUAL_KERNEL_SHA256 --kernel-bytes ACTUAL_KERNEL_BYTES
```

This extracts sw_sta, peer_progress, native receive/decode and the two RX-error helpers only. Receive/decode are separate in the prior kernel; if selected static helpers inline, it retains their actual nst_step caller instead. It validates actual CI/SHA256SUMS/source pins, never downloads/disassembles the whole kernel and never grants source/image/device gates. The original Mach-O reader is reused as a parser utility, not as inherited approval.

Manual review must follow linked branches and call targets: input sentinel255 guard and STA byte55 store, actual STA caller255 vs separate BSS real BMC, unchanged activation/deletion behavior, existing reject masks calling RX diagnostics then returning, fixed-header-only helper bounds/type/mask/decomposition,8-record independent budget, atomic record commit and exact numeric RXERROR emitter literal. Root owns generated-source equality/generic/whole-ISA proofs. No re-audit of the unchanged27 bodies or WPA/GTK/token/scan state machines is requested.

Artifact extraction is explicitly pending human semantic review. The new exact source/CI/kernel report and manifest are written only after actual bytes and that review exist. No build, CI or device action is performed by preparation.
