# Integration

Promote hardware sources audio_hw.c/h, audio_amp.c/h, audio_afe.c/h,
audio_i2c8.c/h, audio_id.c/h, and audio_hw_native.inc. Add profile-sol's generic
audio_board.c/h and audio_board_offsets.h. Do not add test binaries, raw
container files, sourced_profile.h or old audio_profile.c to the repository.
The predecessor header comments retain historical milestone contracts; active
public contract is audio_hw.h and this document.

Include audio_hw_native.inc at the END of generated sweet_touch.c, after NI and
NW declarations, with NAH_NATIVE_DEFINE_PREPARE defined1. Its exported
nah_native_prepare(struct nah_io*,void**) must run on the dedicated audio
worker after ni_gpio, ni_lt and nw.infra exist. It reuses those mappings and
retains six additional16KiB mappings; AFE and SRAM share exactly one mapping.
No new mappings or physical I/O occur from devfs or USB callbacks.

On that worker, verify the private blob's SHA over the exact buffer then call
nah_board_load(blob,length,digest). Its caller-hash contract must be honored;
merely passing the expected digest without hashing untrusted bytes is invalid.
Call nah_native_prepare, then nau_hw_init with nah_board_profile(). Keep both
native and nau_hw objects alive for the boot. nau_hw is large; static/heap only.

Start: nau_hw_start(h,48000,2,16,0,gain_q15,duration_us,now_us). gain0..256 is a
software amplitude ceiling (-42dBFS for full-scale input); duration0 selects
continuous HAL playback; nonzero durations must be <=2000000us. Silent_only1
runs digital streaming while leaving amps powered down. Parent may impose a
more conservative initial gain/duration. Do not infer a validated loudness or
speaker-protection envelope from this numerical ceiling.

Feed: enqueue producer writes in parent first; only worker calls
nau_hw_feed(interleavedPCM,frames). It returns frames accepted. Tick every2–5ms
using monotonic microseconds. First tick synchronizes the post-startup pointer
while SRAM still contains only zeros. Subsequent ticks overwrite consumed DMA
frames behind the read cursor; starvation writes zeros. A >=20ms interval,
>480-frame jump, out-of-ring pointer or stalled pointer stops the stream.
The750ms idle watchdog is active only for continuous mode. Stop is idempotent.

Status: nau_hw_status copies error/gate, AFE phase/op, I2C stage/failure,
last physical address, queue/refill/underrun counters and reset/stop errors.
An uncertain failure prevents restarting, but never panics the kernel or stops
WiFi/iOS. Do not erase an uncertain state to retry or reset the shared bus.

A normal clean stop mutes and powers down both amps, closes their volatile key
latches, disables DL1/I2S3, restores APLL programming and original GPIO fields,
powers down owned AUDIO, and restores the exclusive I2C8 gate. If it enabled
the shared global I2C mux, it retains that clock and reports
retained_shared_clock1, because another I2C controller may now use it.

Remaining hardware checks: actual amp revisions; exact live board compatibility
of recovered container; actual POR fields preserved by stock container RMW;
clock/pointer operation; channel placement/serial alignment; audible limited
output; and absence of power/thermal/fault events. No MTK speaker-protection DSP
exists in this port. Do not remove amplitude bounds for routine playback until
that limitation has been independently addressed.

Uncertain amp transport fail-safe: audio_hw.c asserts only the already-owned
GPIO111/148 reset lines high before disabling digital DMA. It verifies both
output directions/latches and records status.amp_reset_held1. Those GPIOs stay
asserted in the latched error state so an AMPE write whose result is unknown
cannot leave either speaker active. This never resets the shared I2C controller
and never accesses the DDR/UFS bucks. Normal clean stop restores GPIO snapshots.
The parent live status can expose amp_reset_held together with uncertain/error.
