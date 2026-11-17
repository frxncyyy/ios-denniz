# Private Nord2 HAL deployment contract (not promoted)

## Status and promotion gate

Real fixed-path kernel installer/rollback: install-kernel.c, install-flow.inc,
install-restore.inc, package.h and install-kernel.h. The production flow and
recovery code are included directly by test-flow.c. Host backend tests pass
144 injected failures/crashes, plus idempotence, collisions, marker corruption,
known partial rollback layouts, and damaged installed payload refusal. The
complete kernel source compiles NORD2=1 against CI39957914679 with Wall/Wextra/
Werror. No kernel link, Apple SDK build, filesystem execution or hardware claim.
The older validate-kernel.c is a standalone parser prototype; do not promote it.

Call nord2_audio_install() on EVERY new-kernel boot immediately before
load_init_program after root mount, including when audio bootarg is absent.
On nonzero result, do not launch launchd: parent must select recovery behavior.
First install uses nord2audioinstall=1. Rollback uses nord2audioinstall=2 and
requires no package. Exact tokens only; duplicate/unknown tokens refuse.

The current upstream plugin is adopted unmodified: the three pinned files are
in ../upstream/topaz/userland/audio and byte-match ../upstream/hackios-userland/
userland/src. Provenance commits and source URLs live in the parent directory's
JSON receipts. Keep any upstream copyright/license notices unchanged; this work
does not assign a new license to the upstream plugin.

## Fixed package format

ADT /chosen/nord2-audio-package contains N2AP v1. Byte order is little endian.
Total size 96..262144 bytes; exact size, no padding or trailing data. No filenames,
paths, symlinks, permissions or commands are accepted from the package.

| Offset | Width | Value |
|---|---:|---|
| 0 | 4 | ASCII N2AP |
| 4 | 4 | version 1 |
| 8 | 4 | entire property size |
| 12 | 4 | entry count 2 |
| 16 | 4 | executable byte length, nonzero |
| 20 | 4 | plist byte length, nonzero |
| 24 | 8 | zero reserved bytes |
| 32 | 32 | SHA256 executable bytes |
| 64 | 32 | SHA256 plist bytes |
| 96 | variable | executable then plist, no gaps |

The executable maps only to RedwoodAudio.driver/RedwoodAudio (0755, root:wheel);
plist maps only to RedwoodAudio.driver/Info.plist (0644, root:wheel). Directories
0755 root:wheel. Hashes provide corruption detection, not payload authenticity;
the exact outer boot image/package must be pinned by the parent build receipt.
Linux package.py accepts only arm64 little endian MH_BUNDLE and the upstream CF
bundle/factory identities. C parser checks bounds and both hashes, not Mach-O or
plist semantics; CI/package-builder provenance must therefore remain trusted.

## Implemented filesystem transaction

Fixed file list (no package-supplied paths):

- A: /System/Library/Audio/Plug-Ins/HAL
- B: /System/Library/Audio/Plug-Ins/HAL.nord2-original
- S: /System/Library/Audio/Plug-Ins/HAL.nord2-stage
- S/RedwoodAudio.driver/{RedwoodAudio,Info.plist}, with corresponding A paths
- V: /Library/Audio/Plug-Ins/HAL/VirtualAudio.plugin
- D: /Library/Audio/Plug-Ins/HAL.disabled/VirtualAudio.plugin
- J: /System/Library/Audio/Plug-Ins/HAL.nord2-journal
- C: /System/Library/Audio/Plug-Ins/HAL.nord2-commit
- R: /System/Library/Audio/Plug-Ins/HAL.nord2-rollback
- Fixed parent dirs and /Library/Audio/Tunings/D22, for validation/fsync only

J is an immutable 200-byte root:wheel 0600 regular file. Magic N2AJNL1 + NUL,
version 1, original VirtualAudio disabled bit, 16-byte identities (uint64 inode,
uint32 fsid, uint32 vnode type) for original HAL/new HAL/VirtualAudio, executable
and plist sizes and SHA256 hashes, SHA256 of the three identities (original
state identity digest), SHA256 of all preceding journal bytes. Integer encoding
is native little endian arm64; no dynamic paths. Identity hashes do NOT hash the
contents of the original Apple/VirtualAudio trees. Those directories are moved
intact and never modified internally. Directory inode identity ties recovery to
the preserved objects; this is corruption detection, not authentication against
an attacker with root filesystem write access. C and R are 32-byte root:wheel
0600 regular files containing J's digest. All files require single hardlink.

