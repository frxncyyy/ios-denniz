# Next candidate: one preflighted normal-configuration sequence

Build the remaining configuration as one transaction plan on the **same retained
CAP/BASIC worker**, after an actual successful BASIC transport receipt and before
parking. Reuse the reviewed byte builders and regulatory inputs. Do not spend a
separate image merely to print a country or MAC: the current BASIC image already
reports the actual tag3/PHY/resource inputs needed to choose the source branch.
No future RF or transport authority is granted by this document.

The useful next image boundary is: authenticated local address selection and
application, complete selected domain/power/manufacturing command batch, then a
bounded retained RX pump. It must not send only a domain command and label the
normal sequence complete. A first passive scan can share that image **only if**
the BSS/scan/RX and power prerequisites below are all implemented and reviewed;
otherwise the intended milestone ends at a truthful configuration TX receipt and
retained service, without advertising a Wi-Fi interface or scan availability.

## Actual telemetry update from the failed BASIC attempt

The immutable first BASIC log b94b01cb... is now observed: resource v1 PSE
128data/3cmd, PLE912data/0cmd, pages128/64, extra8; one debit leaves2 PSEcmd.
Metadata decode valid1, sku_present0 (no tag3), PHY present/band5=1/BW2/NSS2/pathf.
Country0/efuse0 are absent-tag defaults, NOT an observed board country. This
selects the source **legacy passive/domain/power branch**, so the existing V2
regulatory evaluator/builder cannot directly produce the next current-hardware
commands. Do not keep chasing the V2 missing-file/zero-batch branch as the active
case. It remains relevant only if a later actual CAP changes the SKU branch.

BASIC itself failed phase3/error17 and did not produce a successful transport
receipt; its exact descriptor words remain unknown. The separate frozen
basic-config-txd-source-parity candidate repairs the source completion policy
with captured diagnostics. No normal batch can be admitted until that actual
BASIC receipt succeeds. These logged metadata are useful for offline porting;
they are not a live retained session after recovery.

With only2 conservatively remaining PSEcmd credits, even12 NVRAM frames alone
cannot fit the one-way policy. For a cohesive batch, port/review selected AXI
software-credit release after known successful publication; never reset or clone
the existing budget, never refund an uncertain callback, and retain explicit
first-BASIC debit provenance. Legacy power work must port actual country/group/
project-table selection, not reuse the V2 helper or an arbitrary default table.
Selected source starts with g_rRlmPowerLimitDefault but get_project-based paths
can select OEM tables; the current runtime project/override path must be resolved
from authenticated source/config evidence before choosing a table. No guessed
project number or board-country bytes are supplied here.

## Concrete implementation split

1. Add `normal_config_plan.c/h`: pure, atomic preparation using actual retained
   CAP/PHY/tag3, full authenticated board source, current BASIC resource ledger
   and source policy inputs. Reuse `nni_address_select`, `nni_nvram_plan` and
   `nni_nvram_fragment`; reuse `nrp_caps_decode`/`nrp_board_decode`, `nre_evaluate`
   and `nrp_domain_frame` only for a genuinely observed SINGLE_SKU branch. For
   current sku0, add source legacy passive-info/domain56B builders and actual
   country/subband lookup before preparing any mutating batch. The evaluator
   currently has author/reference tests but its manifest still says independent
   review pending. Do not treat it as a promoted runtime authority.
2. Add `normal_config_power.c/h` for the now observed legacy branch: exact
   selected country-power input/parser, version-specific command builders and
   finite batch planner. This is the remaining substantive source port, not a
   caller `power_ready=1` flag. Resolve null file/zero batch before division or
   any new I/O. No MAX_TX_POWER63, unrelated-chip batch size or rule EIRP value
   may be substituted for a selected power table.
3. Add `normal_config_bind.inc`: same-worker native session, exact object/pool/
   CAP/BASIC/board provenance, immutable plan and current command lease. A new
   reviewed phase may admit completed5+k only when k equals the actual native
   plan cursor and all previous commands have exact completion. Never widen to
   completed>=5 or let an opt-in flag/copy of the plan grant access. Preserve
   current default-off BASIC behavior. Keep shared native ABI unchanged if
   possible; new session/buffers stay separate.
4. Add `normal_rx_service.c/h` and narrowly extend the actual native memory/
   register allowlists to the five existing RX queues. Reuse scan-bss copied
   decoders only after descriptor ownership and classification. Keep request
   and unsolicited-event lifetime on this worker; no artificial new readiness
   flag, pointer to a rearmed buffer or parked-worker replay.

The declarations in continuation-contract.h describe the proposed bounded data
boundary; they have no implementations, callbacks, state-machine authority or
hardware readiness. sequence.json supplies exact source order and wire shapes.

## Address and board handling

Retain an immutable full6146-byte copy of the actual boot's chosen calibration
property and authenticate its exact SHA256 using the existing preparation trust
path. The old FORCE_CAL228-byte subset and `nfa_native.prepared` alone are not an
owned full-board snapshot. Reuse the peer's saved board provenance; no additional
device dump or generic MAC log is needed. The known saved board MAC is valid;
the actual capability MAC in the successful CAP receipt is invalid. Native must
still validate its own same-session immutable input.

