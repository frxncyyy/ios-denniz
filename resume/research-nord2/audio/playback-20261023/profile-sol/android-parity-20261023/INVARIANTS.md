# Coherent private producer/idle/error candidate

Apply `producer-policy.patch` (audio_stream.h/c + audio_device.c only). This
supersedes capacity-only `producer-capacity.patch`; do not apply both. Candidate
source copies are reference; parent generates sourcepatch and owns nativecompile.

1. Producer ring32768frames (two maximumHALperiods), boundedstaticmemory. No writer
   waits for hardware, no enlargedSRAM orhardwarequeue, no IOCTL/binaryABI change.
2. `sound_frames` exactly countsqueuedframes with either rawsample outside[-8,8],
   same existingthreshold as start/lastsound. Enqueueincrements; dequeue subtracts
   originalrawframe evenaftervolumechange; flush/mute/disable resetit. Counter
   cannot exceedcount or underflow under existingproducerlock and validAPIusage.
3. Idleflush never discardsqueuedmeaningfulframes solelybecausetheyareold.
   Autostartrequires actualpendingsound, not age, so validPCMaccepted beforea
   longsuccessfulstartup isstillplayable. Zero-onlyacceptedtail cannotstartamp.
4. Worker owns downstreamqueueinspection: max8192frames memory-onlyscan, exits on
   first actualnonzerosample after independenthardwareceiling. No MMIO, no new
   lock, no allocation. Producerlocknotheld duringthisscan.
5. Idle750ms onlyapplies whenproducer anddownstream have nomeaningfulpendingPCM.
   Additional23334us covers1024frame SRAMring plusone2ms period afterlastpending
   work. last_pending isanchoredagain AFTERtickPCM/monitorwork. Continuous HAL
   zero-writes do notrenewsound orpendingclock and cannotkeepampaliveforever.
6. Explicitmute/disable/STOP overrideidle-drain immediately; preservednewunmuted
   inputduringstop survivesproducerflush racefix fromprevioussource. Fatalhardware
   error handling remainsowned byhardwareagent, no automaticretryintroduced.
7. NAL_ENABLE requires initialized, error0, certain, stop_error0. Otherwise returns
   UNAVAILABLE and preservesstickyerror+streamenabledstate; doesnotpretendrecovery.

Tests:
* run-producer.py: private original-vscandidate receipts, actualsource hardware
  queue/AFE/I2C/amp/board with model,4x16384pacedbursts,200msstart,2msfeeder.
  Originaldrops32768; candidateplays65536/65536 withoutdrop; fullqueuebackpressure
  andhardwarestop restoration stillverify. I2CRself-clear fixtureadapter aligns
  the newcoldinit semantics until canonicalsharedmodel implementsit.
* run-policy.py: actualcandidatequeuefunctions + actualextracteddevicecommands and
  downstreamhelper, ASan/UBSan. Long10secondstartpreservesPCM, rawpendingcounts,
  wrappeddownstream, finalgain-zero handling, ringtailwait, zero-writetermination,
  mute/disable,10000producerwrapcycles, stickyENABLEerror/uncertain/stop/uninit.
* Durablecandidate tests/nord2-audio/test-producer-capacity.c imports existing
  test-full.c fixture (mainrenamed) so parentampresetmodel changes are shared.
  No gitHEADcomparison or savedfirmwaretablevalues; suppliedprivateboard required.
* Durable test_producer_policy.py extracts finalcanonicalhelpers andlinksfinal
  canonicalstream; runnerintegrationsnippet in runner-integration.txt.

No canonical/device/CI changes. Parentmustapplypatch andtest finalcoherenthardware
model beforeusingdurablecapacitytest; the sharedmodel was changing duringaudit.
