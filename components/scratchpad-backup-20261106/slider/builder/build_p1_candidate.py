#!/usr/bin/env python3
"""Private offline kernel relink: shared-UI baseline (kernel 5cd40b93) with optional kernel-p1 patches.

mode baseline : recompile the GPU objects from the unmodified xnu-stage and relink exactly like
                tools/build_mmu_candidate.py (proves the toolchain/inputs are reproducible).
mode patched  : same, but nord2_gpu_core.c, Nord2GPU.cpp, SweetFramebuffer.cpp are compiled from a
                scratch copy of the stage with xnu patches 0085, 0086, 0087 applied in order;
                SweetFramebuffer.cpo is replaced in a COPY of the startup archive.
Never touches gpu/build/xnu-stage, gpu/src, shim/, xnu/patches, existing objects or any device.
"""
import argparse, hashlib, json, re, shutil, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]          # .../work
P1 = ROOT/'gpu/build/kernel-p1-20261026'
STAGE = ROOT/'gpu/build/xnu-stage-backup-pre-p15-20261028/xnu-stage'
AR = '$HOME/Documents/Codex/2026-10-13/por/work/huawei/command-line-tools/sdk/default/openharmony/native/llvm/bin/llvm-ar'
ap = argparse.ArgumentParser()
ap.add_argument('--mode', choices=['baseline', 'patched'], required=True)
ap.add_argument('--out', type=Path, required=True)
ap.add_argument('--with-0088', action='store_true', help='also apply 0088 (P2)')
ap.add_argument('--series', default='', help='comma list of extra patch numbers after 0087, e.g. 0088,0089,0090,0091 (replaces --with-0088)')
ap.add_argument('--recompile-power', action='store_true', help='recompile nord2_gpu_board/clk from the patched tree (needed by 0091); rail is always recompiled')
ap.add_argument('--name', default='', help='kernel name suffix')
ap.add_argument('--cpufreq', action='store_true', help='patch 0104: compile bsd/dev/nord2_cpufreq.c from the patched tree with the CI flags (compile_0104.py) and link it')
ap.add_argument('--sched', choices=['none', 'a', 'b'], default='none', help='patch 0099 (ansuz-port): a = nord2_sched.o + nord2_sched_sysctl.o (measurement), b = also replace archive members sched_clutch.o + sched_prim.o (placement)')
ap.add_argument('--patch-dir', type=Path, action='append', default=[], help='extra directory with NNNN-*.patch and patched-sources-NNNN (searched after kernel-p1)')
ap.add_argument('--baseband', action='store_true', help='patch 0277: compile iokit/Kernel/arm/Nord2Baseband.cpp (NORD2 BB, CommCenter/ABM bridge) from the patched tree and link it; needs 0277 in --series and 0200 for sweet_amfi.c')
a = ap.parse_args()
out = a.out.resolve()
assert not out.exists(), 'refuse to reuse an output directory'
out.mkdir(parents=True)
objs = out/'objs'; objs.mkdir()
PATCH_DIRS = [P1] + [d.resolve() for d in a.patch_dir]

def sha(p):
    with Path(p).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()

def run(label, argv, **kw):
    with (out/(label+'.log')).open('w') as log:
        subprocess.run([str(x) for x in argv], cwd=kw.get('cwd', ROOT), stdout=log, stderr=log, check=True)

def compile_(src, obj, cxx):
    run(obj.stem+'-compile', ['nice', '-n', '10', 'python3', ROOT/'gpu/tools/xnu_staged_compile.py', src, '--out', obj,
                              '--extra=-Wno-unsafe-buffer-usage'] + (['--cxx'] if cxx else []))