Selected precedence is valid host/NVRAM MAC, then an actual decoded valid
firmware address. Refuse if neither is valid; omit the source time-derived random
fallback. `wlanUpdateNetworkAddress` and `nicApplyNetworkAddress` perform local
host copies and `kalUpdateMACAddress`, **not a firmware address command**. Apply
the chosen real address to the owned normal host context; do not create P2P/BOW
interfaces or mark an ifnet up. Later source BSS activation CID11 carries the
actual own address plus reviewed BSS/own-MAC/BMC indices. NVRAM CID48 also carries
the authenticated board prefix. Neither transport completion alone proves the
firmware has applied an address. Log only address source/valid/local-application
state, never bytes, SSIDs/BSSIDs or calibration payloads.

## Source sequence and branch decision

Source order is CAP/resource -> BASIC -> local address selection/application ->
post-ready host power-list initialization -> manufacturing band/country setup ->
(legacy passive-info if SINGLE_SKU off) -> domain -> country-power -> all board
NVRAM fragments. `txPwrCtrlInit` initializes two host lists; it is not a power
command receipt. Manufacturing intersects actual PHY5GHz with both board5GHz
switches. Preserve country roles: the V2 regulatory control uses actual tag3
valid efuse country or source reset00; board country does not override that
control. Legacy manufacturing uses its selected board/domain path and needs a
separate reviewed builder. A source world-table fallback is not geographic/RF
permission. Never select a country from an image name or guessed location.

If actual tag3 is absent, do not feed a fabricated SKU flag to the V2 builder:
implement the source legacy passive/domain/power branch first or refuse the
whole batch before new I/O. If tag3 is present, V2 domain alone is already
structurally supported, but the power contract remains unresolved: selected
SOC3 lacks static file/batch assignments in the bounded audited source; caller
ignores file-load failure and version0 can reach a zero divisor. This is not
proof of an active runtime bug. No firmware table version/file/batch can be
inferred from the BASIC country's numeric value. Resolve source/config inputs
offline before preparing the next mutating image.

The pinned board produces12 structural CID48 fragments, each1564B on wire with
1500B zero-padded payload; terminator4297 and ignored tail1846 follow source.
Preflight every span/length/version, selected power command and full domain list
before publishing any command. Keep the full source immutable through completion.
Do not send all6146 bytes blindly or reuse the FORCE_CAL subset.

## Resource and failure policy

The first BASIC ledger deliberately spends one actual PSE TC4 credit permanently
and zero PLE. A complete batch needs exact additional command count, including
all power batches and12 NVRAM frames. The smallest extension keeps conservative
one-way debits and requires the **whole batch** to fit actual remaining credits
before first I/O. If it does not, it cannot invent refunds or partially send a
prefix and call it complete. A separate implementation may instead port the
selected AXI software-credit release after successful publication (hal_pdma.c
2563..2593); that source release is not a firmware ACK and must not occur on an
uncertain failed callback. It requires independent ledger/replay/failure review,
not resetting the old nnr_budget. Retaining DDONE gating for buffer reuse remains
mandatory under either policy.

Missing pure prerequisite -> report exact reason, zero new publications/debits,
park with actual BASIC transport completion retained. Once any new transaction
is uncertain -> all common halts before captured reporting, existing bounded30s
scheduler drain and panic. Reserve immediately before each one-use publication;
no retry, invented response, unsafe refund, reset or teardown. Every command must
use the next real sequence/ring slot and current exact planned CID/wire bytes.
Reuse3s transaction bounds; choose a separately reviewed total batch deadline and
finite command budget before native promotion. Commands are set/no-response;
per-command DIDX/DDONE receipts must say transport completion, never firmware ACK.

## RX and first scan boundary

Service the existing source event1->DMA1RX0, data0->DMA0RX0, data2->DMA0RX1,
TXdone3->DMA0RX2 and TXdone4->DMA0RX3 queues with independent captured indices.
The current allowlist covers only the event slot; do not extend it to the whole
pool. Check DIDX/DDONE/coherent status/exact ring+buffer extent, copy owned bytes,
classify by actual RXD, then dispatch. An event queue is not the only possible
packet class, and a TXdone/data port is not permission to rearm an ownership
report. Unsupported MSDU/token/report/ownership events require an explicit
source handler or fail-stop, never silent credit return/drop/rearm.

The scan-bss helper covers bounded beacon/probe frames and EID0d completion; it
does not issue CID3 or implement general reports, data, keys, association or an
iOS Wi-Fi service. Before CID3, resolve source BSS/own-MAC/BMC indices and review
CID11 activation, any necessary BSS-info setup, exact1252B wire scan request
(normal64+CMD_SCAN_REQ_V2 1188), passive-only channels/dwell from actual evaluated
policy and power contract, payload scan sequence distinct from command sequence,
all-five-queue lifetime and matching SCAN_DONE/deadline. Never turn completion
alone into BSS entries. A bounded partial or empty snapshot stays partial/empty;
no fabricated networks or RSSI. Keep interface/carrier unavailable until a real
data/association/key path and the iOS control bridge exist.

## Validation before the combined image

Dry-run real authenticated board planning without printing bytes; atomic/refusal
fixtures for missing/duplicate tag3/PHY, actual country branch, malformed power
inputs, insufficient whole-batch credits and mutated board/plan; exact wire and
source-extracted layout tests for each power/BSS/scan builder introduced. Extend
the real native-owner fixture to reject clone/wrong thread/replay/incorrect cursor,
CID/sequence/extent and post-completion callbacks across every planned command.
Fault every callback position across domain/power/NVRAM/RX, retaining no refund on
uncertainty. Source-reference checks for regulatory mapping remain separate from
hardware facts. Only after independent review/normal sibling compilation/fresh
CI/linked/image verification should a new exact image be considered for approval.
