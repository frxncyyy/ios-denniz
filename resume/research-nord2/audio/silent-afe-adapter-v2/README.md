# Private silent-AFE adapter v2

This is a separate source-only repair of the frozen
`silent-afe-adapter` candidate (`c47a94440bf0cb021968fe1e96792a564fafd8da3668f3da010e401412a0dba9`).
The reviewed revision 5 `audio_afe.c/h` remains byte-for-byte unchanged.
There is no AFE cycle call site, boot flag, image, CI run or device access.

The first v1 native gap was stale amplifier evidence: it accepted any
`current_thread()` and made that thread the AFE worker, even when another
thread had performed the ID/I2C8 probe. `audio_id_bind.v2.inc` now records
the exact worker only after both supported amplifier probes complete. The
uncalled `naa_native_prepare` requires that very worker, a clean level-2
probe, both measured amp receipts and a zero AFE cold gate; it consumes the
receipt exactly once. The future integration point must be immediately after
`nab_audio_id()` on the same `ni_thread`, before touch setup. Worker identity
and one-shot consumption exclude a stale or different software caller; they
do not lock out another hardware audio master or prove the amplifiers stayed
quiet during a preemption.

The second v1 gap was a new VA alias for the on-chip SRAM. The current ID
binder maps `0x11210000` with `ml_io_map(..., 0x1000)`. On this SWEET
16 KiB-page build, XNU `osfmk/arm/io_map.c:88-126` rounds that request to
one 16 KiB physical page, so it already covers the source SRAM first block
`0x11212000..0x11212fff` (`mt6893.dts:8059-8065`). The v2 bridge rejects
other page sizes, maps **nothing** additional, and addresses the SRAM through
`nab.afe + 0x2000` with an exact 4 KiB software envelope. This removes the
candidate-created second VA mapping. It does not prove that no platform or
later driver has another alias or ownership of this physical SRAM block.

`audio_afe_adapter.c/h` and the 11 core host cases are exact copies of v1.
The concrete changes are in `audio-id-lifetime.patch` and
`native-va-lifetime.patch`. `verify-private.py` passes those 11 ASan/UBSan
cases, a native host fixture checking wrong-worker refusal, same-worker
one-use consumption, zero AFE/MMIO activity in preparation and SRAM VA
identity, and a copied normal-context ARM64 whole-`sweet_touch.c` compile.
The host fixture synthesizes quiet amp values solely to test the software
handoff: its inherited I2C8 model's second amp is actually active. It is not
evidence of a quiet physical amplifier or of a working audio stream.

Future execution still requires independently measured same-boot full cold
SPM/ADSP/APLL2/pin and amplifier values, exclusive audio master and SRAM
ownership/no conflicting alias, exact source/adapter review, failure-policy
integration and separate human authorization of an exact image. No runtime
audio or safe hardware cycle is claimed.
