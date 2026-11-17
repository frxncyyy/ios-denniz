# Manual changed-branch checklist

Completed against the actual CI kernel; see [REPORT.md](REPORT.md) and [report.json](report.json) for the separate semantic verdict and branch anchors.

1. **Policy, `_niwi_scan_admit`.** Exact5456 extent before loads, authoritative
   owner/request bounds≤39, semantic defaults0/INT32_MIN, exact scan options
   0/2/32 with high word0, mixed34 and all other active bits refused. Distinct
   negative admission families survive. Same channel allowlist/dedup and a single
   complete output commit after validation. No renewed whole-tail zero sweep.

2. **BSS metadata, `_niwb_bss_metadata`.** Only basic-marked membership selectors
   HT127,VHT126,H2E123 are skipped as rates; other values keep existing validation.
   Rate count bounded16, actual RSSI and raw IE bytes preserved. IE bound1024,
   coherent channel requirement and atomic metadata output retained.

3. **Snapshot, `_niwa_snapshot_prepare` including inlined row.** Backend count≤16
   and valid scan/completion state precede publication. Pending/partial/timeout
   return1 and do not make a ready cursor. Completed map/cursor checks remain
   global refusals. Each row independently returns omission reasons1/2/4/8/16;
   only encodable rows increment count, omissions increment omitted/OR mask.
   Output slot uses accepted count, row stride1164, bounded by16; all-omitted
   **completed** scan becomes true ready-empty. No synthetic RSSI/rates/IE data.

4. **Backend, `_nsb_decode` / `_nsb_add`.** Retention room1024; longer source IE
   count still records truncation/true total. Record copy/array stride1112 and
   count16 agree with backend schema. Matching SSID/BSSID update preserves an
   on-channel observation when the newcomer mismatches; otherwise replaces one
   whole observed row, including timestamp/RSSI/IEs, without mixing samples.

5. **Client `_scanLocked`.** Armed/reference requirement before enqueue;
   generation exhaustion returns NoResources, pending ticket and enqueue result2
   return raw16. Owner failure stays NotReady. Admission refusal stays Unsupported;
   cache generation/reset and retain/enqueue/release ordering remain. Diagnostics
   bound channel walk by listed count,400 and actual `n>=84+12*j`; no unchecked
   payload reads. Check exact18696-byte snapshot reset.

6. **Client `_completed`.** Existing phase/generation/ticket/epoch and real view
   checks precede snapshot conversion; actual success commits cache before event.
   New omission fields use correct snapshot offsets and only current snapshots.
   Pending/partial/conversion failure cannot publish a ready empty cache. Release
   backend slot/client after unlocking as before. No new native completion claim.

7. **Client allocation/free only.** init IOMalloc and initial bzero, scan reset,
   free IOFree all use18696 /0x4908. Retained pointer is unchanged; no mismatched
   old snapshot size. These functions are extracted only for size/call sanity.

8. **Schema evidence.** Focused/CI DWARF member layouts agree for nsb_record,
   nsb_scan,niwb_bss,niwa_snapshot wherever used. CI source/generated-header audit
   covers backend normal_rx/normal_transport/sweet_touch as well as snapshot,
   metadata/client. Missing exact CI evidence remains explicit; a copied focused
   object is not evidence of CI identity. Do not infer schemas from symbol names.

9. **Numeric strings/call targets.** Linked ADMISSION/CHANNEL/SCANREQ/COMPLETE
   map to actual bounded references. New omission counters and timestamps carry
   numeric metadata only. String presence alone is insufficient. Raw16 semantics
   are supported by the independent exact-IPSW review, not by generic IOKit enum
   naming. Quiesce proof belongs to the separate review, not this checklist.
