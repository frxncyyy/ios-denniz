#!/usr/bin/env python3
"""Make PipelinePack.tpcpack for a built Nord2Metal bundle on the host.

    # 1. once per UI state seen on a device: descriptions from console logs
    python3 gpu/metal/tegu/pipeline_corpus.py scan LOG... --bundle <bundle that produced the logs> --out corpus.txt
    # 2. for every candidate bundle (after link_metal_bundle.py, before package_metal_addon.py)
    python3 gpu/tools/build_pipeline_pack.py --bundle gpu/build/native/Nord2Metal.bundle \\
        --corpus corpus.txt --work gpu/build/pipeline-pack-<tag> --replay <binary> --pack gpu/build/pipeline-pack-<tag>/PipelinePack.tpcpack
    # 3. ship it
    python3 gpu/tools/package_metal_addon.py --pipeline-pack gpu/build/pipeline-pack-<tag>/PipelinePack.tpcpack

What it does
  * reads the cache build id from the bundle's Mach-O (every cache key contains it);
  * replays every corpus description through the real frontend and compilers with
    that id (gpu/tools/build_pipeline_replay.py builds the binary; --replay reuses one);
  * independently recomputes each description's AIR key (gpu/metal/tegu/pipeline_keys.py)
    and requires the replay to have produced exactly that entry: a description the
    frontend turned into a different pipeline than the record says is reported, not packed
    silently;
  * packs every verified entry with make_pipeline_pack.py (build tag = the bundle's id);
  * with --logs, reports how many logged misses the pack serves (key prefix match) -
    meaningful only for logs of the very build the pack is made for.

Boundaries (read before shipping a pack)
  * A pack serves only descriptions in the corpus.  Constants of pipelines the corpus has
    not seen cannot be guessed; boot a candidate once (it logs TEGU_PSO_REC), `scan` the
    log into the corpus, rebuild the pack for the next candidate.
  * The entries are compiled by the HOST compilers (--air-lib/--mali-lib of the replay
    build).  They must be built from the same sources as the bundle's iOS archives; this
    tool records the host library hashes next to the pack (<pack>.json) but cannot prove
    equality (different targets).  Byte-equality of host and device compiler output is an
    assumption until a device boot serves a pack entry without error.
  * Packs are private-donor material (QuartzCore SPIR-V/Mali code): never publish.
  * Heavy job: one clang invocation (replay build) plus the replay itself.  Refuses to run
    while another compiler/linker/test job of this checkout is active (--ignore-busy to override;
    do not, under the thermal lease).
"""
import argparse
import hashlib
import json
import os
import subprocess
import sys
import zlib
from pathlib import Path

GPU = Path(__file__).resolve().parents[1]
TEGU = GPU / 'metal/tegu'
sys.path.insert(0, str(TEGU))
import make_pipeline_pack as mp  # noqa: E402
import pipeline_corpus as pc  # noqa: E402

BUSY = ('cc1', 'clang', 'rustc', 'cargo', 'ninja', 'ld.lld', 'ld', 'meson')


def busy_processes():
    me = os.getpid()
    out = subprocess.run(['ps', '-eo', 'pid,comm,args'], capture_output=True, text=True).stdout.splitlines()[1:]
    rows = []
    for line in out:
        pid, comm, args = line.split(None, 2)
        if int(pid) != me and comm in BUSY and 'gpu' in args:
            rows.append((pid, comm))
    return rows


