# Independent Android/HackiOS parity audit

Scope: current canonical audio_device/stream/live/hw/native binding, pinned
hal-preview source, local HackiOS/Topaz upstream sources, and selected Android
kernel. No canonical, phone or CI changes. Image watcher was stopped when parent
paused packing; this audit does not authorize a flash.

## Demonstrated blocker: largest valid HAL burst exceeds empty producer queue

Pinned HAL `hal-preview/source/audio_plugin.c:235-236` explicitly defines16384
frames as timestamp period and largest I/O buffer; property returns it at600-601.
WriteMix at1019-1022 writes `frames*4` to an already-open sink. Our `audio_write`
accepts that65536-byte write (under262144 cap), moving256frame chunks, but the
producer has only8192frames. There need be no worker execution between those
chunks. Even an initially empty producer therefore drops half this validperiod.
This does not assert that the real observed HAL currently chooses16384frames;
it establishes a permitted producer contract that the port cannot fully buffer.

Private `producer-capacity.patch` changes only producer8192→32768frames, two
advertised maxperiods, +98304bytes staticBSS. Hardwarequeue remains8192, SRAMring
1024, no MMIO/control changes. This accommodates one maxperiod and startup/backlog
overlap, while keeping nonblocking bounded drop-on-full behavior and counters.
It does not turn write() into a waiting syscall or alter ioctlABI.

`run-producer.py` builds ASan/UBSan regression using actual source queue and actual
hardware/AFE/I2C/amp/board routines through the existing hardware model. Four16384
bursts paced at341.33ms, with an explicit200ms simulatedcoldstartup and2ms worker:

* Original8192: written65536, accepted32768, dropped32768, hardwarePCM32768.
* Candidate32768: written65536, accepted65536, dropped0, hardwarePCM65536.

Full producer beyondcapacity still returns0 with boundedcount, records exact
additionaldrops, and hardwarestop/configrestore succeeds. Saved original/candidate
stream sources make comparison reproducible even after canonicalcapacity changes.
The200ms startup is a test scenario, not measured phone startupduration.

## Shutdown/cadence finding delegated to installer agent

Current `nau_hw_stop` calls full `amp_cold` for amp0, including up to50ms status
polling, before clearing AMPE on amp1. During that valid wait the worker cannot
refill the21.33ms ring and the secondamp can replay stalePCM. Parent assigned
mute-both-before-powerdown-wait correction to audio_install; this audit does not
compete with its implementation.

For fatal DMAownership loss (deadline, pointer, clockstall), normalI2Cstop can
also wait before silencing both outputs. Existing owned-GPIO `reset_hold_fault`
can immediately hold bothactive-high resetlines without addressing the shared
controller. Sent parent/installer recommendation to perform that containment
before I2C/state waits for those fatalerrors, preserveheld/reset evidence and
prevent automaticrestart. Error-path digitalclose remains necessary even when
bus is offline. Parent owns final policy/tests.

## Verified parity and integration facts

| Concern | Verified evidence and outcome |
|---|---|
| PCM format | HAL `format_of`305-318: signedpacked native littleendian16bit,48000Hz,2channels speaker,frame4B. Kernel REDWOOD ABI and enqueue/dequeue agree. |
| Memory versus serial format | DL1 packs leftlow16/rightupper16 in one32bit SRAMword. Android FE S16 and BE forcedS32 (`mt6885-mt6359.c:415-424`) are distinct hardwarestages;32bitI2Sslot does not require32bitDMA PCM. Profile has32bitTDMslots and separateL/R selection. |
| Plugin open/write | HAL initialize343-355 opensonce O_WRONLY; StartIO/StopIO889-924 do notclose/reopen speaker sink. Kernelnode0666 exists before userspace and worker readiness doesnot gateopen. Wholeframewrite validationmatches. Drops onfull are explicitly allowed by upstream redwood_audio.h but undersizing one legalperiod above is avoidable. |
| Volume ABI | HAL volume_to_kernel280-288 sends two uint32 fields via_IOW('r',1), attenuation in eighthdB andmute. Kernel structsize/requestmatch. scalar_to_db262-277 and nsa_gain use0.125dB units; samplemultiplication fits signed32bit. |
| Volume and mute semantics | Normal volume applies when producerdequeues; frames already in hardwarequeue keep oldattenuation, giving up to~170.7ms queuedvolumelag plus21.3ms ring. This is a limitation, not a first-sound blocker. Mute flushesproducer andrequests workerstop; hardwaremute fix above boundsremainingoutput. Android usesampvolume/ramp instead, so do not claim zero-latency volumeparity. |
| Trigger/stop | Kernelstarts on acceptednontrivial PCM, doesnotpoweramp for zero-onlyHAL. HAL StopIO only stopswrites;750ms quietwatchdog terminatesport playback. LastFDclose drainsacceptedtail. Already-fixed stale-idleflush race uses current timestamp underlock. |
| Producer/consumer | Producer protectedby one shortmutex; onehardwareworker. uiomove occursoutside lock; no MMIO/I2C/allocations whileproducerlockheld. Queue writes perchunk256 and hardware feed return count is bounded by availablehardware room. No userPCM overUSB. |
| Clock and DMA cadence | Ring4096B/4=1024frames=21.333ms. Worker sleeps2ms AFTER work. consumed>480 rejects>10msforwardprogress;timegap>=20ms rejectswrapambiguity. InitialCUR0 gets12ms grace; no-progress12ms stops; pointerextent/alignment checked. These are fail-closed bounds, not scheduler guarantees. |
| Runtime monitor | Polls2amps every4ticks (nominal~8ms, oldcomment20ms is not a guarantee). poll_limit200×10us pertransaction permits<=2ms delay peramp; two successes atlimit+2mswait≈6ms plusPIO/MMIO/scheduling. Hostmodel cannot prove actualphysicalmonitorlatency. Coldinit canuse20ms pertransaction but occurs beforefirsttickbaseline on zerofilledring. |
| Scheduling | kernel_thread_start worker is nothardRT; retaining it avoids applying1ms RTbudget around blockingcoldstartup. No synchronous normal/statusprinting while running; sourcecallbacks use saved USBcounters. Other kernelwork can force deadlinefail; containment above matters. |
| Errors/recovery | No automaticrestart after hardwareerror. Uncertainbus blocks furtheramptransactions, resetroute containsoutputs, digital sourceclose stillattempted. Freshinit isoneattemptperboot. LiveENABLE after a hardwareerror cancurrentlyreturnOK whileautostart remainsblockedby error; this is misleadingcontrolresult, not actualrecovery or safeautomaticretry. Prefer NAL_UNAVAILABLE if error persists, rather than clearing error blindly. |
| USB service | Atomicallypublished request/reply transport separatefromhardware. Boundednativewaitinitializesnoncefirst andservicescommands; uninitializedmutationrefused. ActualCtransport/PythonCLI lostACK/stalereply/pending/reboottests alreadyPASS. STATUS0result only means requestcompleted; inspecthardwareerror/init/running fields. |

