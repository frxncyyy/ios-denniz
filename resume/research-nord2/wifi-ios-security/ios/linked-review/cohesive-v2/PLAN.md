# Pending verified cohesive artifact

Do not run extraction until root provides a completed successful CI run, exact
source commit, kernel byte count and SHA256. No polling, downloads, CI launch,
image construction, gate promotion or device action belongs to this recipe.

Run the local read-only helper with those exact values:

```
python3 extract.py --kernel /absolute/verified/kernel.development.sweet \
  --sha256 VERIFIED64HEX --bytes VERIFIED_BYTES --run VERIFIED_RUN \
  --source VERIFIED40HEX --output /new/private/evidence-directory
```

The helper requires exact kernel identity, original Mach-O bounds, local promoted
source pins, and one unambiguous actual linked symbol per requested body. It
refuses to overwrite output. `extraction.json` is explicitly NOT a verdict.
The inventory constant and three diagnostic string addresses are corroboration;
actual instruction branches/stores/calls must establish the behavior below.

The23 bounded bodies cover only changed integration boundaries:

1. CARD native inventory -> client GET12 -> unchanged codec: count2 CCMP+RSN,
 ops63, remaining entries zero; raw13 byte0=08/byte1=40; exact output capacity
 before native query; no PTK/GTK/current-carrier cycle.
2. Join decoder/selector and client join call: station22/24/28 exact normalization,
 caps0/PMKID0/BIP selector checks; AP optional ExtendedKeyID support only;
 secret output clearing, numeric mode/auth/RSN diagnostics. No AKM6 normalization.
3. Enqueue/cancel, actual retained worker, scan delivery and early finish: first
 join admitted behind old scans; new scans blocked; cancellation and absolute
 enqueue8s deadline checked before station start; same uptime conversion;
 fresh native timestamp must be sampled AFTER a successful start and before
 first pump, never reuse pre-constructor time. Existing queued callbacks drain,
 cookie retained until one RETIRED, request wipe, no inline client callbacks.
 Actual worker body contains inlined start/owner/cache routines. Check owner
 clock/preparation guards and the existing30s cleanup/failure drain calls.
4. Native preparation and RX decoder: exact source-complete overflow-only scan
 gate, retained same-worker ownership, group CCMP accepts AP station index or
 group index only for multicast; unicast strict; PN checks preserved. Raw
 non-QoS unprotected FromDS EAPOL requires real AP/self addresses, exact bounds,
 LLC/EtherType and original software-owned frame. No arbitrary frame fallback.
5. STC begin/tick/receipt/management and WPA AP/RSNXE/receive/KDE: captured AP IE
 forwarding; optional RSNXE exact matching, bounded single PMKID KDE M1, unknown
 vendor policy as reviewed; two total auth/assoc attempts,524ms after AIR receipt,
 no grant renewal; bounded auth trailing IEs; original generation/replay/MIC/key
 readiness and cleanup preserved. Installed keys/data remain prerequisites for
 authorization; no firmware acknowledgement is fabricated.
6. Real ifnet link/event: event payload begins at24-byte KEV_MSG_HEADER_SIZE,
 correct total length/family/unit/name; kernel callback and synchronous down
 order remain, no protocol callback under NSS lock, no false carrier before
 native authorization.

Use exact actual symbol addresses and bounded code hashes in the verdict. Static
helper inlining is expected only inside extracted containing functions; if a
requested symbol disappears, inspect and document its actual containing body
rather than treating its string as proof. Source source-pins.json is local review
input only. Parent independently audits the complete generated CI source tree,
actual generic ARMv8.2 compiler commands, all relevant tests and ISA compatibility.
Prior db14928f linked evidence can support unchanged client/event/ifnet lifetime
and clock code, but cannot substitute for this changed artifact's branches.

Do not broaden to firmware reload, RF tuning, scan request inventory or complete
crypto/kernel re-audit. Linked/static success is not hardware association, DHCP,
IP acquisition or Internet proof. Image review remains a later read-only
`verify-image.py --check-existing` with exact parent-provided image identity and
bounded memory, after all required reviews and source identity are supplied.
