# First Metal device probe and recovery

The approved probe boot473f65c0 was flashed successfully on 24 October. Native
USB never enumerated. The user observed frozen XNU console output followed by
an unsolicited reset. The separate UI image25ed71a4 was **not** flashed.

Already-authorized boot-only recovery to calibrated audio/Wi-Fi a5b6e6e3
succeeded. Its USB capture contains371995 bytes, SHA-256
`9c0c4da4cf08726989c3611b50b47d8e6f4dc97b306b02e175578bbe837c7d3b`.
The user confirmed that lockscreen, touch and buttons work. The Metal addon is
still installed; the recovery kernel does not publish a Metal device. There
was no second filesystem transfer or rollback of user data.

## Evidence and limits

The capture preserved the failed boot as a distinct prefix: nine payload
segments, entry7494488, ending at `PE_init_iokit` / `iBoot version: nord2-probe`.
The following loader marker starts the working ten-segment baseline boot.
The user's screen photograph is consistent with early kernel startup.
`gPanicBase is still not initialized` also appears in the successful baseline
and is not a demonstrated explanation for this failure. No panic suffix was
recovered. There is no GPU ready/ID message, compute result, or pixel result.

All118 original C++ constructor targets are present and in baseline order,
with one additional Nord2GPU translation-unit constructor. All119 addresses
are executable. The local linker placed the last constructor table in
`__LAST`, whereas the baseline has `__LASTDATA_CONST`; this is an observed
layout difference, not an identified root cause. No speculative change to
that layout is being made in the diagnostic variant.

A partial ARM64 Unicorn exercise reached post-constructor initialization in
both the failed candidate and the diagnostic kernel. Allocation, locks,
zone creation and symbol creation were modeled. This only checks the bounded
constructor path under those assumptions; it does not reproduce all IOKit
initialization or qualify the phone, allocator, MMU, GPU or Metal.

## Next diagnostic

`gpu/tools/build_startup_diagnostic.py` builds an isolated kernel with messages
before/after IOKit initialization phases and before each early constructor.
It changes two archive members (IOStartIOKit and OSRuntime), verifies all1188
other candidate archive members byte-for-byte, and preserves the approved
kernel and images. Hardware sequencing, GPU transport, installed addon and
compute/pixel criteria remain unchanged. Persistent constructor output is
bounded to the initial kernel load and at most256 entries per section.

This is a diagnostic, not a claimed fix. Its exact new image requires new
authorization. Keep the current working iOS running until then. Do not run the
old conditional UI image after a failure or incomplete qualification.

Local evidence is under `gpu/build/hardware-20261024/`: `probe-failed.json`,
`failed-probe-preserved.log`, `restore-usb/result.json`, `constructor-audit.json`
and the partial emulation reports. The emulator prototype and binaries remain
ignored local artifacts; they are not a reusable kernel-emulation test suite.
Private Tegu derivatives must not be published.

Diagnostic artifact: `gpu/build/device-startup-candidate/nord2-ios-metal-startup-probe-stop23.img`,
SHA-256 `ddba2e9fa5b90ae6dd4546d14435a374cbfa5f35b326fd56f17ce181d0c0ff12`,
33161216 bytes. Full boot-image checker passed; ISA check decoded2231679
instructions without LDAPUR/STLUR. Kernel hash
`8b09d5aeb12ea53ea6beeb24e4552eff04bb8afae6b57fc7e2696b245c39ccce`.
The original probe and UI image hashes remain unchanged. Exact diagnostic approval
was received and boot-only flash/reboot completed. Capture is in progress under
`gpu/build/hardware-20261024/startup-test/`; no filesystem transfer was repeated.


## Startup diagnostic result and demonstrated ADT defect

Exact startup probe ddba2e9f was authorized and flashed successfully. It again
stopped before native USB; the user saw the same early console. Authorized
boot-only restoration of calibrated iOS a5b6e6e3 succeeded. A 394406-byte capture
(SHA-256 dda5933aa05d8e93702be90467236c371a6a2c7e123aa68f362d3105d8010cc0)
preserved the diagnostic run; user confirmed lockscreen/touch/buttons healthy.
No filesystem write was performed. The addon remains installed, system Metal OFF.

