# Exact UECPA LINERESET exception — source audit

Observed CI39503015652/8702715 snapshot: initial preflight failedpredicate9
(engineindex8), rawUECPA80000010; HCE1, HCS10f twice, doorbells/IE/AHIT0;
all23ownership predicates passed. Commands/SETs/reads0. Remaining errorregs and
IS were UNREAD after short circuit, so they have no observed-zero claim.

Selected Android drivers/scsi/ufs/ufshci.h212–214 defines PHY valid80000000,
code mask1f, laneerror maskf. ufshcd.c6113–6126 explicitly ignores LINERESET
indication as not an error, and handles only valid flag AND lane-error bits.
Bit4 is the sole code-mask bit outside lane-error bits. Therefore80000010
contains valid+LINERESET and no laneerror. No generic/reserved-bit interpretation
or broad fatal-bit suppression is needed to explain the refusal.

Canonical engine change accepts ONLY0 or EXACT80000010 at UECPA38. It does
not imitate Linux ignoring arbitrary non-lane bits. Values10 (missingvalid),
80000000 (validonly), any lane bit/reserved bit, or any nonzeroUECDL/NL/TL/DME
still fail. HCS host/device-error and IS checks remain unchanged; an unexpected
UIC_ERROR IRQ still makes the command uncertain. Same exact exception applies
at every existing health check, including transition/rollback/read validation.
Linux's ignore is unconditional in its error handler, not startup-only policy.
This supports the narrow value classification, not proof of two-lane health.

Read-clear behavior remains UNPROVEN by local source. ufshcd_dump_regs136–141
skips all five error registers in broad dumps; update_uic_error reads each once
and performs no writes to those registers. This supports treating reads as
sensitive, but does not prove a particular hardware clear condition/timing.
No read/clear/reset is added. The same original evaluation readsUECPA once per
health invocation. Known lane errors remain captured before refusal. Test
initial-only behavior models possible read-sensitive disappearance without
claiming this is the actual implementation.

Verification: existing full enginefault/timeout/rollback/digest suite PASS;
initial-only, persistent and transition-only exactLINERESET PASS; every other
PHYbitposition and missingvalid/validonly forms reject. Laneerror added at each
of115commands, including rollback, stops uncertain/nohandoff. UIC_ERROR status
still rejects. Differential oldreference baseline and explicit newexception
reference preserve precise read trace/validity/firstfailure without reread.
ASan/UBSan+AArch64 compile PASS; adaptercanonical-onlyPASS. Parent regenerates
active copies; no device/sourcegenerated/CI action here.

Offline parser must retain oldpolicy when analyzing8702715 images; a future
explicit policyversion should allow the narrowexception. Otherwise the old
fail9 record becomes falsely inconsistent with the newallowed value.