# ---- source tree for the three objects that change
if a.mode == 'patched':
    sc = out/'stage'
    (sc/'bsd/dev').mkdir(parents=True); (sc/'iokit/Kernel/arm').mkdir(parents=True)
    for f in (STAGE/'bsd/dev').iterdir():
        if f.is_file(): shutil.copy2(f, sc/'bsd/dev'/f.name)
    (sc/'bsd/kern').mkdir(parents=True); shutil.copy2(STAGE/'bsd/kern/sweet_amfi.c', sc/'bsd/kern/sweet_amfi.c')      # 0200
    (sc/'bsd/conf').mkdir(parents=True)
    shutil.copy2(STAGE/'bsd/conf/files', sc/'bsd/conf/files')
    for f in (STAGE/'iokit/Kernel/arm').iterdir():
        if f.is_file(): shutil.copy2(f, sc/'iokit/Kernel/arm'/f.name)
    shutil.copytree(STAGE/'iokit/Kernel/arm/hid', sc/'iokit/Kernel/arm/hid')                           # 0279 patches hid/SweetHID.cpp
    series = ['0085', '0086', '0087'] + (['0088'] if a.with_0088 else []) + [x for x in a.series.split(',') if x]
    for num in series:
        patch = sorted(x for d in PATCH_DIRS for x in d.glob(num+'-*.patch'))
        assert len(patch) == 1, (num, patch)
        run('apply-'+num, ['patch', '-p1', '-i', patch[0]], cwd=sc)
    top = series[-1]
    def pnum(d):
        m = re.search(r'patched-sources-(\d+)$', str(d)); return int(m.group(1)) if m else 0
    dirs = sorted([d for pd in PATCH_DIRS for d in pd.glob('patched-sources*') if d.is_dir() and pnum(d) <= int(top)], key=pnum, reverse=True)
    for rel in ['bsd/dev/nord2_gpu_core.c', 'bsd/dev/nord2_gpu_core.h', 'bsd/dev/nord2_gpu_transport.h',
                'iokit/Kernel/arm/Nord2GPU.cpp', 'bsd/dev/nord2_gpu_board.c', 'bsd/dev/nord2_gpu_board.h',
                'bsd/dev/nord2_gpu_clk.c', 'bsd/dev/nord2_gpu_clk.h', 'bsd/dev/nord2_gpu_rail.c', 'bsd/dev/nord2_gpu_rail.h',
                'bsd/dev/nord2_gpu_irq.h', 'bsd/dev/nord2_gpu_lvts.h', 'bsd/dev/nord2_gpu_lvts.c', 'bsd/conf/files',
                'iokit/Kernel/arm/SweetSurface.cpp', 'iokit/Kernel/arm/SweetSurface.h', 'iokit/Kernel/arm/SweetFramebuffer.cpp',
                'iokit/Kernel/arm/sweet_fb_present.h', 'iokit/Kernel/arm/sweet_surface_abi.h',
                'iokit/Kernel/arm/sweet_fb_scan.h', 'iokit/Kernel/arm/sweet_fb_swapwait.h',
                'bsd/dev/nord2_gpu_va.h', 'bsd/dev/nord2_gpu_va.c', 'bsd/dev/nord2_cpufreq.h', 'bsd/dev/nord2_cpufreq.c',
                'bsd/dev/nord2_gpu_mmu.c', 'bsd/dev/nord2_gpu_mmu.h', 'bsd/kern/sweet_amfi.c',
                'bsd/kern/nord2_bb_kpi.h', 'iokit/Kernel/arm/Nord2Baseband.cpp', 'iokit/Kernel/arm/nord2_bb_qmux.h',
                'iokit/Kernel/arm/nord2_bb_qmi.h', 'iokit/Kernel/arm/qcombb_channels.h', 'iokit/Kernel/arm/qcombb_control.h',
                'iokit/Kernel/arm/qcombb_sahara.h', 'iokit/Kernel/arm/qcombb_data.h']:
        for d in dirs:
            if (d/rel).exists():
                assert sha(sc/rel) == sha(d/rel), (rel, d)
                break
        else:
            assert sha(sc/rel) == sha(STAGE/rel) or not (STAGE/rel).exists() and not (sc/rel).exists(), rel      # untouched by the series
    srcroot = sc
else:
    srcroot = STAGE

# ---- objects
names = ['nord2_gpu_desc', 'nord2_gpu_jm', 'nord2_gpu_mem', 'nord2_gpu_owner', 'nord2_gpu_pmif', 'nord2_gpu_rail',
         'nord2_gpu_render_plan', 'nord2_gpu_sched', 'nord2_gpu_scpsys', 'nord2_gpu_va', 'nord2_gpu_mmu']
for n in names:                                    # never changed by the patches: always from the pristine stage
    compile_((srcroot if n in ('nord2_gpu_rail', 'nord2_gpu_va', 'nord2_gpu_mmu') else STAGE)/'bsd/dev'/(n+'.c'), objs/(n+'.o'), False)   # rail changes in 0091, the VA table size (header) in 0105
power = ['nord2_gpu_board', 'nord2_gpu_clk'] if a.recompile_power else []
for n in power:
    compile_(srcroot/'bsd/dev'/(n+'.c'), objs/(n+'.o'), False)
compile_(srcroot/'bsd/dev/nord2_gpu_core.c', objs/'nord2_gpu_core.o', False)
lvts = ['nord2_gpu_lvts'] if a.mode == 'patched' and (srcroot/'bsd/dev/nord2_gpu_lvts.c').exists() else []
for n in lvts:
    compile_(srcroot/'bsd/dev'/(n+'.c'), objs/(n+'.o'), False)