def entries_of(cache_dirs, tag):
    """(kind, key) -> payload of every cache entry made for tag."""
    entries, skipped = {}, {}
    for d in cache_dirs:
        for f in sorted(Path(d).glob('k*-*.tpc')):
            entry, why = mp.read_entry(f, tag)
            if entry:
                entries[(entry[0], entry[1])] = entry[2]
            else:
                skipped[why] = skipped.get(why, 0) + 1
    return entries, skipped


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--bundle', type=Path, required=True, help='Nord2Metal.bundle (or its Mach-O): gives the build id')
    ap.add_argument('--corpus', type=Path, required=True, help='file of TPC_MISS v1 air records (pipeline_corpus.py scan --out)')
    ap.add_argument('--work', type=Path, required=True, help='persistent output directory (not /tmp)')
    ap.add_argument('--pack', type=Path, help='default <work>/PipelinePack.tpcpack')
    ap.add_argument('--replay', type=Path, help='pipeline-replay binary (default: build it from --tree into <work>/replay)')
    ap.add_argument('--tree', type=Path, default=GPU, help='gpu tree the bundle was built from (for building the replay)')
    ap.add_argument('--air-lib', type=Path, help='host libtegu_air_bridge.so directory')
    ap.add_argument('--mali-lib', type=Path, help='host libnord2_mali_runtime.so directory')
    ap.add_argument('--profile-dir', type=Path, help='directory of the bundle build\'s tegu-air-profile.h')
    ap.add_argument('--libraries', type=Path, default=GPU / 'build/ui-corpus-20261025/libraries', help='<sha256>.metallib files')
    ap.add_argument('--defines', default=os.environ.get('NORD2_FRONTEND_DEFINES', ''), help='frontend defines of the bundle build (variant.json frontend_defines; default $NORD2_FRONTEND_DEFINES, the variable build_tegu_frontend_objects.sh reads)')
    ap.add_argument('--logs', type=Path, nargs='*', default=[], help='console logs of the bundle build to measure coverage against')
    ap.add_argument('--ignore-busy', action='store_true')
    a = ap.parse_args()
    busy = busy_processes()
    if busy and not a.ignore_busy:
        sys.exit('another heavy job is running (%s); wait for the lease' % busy[:4])
    build = mp.bundle_build_id(a.bundle)
    tag = mp.build_tag(build)
    a.work = a.work.resolve()
    a.work.mkdir(parents=True, exist_ok=True)
    a.corpus, a.libraries, a.bundle = a.corpus.resolve(), a.libraries.resolve(), a.bundle.resolve()
    # The translator profile of THIS bundle (it is in every AIR key and in the replay's frontend).
    exe = a.bundle / 'Nord2Metal' if a.bundle.is_dir() else a.bundle
    profile = pc.bundle_profile(exe)
    inc = a.work / 'profile-inc'
    inc.mkdir(exist_ok=True)
    (inc / 'tegu-air-profile.h').write_text('static const unsigned char tegu_native_profile[]={' + ','.join(map(str, profile)) + '};\n')
    a.profile_dir = inc
    records = [l for l in a.corpus.read_text().splitlines() if l.startswith('TPC_MISS v1 air ')]
    mali_records = [l for l in a.corpus.read_text().splitlines() if l.startswith('TPC_MISS v1 mali ')]
    replay = a.replay
    if not replay:
        cmd = [sys.executable, str(GPU / 'tools/build_pipeline_replay.py'), '--tree', str(a.tree), '--out', str(a.work / 'replay')]
        for flag, value in (('--air-lib', a.air_lib), ('--mali-lib', a.mali_lib), ('--profile-dir', a.profile_dir), ('--defines', a.defines)):
            if value:
                cmd += [flag, str(value)]
        subprocess.run(cmd, check=True)
        replay = a.work / 'replay/pipeline-replay'
    binfo = Path(replay).parent / 'build.json'
    if a.replay and binfo.exists():
        want = hashlib.sha256((inc / 'tegu-air-profile.h').read_bytes()).hexdigest()
        if json.loads(binfo.read_text()).get('profile_sha256') != want:
            sys.exit('--replay binary was built with another translator profile than this bundle: rebuild it (omit --replay)')
    cache = a.work / 'cache'
    cache.mkdir(exist_ok=True)
    env = dict(os.environ, TEGU_PIPELINE_CACHE_BUILD_OVERRIDE=build, TEGU_PIPELINE_CACHE='1')
    log = a.work / 'replay.log'
    with log.open('w') as out:
        rc = subprocess.run(['nice', '-n', '10', str(replay), str(a.libraries), str(a.corpus), str(cache)], env=env, stdout=out,
                            stderr=subprocess.STDOUT).returncode
    summary = [l for l in log.read_text().splitlines() if l.startswith('REPLAY SUMMARY')]
    print('replay rc=%d %s' % (rc, summary[-1] if summary else '(no summary)'))
    print(' '.join(l for l in log.read_text().splitlines() if l.startswith('REPLAY MALI SUMMARY')) or 'no mali variants in the corpus')
    if rc:
        sys.exit('replay failed; see %s' % log)
    dirs = sorted(p for p in [*cache.glob('r*/com.tegu.metal.pipelines'), *cache.glob('m*/com.tegu.metal.pipelines')])
    entries, skipped = entries_of(dirs, tag)
    # Oracle check: the AIR entry each record describes must exist, byte for byte keyed as predicted.
    rows, missing = [], []
    for i, line in enumerate(records):
        p, _ = pc.parse_record(line)
        want = p.key(build, profile)
        ok = (1, want) in entries
        rows.append({'record': i, 'vertex': p.vertex, 'fragment': p.fragment, 'air_key16': want.hex()[:16], 'reproduced': ok})
        if not ok:
            missing.append(i)
    air = sum(1 for k in entries if k[0] == 1)
    mali = sum(1 for k in entries if k[0] == 2)
    print('records %d, AIR key predicted and produced %d, mismatching %d; cache entries air=%d mali=%d skipped=%s' %
          (len(records), len(records) - len(missing), len(missing), air, mali, skipped))
    if missing:
        print('  records whose predicted AIR key was not produced (replay built a different pipeline):', missing[:20])
    pack = a.pack or a.work / 'PipelinePack.tpcpack'
    subprocess.run([sys.executable, str(TEGU / 'make_pipeline_pack.py'), '--bundle', str(a.bundle), '--out', str(pack),
                    *[str(d) for d in dirs]], check=True, stdout=subprocess.DEVNULL)
    blob = pack.read_bytes()
    manifest = {'pack': str(pack), 'bytes': len(blob), 'zlib6_bytes': len(zlib.compress(blob, 6)), 'sha256': hashlib.sha256(blob).hexdigest(),
                'build': build, 'corpus': str(a.corpus), 'corpus_records': len(records), 'corpus_mali_variants': len(mali_records), 'air_entries': air, 'mali_entries': mali,
                'oracle_mismatch_records': missing, 'host_libs': {}, 'replay_sha256': hashlib.sha256(Path(replay).read_bytes()).hexdigest()}
    info = a.work / 'replay/build.json'
    if info.exists():
        manifest['replay_build'] = json.loads(info.read_text())
    (Path(str(pack) + '.json')).write_text(json.dumps(manifest, indent=1))
    print(json.dumps({k: manifest[k] for k in ('pack', 'bytes', 'zlib6_bytes', 'air_entries', 'mali_entries', 'sha256')}, indent=1))
    if a.logs:
        subprocess.run([sys.executable, str(GPU / 'tools/pipeline_cache_report.py'), *map(str, a.logs), '--pack', str(pack)], check=False)
    if missing:
        sys.exit(3)


if __name__ == '__main__':
    main()
