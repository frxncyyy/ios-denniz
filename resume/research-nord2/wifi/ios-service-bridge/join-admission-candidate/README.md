# First-join admission and source-backed RSN normalization

Frozen private proposal. No canonical files, CI, image gates or hardware changed.

`runtime-manifest.json` pins exactly five source files. `candidate.patch` applies
only those files to their pinned baselines. Promotion copies the same five files
from `proposed/shim/nord2-xnu` plus the four portable test files under
`proposed/tools`. The final `manifest.json` also pins tests and evidence.

The original selected IO80211 producer can append a zero PMKID count and optional
BIP selector even when its station capabilities remain zero. The decoder accepts
only exact 22, 24, or 28-byte forms; 28 permits OUI00:0f:ac type6/11/12/13.
Declared length, complete supported PSK/CCMP core, caps0 and PMKIDcount0 must
match. Output always uses the existing 22-byte PSK/CCMP station RSN for both
association and M2. This does not negotiate PMF or change the AP's retained RSN.
Optional AP ExtendedKeyID support no longer excludes an AP; our station still
negotiates key ID0. MFPR, NoPairwise, unknown active profile fields and AKM6
remain refusals. Numeric JOINREQ now includes mode/auth_lower/auth_upper/rsn_bytes;
no credentials, SSID or MAC bytes are logged.

Original AKM6 gating is now resolved, not guessed: dispatch table1d3ef5601 sends
AKM6 to1d3ec0814, requiring `80211W_ENABLED` dictionary value equal true.
Priority6=3 exceeds priority2=2. If selected, final dispatch produces
`auth_upper=1024`. No direct CARD check occurs at this gate. The actual Settings
input flag remains unobserved, so this is a remaining conditional compatibility
risk. We do not relabel PSK-SHA256 as ordinary PSK. `evidence.py` verifies356
instructions and dispatch tables against the original selected cache bytes;
`source-evidence.json` distinguishes static evidence from runtime unknowns.

An admitted first join can wait behind scans already accepted by NSS. Admission
immediately blocks additional scans. The actual retained worker drains the old
queue before any station preparation; cancellation and expiry are checked before
deferral. Cancel/expired work clears the copied request and emits one FAILED then
one RETIRED, with no native preparation, no station IO and no inline callbacks.
Callback storage is held through RETIRED, and reentrant enqueue during retirement
remains busy. A currently running scan is not aborted: it finishes under its
existing bounded DMA lease. Its completion may delay retirement beyond eight
seconds, but an expired join cannot start afterward.

The eight-second enqueue-to-auth budget is directly source-backed by the
producer's400x20000us GET21 wait followed by SET22. Admission reads precisely the
same `clock_get_uptime` -> `absolutetime_to_nanoseconds` -> /1000 monotonic domain
used by `ncd_native_now`, without borrowing worker authority or accessing MMIO.
The remaining deadline is passed to NST and STC; waiting does not restart it.
The existing thirty-second cleanup budget and four-second maximum channel grant
remain. Pure target/lease construction and nwn_begin occur under the admission
lock, serializing cancellation before actual publication; neither invokes client
or ifnet callbacks. All native publication/pumping remains on the original worker.

`niwj_scan_usable` permits only actual terminal matched state7 scans. It refuses
active/unfinished/timed-out scans, backward/incomplete lifetimes, inconsistent
counts, wrong sequence, and incomplete partial scans. The only partial exception
requires dropped>0 and all16 retained rows, with dropped<=frames-count. Source
`nsb_add` increments that dropped counter only on cache overflow; those records
remain intact. NSS caches eligible PARTIAL deliveries, and initial owner startup
also accepts this same complete-overflow case. This is not acceptance of a
malformed/truncated target; per-record RSN/IE/channel/privacy/rate checks remain.

Binder composition includes the peer's exact captured AP IE input for RSNXE
validation and three diagnostic rawRX counters. The fresh unused security stage
has a separate parser proposal under `connection-parser-review`; historical
executed parsers remain immutable. Raw counters and generic frames never prove
DHCP, IP acquisition or Internet access.

Verification:

-134776 ASan/UBSan join checks: all65536 station caps, accepted producer trailers,
 malformed extents/OUI/types, atomic secret clearing, AP optional ExtendedKeyID,
 exact overflow-only eligibility and all prior target/profile negatives.
-Actual service binder regression with pthread admission/notice locks: queued and
 running scan deferral, cancellation, backward/expired time, preserved deadline
 debt, reentrant retirement, synchronous disconnect, close/link races and wiping.
 Lower native prepare is a refusal-only seam in this particular unit test.
-Actual retained native owner test: boot26 plus six real CID3 scans over the real
 ncd/nwfn/nct/ncr/NSS callbacks and all five rings; four old scans drain before
 join selection, new scans refuse, deferred cancel/expiry have zero additional
 MMIO/zero NST preparation/one retirement. Only firmware/hardware is synthetic;
 the existing readback probe remains the preexisting fixture seam.
-Full actual IOUC scan/join/lifetime/CARD host regression passes with the proposal.
-Selected normal generic ARMv8.2 owning sweet_touch, wifi_ios_join and
 wifi_ios_client compilation passes with both peer runtime overlays.

Portable canonical tests are `test-nord2-wifi-join-owner.py`,
`join-owner-host.h`, and the existing `service-test.c` / `test-join.c` upgrades.
The owner runner reads an externally supplied authenticated board fixture (default
private WIFI path); it embeds or prints no real board bytes. These tests establish
bounded source behavior, not hardware association, keys or connectivity.
