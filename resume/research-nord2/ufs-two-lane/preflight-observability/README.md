# Isolated preflight observability candidate

PRIVATE ONLY: copies of canonical engine/header/adapter; no active source,
patch, CI or device changes. Existing predicates, order, short circuit and
writes are unchanged. Host tests run the full engine and adapter fault suites
against THESE private copies plus differential evaluation-trace checks.

Initial preflight alone records every value actually evaluated, including the
original two separate HCS reads. It stops at the same first failure. No MMIO
reread is introduced for output. preflight_valid bit i marks value i as observed;
preflight_fail=i+1 identifies the failing predicate (0 means none). A value0
with a clear valid bit means UNREAD, never a measured zero. Ownership callback
records its initial invocation only, including its first failing subcondition.

Gate indices (GATE report vN):
0 ownership result;1 HCE34 bit0;2 HCS30 ready mask0xb;3 HCS30 error mask0x30;
4 UTRLDBR58;5 UTMRLDBR78;6 IE24;7 AHIT18;
8 UECPA38;9 UECDL3c;10 UECN40;11 UECT44;12 UECDME48;13 IS20.
All except ready/enable gates require zero exactly as before.

Ownership indices (OWN report vN):
0 in_progress;1 stop;2 uncertain;3 root_owned;4 ready;5 writable;6 probe_only;
7 LUN;8 bdev;9 bmajor;10 cmajor;11 regsVA;12 DMA VA;13 DMA PA;
14 first_block;15 nblocks;16 UICpending;17 CPU interrupts enabled;
18 interrupt context;19 preemption level;20 UTRLBA;21 UTRLBAU;22 UTRLRSR.
DMA/partition equality uses the adapter's immediately captured context as before.
All values preserve uint64 representation; valid bits are mandatory to interpret.
Mutex assertion remains first and unchanged (assertion failure cannot be logged).

Existing seven report lines are unchanged, followed by PREFLIGHT/GATE/OWN lines
with unique labels; each is bounded below255bytes even at maximal fields. All
printing remains in halt after mutex release. LAST fields retain their existing
meaning: after a completed UIC, not an initial gate snapshot. No full profile
report is invoked: that hook acquires the mutex and has unrelated report-once
semantics. Stop-before-root and return blocking remain unchanged.

Run: python3 research/nord2/ufs-two-lane/preflight-observability/test.py
Tests: full faults/timeout/rollback/hash suites, AArch64 engine compile,
ASan+UBSan adapter, every existing ownership refusal with contiguous valid mask,
success initial masks, bounded20-line output, differential original/new MMIO
trace for all14 initial gate outcomes. No new hardware claims.
