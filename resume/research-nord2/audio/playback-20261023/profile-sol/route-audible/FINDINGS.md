# Audible route: bottom/top audit and bounded correction recommendation

Actual image3b8cb873/source6ee28295. Both speakers audible, user reports bottom extremely low; volume/pause responsive. No device, canonical, build or CI changes in this audit.

## Physical mapping is established

Selected Android source04d00c2565764c3922c9b4826b5acff1106e533f, sound/soc/codecs/audio/tfa98xx/tfa98xx.c:5180–5188 explicitly documents tfa_channel0=left/top and1=right/bottom. LiveDT dtb/nord2/live.dts:9531–9551 sets34/channel1 and37/channel0. Thus bottom=0x34/P/inputslot1, top=0x37/Q/inputslot0. This is not inferred from left/right naming alone.

Pinned normal Nord2 container applies52 bitfields each after0c74 silicon init. profile-differences.json records all seven device differences: four TDM slot selections, TDMSPKG bottom4/top5, DCMCC boost current bottom10/top11, DCVOS second boost voltage bottom63/top55. AMPGAIN raw190 and other speaker settings are equal. Public field names: tfa9874_tfafieldnames.h enums138,142,157,171 and descriptive table520–521,560,596. TDMSPKG is explicitly the analog-volume field in tfa_dsp.c:1019–1055, capped15. Its dB/code law is NOT established; AMPGAIN190 must not be interpreted using the separate VOL0.5dB/code routine. Do not equalize analog/boost settings: these are intentional stock per-device differences, not a demonstrated bug.

## What actual captured state proves

ios-route/hardware-review/dma-tone.log:4755–4756 records both amps SYS0018, STATUS100016,11e2c0,13850f,1400c8,580x1614,200x2890,230x000b. amp-start-decoded.json shows both PWDN0,AMPE1,PLLS/CLKS/SWS/AMPS/AREFS1,MANMUTE0,MANOPER1,MANSTATE9,hard_mute0,TDME1,TDMSPKE1. reg26 bottom1001/top0010 matches intended input slots1/0. No captured asymmetric mute/clock/enable defect.

status-hal-2.json: volume0 means zero attenuation, muted0, peak32768, dropped0, running1,error0, both revisions0c74; this counter peak precedes hardware scaling. Parent later reports postpause clean,3,249,152 written0dropped14starts. No producer loss evidence.

## Software amplitude and implications

Canonical audio_device.c:118 passes shared NAH_MAX_GAIN_Q15, audio_hw.h:11 fixes256; audio_hw.c:368–369 applies it equally. Effective amplitude=HALsample * nsa_gain(volume)/32768 *256/32768. At volume0 hardware ceiling is1/128=-42.144199dB; halfscale diagnostic tone peaks128=-48.164799dBFS. The tone table is duplicated onto both channels, so its digital amplitudes are identical. This attenuation explains globally very low output, but cannot mathematically create a bottom/top digital ratio. It may expose different acoustic/analog sensitivity at very low levels; that is an unmeasured hypothesis, not proof that the bottom imbalance is expected.

## Android media path is broader than the board container

Offline bounded extraction from existing research/nord2/wifi/vendor-read/vendor.img, saved vendor-evidence/: /etc/audio_param/SmartPa_AudioParam.xml:12 maps smartpa_nxp_tfa9874 to param6; lines83–93 set have_dsp2,libnxp_extamp_intf.so,22msdelay,ALSAcodec1. /etc/smartpa_param directory has tfa98xx_device.bin,tfa98xx_music.bin,tfa98xx_ringtone.bin,tfa98xx_voice.bin. These observations establish additional Android integration/config resources; they DO NOT establish what have_dsp2 means, that any resource is active in the observed Android media mode, or a missing per-speaker DSP gain. The local vendor image may be a later/custom Android build, whereas boardcontainer is sourced A16; preserve that provenance distinction.

