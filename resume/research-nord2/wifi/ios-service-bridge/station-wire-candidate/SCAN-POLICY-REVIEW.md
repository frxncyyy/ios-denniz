# Independent read-only scan-policy review

Subject: `service-compat-resume/scan-policy-hardware-candidate`, manifest SHA256
`5e4fcae88d53d98b39ae46d30792fa05198977dd3011f335136e551a06a67258`.
All nine candidate content hashes and all five baseline source hashes matched
at review time. No candidate, canonical source, image or device was changed.

**No blocking functional issue found** in the semantic parser, power helper or
small client integration patch. This supports root's cohesive integration and
actual-client verification; it does not establish Settings success on hardware.

The selected disassembly directly establishes the rejection cause sufficient
for the observed request profile: dictionary path stores INT32_MIN at offset4880
at `1d3ebbaf0..afc` before optional threshold lookup. The original policy's
unused-byte sweep rejects that default. The dictionary builder zeroes5456 bytes
at `1d3ebb588..590`, so poisoned padding is not the demonstrated live cause.
The proposed parser admits exactly default sentinel or prior zero profile, and
continues refusing explicit thresholds, nonzero64-bit flags, named-network count,
short-SSID, directed SSID/BSSID and timing requests.

Padding and inactive storage treatment is sound for this supported profile:
14..15 and62..63 are alignment bytes; SSID bytes at20..51 are inactive with
SSID length0; channel records beyond count are inactive; the named-network array
at4888 is inactive with count0; trailing5452..5455 are padding. Both halves of
the64-bit scan flags remain checked at4872/4876. The helper ignores no supported
active fields. Broadcast and zero BSSID are reasonable nondirected selectors.
Channel version, flags, count, duplicate checks and unique owner-list resolution
remain enforced; count0 retains the existing actual-owner full-list behavior.

Bounds are checked before reads: exact5456 bytes, nonnull inputs/output, owner
count1..NSS_CHANNELS, request count<=NSS_CHANNELS. All parsing writes only to a
zero-initialized local request; the final assignment is the sole output write,
so failure even at the last channel leaves caller output unchanged. Unused
output channel entries become zero on success.

Power builder at `1d3ece864..8d8` copies a24-byte template, fills only count active
states starting at8 and writes count at4. For one-radio ON, checking version1,
count1 and state1 while ignoring inactive12..23 is semantically appropriate.
The client patch preserves the outer exact carrier/interface/operation/request/
extent/output checks and still calls actual `nord2_wifi_scan_info` before
returning success. It does not write physical power or publish an event. This
does not prove prior observed SET19 failures came from inactive slots.

Re-ran the candidate's5325 ASan/UBSan checks. Added a separate independent harness
`scan_policy_review_test.c`: **5491 checks PASS** under Clang ASan/UBSan with
fail-fast, `-Wall -Wextra -Werror`. It covers every5456 short scan extent,
oversized/SIZE_MAX lengths, null inputs, malformed owner counts/channels,
duplicate owner/request entries, unknown high flags, active named/short/SSID
rejection, all-request-count0 inactive channel poison, sentinel/zero/broadcast
success, last-channel failure atomicity, zero unused output and power inactive
slots/version/count/ON/length rejection. The test only uses synthetic fixtures.
Temporary build files stayed under this agent's `_build/scan-policy-review`.

Nonblocking cleanup: `niwi_power_on_admit` is declared after the header's
`#endif` (lines9..12). Move that declaration inside its include guard during
integration. Its repeated compatible declaration is harmless in current C/C++
call sites, which already wrap the header in extern-C; this is not a link blocker.

Required integration check: use the real native IOUC fixture to send SET10 with
the source-derived INT32_MIN/13 typed-channel profile through `dispatchLocked`,
and SET19 through its helper plus owner-ready condition. The provided candidate
test calls the policy directly; it is honestly a caller projection and does not
execute Apple's builder or the complete IOUC. Retain numeric admission diagnostics
to distinguish other live semantics; do not log request payload or identities.

Reproduce independent harness from repository root:

```
clang -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all \
 -Iresearch/nord2/wifi/ios-service-bridge/service-compat-resume/scan-policy-hardware-candidate/proposed \
 -Ishim/nord2-xnu \
 research/nord2/wifi/ios-service-bridge/station-wire-candidate/scan_policy_review_test.c \
 research/nord2/wifi/ios-service-bridge/service-compat-resume/scan-policy-hardware-candidate/proposed/wifi_ios_policy.c \
 shim/nord2-xnu/wifi_ios_rates.c shim/nord2-xnu/wifi_ios_mapping.c \
 -o research/nord2/wifi/ios-service-bridge/station-wire-candidate/_build/scan-policy-review/test
research/nord2/wifi/ios-service-bridge/station-wire-candidate/_build/scan-policy-review/test
```
