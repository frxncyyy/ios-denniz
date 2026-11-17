# Private audio revision4: I3C register correction

This is an isolated source and host-test candidate. It copies revision3 and
changes only its I3C-mode read, native read allowlist, and focused model/tests.
There is no mode2 hardware authorization, device operation, CI dispatch,
image build, or active-source integration. The snapshot and shared-I2C8
contracts are otherwise unchanged.

The selected MT6893 driver reads `I3C_EN` from `OFFSET_DMA_FSM_DEBUG`
(`i2c-mtk.c:1040`). For I2Cv2, `i2c-mtk.h:295` maps that to AP-channel
`0xec`. Revision3 used `0xe4`, which the same table calls `DEBUGSTAT`
(`:275`). Revision4 reads and allows `0xec`. Its host model rejects an AP
`0xe4` read during the PIO transport, and tests both directions: bit15 at
`0xec` refuses before a write; bit15 at `0xe4` alone does not masquerade as
I3C mode. Blanket refusal at 100 kHz is conservative because the selected
driver's special HFIFO sequence also checks speed; it is a policy choice,
not evidence of electrical ownership.

One Clang ASan/UBSan run of the existing focused core and actual-binding
suite passed. Three ARM64 objects compiled with `tools/nord2-xnu-cc.py`
against **ci-39398655023** normal exported NORD2 headers, with no forced
`SWEET.h` pre-include or extra archived `bsd/dev` include path. The include
trace resolves `wifi_common.c` from the current shim and the audio binding
from this candidate. This verifies nonempty branch compilation; it is not a
full kernel link, current CI result, image, or hardware test. See
`compile/commands.json`, `compile/include-trace.log`, and `REVIEW.md`.

The revision3 handoff's claim that revision2 compiled empty was incorrect.
Revision2 used ci-39398655023 and produced real NORD2 helper symbols; the
empty 808-byte control used the older ci-39158169212 stale exported header.
`REVIEW.md` pins the evidence and limits. Claude should use this revision4
source, and the corrected revision2 provenance, when continuing audio work.
