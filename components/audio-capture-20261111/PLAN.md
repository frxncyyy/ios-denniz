# Nord 2: microphone audio in Camera.app recordings (audio-capture-20261111)

State: **analysis done; kernel part implemented and host-tested (patches 0320/0321); built with the CI compile flags
(objects only, not linked into an image); nothing has run on the phone.** Date: 11 November 2026.
Tags: [V] = read in source or seen in a phone log, [I] = inferred.

## 1. Why the recordings are silent

The chain on this port (no IOAudio2, no AppleEmbeddedAudio/AppleCSxxx codec driver: the iPhone X rootfs has nothing
that finds hardware here):

```
Camera (AVCaptureSession, SRC:Microphone) -> mediaserverd CMCapture BWAudioSourceNode
  -> MediaExperience/CMSession: input VAD "VirtualAudioDevice_DecoupledInput"            [V user6 l.14262, 20660]
  -> VirtualAudio.plugin builds the built-in mic port (pmbi) from the HAL device "Codec"  [V plug-in source; I that this
                                                                                           is the device used]
  -> RedwoodAudio.driver (our AudioServerPlugIn, in mediaserverd) "Codec" input,
     codec_read(): open("/dev/redwood-mic", O_RDONLY|O_NONBLOCK) at the first read,
     read() what is there, zero-fill the rest                                              [V halplug/d/audio_plugin.c
                                                                                           l.1251-1308; the bundle on the
                                                                                           phone has this object: camera-
                                                                                           userland REPORT "byte-identical
                                                                                           to halplug/out-h"]
  -> kernel: **no /dev/redwood-mic exists**                                                [V: q10in/q10jw stage has no such
                                                                                           node; the console shows only
                                                                                           "devfs redwood-audio"]
```

So open() fails (ENOENT), the plug-in marks the mic absent until the input stops and returns zeros forever: the HAL
runs, CMCapture gets thousands of buffers with 0 dropped, the AAC track is digital silence. [V for each link except the
VAD -> "Codec" mapping; the other two inputs of the plug-in ("Speaker" input, "Hawking") are hard-wired silence too,
so the result is the same whichever one VirtualAudio picked.] The open failure itself is not in the logs: the plug-in's
`say()` goes to stderr/syslog, not to /dev/console [V].

