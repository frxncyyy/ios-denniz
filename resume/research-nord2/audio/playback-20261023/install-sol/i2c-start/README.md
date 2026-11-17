# Fixed saved AP preflight diagnostic candidate

Promote only audio_i2c8.c/h + audio_device.c after checking manifest.json's
canonical_baseline hashes. saved-ap-preflight.patch is that exact three-file diff.
No guard relaxation: APbusybit0, earlier shadow/CCU/DMA ownership checks and all
five AP predicates are unchanged. The same5AP reads are collected before their
predicates; callback failure stops immediately, successful slots set validbits.
Predicate refusal explicitly restores exact refusedPA/value in last_*; fresh
state clears savedslots/validbits. Diagnostics print only worker-owned savedRAM.
No added USB command/read interface, controller write, cleanup or retry exists.

Tests to promote:
- test-saved-preflight.c -> tests/nord2-audio/test-saved-preflight.c; link with
  audio_i2c8.c ONLY (unloadedboard allowlist stubs live in this fixture).
- test-diagnostics.c -> existing tests/nord2-audio/test-diagnostics.c, or apply
  test-diagnostics.patch. It accepts ERROR + AP lines and checks full/partial
  validmask, savedpayload, refusedPA/value and zero poisonedMMIOcallbacks.

Both focused ASan/UBSan tests PASS. Transport test covers31predicatecombinations,
5readfailures,6earlierguards, START2success and stalevalidreset. Diagnostic test
extracts actualcandidate diagnose() through verification/ready-diagnose.inc.
Parent owns final suitehook/fulltests. No parent model/test-I2C files changed.
Kerneldevice+transport objects already compiled Wall/Wextra/Werror; per request
no additional native checks were run after delivery. Verification copies are
private test/compile overlays, not files to promote wholesale. CompletionIRQ
printing deferred to avoid extra changes; existing completion_irq remains saved.
