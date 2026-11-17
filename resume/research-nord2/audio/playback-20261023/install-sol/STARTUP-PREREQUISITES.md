# Read-only startup prerequisite followup

Inspected actual local image research/nord2/rootfs-vm/ios16-root-20H392-nord2-v1.img
using the existing strict HFSX reader from
research/nord2/wifi/claude-opus-security-failure/review-log-access/collect.py,
not the initially suggested nonexistent wifi-ios-identity/review-log-access.
No mount, journal replay, write, USB or device access. Volume clean; 180224 bytes
physical reads for 22 exact paths and relevant directory listings. This is the
local baseline image, not a fresh observation of current phone filesystem.
rootfs-prerequisites.json records source reader SHA and metadata.

## Actual paths

- /System/Library/Audio/Plug-Ins/HAL: root:wheel0755, exactly 9 Apple driver dirs.
- /Library/Audio/Plug-Ins/HAL: root:wheel0755, empty.
- /Library/Audio/Plug-Ins/HAL.disabled: root:wheel0755, VirtualAudio.plugin present.
- Disabled VirtualAudio.plugin: root:wheel0755; Info.plist root:wheel0644;
  VirtualAudio executable root:wheel0755. Live VirtualAudio.plugin absent.
- /Library/Audio/Tunings/D22: root:wheel0755; AU, ButtonHaptics, Haptics,
  Sidetone, VAD and VPV6 dirs present. /System/Library/Audio/Tunings/D22 absent,
  so keep the installer check at the existing /Library path.
- Every traversed intermediate component is an ordinary directory; no forbidden
  symlink was encountered. Every inspected fixed private journal/stage/backup/
  commit/rollback path is absent in baseline.
- CoreAudio.framework/Info.plist retains NullAudioDriverConfiguration UUID
  Null_Device, with 44.1kHz and 48kHz formats. VirtualAudio Info.plist retains
  com.apple.audio.CoreAudio.VirtualAudio / New_VirtualAudio_PlugIn. No plist
  modification is required by this evidence. rootfs-audio-config.json records
  hashes and relevant summaries; no Apple binary content was extracted.

## Concrete requirements and remaining uncertainties

VirtualAudio is disabled in baseline: installing only RedwoodAudio into System
HAL cannot enable the iOS Speaker route. The installer must restore VirtualAudio
AFTER verifying the replacement System HAL. Current installer does this.
The source explicitly supplies Speaker plus silent stereo input, Hawking silent
mono input (mandatory vhaw companion), Actuator mono output and Codec mono input
with imic data source. Leaving out Hawking is a concrete routing defect according
to upstream source. We preserve the entire unmodified plugin.

Upstream initialize() opens /dev/redwood-audio exactly ONCE and returns success
even on open failure. It does NOT retry when the devfs node later appears. Thus
the node, its permissions and cdev methods must be ready BEFORE mediaserverd
loads the plugin. An existing route/UI is insufficient evidence: require HAL
'open' log and fresh kernel sample/queue counters during an actual playback.
Codec and Actuator nodes may be absent; their source opens are deferred and
failure-tolerant. That is not microphone/haptic implementation evidence.

NOTES.md line8013 records historical mediaserverd FAR0x390 in VirtualAudio
ApplyBeamFormerPreference, with missing vdef as the cause, before a real HAL
backend existed. That note is historical analysis, not fresh crash evidence.
The new plugin/source prerequisites address device discovery, but startup and
vdef presence still need the first real boot to verify. Do not restore
VirtualAudio alone or claim those two capability flags prove the crash solved.

## /product/audio comparison and optional source patch

Actual native D22AP ADT SHA
9e670987c80493bd69aba7be62e3179c04663324d9249b0973675baac175dbf1 has a /product/audio
child with only wireless-splitter=1, supports-always-listening=1, name and phandle.
There is NO vdef property. Current mkadt-nord2 calls d22ap_product(), which copies
only parent properties and drops ALL children. That omission is factual.

Pinned Topaz mkadt.py at e4772911e116ab4ab6c4ac1f38e6106fefe8b483 ALSO copies only
/product parent properties. Source fetched privately with exact URL/SHA receipt
in topaz-mkadt-provenance.json. Therefore absence of /product/audio is NOT a
proven unique Nord2 startup blocker or a proven cause of missing vdef. We found
no source evidence requiring a new virtual-device property in that ADT node.

Optional mkadt-nord2-audio.patch / mkadt-nord2-audio-proposal.py restores ONLY
the two native identity flags in an opt-in --audio-service child, allocates a new
unique phandle, rejects unexpected/placeholder values, never copies Apple
hardware nodes. Tests prove default output byte-identical, exact property
allowlist, unique phandles and required native source. Parent may promote this
narrow native identity completion; it must not be labeled a verified vdef fix.
Baseline ADT12976 bytes; audio option13148 bytes. Hash receipt in
adt-audio-comparison.json. No canonical file was modified.

## Journal-free rollback idempotence change

Mode2 previously returned ENOENT without J. It now returns0 without writes only
for the verified baseline STRUCTURE: exact nine Apple HAL directory entries,
VirtualAudio disabled and absent live, safe root parents/D22, and B/S/C/R absent.
Unknown or live-VirtualAudio no-journal arrangements refuse, because original
placement cannot be inferred from no receipt. This is stronger than treating
missing journal as success. It verifies arrangement, not all Apple file hashes.
After a real journaled rollback, both original live/disabled states are already
idempotently recognized using the journal.

Production shared-flow144 fault/crash tests still PASS ASan/UBSan, now also test
journal-free mode2 baseline success and unknown live arrangement refusal. Full
kernel source -DNORD2=1 -Wall -Wextra -Werror compiles using CI39957914679 headers.
New object SHA256
46027751e7c4b7e69c4d7695a27a8f432e94983ad623f14d3f26d16eba88bf45.