Other facts from user6 [V]: Camera asks 44.1 kHz, CMSession picks 48000 (the plug-in's only rate) and VA resamples;
`PreferredInputs` = data source 'mc03'/'mc02' on route 94 (Camera chooses the back/front mic data source of the
built-in mic port); the track is mono (the "Codec" input has 1 channel). `NORD2 AUDIO PCM: opens=1 writes=0 ...` is the
playback queue of /dev/redwood-audio (not capture).

### Is capture implemented in our kernel? Partly — the hardware path exists and works, only for calls

`bsd/dev/audio_call.c` (patch 0280 + fixes, the CS voice call) already does everything a recording needs at the register
level [V source]:

* MT6359P codec, from sound/soc/codecs/mt6359.c DAPM order for `builtin_Mic_Mic1` (table `nac_up`/`nac_down`, run on the
  input thread, the PWRAP owner): VAUD18, DCXO audio clock, AUDGLB, codec GPIO mux (MOSI/MISO), CLKSQ, ADC clock gen,
  AUD/AUDIF clocks, DCC clock 50 kHz, **MICBIAS0 2.5 V DCC** (no "mediatek,mic-type" in the Nord 2 DT), AUDIO_TOP,
  AFE_ON, MTKAIF mux, UL_SRC = AMIC, **PGA_L <- AIN0 (main/bottom mic), ADC_L, PGA 18 dB** (kern.n2call.pga), DCC
  precharge, MTKAIF TX protocol 2 + CLK_P2 + the boot-time phase calibration (mt6885_mt6359_mtkaif_calibration, async).
* AP AFE (mt6885 ASoC: mt6885-dai-adda.c, mt6885-reg.h): AUD_* pins function 1, aud_adc gate, ADDA UL 48 kHz with the
  35 Hz IIR, MTKAIF RX, `ADDA_UL_CH1 -> UL1_CH1 (AFE_CONN21 bit 3) -> VUL12` memif, mono 16-bit 48 kHz into AFE SRAM
  0x11213800 (2 KiB; DL1 owns 0x11212000, AWB 0x11213000), VUL12_ON = AFE_DAC_CON0 bit 31 (VUL12_ON_SFT 31).
* **Seen working on the phone** as the in-call "mic probe": runs q10ct, q10cv, q10dz, q10ea, q10ev, q10f0, q10f3,
  q10f3n print `NORD2 CALL: uplink: mic N frames peak P` with ~48000 frames/s and speech-like peaks 300..27244,
  `cal ok` [V autocycle/runs/*/ui-usb/xnu-live.log]. (Sound quality / far-end intelligibility: not judged here.)

What is missing for a recording: (a) a way to run that path without a call (it is coupled to PCM2/modem/AWB and to the
call state machine, and its MT6359 tables run synchronously, which would stall the 2 ms audio worker while the speaker
path runs); (b) somewhere for the samples to go: the device node the plug-in already reads.

### How playback reaches CoreAudio (the interface capture uses too)

Not IOKit/userclient, not AppleAudioSharedMemoryBuffer: a char device. `/dev/redwood-audio` (audio_device.c, cdevsw +
devfs, 0666): the plug-in writes S16LE 48 kHz stereo, the worker (one kernel thread, sole owner of AFE/amp I2C) feeds
the DL1 SRAM ring -> I2S3 -> TFA9874 amps. Capture uses the mirror image the plug-in already expects:
`/dev/redwood-mic`, read(), S16LE 48 kHz mono, nonblocking. **No userland change is needed.**

## 2. Option A (implemented): kernel capture behind /dev/redwood-mic

### 0320-nord2-mic-capture-afe.patch (bsd/dev/audio_call.c, audio_call.h; 190 lines)
* `nac_afe_up()` becomes `nac_afe_up_mode(h, call)`; `call=1` is byte-for-byte the old call path (run.sh compares the
  full register-write trace of base vs patched: identical, 334 writes); `call=0` = `nac_mic_afe_up()`: the same save
  of every word, pins, ADDA UL, MTKAIF/calibration, VUL12 + CONN21, `AFE_DAC_CON0` bit 31 only — no PCM2, no
  CONN17/18 (mic -> modem), no CONN5/AWB. `nac_afe_down()` restores everything exactly as before.
* `nac_mic_pmic_submit(up)` / `nac_mic_pmic_poll()`: the whole `nac_up`/`nac_down` table through the existing async slot
  (`nac_async_submit_static`, no copy), so the worker never waits for the input thread (a >= 20 ms worker gap stops the
  speaker path with NAH_E_DEADLINE, which is sticky).
* `nac_mic_take()`: drains VUL12 (same ring reader as the probe), keeps the probe statistics.
* `nac_tick()`: the uplink-mute rewiring of CONN17/18 only in call mode.

### 0321-nord2-mic-devfs-worker.patch (bsd/dev/audio_mic.inc new, audio_device.c; 467 lines)
* `/dev/redwood-mic` (cdevsw + devfs 0666, created after /dev/redwood-audio): open (read-only), read (O_NONBLOCK never
  waits: EWOULDBLOCK when nothing is ready; a blocking reader waits <= 1 s, for `dd` diagnostics), close (= last close,
  XNU has no D_TRACKCLOSE: all readers gone).
* Jitter buffer (8192 samples): nothing until `prime` (2048 = 43 ms) samples, then what is there; dry -> prime again;
  more than `max` (6144) -> drop the oldest at once down to (prime+max)/2 (one discontinuity per clock-drift correction).
  Host simulation, 10 min, worker 96 samples/2 ms vs HAL 1024/21.3 ms: 0 underruns/drops at 0 and +-50 ppm; +200 ppm:
  2 jumps; -200 ppm: 4 re-primes.
* Worker state machine `mic_poll()` (NM_IDLE -> NM_UP_WAIT -> NM_ACTIVE -> NM_DOWN_SUBMIT/NM_DOWN_WAIT, NM_FAILED):
  MT6359 up (async) -> if the speaker path is stopped, start it **silent** (`nau_hw_start_channels(..., silent_only=1)`:
  AFE + DL1 zeros, amplifiers never powered) -> `nac_mic_afe_up` -> `mic_feed()` every 2 ms pass: `nac_tick`
  (calibration on the first use of the boot, codec monitors), VUL12 -> Q8 gain -> ring, plus the playback queue or
  zeros so the 750 ms watchdog never fires. Teardown on last close / a call wanted / hardware stopped: AFE restore,
  stop the path only if the recording started it, MT6359 down (async). A call (`call_poll`) waits until the
  microphone gave the MT6359 back (`mic_blocks_call()`), the recording resumes by itself after the call if still open.
  Three failures in a row park the device until every reader closes (no retry loop on a broken path).
* Speaker sound during a silent recording: dropped by default; `kern.n2mic.upgrade=1` restarts the path with the
  amplifiers (see risk 1 for why it is off).
* sysctl `kern.n2mic.{enable,prime,max,gain,upgrade}` (rw), `{users,opens,reads,state,sessions,rc,in,out,dropped,
  underruns,peak}` (ro). Status: the worker prints nothing while DMA runs; the modem thread prints the existing
  `NORD2 CALL: phase 7 ... uplink: mic N frames peak P ...` lines every 5 s during a recording (phase 8 = MT6359
  powering), and the idle line `NORD2 MIC: opens=.. reads=.. state=.. rc=.. sessions=.. upgrades=.. last: in=.. out=..
  dropped=.. underruns=.. peak=.. silent=..` follows `NORD2 AUDIO PCM` once something has opened the node.

### Files
| path | what |
|---|---|
| `src/bsd/dev/{audio_call.c,audio_call.h,audio_device.c,audio_mic.inc}` | patched sources |
| `base/bsd/dev/*` | q10in stage originals (= q10jw for these files: 0310/0311 do not touch them) |
| `patches/0320-nord2-mic-capture-afe.patch`, `patches/0321-nord2-mic-devfs-worker.patch` | `patch -p1` format |
| `make_series.sh [--check]` | regenerate/verify; checks base == q10in stage and that both apply in order to it |
| `test/run.sh` | host tests (ASan/UBSan, -Werror): call regression (the old test_call.c on the patched file), 0320 mic path, ring, worker, call-path write-trace identity |
| `MANIFEST.txt` | sha256 of base/src/patches |

Verification done on the host [V]: `bash test/run.sh` all pass; `gpu/tools/xnu_staged_compile.py` (exact CI flags,
-Wall -Wextra -Werror) compiles patched `audio_call.c` and `audio_device.c` with no diagnostics; new undefined symbols
in audio_device.o: `_msleep _wakeup _sysctl_handle_quad _sysctl__kern_children _sysctl_register_oid_early
_sysctl_handle_int` (present in the q10jw kernel) and the four `nac_mic_*` from audio_call.o.

## 3. Next steps (main session)

1. Build (host, one build lease), e.g. NAME=q10ka on top of the q10jw recipe (gpu/hwcodec/kern/INTEGRATION.md):
   `SERIES=<q10hq S>,0296,0298,0299,0300,0310,0311,0320,0321` and add
   `--patch-dir $W/gpu/build/audio-capture-20261111/patches` to PD (0320/0321 are unique numbers). Expect
   `apply-0320.log`/`apply-0321.log` clean; changed objects: audio_call.o, audio_device.o only. Same pack/ADT/audio pin
   (`autotest/pins/audio-q10iu.pkg`: the plug-in is unchanged).
2. Phone test (needs the user's approval of the exact image): boot, before touching Camera check the console for
   `NORD2 MIC: devfs redwood-mic ...`. Record a 15 s video speaking near the bottom mic. Expect during it
   `NORD2 CALL: phase 8` then `phase 7` lines with `uplink: mic N frames peak P` growing; after it `NORD2 MIC: opens>=1
   sessions>=1 last: in~720000 out~in underruns<=2 peak>500 silent=1`. Pull the .MOV and check the AAC level.
   Decision table: opens=0 -> VirtualAudio does not run the "Codec" input for Camera (userland: plug-in data sources /
   VA port mapping, see section 5); opens>0, in=0 -> MT6359/AFE (rc, codec monitors in the phase-7 lines); in>0,
   out=0 -> the plug-in does not read (sandbox/permission: check `kern.n2mic.reads`); all fine but quiet -> raise
   `kern.n2call.pga` (0..5 = 0..30 dB) or `kern.n2mic.gain`.
3. Tuning after it works: gain (Android records the handset mic with the HAL's record PGA; ours shares the call's 18 dB),
   A/V offset (~45-65 ms of jitter buffer: either lower `prime` to 1536 or report the latency from the plug-in, U1).
4. Optional userland (audio.pkg, not needed for sound): U1 plug-in reports input latency = prime on the "Codec" input
   stream (kAudioStreamPropertyLatency) so CoreAudio timestamps compensate; U2 plug-in `say()` also to /dev/console
   (as rwcam_log.c does) so its mic open/close lines reach the USB log.

## 4. Effort, risks

Effort: kernel part done (about half a day). Remaining: 1 build (~30-60 min host), 1 phone run, then tuning (0.5 day).
If opens=0 on the phone (VA not using "Codec"), add 0.5-1 day of userland routing work.

Risks:
1. **Co-blocker, found here: the speaker path dies in every recent session.** In every autotest/autocycle run since the
   camera bring-up (q10g2, q10he ... q10io, user2..user6) the first amplifier start fails:
   `NORD2 AUDIO: start=-3 ...` / `NORD2 AUDIO ERROR: where=start reason=-3 ... i2c=5/16 ... held=1 uncertain=1`
   (q10g2: `i2c=4/4 i2cpa=100053f0`, the GPIO mode of the I2C8 pins). The last runs with working playback/calls are
   q10eb..q10f3n, before the camera code. The camera driver bit-bangs the FAN53870 camera LDOs on **I2C8, the
   amplifiers' bus** (nord2_cam.inc n2c_fan_setup, under the audio lease, pins given back afterwards), and every audio
   start after `NORD2 CAM: FAN53870 ... all camera LDOs on` fails [V correlation in 20+ runs; I the cause]. Because
   `uncertain=1` is sticky, nau_hw refuses **every** later start, so the microphone (which needs the AFE through the
   same path) is refused too (`NORD2 MIC ... state=5 rc=<error>`). A recording made before the first failed playback
   works; after it, it stays silent. In user6 the failure came at the first shutter sound (l.15638), the second
   recording (l.20660) was after it. Fix: separate task (I2C8 controller/clock/pin state after the camera's bit-bang,
   audio_i2c8.c stage 5 failure 16, `pa=10006170`). This is also why `kern.n2mic.upgrade` defaults to 0.
2. VirtualAudio may run the "Codec" input continuously (e.g. speech detection: user6 shows `Speech Detection: YES`
   once) -> the plug-in keeps /dev/redwood-mic open -> MT6359 + AFE + silent DMA stay on (power). The status lines
   show it (sessions/opens growing without Camera); kill switch `sysctl kern.n2mic.enable=0` (refuses new opens).
3. The plug-in opens the node only at the first read after the input starts and stops trying until the input stops;
   a refusal (ENXIO while `enable=0`, or before the audio worker is ready) means silence for that whole session.
4. mediaserverd's sandbox for the new node [I]: same 0666 devfs node type as /dev/redwood-audio and /dev/redwood-camera,
   which it opens today; no path allowlist found in the kernel.
5. Mono main mic only (AIN0); the 'mc02'/'mc03' data-source choice of Camera is accepted and ignored. Second mic /
   stereo: not done.
6. During a call the node delivers nothing (EWOULDBLOCK -> zeros in iOS); the call's uplink stays the kernel's.
7. The upgrade path (silent -> amplifiers, `upgrade=1`) and coexistence with a running playback are host-tested only.

Option B (drop the audio track) is unnecessary.
