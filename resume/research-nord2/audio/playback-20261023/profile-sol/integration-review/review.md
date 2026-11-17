# Bounded integration audit

Reviewed canonical audio_device/audio_stream/audio_live against pinned built
hal-preview/source/audio_plugin.c and redwood_audio.h. Actual producer is48k
stereoS16 interleaved; single sinkopen at plugininitialize; DoIO WriteMix sends
frames*4 with intentional drops rather than waiting; volumeioctl is two uint32
fields using _IOW('r',1), matching kernel. Core StartIO/StopIO do not close sink;
lastFD drain behavior is therefore auxiliary rather than normalHALstop signal.

Demonstrated bug: worker snapshots old last_sound, HAL enqueues a complete brief
click after snapshot, worker idleflush uses stale timestamp and drops accepted
click before hardware starts. A second interleaving allows new unmutedPCM while
ampstop runs then unconditional poststopstreamflush dropsit. Minimal private
patch removes unconditional poststopflush (mute/disable alreadyflush at origin)
and tests fresh stream timestamp under producerlock before idleflush. Hosttest
links actualcanonicalaudio_stream.c; reproducesoldloss and verifiespreservation,
normalstale/zeroqueueflush, and unmute-during-stop case. Parent owns application.

Additional bounded finding sent parent: active redundantENABLE still prints via
PIOconsole because commandsprintf guard excludesSTATUSonly. Change toprintonly
when notrunning; commandresult remains USBvisible. Startprint beforefirsttick
has firsttickbaseline allowance; errorprintafterstopping doesnot competeDMA.

Actual Ctransport + PythonCLI fakeUSB tests5PASS: everycommand and signedcounter,
lostACK nosecondwrite, pendingrefusal, previousreply wait, rebootrefusal. Runtime
boarddecoder104operations equal referencePython, alltruncations/digest/reload
reject, supportedrevision/address pairs verify. No device access, canonicaledits,
build, download or generic risk claims made in this bounded audit.
