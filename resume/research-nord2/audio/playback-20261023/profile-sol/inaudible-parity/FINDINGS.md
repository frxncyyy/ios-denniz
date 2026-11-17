# Profile and gain audit after parity image

No canonical/device operations. Evidence: ios-parity/hardware-review; observed revisions 0x0c74 at both 0x34/0x37. Parent/hardware agent independently established the actual AFE sink routing defect; this audit does not replace that finding.

## Proven profile identity and routing

Pinned 4163-byte container SHA256 635300fbc96f258ff9a35f82435f2e9d3ff6f1370c156067b40146e011038692 is the normal Nord2 A16 container, application 9874. It matches actual address pair; t0 is inappropriate (0x35 second address). It is sourced stock firmware, not a dump from this exact phone. Canonical audio_board.c:supports explicitly accepts 0x0c74, matching selected Android tfa_init.c:945–964. The embedded six-byte patch identifies family 0x74, without firmware patch code.

The reproducible check-profile.py verifies both complete device+speaker operation lists (52 bitfields each): P/0x34 audio input slot1; Q/0x37 slot0; slot/sample widths32; AMPGAIN raw190; TDMSPKG P4/Q5; INPLEV0; speaker DCA1 and LNMODE0. Thus dual-mono test tone cannot be lost through stereo channel reversal. Canonical audio_board.c applies the same order and reads back each changed mask; its success is evidence for those masked values, not untouched reset bits.

Field names/layout: selected Android sound/soc/codecs/audio/tfa98xx/tfa9874_tfafieldnames.h, enums near lines74–149 and descriptive entries near514–521. reg58 bit1 (field0x5810) is hard_mute; profile reg58 mask0x01f0 is pwm_delay, not mute. Do not infer a mute setting from that operation.

## Quantified attenuation

Canonical audio_hw.h NAH_MAX_GAIN_Q15=256 and audio_hw.c:368–369 apply 256/32768 after producer volume scaling. audio_device.c:118 always requests this ceiling. audio_stream.c:nsa_gain(0)=32768: reported volume0 means zero eighth-dB attenuation, not silence. Effective full-scale DMA ceiling is 1/128, -42.144199dB. The half-scale tone in audio_device.c:162–163 consequently peaks at128 (-48.164799dBFS); full-scale negative HAL samples reach-256, positive32767 reaches255. HAL peak counters are measured before this hardware attenuation, so peak32768 does not show large DMA amplitude.

This makes audibility an unreliable binary routing test at present. It does not prove gain is the silence cause. No gain increase is proposed without the parent's explicit hardware experiment choice; changing the ceiling is separate from fixing source-proven sink routing.

The selected Android tfa_dsp.c:1020–1055 exposes TDMSPKG as a raw analog-volume code0..15. tfa98xx.c:431–492 adds raw ALSA analog controls, with sentinel g_tfa98xx_ana_vol16, but no automatic application of that sentinel. No numeric dB law is provided for TDMSPKG4/5 or AMPGAIN190. The neighboring tfa98xx_set_volume_level -0.5dB/code comments describe VOL, a different field: applying that law to AMPGAIN would falsely infer-95dB. Goodix TFA9874 SDS Rev2.1 provides only overall TDM-to-output gain range6..21dB at INPLEV0 (private datasheet text), insufficient to assign these raw codes. Total board+software gain therefore cannot honestly be quantified beyond the exact -42.144dB software contribution.

## Untouched enables and next evidence

Container does not write TDME(reg20bit4), TDMSPKE(reg23bit0), TDMDEL/TDMADJ(reg21bits14/15), or hard_mute(reg58bit1). The exact Android0c74 init table also leaves these fields at reset values. Thus this is parity with Android device/profile application; adding guessed enables would deviate. Their actual post-reset values are not present in current status evidence. If routing correction still yields silence, capture bounded public readback of00/01/02,20–27,52,58,61,64,68 while active, with existing lease and no writes; compare against expected masks, specifically TDME/TDMSPKE/hard_mute and MANOPER. Installer agent owns final state/mute analysis. No conclusion that any of these is wrong is supported now.

No additional proven profile blocker was found. Preserve the pinned speaker profile and0c74 silicon table; avoid switching to MMI/calibration or t0 to obtain gain. The check is offline field verification, not acoustic/hardware validation.
