Read-only reader: hfsx-read.py. No device operations were performed by its author.

Parent-owned command from repository root, substitute exact known ADB executable/serial:

    python research/nord2/display-capabilities/hfsx-read.py --adb /path/to/adb --serial EXACT_SERIAL --output research/nord2/display-capabilities/live-rescue-read1

Output directory must not exist. Existing nord2-install-root.py preflight verifies root rescue marker, userdata mapping/geometry and no block mounts. Every subsequent transport invocation is shellv2 -T with a fixed read-only dd command,4096-byte aligned single page, process status/30sec deadline and exact4096-byte length required. Max4MiB data plus final header stability reread. Original image validation used110592bytes plus final4096 stability read.

Default refuses volume not marked cleanly unmounted or marked inconsistent. Optional --allow-dirty-observation permits only untrusted on-disk observation: no journal replay/repair, found/missing results may be stale after panic. Use a new output directory. No reset/write conclusion should rely on dirty missing evidence alone. Host-page cache and raw filehex preserve evidence. Header is reread after lookup; equal headers do not prove an on-disk journal is clean.

Only HFSXv5/4096 block/binary0xbc catalog supported. Inline8-extent forks only; overflow, overlapping or malformed extents fail. Bounded nodes/depth/key/record offsets/child IDs/physical ranges; exact ASCII paths only. Symlinks, compressed files, hardlink markers and special files refused. Global /Library is a symlink in baseline, so that optional path returns a refusal rather than following it. Three mobile targets resolve directly.

Tests: python research/nord2/display-capabilities/test-hfsx-read.py
Baseline springboard SHA e80395c626cc633e4998428d4c85bba17f55a36b00fd2d74e3b479450b037856 agrees with separately booted QEMU read-only HFS mount. This validates baseline extraction, not arbitrary malformed filesystem correctness or exact live Zoom key.

Format reference: https://developer.apple.com/library/archive/technotes/tn/tn1150.html
