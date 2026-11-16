# Transaction speed-up, 2026-10-26

New code only (old three tools untouched, old paths still work). Nothing here touched a device.

Files (gpu/tools): `fastroot.py` (shared), `snapshot_current_root_fast.py`, `update_metal_offline_fast.py`,
`commit_root_delta_fast.py`, `check_update_catalog_fast.py`, `test_fastroot.py`;
template `gpu/build/transaction-speed-20261026/run_transaction_fast_template.py`.

## Where the time went (c6hc logs, mtimes)

| stage | before | cause |
|---|---|---|
| snapshot 249 s | device hash 16 MiB chunks, one serial shell 50 s; local hash of all chunks + refresh of 102 chunks (1.6 GiB at ~21 MB/s) ~113 s; "independent" full device hash + full local hash ~86 s | serial hashing, 2 full local passes, 16 MiB refresh granularity, redundant whole pass |
| offline 219 s | sha256 of snapshot twice + clone once (~130 s, hashlib 480 MB/s single thread), `cp` 16 GiB 46 s, VM 43 s (3 fsck) | host hashing, copy |
| catalog 10 s | | |
| install 220 s | build_manifest = 4 SHA-256 passes over both 16 GiB images 157 s; whole-device device hash 50 s; the 78 chunk writes only 13 s | host hashing, redundant device pass |

Host SHA-256 is ~9 full passes (~6 of the ~12 min). Device write phase is not the problem.

## What changed

1. Warm persistent mirror (raw image + per-16 MiB manifest sidecar), refreshed in place, advanced to the installed state after a verified install, so next snapshot only refreshes what iOS changed. No 16 GiB copy ever (one-time `--seed`).
2. Device hashing in N parallel shells (toybox `dd | sha256sum`, libcrypto-backed; mksh `&`/`wait`; all verified present in the rescue ramdisk). Local mirror hashing (6 threads) runs concurrently with it.
3. Refresh: changed 16 MiB chunks are refined to 1 MiB pieces on both sides, only differing pieces fetched, `gzip -1` on the device (toybox gzip present, falls back to raw), 3 parallel streams, each piece hash-checked before it is written to the mirror.
4. Offline VM runs on a qcow2 overlay (1 MiB clusters) backed by the read-only mirror. The result is a delta (changed 1 MiB chunks + digests) taken from the overlay allocation map. Journal replay: same validation and Apple oracle (run read-only on the mirror), bytes applied in the VM. Post-journal `fsck -n` only if `fsck -y` returned non-zero; final `fsck -n` after unmount kept.
5. Install: digests come from the delta table (no image hashing); classification of all changed chunks on-device before the first write; then 64 MiB batches, one adb shell each (payload staged to /tmp tmpfs by stdin, like the old tool).
6. Whole-device pre/post hashes replaced by the epoch check (below).

## Measured on this host (real c6hc images, nice 10)

* local 16 GiB SHA-256, 6 threads, cold cache: 19.8 s (was ~43 s per single pass; 9 passes in old flow)
* offline update on overlay of the real c6hc snapshot with the c6i addon: 27.8 s total (VM 25 s), vs 219 s. Result 79 changed chunks (old flow 78 for c6h), final fsck clean, VM markers asserted.
* audit: full `qemu-img convert` of the overlay compared chunk by chunk with the mirror: changed set == allocation-map delta exactly (0 mismatches over 16384 chunks).
* catalog check on mirror + delta: runs, 10 changed paths, 0 outside owned.
* install host prep (receipt checks, delta/mirror digest checks, manifest): 1.0 s vs 157 s.
* snapshot pipeline with fake adb (real script text, GNU tools, 6 jobs, device = the c6hc after image): 43.7 s wall, mirror byte-identical to device afterwards (`cmp`), 78 MiB fetched. That 41 s is the host's own dd|sha256sum, so it says nothing about phone speed.

## Estimates for the device parts (not measurable offline)