Extracted aurisys_config.xml:63–68 and395–413 lists playback-smartpa through aurisys_demo, interleaved2-channel16/32bit PCM. PlaybackVolDigi_AudioParam maps Music/SPK to common param0; PlaybackVolAna_AudioParam speaker_pga10 has no demonstrated mapping to TFA TDMSPKG. None supplies a verified bottom/top coefficient. The selected TFA Makefile:21 defines TFA_NON_DSP_SOLUTION; neither that macro nor have_dsp2 alone proves absence/presence of host media processing. Source-backed certainty stops here.

## Concrete recommendation

Do not ship a guessed register correction, double bottom amplitude, remove the42dB ceiling, change boost limits, or import MMI/calibration profiles. No exact Android parity gain coefficient is established by current evidence.

Minimal bounded implementation for the parent's next coherent diagnostic candidate, if needed: add independent left/right peak counters AFTER actual DMA scaling, and a selector for the existing1s tone (both/top-only/bottom-only), without raising peak amplitude256 ceiling or changing pinned amp settings. Saved source mapping makes isolation unambiguous: top=left/slot0; bottom=right/slot1. Assert actual DMA samples in host tests, not just generated tones: both mode identical channels; each isolated mode zero on the other channel; negative/positive fullscale remain[-256,255]; pause/reset/STOP clears both. This separates content/channel imbalance from physical response with one candidate and avoids blind gain reflashes.

For an actual balance correction after matched evidence: use a software attenuation-only per-channel trim, Q15 coefficient0..32768, applied before the existing shared ceiling; default unity both. If a measured ratio establishes bottom is quieter, attenuate top to match rather than invent bottom amplification. This preserves all source board/protection settings and never increases per-speaker amplitude. A numeric coefficient requires matched acoustic/reference measurement; no current artifact supports one. It corrects balance while reducing total loudness, so it is not a substitute for later properly protected Android-volume parity work.

No demonstrated complete bottom-loudness correction can honestly be claimed yet. The actionable result is exact physical mapping, complete board asymmetry, healthy captured enable state, and a bounded discriminator/correction path; the user's desired louder bottom remains unresolved pending that evidence.

## Review of parent's bounded digital-control proposal

Parent proposes one image with idle-only per-speaker steps0,128,256,362,512,724,1024; default256; matched isolated tones; bounded ramp. This is a reasonable diagnostic mechanism with explicit changed experimental amplitude, not a source-proven final gain correction.1024Q15 is absolute1/32=-30.1029996dBFS, +12.0411998dB relative256. At unchanged linear transfer that can imply16x signal power; low digital dBFS does not establish speaker excursion/thermal safety or replace Android processing. No claim of protected/full/Android-equivalent volume is justified.

Review requirements:
- Gains are absolute per-channel Q15, not an extra multiplier atop256. Top is left/slot0/37; bottom is right/slot1/34. Keep HAL volume/mute multiplying both, existing board configuration untouched.
- Idle-only update requires no producer/hardware/DMA pending audio and stopped amps; running=false alone need not imply all queues empty. Reject busy and sticky-error updates; validate both gains before mutating either. Query reports configured/effective gains clearly.
- Ramp progresses inside ordinary DMA-fill work, without sleeps or I2C transactions, and cannot exceed the chosen gain/ceiling or reverse sign. STOP/error clears ramp/output immediately. Gain0 and ramped zeros must not keep amps alive via pre-gain sound bookkeeping.
- Host regression checks actual serialized DMA PCM at every allowed gain, positive/negative extrema, top-only/bottom-only/both mode, below-volume and mute, configuration rejection atomically when busy/out-of-range, STOP/error/reset and response idempotence. int32 sample*1024 remains within33554432, so current arithmetic width suffices.

Keep default256 bit-identical. Use matched speaker-isolation tones before increasing a quieter channel; all comparisons should use the same waveform/frequency/duration and report exact configured gains. Numeric tuning is empirical, not an Android analog-gain derivation. This plan minimizes reflashes by keeping bounded controls in one candidate; it does not prove a higher ceiling safe merely by enabling it.
