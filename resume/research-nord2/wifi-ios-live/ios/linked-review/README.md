# Exact live-control/rejoin/PID linked review — pending

Prepared for source c35ce62d7368a8b3f0a8c91bd52d743cc08d56a3 and CI39924876139 attempt1. No kernel has been read or inspected here; no CI/network query, device operation or stage gate action occurred. requirements.json pins exact committed canonical source, generated0082 patch, existing owning normal ARM64 object context, function selection and reused Mach-O helper.

After root notifies the downloaded complete successful artifact with exact length/hash:

```
python3 research/nord2/wifi-ios-live/ios/linked-review/extract.py \
 --artifact-ready --ci 39924876139 --attempt 1 \
 --kernel-sha ROOT_NOTIFIED_SHA256 --kernel-bytes ROOT_NOTIFIED_LENGTH
```

This verifies local run.json, headSHA/run/attempt/success, kernel bytes/SHA and artifact SHA256SUMS before opening the Mach-O. It writes a fresh artifact/ directory, per-body binary bytes/SHA/start/end, bounded LLVM disassembly/direct-call lines and numeric emitter offsets. A missing/ambiguous required public body stops extraction. Static helpers may be inlined: the first actual selected linked caller is then retained and the absent standalone body remains explicitly unresolved until manual caller review. Frame-validation fallback can retain all three tiny public frame entry points. Total extracted text is bounded256KiB; each function remains below64KiB. No whole-kernel disassembly is performed.

Manual review follows representative actual branch paths in mailbox entry/submit/take/complete/reply, the NSS owner loop, exact current-generation/capture/profile/leave handling and JOIN_READY snapshot. Rejoin proof follows enqueue→same retained worker→native staging/rollback→key-ledger cleanup/callback closure→scan-return→fresh-scan callback→readiness, preserving current HW0/token/allfive RX state. Inlined helpers must be read in these actual callers; source names or emitted strings alone do not prove behavior.

PID review follows validated frame reserve→same wire/HW0 rebuilt PID→attempt immediately before write→confirmed publication after successful index callback→first matching TXS and terminal closure. Ranges do not reuse across generations, fallback127 is ambiguous, and no status may free DDONE/token storage or manufacture authorization. nsdd_test_boot_reset/nsdd_test_pid must be absent from kernel symbols. Capture windows must not reset this ledger.

Existing fatal owner/DMA branches retain common halt/drain; bounded representative call targets are enough to corroborate unchanged failure integration, without repeating complete earlier firmware/WPA/GTK/normalTX-layout reviews. Root owns complete generated-source/generic/whole-ISA proof. Local object identity is context, not assumed linked object equality. This is shell/LLVM evidence; no REA engine analysis or runtime execution is claimed.

The extraction result deliberately has verdict:null and pending manual review. Only the subsequent source-bound address/branch findings and exact artifact identity may produce a separate review.json/manifest.json. Do not copy an old DHCP linked PASS or permit pending requirements to open image/prepared/authorization gates.
