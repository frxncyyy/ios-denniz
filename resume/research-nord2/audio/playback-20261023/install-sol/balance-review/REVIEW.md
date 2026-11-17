# Minimal live speaker balance extension review

Current source reviewed: audio_live.h/c, audio_device.c, audio_hw.c, tools/nord2-audio-live.py and existing protocol tests. No canonical or device changes.

## Compatibility trap to fix in the design

The existing CLI compares caps words0–6 EXACTLY against `(MAGIC,1,64,192,0x3e,4095,0)`. Expanding NAL_COMMANDS or using capsword6 for extension flags will break that CLI even for STATUS/STOP. Keep version1,64-byte caps,192-byte reply, legacy mask0x3e and word6=0. Advertise extra op bits in previously unused capsword14; use word15 for maximum gain1024. Old clients ignore both. New clients require the relevant extension bit before issuing a new command. Do not assume that appending commands to the enum alone is backwards compatible.

The existing reply has42 named words and48 allocated words. Its six trailing zero words can report configured channel0/channel1 gains, effective channel0/channel1 gains, maximum gain and current tone selector. Keep every existing field/index unchanged; move NAL_F_END only by appending new fields. Old CLI's zip ignores the trailing words. New CLI should expose both configured and effective gain so the fixed legacy tone cannot look like a successful high-gain test.

## Minimal command design

Retain existing operations1–5 and their required index0. Add SET_BALANCE=6 and SPEAKER_TONE=7. Both still fit the four-bit opcode in wValue. Use wIndex only as a bounded argument for these new operations; the current `generation` request field already stores and echoes it, so no USB data stage or transport structure expansion is required.

SET_BALANCE packs two three-bit STEP INDICES: channel0 inbits2:0, channel1 inbits5:3. Reject either index7 and any bits15:6 before publishing pending/token. Map indices0–6 to `{0,128,256,362,512,724,1024}`. Never accept arbitrary Q15 values or byte truncation. SPEAKER_TONE accepts exactly selector1(channel0) or2(channel1); zero,3 and every other bit pattern are refused. Keep legacy TONE as the existing quiet, both-channel, fixed one-second tone at gain256 regardless of configured balance. SILENT_TEST remains unchanged.

`nal_submit` currently rejects all nonzero indices and enforces same-token replay identity using op AND index. Preserve that identity check for argument-bearing operations. A retry with the same token but a different balance/selector must fail; never execute twice after a lost ACK. Retain monotonic tokens, pending states and boot nonce checks.

## Worker and channel semantics

Apply gain changes only in the audio worker. Require initialized, no error, no uncertainty, no stop_error, running0, tone_active0 and stream.enabled0. Read/check enabled under nsa_lock. Rejection must leave both configured gains and all queue/counters unchanged. A disabled stream rejects concurrent HAL enqueue already, so STOP → SET_BALANCE → SPEAKER_TONE is a deterministic transaction. New isolated tones must use the same stopped/disabled/clean checks, flush the producer queue under its lock, and preserve the existing one-second deadline rebasing and fault cleanup.

Use PCM channel numbers initially, with an explicit route label: channel0 → I2S3 slot0 → amplifier0x37; channel1 → slot1 → amplifier0x34. The pinned profile establishes this mapping. Do not call these bottom/top or left/right until the acoustic isolation test establishes the mechanical identity. A zero gain on the non-selected channel must produce exact signed16 zero, rather than relying on nominal quiet input or changing any amplifier register.

Keep configured gains separate from effective playback gains. Configuration defaults remain256/256 after boot. HAL playback starts with the configured pair. Legacy TONE overrides effective gains to256/256; isolated tone uses selected gain on its selected channel and exact zero on the other. Stopping a diagnostic must not overwrite the configured balance. No new controls should change amplifier gain, TDM slots, reset, clock or MTP settings.

A clean low-level interface is a new channel-gain start helper with an explicitly validated pair, while retaining the current scalar nau_hw_start wrapper and its old behavior for existing callers/tests. The old wrapper continues to start equal gains and retain its original256 ceiling. New starts accept only the enumerated steps up to1024. Both gains must be installed before enabling playback; never call a setter after hardware startup while running. Scale each signed16 channel with its own Q15 value using int32 multiplication/division, then pack into SRAM as before. Worst product at1024 is33554432, within int32. Negative division must continue truncating toward zero.

Update hardware_sound_pending to use the same per-channel effective scale as SRAM packing. Otherwise a muted channel can keep hardware running, or a newly audible channel can be ignored by the idle-drain logic. With both configured gains zero, HAL auto-start can simply remain idle; do not enable two amplifiers for intentionally zero output. Existing volume/mute, tail retention, bounded queues and report-only-after-stop behavior stay intact.

## CLI requirements

Preserve old command spellings and status parsing. Add an explicit balance command accepting only the seven step values for each channel, plus a speaker-tone command requiring channel0 or1. Convert to the compact wire encoding after validation. Echo and verify the actual wIndex in replyword7, not the current hard-coded `(op,0)` comparison. Capability checks must reject new mutations on an old kernel before sending USB. JSON should include configured and effective gains and the amplifier mapping; no statement should claim audibility based on an ACK.

On a lost ACK or timeout, preserve the existing rule: inspect capability/current reply using the same token, never send a new mutation token automatically. Recovery from a health fault still requires a fresh healthy boot; tuning commands must not clear error/uncertainty.

## Required focused regressions

1. Old client accepts new caps because words0–6 and lengths remain exact; old STATUS/ENABLE/STOP/TONE/SILENT round trips still require index0. New client can query an old kernel but refuses unsupported tuning commands before USB mutation.
2. All49 valid gain pairs round-trip, with same-token replay idempotent. Enumerate all65536 wIndex patterns for SET_BALANCE and assert only49 accepted encodings; malformed submissions consume no token or pending slot. Test each invalid tone selector and same-token changed argument.
3. Running, enabled, uninitialized, error, uncertainty and stop_error each refuse mutation with unchanged settings, queues and MMIO counts. Successful stopped/disabled tuning performs no hardware callbacks.
4. Actual worker/start logic uses configured gains for HAL, fixed256 for legacy tone, and selected-only effective gains for isolated one-second tones; stop preserves configuration. Duration starts after startup latency and fatal errors retain existing containment.
5. Actual SRAM packing with positive/negative full-scale, each step, channel0-only, channel1-only and both zero. At1024, outputs stay within magnitude1024 and the non-selected channel is bit-exact zero. Default256 output is unchanged. Unequal gains are not swapped relative to amplifier37/34.
6. Tail/idle tests use effective gains: zero channels do not sustain pending sound, audible selected-channel tails drain, and configured gains survive stop/restart. Snapshot tests verify configured/effective fields and unchanged legacy indices.

No source-backed reason was found to require an amplifier-register change for this bounded balance experiment. The current measured difference in loudness can be addressed first with digital gain and isolated acoustic tests, while preserving the source-derived hardware initialization.
