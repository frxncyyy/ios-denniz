# Private lifecycle-to-scan snapshot adapter

This uncalled pure adapter connects actual immutable `nsb_scan` input to the reviewed age-aware IO80211 scan-row/cache contracts. It implements only the state boundary that can be proven. It does not register an IOKit class, publish an interface, activate radio scanning, install a service dispatch or manufacture notifications/readiness/networks. Frozen lifecycle shell v2 and completed-scan-adapter-age remain unchanged.

## State and buffer ownership

All `nils_open/begin/finish/take/close` calls require the actual lifecycle client's existing `gate`. This helper is not itself a lock or task authority. Native admission must first perform the shell's actual owning-task/phase validation. Opening requires a strictly newer nonzero generation and no inflight preparation. Begin allows one preparation, assigns a monotonic serial, detaches an old cache and returns a generation/serial ticket. No generic caller boolean grants a backend lifetime.

The caller retains the actual backend scan owner, immutable scan object, output allocation, client object and rate map through preparation and matching finish. Only `nils_prepare` runs outside the client gate, on this retained immutable input. The output must be freshly zeroed and remain at a stable address through its cursor lifetime. Snapshot memory is not copied or relocated by this adapter; cursor pointers remain within the original output. Snapshot conversion is bounded by existing NSB_RESULTS and row lengths. There is no backend MMIO or additional clock access.

Finish consumes only the current exact ticket. A closed generation returns canceled (2) and never installs output; mismatched or replayed tickets cannot consume another preparation. A matched preparation error consumes the ticket and leaves no cache. Open cannot proceed while a canceled preparation is outstanding. Close closes admission and detaches the cache immediately, but must retain state and inflight buffers until finish. This permits `clientClose/clientDied` to prevent future reads without destroying memory used by an outstanding converter. `nils_take` is gate-held and refuses closed, inflight, stale or missing caches. It does not expose raw row pointers.

Complete state7 snapshots, including genuine zero-result scans, install a reviewed cursor. Empty complete returns actual EOF (0), not a fictional populated result. Pending, partial, dropped and timeout snapshots return noncomplete (1), never install READY/EOF and preserve their distinct NIWA status in the output. Invalid completion/age/rate conversion refuses. Ages use caller-authenticated same monotonic microsecond clock and explicit capture/reference instants as already established by the age revision; GET does not silently refresh them. Populated results still require an independently proven Apple rate map. The test-only synthetic rate123 is not a native map.

## Exact native shell connection boundary

The existing `native-lifecycle-shell-v2/LifecycleShell.cpp` supplies an actual IOLock, retained task, close/death path and mapping generation. It deliberately supplies no backend dispatch. The future installed backend must, under that same gate:

1. Admit the owner request only while open, bind the helper generation to the actual retained scan-session generation, retain the client and immutable backend objects, then call begin. Mapping-generation changes must not reuse a scan ticket or permit reopen until outstanding work drains.
2. Release the gate before snapshot preparation; it must not launch a radio request from this helper. The real `ncs` command/`nsb` receive owner must have already produced the scan receipt, or return its truthful pending/partial state.
3. Reacquire the gate, finish the exact ticket, and only then release the retained backend/output/client objects according to installed-vs-canceled status. A stale callback cannot install output. Failed preparation passes null to finish so the matched ticket is retired.
4. Route GET only through gate-held take. Close/death calls close before releasing the cache or async reference. Client destruction must wait by retained-object lifetime for the last matching completion; waiting while holding the conversion gate would deadlock.

These are concrete hook obligations, not an implemented OSObject retention mechanism. This package intentionally does not alter the native shell: no authoritative native scan callback/worker binding or generation handoff is installed there, and no proven event-pipe producer/cancellation contract exists for notifying wifid. Adding a synthetic event or wiring a request to nonexistent native authority would cross that unresolved boundary. Nothing here establishes the broader wifid startup contract or CARD/security readiness.

## Verification

`python3 test.py` executes the real helper plus unchanged age, snapshot, result codec, BSS metadata, cursor and reviewed bounded rate helper under Clang ASan/UBSan failfast. Cases cover completed-empty EOF, pending/timeout no READY, stale/replayed tickets, close during preparation and reopen refusal, preparation failure consumption, generation/serial limits, too-short row output, fixed ages and one synthetic populated result; missing rate map refuses that populated result. There are no IOKit mocks or native backend execution in this test.

A separate freestanding generic ARMv8.2-a+crypto compilation of lifecycle_scan.c passed with canonical scan header include path. This is a pure C object compilation, not a normal-context whole native shell compile, kernel link, concurrency scheduler proof or hardware test.

V2 preserves the initial draft and strengthens identity: begin requires a freshly zeroed output and pins its exact address in both pending state and ticket. Prepare requires that ticket address; finish rejects any other output without consuming the pending preparation. A null output consumes only an otherwise matching failed preparation. This prevents accidental same-generation reuse of an old snapshot; mutable C objects still require the trusted native gate/owner contract. Begin detaches old cache without freeing it; caller owns release after gate-held readers quiesce. Tests model serialized operations, not actual concurrent IOUserClient callbacks.
