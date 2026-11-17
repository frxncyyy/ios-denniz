# AFE protection request / acknowledgement correction

Selected source pg.c231-235 defines requestEN2 10001710 independently from STA1ACK10001724. On-path3351-3353 deliberately ignores releaseACK. Append NAU_S_PROT2_EN as index69, keeping original69 measured indices/values unchanged. Fixture explicitly labels newword0 SYNTHETIC/unmeasured; next realinit reads it. All ownership/access/watch/releaseverification uses requestbit4. ACK remains captureddiagnostic and required1 before shutdown SRAM/isolation.

Powerdown verifies BOTH requestSET1 and ACK1. Allowing stale releaseACK1 otherwise permits a droppedSET to falsely pass an alreadyasserted ACK. Dropped CLEAR/SET or missing mandatoryACK still latchesuncertain; never forces powerdown. Stale releaseACK is not watched as ownership drift or required to reproduce bootACK0.

Runtime source changes only audio_id.h/.c and audio_afe.c. New address is read-only in existing mappedinfra window; no addedMMIOwrite. Arrays/initialsnapshotloops/logging use countdynamically. Snapshot remains internal, absentfromHAL/USBstatuspayload; no active fixed69 parser found. Watchcount stays23<32; plans48/35 inherited and73/50 cold <128. Snapshot +4bytes; extraSET-request poll up to10ms occurs only during stop after amp disable.

Tests/models: cold fixture gives request1; request aliases update request and diagnosticACK separately; inherited69fixture unchanged with explicit syntheticappend; restored() verifiesrequest and ignores only ACKbit4 (otherACKwordbitsstillverify). Existing protect-timeout now means SETcommitsbutACKneverarrives, exercising requiredACK1. Test-protection adds3staleACKcycles, all4request/ACKownershipcombinations, requestdrift, droppedstartupCLEAR, droppedfinalCLEAR, droppedSETwithstaleACK1, missingACKafterSET. No ampmodelpatch bundled: installagent owns it.

Durable tests/test-order.c also verifies Androidorder andMCUlast under sourceINTR_MASK129, plus dropped/stickycompletionW1C/readbackfailure. tools/nord2-audio-check.py registers order(always) andprotection(withprivateboard). New test source reuses test-inherited's registermodel behind explicitINHERITED_MODEL_ONLYguard; standardinheritedmainunchanged.

Private verify.py passed sanitizerprotection+all13inheritedcases+order+plandump andnormalNORD2ARM64id/afecompile against parentGIC/I2Csource and installampmodel. Fullstream/fullhardwaretests were not counted: concurrentparent fatalDMApolicy intentionallychangederroruncertainty and installagent is updating those expectations. Parent must run unifiedsuite aftermerging nonoverlappingtest-streamhunks.

Apply combined.patch or individual focusedpatches; do not copy entirestagedsource tree because it contains dependency snapshots. No canonical/device/CI modifications by this agent.
