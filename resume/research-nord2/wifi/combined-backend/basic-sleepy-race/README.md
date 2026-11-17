Private cohesive unsolicited sleepy race correction

Observed failure and exact unknown

The immutable f53ad2b7 receipt research/nord2/wifi-scan-receive/ios/backend-first-receipt.log is157371B/SHA c12ecd8d0c2d865adddcc1822b3ca06c864e38a9520bfd9fbb1c6e8bfd355861. MAIN and CAP succeed. BASIC publishes CID2, observes DIDX5/DDONE1, then stops error23 on a40B outer3800/EID7/seq0/inner3800 event before any rearm; shared completed remains4. Old DEBUGHEADER captures only first36B, so the actual raw state at byte36 is UNKNOWN. It must not be reconstructed from earlier normal-phase notifications or assumed1. This package does not alter that executed stage/log.

Source-backed handler and narrower native behavior

Selected gen4m include/wsys_cmd_handler_fw.h391 declares EID7 unsolicited. include/nic_cmd_event.h1601–1604 defines EVENT_SLEEPY_INFO as raw uint8 state plus3 reserved bytes. nic/nic_rx.c144 routes the EID to nicEventSleepyNotify; nic/nic_cmd_event.c3783–3798 stores the raw uint8 and, under multithread, schedules HIF work when nonzero. There is NO0/1 restriction, sequence matching, inner-type equality, declared-length echo or reserved-byte-zero check. os/linux/hif/common/hal_pdma.c595–631 additionally requires sleepy plus powerblockcount0, no existing FW ownership, and interrupt conditions before actual FW-own. A notification by itself is not permission to write LP ownership.

The candidate adds shared inline wifi_async_event.h and uses it from CAP, BASIC and normal RX. It recognizes exact outer mask380f/event3800 + EID7 and requires actual copied extent>=40 before reading the state. All256 state values remain valid captured hints. Actual RX extent bounds and ownership remain with existing checked IO. Inner type, sequence, declared lengths and reserved3 remain metadata/opaque. It changes no firmware/AP/WF ownership state, sends no command/result response, invents no ACK, does not refund credit and does not modify any fatal/unknown-event allowance.

CAP now consumes/rearms bounded known EID7 while waiting for EIDec, under existing16 total events/3second deadline/current-worker lease. Kind3 records raw state in the existing event.debug_type byte; ncd_state remains5152B and no shared native ABI grows. It does not touch capability match, reply-sequence echo/mismatch, TLV decode or which final frame supplies BASIC prerequisites. Unsupported/fatal events still fail-stop before rearm.

BASIC's existing drain now recognizes either bounded ASCII EID27 or EID7. Separate sleepy_events/last_sleepy fields preserve debug's original meaning; the16-event budget applies to debug+sleepy combined, including mixed streams. The existing descriptor sync/read, coherent interrupt check, owned-buffer reconstruction, descriptor write/sync and CIDX publication remain unchanged. TX DIDX/DDONE still independently decide completion. Its before() native lease,3second/3000poll bounds, completed4->5 transition, one CID2 publication and one-way BASIC debit are unchanged. On any uncertain IO/malformed/fatal/unknown failure the existing commonhalt/30second sleepable drain remains. Unknown EIDs are not newly discarded.

Normal RX already accepted EID7 with>=40B and captured raw state. It now calls the exact same inline decoder; its existing malformed accounting, event history, all-five-ring ownership, fatal dispatch and unknown policy are unchanged. No new frame class is accepted there. This avoids learning the same legitimate asynchronous event separately in each phase.

Exact proposed receipt additions

For each CAP kind3, between its EVENT and EVENTHEADER rows:
NORD2 WIFI CAP SLEEPY: n=%u state=%u
Existing CAP EVENT has kind3/eid7/bytes>=40/debug_type=rawstate/debug_bytes0; sequence is opaque and has no echo/mismatch effect. Header stays exactly36B, so no payload/MAC bytes are logged.

After every BASIC summary:
NORD2 WIFI BASIC ASYNC: events=%u sleepy=%u state_valid=%u state=%u
Here events=debug_events+sleepy_events<=16; state_valid=!!sleepy_events; state=last rawstate or0 when none. Successful BASIC rearmed equals that total. Existing last DEBUG kind3 has eid7/format=rawstate/id0/length0, even for a larger event; ASCII fields are extracted only for EID27. Normal RX grammar/status rawstate is unchanged. A hint is neither command ACK nor scan completion.

Portable integration fixtures

proposed/tools contains promotion-ready changes to the canonical normal runner and fixture sources; all source lookup is canonical shim/nord2-xnu, with explicit read-only authenticated --board input. No private research/proposal source dependency or embedded board data. The temporary binder hook simulates firmware arrival before BASIC preparation; the native owner/checked callbacks themselves remain actual production code. DMA fixture completion injects notifications on CID2 publication and during a later delay. Simulated hardware writes are test fixtures, not extra host MMIO or an ownership bypass.

Tests cover unsolicited events before CAP, queued after CAP but before CID2, after CID2 publication, normal RX, states0/1/2/255, opaque inner/seq/declared/reserved metadata, longer bodies without unrelated-field exposure, a15occupied-cell CAP ring (never ambiguous16/16 full),16sleepy then pending17th rejection, mixed8ASCII+8sleepy then pending17th rejection, malformed39B/wrongouter/fatal0xf0/fatal0xed/unknown0x28 refusal, DDONE0 no completion and delayed DDONE+hint second-poll success. The shared decoder exercises all256 state values and256 representative inner values plus truncation/outer/EID atomic refusal,65578 checks.185 real-native BASIC callback fault positions with queued pre/postCID2 notifications prove no postfailure callback/retry/refund/completed5. Existing full22/five-ring/readback restoration/no-write regression and450 final-command faults remain. All run Clang ASan/UBSan failfast. Three changed C TUs compile with recorded normal ARM64 flags and an explicit proposal-header include; owning generated sweet_touch compile remains root-owned after promotion.

Scope remains one private source/test proposal. No canonical changes, generator/CI/image/gate/ADT/service/device action, no hardware result, and no usable Wi-Fi claim. Parent owns review, promotion and the single cohesive next image with already-reviewed V2 BSSID/readback/RF changes.
