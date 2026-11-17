# Bounded connection observations

`parse-connection.py LOG` validates only numeric connection diagnostics. Run the
strict `parse-backend.py` independently for the retained boot/scan receipt.
`parse-ios-service.py` retains its historical scan summary and includes this
parser under `connection_observations`; it makes no implementation-readiness
claim.

The connection parser bounds the input to 4 MiB, 8192 relevant rows and 1024
bytes per relevant row. Exact schemas, integer widths and source enum domains
are required. Incomplete, duplicate, unknown or interleaved connection rows
invalidate its top-level positive observations. The source budget is at most
112 aggregate STATION/STATIONGUARD/IOS LINK rows and 32 IFNET LINK rows. Client
rows are independently bounded by the total input and row limits, since clients
can reopen. No SSID, MAC, PMK, PTK, GTK or payload bytes are decoded or reported.

The evidence layers remain distinct:

- `association_observed`: native transaction's associated flag.
- `native_authorized_observed`: coherent native service6/result1, CONNECTED15/
  AUTHORIZED1, associated/station/PTK/GTK/controlled-port flags and linked1,
  without native/owner halt/error.
- `authorized_link_observed`: that native proof plus a successful IFNET link-up
  and current JOINED snapshot for the same native generation/ticket.
- `current_authorized_link_observed`: latest native/link/current-callback snapshots still agree,
  with no fatal STATIONGUARD (this first-epoch guard always halts the owner).
- `join_notification_observed`: authorized-link chain plus actually published
  and notified event9 for the same generation/ticket/client epoch.
- `data_air_tx_success_observed`: native ordinary-data FRAME_RESULT status0
  counter; `bsd_input_success_observed`: accepted BSD input counter.

These are source-bound driver observations, not packet captures. All DHCP, IP
acquisition and Internet-connectivity proof fields remain false. Arbitrary text
mentioning DHCP/IP cannot change them. Bounded logging may omit later events, so
a missing observation is not proof that an operation did not occur. Positive
attempt details remain diagnostic when a different row invalidates the overall
receipt; only top-level observations are gated by `valid`.

A retired cookie may retain a globally authorized connection. Stale callbacks
with current0 are recorded but cannot authorize a new epoch. Terminal historical
success and current link readiness are reported separately.

Counters use source uint32 arithmetic. tx_result equals tx_ok+tx_fail modulo32;
accepted-results-queued_drop modulo32 is at most9 (eight queued frames plus one
ordinary wire/air frame). This is a residual bound, not exact current outstanding:
port closure may discard an already-dequeued unpublished frame with no result,
leaving one unsettled accepted counter. DMA completion is never an air-TX proof.

`test-connection-parser.py` tests coherent/partial/terminal traces, stale and
contradictory epochs, malformed and width/domain/budget cases, modular counters,
cookie detachment, and preservation of the scan summary. Synthetic success does
not prove native hardware association, keys, DHCP or IP. Actual source fixture
traces, when pinned in the accompanying report, validate emitted schemas only;
they may contain explicitly seeded native state and mocked hardware.

The stage builder/runner are never invoked by these tests. All image, identity
and authorization gates remain owned by the parent task.
