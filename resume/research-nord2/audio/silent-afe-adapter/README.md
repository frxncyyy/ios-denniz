# Private silent-AFE adapter candidate

This is a source-only next step after the independently reviewed revision 5 AFE
plan and current audio-ID integration. It has **no runtime call site**, new
boot flag, image, CI job, or device test. `audio_afe.c/h` and the ID/I2C8
components are referenced from their frozen candidate directories unchanged.

## Concrete implementation

`audio_afe_adapter.c/h` retains the source plan and the complete same-boot
snapshot/quiet-amplifier evidence. Preparation is pure. A boot-wide atomic
one-shot latch and an exact worker token serialize `naf_cycle`, whose own
static plan store otherwise would be unsafe across callers. Every callback
checks the worker and the reviewed operation's phase, physical address, write
value and RMW result. Initial/final live reads must follow the reviewed 22-word
watch order. The first AFE/SRAM access requires observed AUDIO status in both
SPM registers, released SRAM ack, and released protection. The 4 KiB SRAM
zero fill must be sequential, word-aligned, and exactly zero. Errors after a
possible write remain retained/uncertain; there is no retry or invented
rollback. A DL1 pointer observation failure may return a *cleanly torn down*
non-success (`-3`); it is never reported as working audio.

`audio_afe_native.inc` is an **uncalled** private bridge for a copied current
`sweet_touch.c`, included after the existing `audio_id_bind.inc`. It reuses
the ID binder's SPM, infracfg, top clock, apmixed, GPIO, LT and AFE pages and
defines a separate `ml_io_map(0x11212000, 0x1000)` mapping for the first
selected on-chip SRAM block. It explicitly rejects I2C8, APDMA, IMPS, BM and
RM pages. Through the adapter, no callback reaches a page outside the reviewed plan. The
bridge only exposes `naa_native_prepare` for a later integrator; that function
is uncalled and cannot itself run the stream. Its token is the existing
`ni_thread` worker after a clean level-2 ID/amp probe. A future cycle would
have to run contiguously on that **same** worker, before thread termination,
and only after separately proving hardware exclusivity. A newly spawned
worker cannot reuse this token or assert quiet amplifier state by copying the
old snapshot.

The SRAM physical base/0x18000 region/0x1000 block size are from selected
`arch/arm64/boot/dts/mediatek/mt6893.dts:8059-8065` at Android commit
`04d00c2565764c3922c9b4826b5acff1106e533f`. The selected power,
clock, I2S, DL1 and teardown order is pinned in the frozen
`revision5-review/candidate/shim/nord2-xnu/audio_afe.h`, notably
`clk-mt6885-pg.c:3314-3355`, `mt6885-afe-clk.c:212-298,421-451`,
`mt6885-dai-i2s.c:1754-1808`, `mtk-afe-fe-dai.c:776-973`, and
`clk-mt6885-pg.c:3269-3313`. The adapter adds no source-unspecified
register values or I2C8/PMIF/codec/amplifier writes.

## Verification and remaining gates

`python3 research/nord2/audio/silent-afe-adapter/verify-private.py` passes
eleven separate ASan/UBSan host cases: no arm, clean cycle, first/final write
faults, wrong planned value/phase/SRAM fill, worker change before and after a
write, live read fault and
non-advancing DL1 pointer with cold teardown. It also compiles the adapter
and a copied whole `sweet_touch.c` plus uncalled bridge against the normal
CI-derived SWEET ARM64 headers. `llvm-nm` confirms a nonempty native bridge
and `naa_prepare` dependency, with no `naa_run` call from that copied TU.
The host register model manufactures status acknowledgements and DL1 pointer
movement; it is not evidence that the hardware will do so.

Before any runtime call, the sole integrator still needs actual same-boot
level-2 amp evidence and all `naf_gate` cold/APLL2 values, an exclusive audio
master and first-SRAM-block ownership proof (including no other VA alias),
review of the private bridge and failure policy, a distinct exact image
authorization, and a test on hardware. The worker latch excludes only this
component's concurrent/repeated callers. Mapping SRAM is **not** ownership
proof. No sound, silence, speaker output, or safe hardware cycle is claimed.
