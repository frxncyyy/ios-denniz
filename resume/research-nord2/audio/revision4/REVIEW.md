# Revision4 narrow validation and handoff

Selected Android tree: `../nord2-current-kernel` commit
`04d00c2565764c3922c9b4826b5acff1106e533f`. Source
`drivers/i2c/busses/i2c-mtk.c:1040` reads I3C bit15 using
`OFFSET_DMA_FSM_DEBUG`; V2 register table `i2c-mtk.h:275,295` distinguishes
`DEBUGSTAT=0xe4` from `DMA_FSM_DEBUG=0xec`. Revision3 read the former.
Revision4 changes `audio_i2c8.c` to AP `0xec`, the binding read allowlist to
`0xec`, and the model/tests to make confusing the two registers fail.
No source beyond that correction was intentionally changed.

Host validation: `CC=clang python3 candidate/tools/test-nord2-audio-id.py`
passed core and actual native-binding ASan/UBSan tests. The I3C positive
case refuses before any write or START when `0xec` bit15 is set; the
negative case proceeds when only `0xe4` bit15 is set. The model itself
rejects an AP `0xe4` transport read. No real I2C8 transaction occurred.

ARM64 validation: `compile/commands.json` records three exact invocations
of `tools/nord2-xnu-cc.py` with ci-39398655023 normal exported headers.
There was no `-DNORD2`, source-header pre-include, or added `bsd/dev` path.
The 24,120-byte audio-ID helper defines `_nau_run`; the 28,352-byte
transport defines `_na8_probe`/`_na8_return_ok`; the 348,736-byte private
`sweet_touch` object references those symbols. `-H` trace shows exported
ci-39398655023 `SWEET.h`, current-shim `wifi_common.c`, and this
revision4 `audio_id_bind.inc`. This avoids the older-tree sibling include
masking seen in revision3's supplemental compile. The private
`sweet_touch.audio-current.c` is still a source copy; a final integration
requires a fresh generated buildtree and full link review.

Factual revision2 correction: saved
`research/nord2/audio/revision-completion/compile/commands.json` SHA
`83068bae7c2899870958d315612e28c003548272b14765012d0693c0e3464373`
used ci-39398655023, whose exported `SWEET.h` defines NORD2. Its saved
`audio_id.c.o` 24,152 B defines `_nau_run`, `audio_i2c8.c.o` 27,184 B
defines `_na8_probe`, and `sweet_touch.audio-review.c.o` 275,576 B refers
to both. The ci-39158169212 808-byte empty objects are a separate stale
export control, not revision2's actual result. The revision3 handoff,
README, and result log must not be used for their contrary claim.

The clean NACK path is source-consistent with
`i2c-mtk.c:1326-1330`, and the V2 HW timeout with `:963-974`, but neither
proves physical STOP or independent shared-bus ownership. Polled status
with `INTR_MASK=0` is also not hardware-verified. Mode2 remains private and
unauthorized; do not run it until the separate bus review and exact user
authorization. This revision does not initialize AFE, I2S, DMA, TFA
profiles, audio HAL, or playable sound.
