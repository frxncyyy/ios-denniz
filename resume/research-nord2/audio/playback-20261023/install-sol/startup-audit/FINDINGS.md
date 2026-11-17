# Nonhardware audio startup audit for Claude

Scope: canonical audio_device.c / audio_stream.c / audio_live.c / native bridge
and unchanged HAL, after actual first-PCM evidence. No canonical/phone/CI edits.
No fatal memory-ring wrap, live token ownership or HAL routing defect found in
this bounded source audit. Hardware cold gate f8 remains outside this scope.

## Proposed bounded fix: diagnostic deadline starts before backend startup

Source: audio_device.c108..114 calls nau_hw_start(...duration,now_us()), then
prints synchronously even while running. audio_hw.c243 sets deadline=oldnow+
duration before AFE/amplifier/board work; tick301 checks this deadline before the
first pointer baseline. Therefore elapsed startup consumes the advertised1s
TONE/SILENT interval. Injected1.2s startup reproduces an ALREADY EXPIRED1s deadline
immediately on successful return, so first tick stops normally with zeroPCM.
Continuous HAL uses duration0 and is not affected by this deadline defect.

Private audio-device-test-deadline.patch changes ONLY the device start wrapper:
retain low-level validation and all bounded hardware startup/cleanup; after
successful startup, set a nonzero test deadline to fresh now+duration. Reject
clock regression/overflow, set deadline error and stop (preserve stop_error/
uncertainty). duration0 is unchanged. Successful startup no longer printf's while
DMA runs, keeping synchronous console IO out of the2ms feeder. Failure logs stay.
No clock/I2C/AFE/HAL source changes or new hardware operations are introduced.
The bound remains requested active diagnostic duration<=2s AFTER already-bounded
startup; it does not assert total startup+test wall time<=2s. No scheduler policy
or watchdog threshold changed. This measures active test wall interval, not a
claim of exactly48000 audible frames through the21ms prefilled DMA pipeline.

Files: audio_device.c (complete proposed source), audio-device-test-deadline.patch,
start-{original,proposed}.inc (exact extracted start bodies), test-test-deadline.c.
Original regression reproduces expiry and console-during-DMA. Fixed regression
passes ASan/UBSan for silent/audible modes and0/50/250/1200ms injected startups,
continuousHAL, overlimit duration refusal, start failure, clock regression/
overflow, cleanup failure evidence and absence of console during runningDMA.
This tests the exact proposed wrapper against mocked low-level start/stop; it
is not hardware execution. Complete proposed audio_device.c compiles NORD2 with
Wall/Wextra/Werror using CI39969463181 exported headers; objectSHA
4637b45ca01016d6303160b137b3392c8c9712eec77c564d46ed87a883c3d92f.
Parent should review/promote this small patch before regenerating/building.

## Accounting observation only; parent explicitly deferred unrelated change

nsa_flush (audio_stream.c18..20) discards queued acceptedframes without adding
to dropped or exposing a discarded-frame counter. Reproducer test-drop-accounting
shows mute/STOP/idle each yielding written4,delivered0,dropped0,queued0 after
four accepted frames are flushed. Current dropped counts only ingress rejection,
not all eventual discardedPCM. Document this when interpreting live telemetry;
no accounting change is included in the proposed fix and it is not a playback
startup blocker.

## Checked with no new findings

8192frame producer/consumer ring indexing and capacities are bounded; stereo
16bit scaling remains int32-safe at-32768; ownership keeps hardware feed/tick/
stop on one worker; live duplicate token/pending/sequence logic remains bounded;
existing fresh-locked idle logic preserves a concurrent new short sound. Stop
and mute flush producer before bounded worker stop; future freshPCM survives the
new stop path. First valid pointer establishes baseline, zero pointer is bounded
12ms, active deadline guards remain fail-closed. Do not rewrite the unmodified
HAL: actual log already proves opens1 and nonzeroPCM writes/peaks.

## Integration-ready current-source regression (latest followup)

Promote private test_device_deadline.py -> tests/nord2-audio/test_device_deadline.py
and test-device-deadline.c -> tests/nord2-audio/test-device-deadline.c. Apply the
one-line tools hook in audio-check-deadline-hook.patch. Unlike the earlier .inc
fixtures, this helper reads canonical shim/nord2-xnu/audio_device.c at EVERY test
run, extracts exactly one start() body with comment/string/character-aware brace
scanning, writes only a temporary include, and compiles/runs the fixture with
clang C11 Wall/Wextra/Werror + ASan/UBSan + no sanitizer recovery. Hook passes no
source override, so it exercises the actual current canonical source. Temporary
compilation and execution CWD prevents assertion core files in the repository.

Validation: current unfixed canonical fails specifically on deadline duration
with1.2s injected startup (current-source-negative.log). Explicit --source pointing
to proposed complete source passes all focused cases. Lexer cases verify brace
characters in comments/strings/escaped chars and refusal of missing, duplicate
or unterminated function. The optional --source exists only for private proposed
source validation; normal integration uses the canonical default. No duplicate
start implementation remains in the promoted C fixture. Broad unchanged suite
was not rerun, as requested. Dropped-accounting remains deferred.

## Claude candidate merge review (latest)

Candidate path: playback-20261023/claude-startup-cold/candidate/shim/nord2-xnu.
Its audio_device.c changes diagnostics only: saved last-I2C register/value,
ownership and amp status in diagnose(), plus one boot OWN log. New fields match
candidate audio_hw.h and are filled by candidate audio_hw.c/nau_hw_status from
saved structures; no extra MMIO is added. USB snapshot remains manual48words,
so adding fields to internal nah_status does not alter the live wire ABI. Promote
candidate audio_hw.c+h together with audio_device.c; old header lacks the fields.
No correctness blocker found in those diagnostic additions.

The Claude candidate DOES NOT include the test-deadline/active-console fix.
Private claude-merged/shim/nord2-xnu/audio_device.c is the exact merged file:
all Claude diagnostic changes retained, only start() replaced with reviewed fix.
claude-candidate-device-merge.patch is relative to Claude's candidate, not current
canonical. claude-candidate-check-hook.patch adds the regression to Claude's newer
check tool without dropping its inherited-state or ready tests.

Promote these FOUR files from claude-merged after the candidate hardware set:
- shim/nord2-xnu/audio_device.c
- tests/nord2-audio/test_device_deadline.py
- tests/nord2-audio/test-device-deadline.c
- tools/nord2-audio-check.py

Full shim copy in claude-merged exists only to resolve exact candidate headers
for private compilation; it is not a request to replace the whole canonical shim
from that copied tree. claude-merge-receipt.json pins the four intended files.
Helper root discovery now recognizes shim+tools layout rather than AGENTS.md,
so it works in Claude's temporary test overlay (which has no AGENTS.md).

Focused regression extracted the merged source at runtime and PASSed strict
ASan/UBSan. Complete merged audio_device.c cross-compiles NORD2 Wall/Wextra/Werror
against CI39969463181 with candidate header overlay; objectSHA
a07bf046c2d096298afcfebe769d1821763df36b2135f4f9549bd922b4971ce7.
No broad unchanged suite rerun, canonical edits, CI or phone actions.
