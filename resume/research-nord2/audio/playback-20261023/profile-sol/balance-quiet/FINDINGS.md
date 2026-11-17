# Bottom still quiet after balance controls

Scope: source-only follow-up to route-audible/FINDINGS.md. No device actions, canonical mutations, binary reverse engineering, or new gain settings. Actual balance image d0b18b640d7bf64d1cbeb89cc19de89560549d2009e4a15708249c5282475224/source65089823. Parent reports matched isolated tones have exact peak128 on intended physical channels; media at top256/bottom362 has peaks255/362, no faults/drop; bottom remains extremely quiet. Bottom1024 listening result pending at audit time.

## Conclusion

The arbitrary software ceiling is a PROVEN large attenuation. A bottom-specific configuration defect is NOT proven. These are not exclusive: correcting amplitude numerically does not establish correct Android acoustic response. No source-supported analog gain patch or higher safe software ceiling follows from the current evidence.

At below-volume0 the current top256 is1/128=-42.144dB relative input; bottom362 is-39.137dB (+3.007dB), bottom1024 is1/32=-30.103dB (+12.041dB). The existing max1024 remains32x below fullscale amplitude. Actual peaks match arithmetic, so there is no newly discovered software attenuation between queue and DMA, or physical channel swap. The fade only affects first480 effectivePCMframes=10ms; persistent quiet cannot be attributed to a continuing ramp when gain_ramp_frames reaches480.

## What Android source establishes

Physical bottom34/P/channel1/right/slot1; top37/Q/channel0/left/slot0: selected Android tfa98xx.c:5180–5188 explicitly says channel0 left/top,1 right/bottom; live.dts:9531–9551 supplies channels. Exact0c74 silicon table tfa_init.c:945–964 is used. Pinned speaker profile differences remain stock: TDMSPKG4/5, AMPGAIN190/190, DCMCC10/11, DCVOS63/55, plus four expected slot fields. Actual prior active startup readbacks confirm PWDN0,AMPE1,MANMUTE0,hard_mute0,TDME1,TDMSPKE1 both. Those observations do not cover every possible automatic limiter or calibrated analog transfer.

Selected tfa_dsp.c:1019–1055 maps analog-volume controls directly to raw TDMSPKG0..15. tfa9874_tfafieldnames.h identifies AMPGAIN0x5257 and TDMSPKG0x6163, not their dB laws. The separate VOL routine's -0.5dB/code law cannot be applied to either. Goodix official TFA9874B SDS Rev2.1 (private tfa9874-datasheet.txt:796, datasheet p17) specifies overall TDM-to-VO gain6..21dB at INPLEV0; it does not map raw4/5/190 to a gain. Therefore we cannot even prove from public register evidence that4 is quieter than5. The one-code asymmetry is insufficient justification to equalize them. Avoid changing boost/current limits, speaker profile, OTP/MTP, or protective controls.

## Missing Android processing remains a concrete evidence gap

Existing locally extracted vendor XML (route-audible/vendor-evidence/SmartPa_AudioParam.xml:12,83–93) selects TFA9874 param6, have_dsp2 and libnxp_extamp_intf.so; /etc/smartpa_param contains separate device/music/ringtone/voice bins. aurisys_config.xml lists stereo smartpa PCM, and generic playback volume XMLs do not identify a bottom correction coefficient. The selected kernel Makefile defines TFA_NON_DSP_SOLUTION; tfa_container.c:1534 writes only register/bitfield descriptors when ext_dsp0. Neither fact explains the meaning of the userland have_dsp2 enum or reconstructs its media processing. No verified host-DSP gain or missing mandatory stage has been derived.

To resolve this gap requires the actual libnxp_extamp_intf.so call path and music/device parameter interpretation, ideally primary vendor HAL source; otherwise bounded binary analysis of those exact extracted files is necessary. That would use the reverse-engineer-anything skill with identity/exports/call tracing and distinguish configuration from executed behavior. It has NOT been started here; no claim from filenames or have_dsp2 alone is valid. Existing vendor image is not proven to be the same A16 release as the pinned container, so provenance must remain explicit.

## Actionable next decision

Use the already-installed matched isolated tones at identical gains, then the already-selected bottom1024 setting, with recorded user outcome. This needs no new flash. A source-proven channel or arithmetic defect has been excluded by current observations and the actual DMA regression; a further ceiling increase is a new amplitude experiment, not a bug fix or Android parity restoration. If bottom1024 remains grossly weak relative to top256, stop guessing coefficients and investigate the exact Android extamp/media path above before another image. Preserve board/boost/protection settings. A measured per-speaker response is also needed to choose a balance coefficient; subjective global quiet alone cannot establish it.
