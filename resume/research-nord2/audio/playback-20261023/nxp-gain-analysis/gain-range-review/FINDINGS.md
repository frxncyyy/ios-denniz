# Review: additive 16-step digital gain range through unity

Technical result: the existing wire encoding and arithmetic can support the proposed16 steps through Q15unity32768 without changing USB lengths, opcodes or reply indices. No software arithmetic blocker was found. This does not establish a safe loudspeaker level, calibrated Android loudness, or equivalent SmartPA protection. No source, device, build or CI change was made.

## Two compatibility traps

1. Current `audio_live.c` declares the seven-entry table as `{0,128,256,362,512,724,NAL_GAIN_MAX}`. **Replace the existing final entry with literal1024 before raising NAL_GAIN_MAX.** Otherwise old index6 silently becomes32768, a32× increase for an old client. Preserve indices0–6 exactly, then append2048,2896,4096,5793,8192,11585,16384,23170,32768 at7–15. Keep invalid index16 and UINT32_MAX rejected. With16 steps all256 low-byte nibble pairs are valid; bits15:8 must still be zero.
2. Do not derive the tested default bottom gain from the maximum macro. Use explicit boot defaults top256/bottom1024. Keep legacy fixed tone at256/256. Increasing a cap must not silently increase a boot default or old diagnostic tone. The currently reviewed source still initializes256/256, so the requested tested256/1024 default is a separate explicit change.

## Capability negotiation without an attempted mutation

Capabilities words14 and15 are currently unused; words0–13 are occupied, including current command argument at13. Suggested contract: word14=`gain_max_q15`, word15=`gain_step_count`. Preserve version1, mask0x1fe,64-byte caps and192-byte replies. Existing seven-step clients ignore the new tail words and retain correct index semantics.

A new client should interpret:

- mask0x3e: no GAIN operation; refuse all tuning before write.
- mask0x1fe with both tail words zero: existing seven-step kernel; infer max1024/count7. Refuse index7–15 before any USB command, pending slot or token mutation.
- explicit nonzero pair: validate count1–16, known table prefix and maximum. For the intended kernels accept max1024/count7 or max32768/count16. If allowing a future restricted prefix, require its maximum to match that prefix's last value. Reject inconsistent/unknown capabilities, including one zero word, rather than assuming unity.

After encoding the two requested values, check BOTH nibble indices against advertised count and both gains against advertised maximum BEFORE `t.write`. Keep the existing per-op support check, boot nonce, pending check, echoed argument and one-write-on-lost-ACK behavior. Snapshot gain_max at replyword44 should agree with capabilities; it is too late to use a post-mutation reply as the only authorization check.

The host value list must be append-only too. Do not compact or reorder steps on old kernels: derive the common prefix and reject unsupported requests. New clients can query old kernels and use old gains; old seven-step clients continue issuing the same values against a new kernel. Very old mask0x3e-only clients already reject mask0x1fe, unchanged from the present balance build.

## Arithmetic and actual signed16 limits

Current SRAM scaling is `(int64_t)sample * gain * ramp_frames / (32768u *480u)`. With sample−32768, gain32768 and ramp480, the numerator is−515396075520, far within signed64. The denominator15728640 fits unsigned32 and is converted to signed64 for this division. Final output is bounded−32768..32767. At completed ramp, unity maps every input sample exactly to itself, including−32768 and+32767; zero gain remains exact zero. No additional clipping is necessary while the hard maximum stays<=32768.

The current signed32 pending-sound check `sample*gain/32768` also fits: extrema are−1073741824 and+1073709056. Negating the final signed32 output to measure peak is safe; magnitude32768 fits uint32. Packing through uint16 preserves signed16 two's-complement bits, with no wrap beyond the legitimate signed16 range. HAL volume scaling is another Q15 stage capped at32768 and also fits signed32.

The480-frame ramp remains10ms of nonzero active PCM. It smooths initial gain but does not impose an energy, duration or thermal limit. Isolated sine tones remain half scale BEFORE gain, so a unity isolated tone peaks16384 (−6.02dBFS); full-scale HAL content can peak32768. The previous−30.1dB digital ceiling is removed at unity. This is a numerical statement, not a safety or calibration conclusion.

## State, lifetime and idle behavior

Keep current gain mutation restrictions: initialized, clean error/uncertainty/stop status, stopped hardware, disabled HAL, empty producer and hardware queues, no tone. Gain is installed before hardware starts. STOP and close/reopen retain the configured pair; reboot returns to the explicit tested defaults. No persistent setting is implied.

`mask_disabled_channels` still needs to zero only channels whose configured gain is exactly0, under the same producer lock used by mutation. Larger nonzero values do not change frame ownership or queue bounds. The hardware pending scan uses each effective gain; its signed32 product remains safe at unity. Legacy tones override both effective gains to256; isolated tones use the configured selected side and zero the opposite side. New high settings must not leak into that fixed legacy tone.

Continuous HAL playback has no fixed total-duration limit while fresh PCM arrives. A higher cap alone does not change that lifetime policy. Existing fault containment must remain sticky and must not be cleared by GAIN, ENABLE or a diagnostic. No extra amplifier, MTP or analog register operation is required to implement this software range.

## Focused acceptance tests before implementation is considered complete

- Assert all16 index-to-value mappings, especially old index6==1024 and new index15==32768; index16 invalid. All65536 encoded arguments admit exactly256 low-byte pairs and reject high bits before token mutation.
- Cross product of new host with old no-gain kernel, old seven-step kernel with zero tail caps, explicit seven-step caps and new16-step caps. Higher gains must cause ZERO command writes on old kernels; old49 pairs retain their wire encoding. Inconsistent caps must refuse rather than fall back.
- Raw USB argument echo, same-token changed argument rejection, ACK loss with one command write only, and unchanged legacy reply field positions.
- Actual SRAM arithmetic under ASan/UBSan for every step, both signs,−32768,+32767, opposite-side zero and ramp boundaries0/1/479/480. Completed unity must be bit-exact for all65536 signed16 inputs. Assert maximum measured magnitude32768 and no channel swap.
- Actual pending-sound checks at unity and extreme negative sample; both-zero configuration stays idle, selected-side tails still drain. Mutation while running/enabled/nonempty/faulted remains refused without MMIO or changing settings.
- Boot defaults explicitly256/1024, legacy tone explicitly256/256, isolated tones at the selected configured gain, configuration preserved across STOP/reopen and reset only on reboot. Protocol cap and hardware hard maximum must be statically consistent.