Kernel root context, namei UIO_SYSSPACE and NAMEI_NOFOLLOW_ANY reject every path
symlink. Fresh files use vnode_open O_EXCL/O_NOFOLLOW_ANY, vnode_setattr,
vn_rdwr UIO_SYSSPACE with IO_SYNC/IO_UNIT, fsync, close and full readback.
Directories use namei + VNOP_MKDIR, all renames use namei + VNOP_RENAME under
mount rename serialization acquired before lookup iocounts. Source identities
and absent destinations are checked. Both parents are fsynced after cross-dir
moves. This relies on the documented single pre-launchd caller and no competing
writer of these fixed directories: VNOP_RENAME itself is not exclusive if a
concurrent writer creates a destination between namei and rename. Neither the
installer nor boot flow may permit such concurrency. No syscall kernel-pointer
casts, raw disk IO, mount, arbitrary unlink or recursive cleanup exist.

Preflight verifies package, parent ownership/type, no unknown B/S/C/R, original
HAL identity, exclusive V-or-D original VirtualAudio arrangement, and D22 tuning
directory existence. It preserves original HAL as a whole, without guessing
which Apple drivers should be copied. Fresh staging is exclusively created,
file bytes and metadata read back, exact entries enumerated, directories fsynced;
then immutable J is created/readback/fsynced before ANY live layout mutation.
Install moves A->B then S->A, restores D->V if originally disabled, checks custom
file hashes/metadata/exact entry allowlists and VirtualAudio identity, and creates
C/readback/fsync last. No directory or file is overwritten or deleted.

Before-journal failures/crashes leave the original layout intact and may leave
an incomplete staging tree. A later explicit install refuses that unknown stage;
it does not remove or reuse it without a reviewed rescue cleanup. This trades
automatic retry of incomplete staging for no unknown-content overwrite. Partial
or corrupt journal also refuses launchd; original layout was untouched before
journal durability. Rescue may inspect and clean those fixed paths separately.

Every boot with valid J checks recovery BEFORE launchd. Without valid C it
restores the original layout. Explicit rollback creates/fsyncs R first. Valid R
causes rollback even if C is valid. Known inter-rename/VirtualAudio rollback
layouts are recognized even if R was lost but C remains valid. Successful moves
followed by fsync errors are recovered by observed inode locations, not a guessed
last-completed operation. Rollback restores the original VirtualAudio arrangement,
moves verified custom A back to empty S, and B back to empty A. All moves are
read back and parent fsynced. Unknown collisions/modified custom payload refuse;
no unknown content is deleted or overwritten. S/J/C/R are retained for review.
The restored original layout is recognized on later boots; mode 2 is idempotent.
Mode 1 after rollback returns EALREADY (no automatic reinstall/cleanup).
Without J, mode2 is a read-only no-op only after checking the verified baseline
structure: exact nine Apple HAL entries, disabled-only VirtualAudio, required
root dirs/D22 and absent B/S/C/R. Other no-journal arrangements refuse. This
structural verification is documented in STARTUP-PREREQUISITES.md.

Committed installation is idempotent only with exact file bytes/metadata/entry
allowlists, backup identity and live VirtualAudio identity. Repeated mode 1 also
requires a matching ADT package; package-free normal boots validate from J.
Any post-journal install error attempts synchronous rollback; R is recorded first
best effort. Logs include mode, phase, operation error and recovery error.
Nonzero return requires parent recovery handling rather than silent launchd.
Directory fsync completion is the filesystem's durability contract; no claim is
made of stronger guarantees under arbitrary storage corruption or fsync lies.

Model coverage is not HFS/VNOP execution coverage. Validate final full-kernel
link and exact payload, independently review these helpers, then obtain exact
image authorization for the first hardware install. Retain verified RAM ADB and
a rollback-capable new kernel; fallback55 alone does not repair a crash gap.

## iOS and fallback55 behavior

Upstream is flat BNDL RedwoodAudio.driver/{RedwoodAudio,Info.plist}, exported
factory RedwoodAudio_Create and CoreFoundation/libSystem imports. Its four UIDs
are Speaker/Hawking/Actuator/Codec. VirtualAudio supplies iPhone X D22 routes,
including Hawking companion vhaw and d22_speaker_general tunings. Keep D22
routing and /Library/Audio/Tunings/D22. Speaker is 48kHz stereo signed 16-bit to
/dev/redwood-audio. Actuator and Codec reference redwood-haptic/redwood-mic;
these nodes are not implemented by this installer work. No microphone/haptic
hardware success is implied by displaying their routes.

Persistent filesystem install survives changing boot partition. fallback55
lacks new devfs output and installer/rollback code: it may load RedwoodAudio
with /dev/redwood-audio missing, and the restored VirtualAudio can change its
startup behavior. Thus fallback55 is a kernel recovery image, NOT a filesystem
audio rollback. Need a verified rollback-capable new kernel or already verified
rescue write procedure. Do not require full userdata reflash as routine rollback.