| stage | before | after (estimate) |
|---|---|---|
| snapshot | 249 s | 50-120 s: device pass 20-50 s (serial 50 s now, probably pipe/CPU bound, floor = UFS read of 16 GiB; run `snapshot_current_root_fast.py --bench` once in a rescue window to pick `--jobs`) + refresh 15-70 s (1.6 GiB at 21 MB/s is 78 s raw; 1 MiB pieces and gzip can only reduce it); local hashing hidden |
| offline + catalog | 229 s | ~40 s (measured 28 + ~10) |
| install | 220 s | ~15-25 s (epoch check ms; 79 MiB at ~21 MB/s ~4 s; device ops ~5-10 s) |
| total | ~11.7 min | ~2-3 min |

Biggest remaining cost: reading all 16 GiB from UFS once per rescue session, and the download of what iOS dirtied.

## Safety argument

Kept, per chunk, in `script_apply` (device shell text, executed verbatim by tests): preimage hashed on the device immediately before the dd write and write refused unless it equals the verified before-digest (already-done chunks skipped, anything else aborts); write guard (block node, `readlink` = sdc57, rescue marker) re-checked before every write; payload piece hashed on device against the after-digest before use; after `sync`+`blockdev --flushbufs`+drop_caches every written chunk is read back and compared (stronger than before: old readback could be served from the page cache). Unknown preimage, even for the last chunk, aborts the batch. Classification of all chunks happens before the first write.

Snapshot == device: each of the 1024 mirror chunks is hashed locally after the refresh and equals the digest the device computed in the same session, so mirror == device chunk-wise; the whole-device hash only added "no concurrent write", which is now proven by the partition's block-layer write/discard counters and boot id being identical before the pass and after the last read (the phone partition is never written; the same counters prove it).

Install pre-check (replacing the whole() hash): if the counters/boot id still equal those recorded at snapshot end, the unchanged regions are as verified. Otherwise (new rescue boot, any write, `--resume`) the tool re-hashes the whole device in parallel and, on a mismatch, drills down to 1 MiB and accepts only before or after content of delta chunks. This is the one place where proof by counter replaces proof by hash; it is a decision for the parent to accept (rescue is RAM-only, userdata unmounted, preflight checks mounts).

Offline result: fsck-clean (final fsck after unmount asserted in the log), catalog check on owned paths only, delta from overlay map audited above; qcow2 semantics guarantee chunks not allocated equal the mirror, and each allocated candidate is compared by SHA-256 against the mirror, so the delta is exactly the changed set. Image identity is now `before_root` (SHA-256 over the 1024 chunk digests) and `after_id` (SHA-256 over before_root and the sorted changed-chunk digests); receipts carry them instead of whole-file SHA-256.

## Tests (`python3 gpu/tools/test_fastroot.py`, 12 pass, fake adb runs the real shell text on a file)

Refresh fetches only differing pieces and the mirror equals the device; corrupt gzip transfer rejected; write during snapshot detected via counters; parallel device hash equals Python; happy-path install (epoch skip, mirror advanced, manifest consistent); preimage drift aborts with the device untouched; drift detected after classification stops at that chunk (earlier chunk legitimately written, nothing at/after); payload corruption refused without writing; readback mismatch (dd shim corrupts a written byte) fails with "readback"; epoch change forces full verify and refuses drift outside the delta; resume after a partial install.

## Not verified without the phone

On-device speed of parallel `dd|sha256sum`, adb throughput with gzip, `blockdev --flushbufs`/drop_caches (best effort, errors ignored), `/sys/class/block/sdc57/stat` field layout (kernel 4.19: fields 5-7 and discard 12-14 used), tmpfs room for 64 MiB batches (`--batch-mib` smaller if needed), `adb shell -T` returning the remote exit status (the tool also requires explicit `BATCH_OK`/`V` lines).

## Switch-over (parent)

1. Copy the template into the next run dir; first run pass `--seed <last verified full image>` (one-time 46 s copy) and it creates `gpu/build/mirror/rootfs-mirror.img`.
2. Optionally `snapshot_current_root_fast.py --bench` once in rescue to choose `--jobs`; `--trust-sidecar` skips the local re-hash (saves what is already hidden behind the device pass).
3. Owned-path list for the catalog gate defaults to the current Metal set; pass `--owned-json` for another addon.
4. Keep the old tools as the fallback; the receipts of the two families are not interchangeable.
