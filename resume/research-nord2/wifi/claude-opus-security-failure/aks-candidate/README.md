# NORD2 AKS10/11 insecure bring-up compatibility — private candidate

This candidate implements the missing wrap/unwrap operations with the exact published HackiOS/Topaz `HKwr` storage format. **It provides no encryption, authentication, SEP, device secret, passcode protection or secure persistence.** Anyone who can read a blob can recover its contents. This is a deliberately insecure prototype compatibility policy, not a secure keychain implementation.

The actual wifid crash reports identify a separate nil `PRIVATE_MAC_ADDRESS_VALUE` dictionary conversion, owned by another candidate. The existing 157 securityd selector-10 errors establish a real missing operation; this package does not claim those errors caused the crashes or that implementing AKS alone establishes Wi-Fi association.

## Source and exact integration boundary

- `SweetKeyStore.base.cpp` was extracted from successful executed CI39790703398 `xnu-buildtree/buildtree.tgz`, member `xnu/iokit/Kernel/arm/SweetKeyStore.cpp`, SHA256 `0224ef7ac3fd78511b4cb4369f9099352f80a9b434ae3fd15b5a3dbd1f145d55`. It includes the selected existing0038/0055 changes.
- `proposed/iokit/Kernel/arm/SweetKeyStore.cpp` is the complete proposed replacement. `candidate.patch` applies to the exact generated base after the existing patches; zero-fuzz application and byte equality are recorded in `verification/patch-apply.json`. No canonical patch/generator was changed. Root chooses the canonical patch integration after reviewing this policy.
- Only NORD2 selectors10/11 use the new helper/early dispatch. Other selectors follow their existing body, and non-NORD2 SWEET preprocessing excludes all additions. The existing registration, entitlements policy, lock/bag state and other targets are not broadened.
- Source attribution: `MiniAppleKeyStore.upstream.cpp`, HackiOS `oosexgang/hackios-xnu-kernel` commit `8e61187098f2c26c5daee8f9f72b6416aa64b9ab`, file `xnu/files/iokit/Kernel/MiniAppleKeyStore.cpp`; Topaz `MakrSas/ios-topaz` commit `e4772911e116ab4ab6c4ac1f38e6106fefe8b483`, patch0900, `SweetKeyStore.cpp` hunk. APSL2.0 license is retained at `LICENSE.upstream-APSL2`. Source/trace object verification was frozen separately in `../review-upstream/manifest.json` SHA `7847eb560deb473f424389d5abcc4da534efb2a0f521fde1cd3282e13db4fbbe`.

## Bounded admitted ABI

Both operations require a current-version synchronous request, matching selector, exactly two scalar inputs with a nonnull pointer, system bag handle0, no variable-output object, and a nonnull inband input. Input descriptors are explicitly refused even when an inband pointer is also present. This is the observed ABI, not a claim to implement other bags or arbitrary libaks selectors.

| Operation | Input | Output |
|---|---|---|
| 10 wrap | 1..64 key bytes; scalar1 is opaque protection-class metadata | `HKwr`, low class byte, three zero bytes, then `key[i] XOR ((0x5a +37*i) mod256)`; 8+key bytes. Optional output scalar count0/1; count1 echoes the complete input scalar. |
| 11 unwrap | 9..72 bytes; exact marker, zero reserved bytes and low class byte matching scalar1 | Reversed transform, 1..64 bytes; zero output scalars. |

The emitted bytes are exactly upstream-compatible. The added marker/reserved/low-byte-class checks enforce the format the upstream emits; they are stricter refusal policy than its permissive unwrap and **are not cryptographic integrity or class authentication**. Full class high bits remain opaque, as upstream records only one class byte; equal low bytes do not establish equal full classes. A payload changed under a valid header is necessarily decoded: this public reversible format cannot identify tampering. Non-`HKwr`/foreign formats fail; no format guessing, rekeying or keychain reset is attempted.

The observed real ABI `2 scalar inputs /32 key bytes ->1 scalar /40 bytes` is covered. Scalar/structure pointers, counts, bag, version, selector, capacities and active descriptor modes are checked before input copying or output mutation. Optional upstream scalar echo0 is preserved; extra scalar outputs or inputs are refused rather than silently consumed.

Inline output has complete preflight before its synchronous copy. Descriptor output uses its exact length/capacity, `prepare(kIODirectionIn)`, one bounded `writeBytes`, and `complete(kIODirectionIn)` after every successful prepare, even a short write. The real selected MIG retains an inactive nonnull inline pointer for descriptor output: it is accepted only with inline size0, and never dereferenced in this branch. Positive-size mixed output is refused.

Only successful full delivery and completion commit output size or scalar echo. On descriptor prepare/write/complete error the helper returns failure with these metadata unchanged. A short write or failing completion may already have changed caller bytes; this is not falsely described as byte-atomic OOL output. A complete error also does not prove pages were successfully unwired. The IOUserClient/MIG retains descriptor ownership; this code never assumes or releases its reference.

The only temporary key-bearing array is72 bytes. An explicit volatile wipe clears it at helper entry and after every transformation/output result, including prepare/write/complete failure. Numeric diagnostic fields contain selector/input extent/output extent/status only; no key, blob, class, SSID, MAC, password or credential payload is printed.

## Persistent format implications

The same public deterministic format is used across clients and boots: no per-boot key or invented device key makes old items unreadable. There are no filesystem, keychain, bag-reset or migration calls. Adopting this scheme on persistent UFS would establish an insecure on-disk compatibility format. A future secure implementation needs an explicit migration/storage policy; this candidate does not silently convert or erase existing data. Existing foreign wrapped items fail instead of being guessed. Parent must review this policy before promotion.

## Verification and limits

`test-handler.py --output <fresh-directory>` compiles the actual old and proposed owning translation units with narrow XNU API doubles under Clang ASan/UBSan/Werror. It does not copy the handler logic into a test model. The final author run is `verification/host-final/report.json`:

- 164628 proposed-handler checks: a fixed independently frozen upstream32→40 vector, all64 accepted lengths and all256 header-class values, round trips, malformed header/class/reserved/bag/mode/count/pointer/extent/capacity refusal, input-descriptor refusal, exact selected-MIG OOL shape, successful and injected failing descriptor paths, output metadata preservation and observable scratch wiping through the real helper.
- The old generated handler and proposed non-NORD2 handler each execute the same valid wrap request and return Unsupported without output mutation (3 checks each).
- Descriptor failure doubles require correct direction and balanced prepare/complete. The final fixture models the inactive nonnull inline pointer discovered during independent source review, avoiding a mock-only OOL contract.
- `compile-native.py --output <fresh-directory>` compiles the complete proposed owning SweetKeyStore TU against the same selected real XNU headers/normal kernel C++ flags as the current focused client compilation, `-mcpu=generic -march=armv8.2-a+crypto`; output at `verification/native`. This is a real ARM64 object compile, not a full link or device test.
- `verification/patch-apply.json` proves zero-fuzz application to the exact selected generated baseline and byte equality with the proposed source.

The earlier `verification/host` run is preserved but superseded: it did not yet model MIG's inactive inline pointer. Its runtime hash differs. `verification/host-final` and the native report pin final source `5614c465ac30f6bc06e8fd1512e2d0dbdeed6aecd7a47c8a1b47146becbb8d8b`.

No Mach IPC, securityd/MobileWiFi operation, keychain persistence, userland crash fix, association/key exchange, DHCP/IP or Internet success has been demonstrated by these host/compile checks. No device, CI, canonical, generator, approval gate or phone storage was changed.