compile_(srcroot/'iokit/Kernel/arm/Nord2GPU.cpp', objs/'Nord2GPU.o', True)

# ---- patch 0099 objects (built by ansuz-port with compile_0099.py; hashes checked against its compile-result.json)
S99 = ROOT/'gpu/build/ansuz-port-20261026/0099'
sched_new = []
cpufreq_new = []
if a.cpufreq:
    assert a.mode == 'patched'
    run('nord2_cpufreq-compile', ['python3', P1/'compile_0104.py', srcroot/'bsd/dev/nord2_cpufreq.c', objs/'nord2_cpufreq.o'])
    cpufreq_new.append('nord2_cpufreq')
if a.sched != 'none':
    res = json.loads((S99/'objs/compile-result.json').read_text())
    want = {'nord2_sched.o': 'nord2_sched.c', 'nord2_sched_sysctl.o': 'nord2_sched_sysctl.c', 'sched_clutch.o': 'sched_clutch.c', 'sched_prim.o': 'sched_prim.c'}
    for o, c in want.items():
        assert res[c]['rc'] == 0 and res[c]['errors'] == 0 and sha(S99/'objs'/o) == res[c]['object_sha256'], o
    for o in ['nord2_sched.o', 'nord2_sched_sysctl.o']:
        shutil.copy2(S99/'objs'/o, objs/o); sched_new.append(o[:-2])

# ---- patch 0277: the NORD2 BB IOKit bridge, a new object linked before Nord2GPU.o (its classes register through the kernel's own
#      C++ static initialization, OSlibkernInit; its static constructor installs sweet_amfi.c's nord2_bb_hooks)
bb_new = []
if a.baseband:
    assert a.mode == 'patched' and '0277' in series and '0200' in series, 'needs --series ...,0200,...,0277'
    compile_(srcroot/'iokit/Kernel/arm/Nord2Baseband.cpp', objs/'Nord2Baseband.o', True)
    bb_new.append('Nord2Baseband')

# ---- archive (SweetFramebuffer.cpo only in patched mode)
base_cmd = json.loads((ROOT/'gpu/build/mmu-shared-candidate/link-command.json').read_text())
archive_old = next(x for x in base_cmd if x.endswith('/startup-diagnostic/kernel.startup.a'))
archive = out/'kernel.startup.a'
shutil.copyfile(archive_old, archive)
if a.mode == 'patched':
    replace = ['SweetFramebuffer']
    if '0096' in series: replace.append('SweetSurface')
    members = subprocess.check_output([AR, 't', str(archive)], text=True).splitlines()
    for n in replace:
        compile_(srcroot/'iokit/Kernel/arm'/(n+'.cpp'), out/(n+'.cpo'), True)
        assert members.count(n+'.cpo') == 1
    run('ar-replace', [AR, 'r', archive] + [out/(n+'.cpo') for n in replace])
    if '0200' in series:
        assert members.count('sweet_amfi.o') == 1
        run('sweet_amfi-compile', ['nice', '-n', '10', 'python3', ROOT/'gpu/tools/xnu_staged_compile.py', srcroot/'bsd/kern/sweet_amfi.c', '--out', out/'sweet_amfi.o',
                                   '--extra=-Wno-unsafe-buffer-usage', '--extra=-Wno-strlcpy-strlcat-size'])
        run('ar-replace-amfi', [AR, 'r', archive, out/'sweet_amfi.o'])
    if a.sched == 'b':
        for o in ['sched_clutch.o', 'sched_prim.o']:
            assert members.count(o) == 1
            shutil.copy2(S99/'objs'/o, out/o)
        run('ar-replace-sched', [AR, 'r', archive, out/'sched_clutch.o', out/'sched_prim.o'])
    if '0278' in series:
        # 0278: the startup archive's sweet_touch.o was compiled from the Metal stage with the 23 Oct 16:27
        # audio_hw_native.inc (no nah_native_publish_ready, prepare returns -1 before input maps exist, no GIC
        # read), while audio_device.o/audio_hw.o are the 24 Oct CI objects that wait for NAH_NATIVE_WAIT.
        # Recompile it from the patched tree; the big-window movz patch below then applies to this object.
        assert members.count('sweet_touch.o') == 1
        run('sweet_touch-compile', ['nice', '-n', '10', 'python3', ROOT/'gpu/tools/xnu_staged_compile.py', srcroot/'bsd/dev/sweet_touch.c', '--out', out/'sweet_touch.o',
                                    '--extra=-Wno-unsafe-buffer-usage'])
        run('ar-replace-sweet_touch', [AR, 'r', archive, out/'sweet_touch.o'])
    if '0279' in series:
        # 0279: alert slider -> iOS ring/silent switch. The slider code is in sweet_touch.c (nord2_slider.inc),
        # which only the 0278 branch above recompiles; SweetHID.cpo gets the Telephony/Ring usage and copyEvent.
        assert '0278' in series and series.index('0279') > series.index('0278'), 'needs 0278 before 0279'
        assert members.count('SweetHID.cpo') == 1
        compile_(srcroot/'iokit/Kernel/arm/hid/SweetHID.cpp', out/'SweetHID.cpo', True)
        run('ar-replace-SweetHID', [AR, 'r', archive, out/'SweetHID.cpo'])

