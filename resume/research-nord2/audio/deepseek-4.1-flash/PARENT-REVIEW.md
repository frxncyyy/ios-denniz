# Parent's narrow review of the private audio revision3 candidate

2026-10-21. Read-only inspection of the saved handoff, revision2 build
evidence, revision3 candidate and manifest, and selected Android source
`../nord2-current-kernel` at
`04d00c2565764c3922c9b4826b5acff1106e533f`. No candidate, canonical
source, CI, image, device, session or test output was changed. All 38 files
listed in revision3 `manifest.json` matched their hashes at review time;
that manifest's SHA-256 is
`fa19d12bcc5a63ba470070c758c432262d931c75def60d87da250ad9297b9adb`.

## Findings for Claude continuation

1. **P1, mode2 I3C refusal reads the wrong V2 register.** The selected
   driver obtains `I3C_EN` from `OFFSET_DMA_FSM_DEBUG`
   (`drivers/i2c/busses/i2c-mtk.c:1040`). Its V2 table places that register
   at **0xec** (`i2c-mtk.h:295`); `0xe4` is `V2_OFFSET_DEBUGSTAT`
   (`:275`). Revision3 reads `NA8_CH+0xe4` in
   `candidate/shim/nord2-xnu/audio_i2c8.c:189` and allows only `0xe4` in
   `audio_id_bind.inc:152`. The model exercises that same wrong address.
   Hence bit 15 in the actual I3C-mode register could be set while this
   guard passes. Update the core, binding allowlist, model and focused test
   to source-backed `0xec` before treating mode2 as safe to integrate. The
   driver's extra high-speed condition at `i2c-mtk.c:1041-1045` means a
   low-speed blanket refusal is conservative, but reading the correct
   register is still necessary for the stated policy. Candidate core SHA
   `79e31019b3b703c6a8ff8d8a5735cc45ff2d07245ee62dbe64038d6744b41881`;
   binding SHA
   `ae5816161437023391857afe10999eb5a3225446b65e67127e99436989e238b0`.

2. **P1 factual correction, revision2's actual native compile was not
   empty.** `HANDOFF-FOR-CLAUDE.md` §1, revision3 `README.md` item 5,
   `REVIEW.md` D1, and `compile/result.log` incorrectly generalize the
   **ci-39158169212** stale-export control to revision2. Revision2's actual
   `revision-completion/compile/commands.json` (SHA
   `83068bae7c2899870958d315612e28c003548272b14765012d0693c0e3464373`)
   uses **ci-39398655023**, whose exported `pexpert/arm64/SWEET.h` defines
   `NORD2`. The three saved outputs are `audio_id.c.o` 24,152 B (SHA
   `cca8b87e64ca6503a3d6bcbf1ded6eb841bd9467f8c05d0ee40b74eae05fcf77`),
   `audio_i2c8.c.o` 27,184 B (SHA
   `6fadc43658aa7f09c52a5cf1f6112b10cc9bcf878d0ebc164857576866ef8486`),
   and `sweet_touch.audio-review.c.o` 275,576 B (SHA
   `b59ba60046bbada1d01fa751b5dc80367b72bd7013b42e98c4b358dc5ce4d3b0`).
   `aarch64-apple-darwin-nm` finds defined `_nau_run`, `_na8_probe`, and
   `_na8_return_ok` in the helpers and their undefined references in the
   `sweet_touch` object; the latter also contains `NORD2 AUDIO ID`. Thus
   revision2's native branch was compiled. Revision3 `REVIEW.md` §1 itself
   states this correctly, contradicting its D1 and the handoff. Correct the
   handoff before Claude uses it; keep the ci-39158169212 stale export as a
   **future validation hazard**, not a revision2 invalidation.

3. **P2 compile-context limit.** Revision3's pre-included patched `SWEET.h`
   produces nonempty NORD2 objects against the older ci-39158169212 tree;
   this is real branch-compilation evidence, not a full-kernel link or proof
   of the current generated integration. Its extra `bsd/dev` include path
   resolves sibling quoted includes from that archived tree. For example,
   `bsd/dev/wifi_common.c` and current `shim/nord2-xnu/wifi_common.c` differ
   by hash, and `sweet_touch.audio-current.c` includes `wifi_common.c`.
   Claude/parent should compile the eventual patch in the exact promoted
   generated buildtree, checking the included file provenance and real
   NORD2 symbols, before using it as current integration evidence. This
   caution does not undo the revision3 helper compile or the valid
   revision2 ci-39398655023 compile.

## Confirmed narrow source relationship and remaining limit

The added HW timeout register is the V2 `0x4c` in `i2c-mtk.h:274`, and the
selected driver computes/writes it at `i2c-mtk.c:963-974`; the candidate
saves/restores that word. The exact `TRANSAC_COMP|ACKERR` multi-channel path
matches the source's early `-EREMOTEIO` return without `init_hw` or
`RESUME_ARBIT` (`i2c-mtk.c:1326-1330`); the candidate's exact-status and
START-clear checks are conservative. These source comparisons do not prove
physical STOP, electrical ownership of shared I2C8, or that another master
cannot act between reads. The driver IRQ handler masks then reads status
(`:1695-1698`), but this alone does not prove the candidate's polling with
`INTR_MASK=0` will see every completion. Keep those as explicit unverified
assumptions; no mode2 hardware action follows from this review.
