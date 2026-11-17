# Captured combined backend parser

`parse-backend.py` is portable Python with no parser/data dependency and no old
E000 debug parser. It independently validates critical MAIN readiness/ownership/
transport/PHY receipts, paired CAP first36B headers, actual selected BASIC
profile and one-credit receipt, then the exact22-command recipe. This fixed
first-run contract uses command sequences6..27 and ring indices5..27; it is not
a generic wrapping-session parser. CAP/BASIC debug inner16bit types are opaque
metadata under the updated source policy; outer class/EID/format3/body bounds
and every captured raw-header correlation remain strict.

The parser requires20 source-order configuration commands followed by CID11 and
passive CID3, exact wire lengths, all publications/releases/DDONE receipts and
PSE3 minus prior BASIC debit1 = capacity2 throughout. Returned descriptor fields
other than DDONE are opaque. It correlates all five RX queue indices, aggregate
rearms/packets, bounded event metadata and copied BSS counts. A matching scan
completion must have retained EID0d metadata, pending/drain correlation and
source state7. Firmware ACK, interface, association and network availability
are always false. MAIN checks cover this selected prerequisite summary; this
module does not reproduce the separate full ROM/main build-provenance verifier.

A valid clean state7 scan receipt sets `complete=true`, CLI exit0. A validated
retained timeout, pending-DDONE timeout, event-budget partial or partial cache
sets `terminal_receipt_valid=true` and `backend_config_transferred=true`, but
`complete=false`, CLI exit2. `scan_complete` identifies an actual state7 scan
completion separately from cache partialness. Empty completed snapshots remain
empty; no network names, addresses, RSSI or BSS records are invented/returned.
Failure/missing/conflicting/unknown/truncated/duplicate records never certify a
transferred batch. First64 RX-event history is bounded; when RX events<=64, full completion requires an EID0d record in history. For
RX events>64 and complete64-row history, source matched-body pending/drain
summaries may prove a later completion outside that captured prefix. All command/queue source rows and
worker parking order are strict. Debug declared lengths/sequences remain
captured metadata, with no invented equality to actual descriptor extent.

`test.py` combines critical MAIN rows from the pinned actual failed-BASIC log
with the pinned *synthetic* real-native-owner fixture's first successful backend
run. Additional fixture runs exercise no-done/pending-DDONE/deadline partials.
This combined fixture has never run on hardware. Test-report records exact
positive/negative counts and source hashes. The parser outputs only status,
classification, counts, fixed unavailable flags and input hash; it never returns
raw rows, MAC, SSID, BSSID or calibration bytes. No canonical, stage, CI, image or
device action is performed here.

V2 erratum: v1 incorrectly required the EIDec reply sequence to echo the
outgoing query4. The dispatcher accepts the actual captured EIDec sequence and
records echo/mismatch; backend decoding deliberately uses that captured
sequence. V2 requires EVENT/header sequence agreement and exact echo/mismatch
correlation while keeping outgoing query4, BASIC5 and normal6..27 unchanged.
V1 remains preserved. Explicit mismatch cases0,1,5,255 and contradictory echo/
mismatch/header captures are covered. No new hardware claim is made.

V2 also enforces disjoint RX report/event/valid-frame counts within packets,
exact event_limit correlation at8192, and source no-done terminal reasons
(service timeout or event limit). Bounded first64 history is interpreted as a
prefix, not as evidence that no later matching completion occurred.
