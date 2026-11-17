# Claude candidate independent review

Reviewed candidate audio_afe.c/.h against actual69 snapshot and selected Android source. No material blocker found on the captured inherited-state path.

- ADSP on/off classification matches exact source control and dual status; no ADSP writes. Separate protection bit3 remains untouched (pg.c507/509,3370-3460).
- Borrowed live APLL2 is rate-validated, has no PCW/postdivider/power/enable/global-tuner writes, and remains running through stop. Borrowed aud_2 and engen2 remain ungated on their correct parents. Cold PLL source path remains separate.
- Audio and intbus reparent-to26M uses source CLR/SET/UPDATE fields and correct update bits0/1 (clk-mt6885.c123-124,1834-1844; afe-clk.c246-260). Only owned fields change; inherited parents restored at stop. Neighbor ADSP/UFS fields preserved.
- AUDIO-only protection bit4 follows source on/off sequencing and returns to inherited clear after domain isolated/off. Already-live infra gates stay live. audio_h is read-only and now included in live drift verification.

Independent ASan/UBSan harness passed five isolated cases: three actual-snapshot init/start/feed/stop cycles (1335 modeled transfers), wrong livePLLrate, inconsistent dual ADSP status, inconsistent ADSP control and audio_h drift. All69 snapshot words restored bit-exact except keyedSPMhighhalf; no borrowedPLL/ADSP writes or transient inherited supply gating; no AFE accesses before AUDIO fullyon. Receipt verification/candidate-20261023T175106.025792Z/results.json and source-sha256.json.

Test-only correction: seed bm.ifr1/ifr2 from captured snapshot. m_read internally gates DMA access using those model fields while the wrapper independently supplied captured MMIOclock words. Without seeding, model default falsely represented gatedDMA and rejected its legitimate read. DMA EN0/channelidle/exact9874 POR are explicit simulated assumptions, not measured hardware facts.

Scope limitation: source classification permits additional inherited combinations beyond the captured image. This review establishes capturedstate preservation and existing cold-path retention, not every possible firmware clock-tree configuration. Uncaptured controller/ampstate guards stay intact; first physical transfer can still refuse with meaningful diagnostics.