The preserved run completes every constructor, post-constructor subsystem,
postModLoad (result=0), IOMachPortInitialize and interruptAccountingInit. Its last
marker is before `gRootNub->init(dtTop)`. Thus neither the constructor segment
layout nor GPU power-on explains the observed stop. IOPlatformExpertDevice::init
imports the device tree before starting the GPU driver.

Mali had `interrupts` but no `interrupt-parent`, and GIC had no interrupt cell
count. XNU's IODeviceTreeAlloc -> IODTMapInterruptsSharing -> IODTMapOneInterrupt
therefore picks arm-io as parent. That node has neither interrupt-controller nor
interrupt-map; the do/while body leaves parent unchanged forever.

`gpu/tests/adt_interrupt_import_test.py` extracts the **unchanged** mapping functions
from the staged XNU source and compiles them with container/registry shims under
ASan/UBSan. The actual failed ADT exceeds a 1000-property-read budget. The fixed
ADT maps all five cells to the explicit GIC controller and terminates. This is
a source-level reproduction, not a GPU, interrupt delivery or full boot test.
Walker SHA-256: 1ff256bf7f5dcd56362f7825ee4c8188e0d6defd7e003ddedda19d371a732f37.

The new packer changes exactly three ADT properties relative to ddba2e9f:
Mali interrupt-parent -> existing GIC phandle, GIC #interrupt-cells=1 and
#address-cells=0. Raw SPI values 362..366 stay unchanged. These are the local
ADT's one-cell values, not Linux's three-cell GIC binding. Native GPU completion
currently polls; this does not claim to implement IOKit IRQ delivery.
All other ADT content, the diagnostic kernel and installed addon are preserved.
Device verification still requires exact new image approval. UI remains blocked
until all compute/pixel/IOSurface checks pass.

Evidence: `gpu/build/hardware-20261024/startup-test/`, particularly
`failed-startup-preserved.log`, `dtirq-analysis.json`, `dtirq-import-test.log`,
`restore-usb/result.json` and `flash-restore/completed.json`.


Corrected boot artifact (not yet authorized/tested):
`gpu/build/device-dtirq-candidate/nord2-ios-metal-dtirq-probe-stop23.img`
SHA-256 `70ab562e4cbd68237c2b52733c3f4df60293a874a0b7f0db108c96222189d659`.
33161216 bytes;393216 bytes spare. Full boot checker PASS;2231679 instructions
passed RCPC2 exclusion. Packaged kernel byte-identical to diagnostic ddba2e9f.
Only the three stated ADT properties differ. The actual packed ADT also passes
the unmodified XNU mapping regression. All four previous approved artifacts
(probe, startup probe, UI, addon) were rehashed unchanged. No public CI used.


## DTIRQ result and combined Android initialization candidate

DTIRQ70ab562e was authorized, flashed and boots native USB/iOS. User confirms
lockscreen/touch/buttons healthy. Capture412911 bytes, SHA256
01810dbc1c849af12e40a54c7db30b6d842c42956f2e2a954d94e69ea44ccf05,
shows IOKit import completed, probe executed, GPU PLL preflight stage1/error3.
No new device writes were made during the following Android-source audit.

The audit found historical actual-device clock349900kHz and GPU r0p1, motivating
both cold PLL normalization and the required Android TTRX_3485 workaround.
See ANDROID_BOARD_PARITY_20261024.md for all source/oracle/lifetime evidence.
New exact combined probe, not yet authorized or tested:
`gpu/build/device-board-wa-candidate/nord2-ios-metal-android-init-probe-stop23.img`
SHA25615143c05f7c7365c9f501aae6b4a693b29297223291eab714755547071cbecc1,
33165312 bytes (389120 spare), kernel3f72ba61899f080486ec545dc11786d8c0fab84ba443e6a65117461b7b06ee72.
Boot checker PASS;2232756 instructions exclude LDAPUR/STLUR. Fixed ADT unchanged,
all other linked baseline objects hashed unchanged, vendor WA bytes found in
both compiled adapter and final kernel. System Metal OFF, installed addon reused.
No compute/pixel/UI execution success is claimed. Old UI and superseded probes
must not be flashed. Next step is exact approval, USB collection and boot-only
probe. Restore calibrated iOS a5b6e6e3 if needed under that test's authorization.