# ---- big window: the Wi-Fi layout checks compare boot_args.memSize with 0x3e000000 (movz w9, #0x3e00, lsl #16); patch the two archive members in the archive copy
import struct, tempfile
BIG_MEM = 0xec000000
if BIG_MEM:
    tmpd = Path(tempfile.mkdtemp(prefix='bigwin-', dir=out))
    for member in ('wifi_rom_xnu.o', 'sweet_touch.o'):
        run('ar-x-'+member, [AR, 'x', archive, member], cwd=tmpd)
        data = bytearray((tmpd/member).read_bytes())
        n = 0
        for i in range(0, len(data) - 3, 4):
            w = struct.unpack_from('<I', data, i)[0]
            if (w & 0xffffffe0) == 0x52a7c000:                       # movz wN, #0x3e00, lsl #16
                struct.pack_into('<I', data, i, (w & 0x1f) | 0x52a00000 | ((BIG_MEM >> 16) << 5)); n += 1
        assert n == 2, (member, n)
        (tmpd/member).write_bytes(bytes(data))
        run('ar-r-'+member, [AR, 'r', archive, tmpd/member], cwd=tmpd)
    print('big window patched: wifi_rom_xnu.o, sweet_touch.o ->', hex(BIG_MEM))

# ---- link
cmd = []
mapping = {n+'.o': str(objs/(n+'.o')) for n in names + power}
mapping.update({'nord2_gpu_core.o': str(objs/'nord2_gpu_core.o'), 'Nord2GPU.o': str(objs/'Nord2GPU.o')})
kernel = out/('kernel.development.nord2-metal-'+(a.name or ('p2' if a.with_0088 else 'p1'))) if a.mode == 'patched' else out/'kernel.development.nord2-metal-shared'
for x in base_cmd:
    if x == archive_old: cmd.append(str(archive))
    elif Path(x).name in {k for k in mapping} and x.endswith('.o') and ('/objs/' in x or 'candidate/' in x):
        cmd.append(mapping[Path(x).name])
    else: cmd.append(x)
cmd[cmd.index('-o')+1] = str(kernel)
for n in lvts + sched_new + cpufreq_new + bb_new:                      # new objects: insert before Nord2GPU.o
    cmd.insert(cmd.index(str(objs/'Nord2GPU.o')), str(objs/(n+'.o')))
replaced = [x for x in cmd if x.startswith(str(objs))]
assert len(replaced) == len(names)+len(power)+len(lvts)+len(sched_new)+len(cpufreq_new)+len(bb_new)+2, replaced
run('link', ['nice', '-n', '10'] + cmd)
(out/'link-command.json').write_text(json.dumps(cmd, indent=2)+'\n')
rec = {'mode': a.mode, 'kernel': str(kernel), 'kernel_sha256': sha(kernel), 'bytes': kernel.stat().st_size,
       'objects': {p.name: sha(p) for p in sorted(objs.glob('*.o'))}, 'archive_sha256': sha(archive),
       'baseline_kernel_sha256': '5cd40b9380a3f278455505f92cbc5ee4f2efe8807c538e67468f25e84101c3a4',
       'sched_0099': a.sched, 'cpufreq_0104': a.cpufreq, 'baseband_0277': a.baseband, 'device_tested': False}
if a.sched != 'none':
    rec['sched_objects'] = {o: sha(S99/'objs'/o) for o in ['nord2_sched.o', 'nord2_sched_sysctl.o', 'sched_clutch.o', 'sched_prim.o']}
if a.mode == 'patched':
    for n in replace: rec[n+'.cpo'] = sha(out/(n+'.cpo'))
    if '0278' in series: rec['sweet_touch.o'] = sha(out/'sweet_touch.o')
    if '0279' in series: rec['SweetHID.cpo'] = sha(out/'SweetHID.cpo')
(out/'receipt.json').write_text(json.dumps(rec, indent=2)+'\n')
print(json.dumps({k: v for k, v in rec.items() if k != 'objects'}, indent=2))
