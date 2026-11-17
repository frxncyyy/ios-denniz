Offline parser: `python3 research/nord2/ufs-linereset-common/ios/parse-common.py CAPTURE.log`.
Exit0 means consistent evidence, NOT necessarily success; inspect classification
and success. Exit2 means incomplete/malformed/conflicting evidence. No image,
USB or device action is performed and no existing report is overwritten.

Full clean_success requires the exact canonical COLD/result/RESTORE/TIME/outer
window/outerrestored flow, validcoldmask7 with CPUbit0/topbit7/powerbit2 clear,
40snapshots and40verified,40touched/restored bits, phase4/status0/error0/
restore_error0/clean1, finalguardvalid3fff/field14, actualnonzerotimerfrequency,
outerstage3/chipid20010101/success1 and outerpowerrestored1. Duration is only
common transaction ticks/frequency, not fullboot or radio initialization time.

Other classifications distinguish skipped_guard, cold_guard_refused,
clean_failed_transaction, dirty_halt, dirty_state_halt_not_observed and
common_not_observed. No common logs is consistent with the actual opt-out path,
but cannot prove opt-out rather than missed logging. Dirty reports with a panic
record conflict with any claimed outer continuation/restoration. A dirty result
without panic is retained explicitly as halt-not-observed, never success.

Source: canonical wifi_common_adapter.inc, wifi_common.c,
wifi_cold_snapshot.c and active sweet_touch.c's outer power-window lines.
Parser checks count/mutation/restore masks, prefix snapshot-valid masks,
canonical order, error/phase shapes, missing/duplicate/conflicting fields,
integer bounds and LFtermination (CRLFaccepted). Unknown/reserved cold bits are
preserved, only the source's precise cold masks are interpreted. Exact original
bytes are recoverable in raw_base64; raw_text/lineevidence/SHA256 remain on
failure. Newline loss or clipped fields cannot produce success. As with any
unchecksummed text, a smaller but syntactically valid number followed by a
manufactured newline cannot always be distinguished from authentic data.

Tests use source-shaped fixtures for complete success, each skip reason,
predicate skips withread/unread fields, noncold/partialreads, snapshot failure,
clean partial-write failure, dirty restoration with/without panic, opt-out and
all missing/duplicate/truncated/conflicting record positions. Fixture formats
are checked against canonical C source strings. No firmware execution, network
connectivity, visibleGUI or performance gain is inferred from these reports.