## Native mapping and ownership comparison

`osfmk/arm/io_map.c:88-146` truncatesPA to16KiB, mapsrounded(size+offset), returns
VA+offset. Thus capturednw.infra-0x1000 andni_gpio-0x1000 safely reusewholepages
10000000 and10004000; alignedni_lt retained; AFE andSRAMshare11210000 mapping.
Sixnew alignedretainedwindows plus threeborrowedones cover everyactualaddress.
Release/acquireNIpublication afterinitialtouch/WiFi writes avoids reading partial
mapstate; wait doesnoMMIO; mapping and hardwareinit attempt once. SharedNI polls
GPIO202-205 and read155; audio reset111/148 uses SET/CLR aliases, no stale RMW.

Nativelease is only audio's local exclusive-I2C8 owner, not a universalhardware
semaphore. CurrentportbatteryusesI2C7/I2C11, touchI2C0, PMICseparatePWRAP; no second
activeAP I2C8 writer found. Eachtransaction checks CCU/CAMoff, shadow/CCU/APidle,
DMAidle or clockgated, existingpads and stableparent. Portdoesnot resetsharedbus,
writeCCU/shadowRESUME, or seizeAPDMA. Parent keepssharedi2c_sel monotonicenabled
if acquired; it restores onlyI2C8exclusivegate. This differs fromLinuxCCFglobal
refcounts intentionally; it must notclaim leaseprotects hypotheticalfutureAPusers.

BorrowedADSP/APLL2state preservation is separately reviewed in cold-state/FINDINGS;
knownactualinheritedsnapshot now covered by parentregressions. CurrentI3Cinherited
100k correction and exactnativeallowlists audited in i3c-inherited/FINDINGS:
byteDATAport and16bitsetup/restorematch Android1196/1350/1040-1047, all0a74/b74/c74
silicon tables allowlisted; no debugger/HFIFO registerwrite required at100k.

## Residual limits before hardware

Physicalamprevision, realbusresponse and postpoweronDMAprogress remainunmeasured;
none can be inferred from HALopen, userspacePCMcount, hostmodel or validimage.
The immutableboard rejectsunsupported9873/future9874 revisions. Currentfixedquiet
ceiling is a diagnostic outputpolicy, not AndroidspeakerDSP/loudnessparity.

Capacitycandidate requires coherentfinalsource/run after parentmergesotherfixes.
Its32768producer +8192hardware queues canhold~853ms PCM inworstcase. Currentidle
policy is750ms since lastacceptedsound, independent ofqueuednonzeroaudio. Verylong
successfulstartup/backlog couldtherefore stopbeforeallacceptedPCMdrains; thetest's
200ms startup hasno suchloss. Parent should either documentandboundstartup/latency,
or addpending-sound-awareidle policy beforeclaiming everyacceptedframealwaysdrains.
Do not simplywaitforemptyqueues: HALwriteszerotails, so zeroframes wouldkeepampson.

No other confirmedPCMformat/ioctl/nativebinding blocker found in this pass.
