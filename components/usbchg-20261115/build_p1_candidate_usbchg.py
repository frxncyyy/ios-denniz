#!/usr/bin/env python3
"""integ-20261114/build_p1_candidate_integ.py = wifi-sae-20261113/build_p1_candidate_sae.py (0430 members, 0432..0434 SAE members, 0436 binary
patch) + the 0450/0451 802.1X objects of wifi8021x-20261113/build_p1_candidate_wifi8021x.py + optional N2_OPT size flags (default none).

Private offline kernel relink: shared-UI baseline (kernel 5cd40b93) with optional kernel-p1 patches.

mode baseline : recompile the GPU objects from the unmodified xnu-stage and relink exactly like
                tools/build_mmu_candidate.py (proves the toolchain/inputs are reproducible).
mode patched  : same, but nord2_gpu_core.c, Nord2GPU.cpp, SweetFramebuffer.cpp are compiled from a
                scratch copy of the stage with xnu patches 0085, 0086, 0087 applied in order;
                SweetFramebuffer.cpo is replaced in a COPY of the startup archive.
Never touches gpu/build/xnu-stage, gpu/src, shim/, xnu/patches, existing objects or any device.
"""
import os, argparse, hashlib, json, re, shutil, subprocess, sys
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
ap.add_argument('--hwmodel-default', default='', help='patch 0291 (gpu/build/hwmodel-20261110): value of n2hwmodel when the boot-arg is absent, e.g. Camera=N104AP')
ap.add_argument('--applog-name-default', default='', help='patch 0285 (gpu/build/applog-20261110): value of applog_name when the boot-arg is absent (the Nord 2 command line is full)')
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

# integ-20261114 (q10jia): size optimisation per object (boot_spare budget).  N2_OPT="Stem=-Os,..." overrides; "none" = all CI flags.
OPT = {}      # filled in below after measurement (OPT_DEFAULT)
OPT_DEFAULT = {}
_env = os.environ.get('N2_OPT')
OPT = {} if _env == 'none' else (dict(x.split('=', 1) for x in _env.split(',') if x) if _env else dict(OPT_DEFAULT))
print('size flags (integ-20261114):', OPT or 'none')
def opt_extra(stem):
    return ['--extra=' + OPT[stem]] if stem in OPT else []

def compile_(src, obj, cxx):
    run(obj.stem+'-compile', ['nice', '-n', '10', 'python3', ROOT/'gpu/tools/xnu_staged_compile.py', src, '--out', obj,
                              '--extra=-Wno-unsafe-buffer-usage'] + opt_extra(obj.stem) + (['--cxx'] if cxx else []))

# ---- source tree for the three objects that change
if a.mode == 'patched':
    sc = out/'stage'
    (sc/'bsd/dev').mkdir(parents=True); (sc/'iokit/Kernel/arm').mkdir(parents=True)
    for f in (STAGE/'bsd/dev').iterdir():
        if f.is_file(): shutil.copy2(f, sc/'bsd/dev'/f.name)
    (sc/'bsd/kern').mkdir(parents=True); shutil.copy2(STAGE/'bsd/kern/sweet_amfi.c', sc/'bsd/kern/sweet_amfi.c')      # 0200
    shutil.copy2(STAGE/'bsd/kern/kern_exec.c', sc/'bsd/kern/kern_exec.c')                              # 0285 (applog)
    shutil.copy2(STAGE/'bsd/kern/proc_info.c', sc/'bsd/kern/proc_info.c')                              # 0297 (foreground hw for applications)
    shutil.copy2(STAGE/'bsd/kern/kern_mib.c', sc/'bsd/kern/kern_mib.c')                                # 0291 (hw.model per process)
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
S99 = ROOT/'gpu/build/cpu-20261114/sched0099'   # cpu-20261114 0642: prime-first nord2_sched.o (copy of ansuz-port 0099, only nord2_sched.c recompiled)
sched_new = []
cpufreq_new = []
if a.cpufreq:
    assert a.mode == 'patched'
    run('nord2_cpufreq-compile', ['python3', ROOT/'gpu/build/cpu-20261114/compile_0104_integ.py', srcroot/'bsd/dev/nord2_cpufreq.c', objs/'nord2_cpufreq.o'] + [OPT[k] for k in ['nord2_cpufreq'] if k in OPT])
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
call_new = []                                       # 0280: call audio (bsd/dev/audio_call.c), a new object
if a.mode == 'patched' and '0280' in series:
    assert '0278' in series and series.index('0280') > series.index('0278'), 'needs 0278 before 0280 (sweet_touch.o recompile)'
    assert '0281' in series and '0277' in series and a.baseband, '0280 needs 0281 (bb hooks in sweet_amfi.c/Nord2Baseband.cpp) and --baseband'
    compile_(srcroot/'bsd/dev/audio_call.c', objs/'audio_call.o', False)
    call_new.append('audio_call')
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
    if '0296' in series:
        # 0296 (gpu/build/autotest): sweet_usb.c calls nat_usb where it called nal_usb. The archive's sweet_usb.o is a CI object
        # that nothing here recompiles: the one undefined symbol is renamed in a copy of the member (same length), which is
        # exactly what the one-line source change of the patch compiles to. nat_usb itself is in sweet_touch.o (0278 branch).
        assert '0278' in series and members.count('sweet_usb.o') == 1
        t296 = out/'usb-0296'; t296.mkdir()
        run('ar-x-sweet_usb', [AR, 'x', archive, 'sweet_usb.o'], cwd=t296)
        blob = (t296/'sweet_usb.o').read_bytes()
        assert blob.count(b'\0_nal_usb\0') == 1 and b'_nat_usb' not in blob      # (the other nal_usb is DWARF: the declaration's name)
        (t296/'sweet_usb.o').write_bytes(blob.replace(b'\0_nal_usb\0', b'\0_nat_usb\0'))
        run('ar-r-sweet_usb', [AR, 'r', archive, t296/'sweet_usb.o'], cwd=t296)
    if '0280' in series:
        # 0280: the call state machine lives in the audio worker; the archive's audio_device.o is the 24 Oct CI object.
        assert members.count('audio_device.o') == 1
        run('audio_device-compile', ['nice', '-n', '10', 'python3', ROOT/'gpu/tools/xnu_staged_compile.py', srcroot/'bsd/dev/audio_device.c',
                                     '--out', out/'audio_device.o', '--extra=-Wno-unsafe-buffer-usage'])
        run('ar-replace-audio_device', [AR, 'r', archive, out/'audio_device.o'])
    if '0282' in series:
        # 0282: in-place RedwoodAudio package upgrade (NORD2_AUDIO_UPGRADE=1); the archive's audio_install.o is the 23 Oct CI object.
        assert members.count('audio_install.o') == 1
        run('audio_install-compile', ['nice', '-n', '10', 'python3', ROOT/'gpu/tools/xnu_staged_compile.py', srcroot/'bsd/dev/audio_install.c',
                                      '--out', out/'audio_install.o', '--extra=-Wno-unsafe-buffer-usage'])
        run('ar-replace-audio_install', [AR, 'r', archive, out/'audio_install.o'])
    if '0330' in series:
        # 0330 (gpu/build/i2c8cam-20261111): the CI members audio_i2c8.o / audio_hw.o refuse the amplifiers while the CAM
        # MTCMOS is on (the camera keeps it on after its first session): three tbnz #23 become nops (exact CI member sha256,
        # word and context checked). The pipeline never recompiles these two (the stage .c files are older than the CI objects).
        a330 = next(d for d in PATCH_DIRS if (d/'patch_audio_objs_0330.py').exists())
        run('audio-objs-0330', ['python3', a330/'patch_audio_objs_0330.py', AR, archive, out/'audio-0330'])

    if '0285' in series:
        # 0285: applog / applog_name. kern_exec.o and mem.o of the archive are CI objects of the pristine stage sources.
        # kern_exec.c uses kalloc_type: compile_0285.py reproduces the typed-allocator views of the CI object and
        # fails if they differ (the registered-ports array allocated there is freed by mach_ports_register in ipc_tt.o).
        APPLOG = ROOT/'gpu/build/applog-20261110'
        assert members.count('kern_exec.o') == 1 and members.count('mem.o') == 1
        orig = out/'orig-0285'; orig.mkdir()
        run('ar-x-kern_exec', [AR, 'x', archive_old, 'kern_exec.o'], cwd=orig)
        run('kern_exec-compile', ['python3', APPLOG/'compile_0285.py', srcroot/'bsd/kern/kern_exec.c', orig/'kern_exec.o', out/'kern_exec.o',
                                  '--name-default', a.applog_name_default, '--include-dir', STAGE/'bsd/kern'])
        assert json.loads((out/'kern_exec.o.views.json').read_text())['equal'] is True
        run('mem-compile', ['nice', '-n', '10', 'python3', ROOT/'gpu/tools/xnu_staged_compile.py', srcroot/'bsd/dev/mem.c', '--out', out/'mem.o',
                            '--extra=-Wno-unsafe-buffer-usage'])
        run('ar-replace-applog', [AR, 'r', archive, out/'kern_exec.o', out/'mem.o'])
        shutil.rmtree(orig)
    else:
        assert not a.applog_name_default

    if '0291' in series:
        # 0291: hw.model per process name. kern_mib.o of the archive is the CI object of the pristine stage source (no typed
        # allocations in it); -Wno-sign-compare / -Wno-void-pointer-to-int-cast are for the pristine code under the local clang.
        assert members.count('kern_mib.o') == 1 and re.fullmatch(r'[A-Za-z0-9_.,=-]*', a.hwmodel_default), a.hwmodel_default
        run('kern_mib-compile', ['nice', '-n', '10', 'python3', ROOT/'gpu/tools/xnu_staged_compile.py', srcroot/'bsd/kern/kern_mib.c', '--out', out/'kern_mib.o',
                                 '--extra=-Wno-unsafe-buffer-usage', '--extra=-Wno-sign-compare', '--extra=-Wno-void-pointer-to-int-cast',
                                 '--extra=-DNORD2_HWMODEL_DEFAULT="%s"' % a.hwmodel_default, '--extra=-I' + str(STAGE/'bsd/kern')])
        run('ar-replace-kern_mib', [AR, 'r', archive, out/'kern_mib.o'])
    else:
        assert not a.hwmodel_default

if a.mode == 'patched' and '0297' in series:
    # 0297 (gpu/build/autotest/fghw, from gpu/build/photo-fghw-20261111): proc_can_use_foreground_hw grants applications that are not
    # marked background. proc_info.o of the archive is the CI object of the pristine stage source. It has no typed-allocator views
    # (no __kalloc_type / __kalloc_var section: checked below on the CI object and on the new one), so the plain staged compile of
    # kern_mib.c is enough.
    assert members.count('proc_info.o') == 1 and sha(sc/'bsd/kern/proc_info.c') != sha(STAGE/'bsd/kern/proc_info.c')
    orig = out/'orig-0297'; orig.mkdir()
    run('ar-x-proc_info', [AR, 'x', archive_old, 'proc_info.o'], cwd=orig)
    run('proc_info-compile', ['nice', '-n', '10', 'python3', ROOT/'gpu/tools/xnu_staged_compile.py', srcroot/'bsd/kern/proc_info.c', '--out', out/'proc_info.o',
                              '--extra=-Wno-unsafe-buffer-usage', '--extra=-Wno-sign-compare', '--extra=-Wno-void-pointer-to-int-cast',
                              '--extra=-Wno-unused-but-set-variable', '--extra=-Wno-cast-align', '--extra=-I' + str(STAGE/'bsd/kern')])
    OBJDUMP = str(Path(AR).parent/'llvm-objdump')
    for o in (orig/'proc_info.o', out/'proc_info.o'):
        heads = subprocess.check_output([OBJDUMP, '-h', str(o)], text=True)
        assert '__kalloc_type' not in heads and '__kalloc_var' not in heads, o
    run('ar-replace-proc_info', [AR, 'r', archive, out/'proc_info.o'])
    shutil.rmtree(orig)

# ---- 0700..0702 (gpu/build/usbchg-20261115): sweet_usb.o, SweetBattery.cpo, nord2_battery.o, nord2_battery_platform.cpo and
#      mtk_i2c_read.o are CI members of the startup archive that nothing else recompiles.  Recompiling the UNMODIFIED final13 sources
#      with the staged CI flags gives the same global symbol set as the CI members (only sweet_usb.o differs by the 0296 rename, which
#      the source already contains), checked 2026-11-15.  They are rebuilt from the patched tree and replaced in the archive copy.
#      Own-source warnings are capped at the unmodified-source counts (logs are named *-build.log: no reference log exists).
USBCHG = {'0700': [('bsd/dev/sweet_usb.c', 'sweet_usb.o', False, 0)],
          '0701': [('iokit/Kernel/arm/nord2_battery.c', 'nord2_battery.o', False, 0), ('iokit/Kernel/arm/mtk_i2c_read.c', 'mtk_i2c_read.o', False, 0),
                   ('iokit/Kernel/arm/nord2_battery_platform.cpp', 'nord2_battery_platform.cpo', True, 5),
                   ('iokit/Kernel/arm/SweetBattery.cpp', 'SweetBattery.cpo', True, 9)]}
USBCHG['0702'] = USBCHG['0701']
USBCHG['0703'] = USBCHG['0701']   # 0703: fast VBUS edge (SweetBattery.cpp, nord2_battery_platform.cpp, nord2_battery.c)
usbchg_new = []
if a.mode == 'patched' and any(n in series for n in USBCHG):
    assert '0700' in series, '0701/0702 need 0700 (nord2_chg_vbus_good, nord2_usb_* live in sweet_usb.c)'
    assert '0702' not in series or '0701' in series, '0702 needs 0701'
    assert '0703' not in series or '0702' in series, '0703 needs 0702'
    done = set()
    for num in ('0700', '0701', '0702', '0703'):
        if num not in series: continue
        for rel, mem, cxx, cap in USBCHG[num]:
            if mem in done: continue
            done.add(mem)
            assert members.count(mem) == 1, mem
            label = 'usbchg-' + mem.split('.')[0]
            run(label + '-build', ['nice', '-n', '10', 'python3', ROOT/'gpu/tools/xnu_staged_compile.py', srcroot/rel, '--out', out/mem,
                                   '--extra=-Wno-unsafe-buffer-usage'] + (['--cxx'] if cxx else []))
            stem = Path(rel).name
            own = sum(1 for l in (out/(label + '-build.log')).read_text(errors='replace').splitlines() if 'warning:' in l and ('/' + stem + ':') in l)
            assert own <= cap, '%s: %d own-source warnings > %d (unmodified source)' % (rel, own, cap)
            run('ar-replace-' + label, [AR, 'r', archive, out/mem])
            usbchg_new.append('%s(%d warn)' % (mem, own))
    print('usbchg members rebuilt:', ' '.join(usbchg_new))

# ---- 0430/0431 (gpu/build/wifi-20261113): the Wi-Fi C members of the startup archive that these patches change are CI objects
#      nothing else recompiles. They are rebuilt from the patched tree with the staged CI flags and replaced in the archive copy.
#      The Wi-Fi binders (wifi_*_bind.inc) are part of sweet_touch.o, which the 0278 branch above already recompiles.
WIFI_MEMBERS = {'0430': ['wifi_ios_join', 'wifi_ios_policy'], '0431': ['wifi_normal_transport', 'wifi_normal_rx']}   # 0431 = diag-q10jhh only (q10jhh);   # q10jgw2: wifi_normal_transport/rx stay the CI members (q10jgw regression)
wifi_new = []
if a.mode == 'patched':
    for num, mem in WIFI_MEMBERS.items():
        if num not in series: continue
        assert '0278' in series, 'needs the sweet_touch.o recompile (0278)'
        for n in mem:
            assert members.count(n+'.o') == 1, n
            assert sha(sc/'bsd/dev'/(n+'.c')) != sha(STAGE/'bsd/dev'/(n+'.c')), (n, 'not changed by the series')
            compile_(srcroot/'bsd/dev'/(n+'.c'), out/(n+'.o'), False)
            run('ar-replace-'+n, [AR, 'r', archive, out/(n+'.o')])
            wifi_new.append(n)
    if wifi_new: print('wifi members rebuilt:', ' '.join(wifi_new))

# ---- 0432..0434 (gpu/build/wifi-sae-20261113): WPA3-Personal (SAE), PSK-SHA256, PMF negotiation.
#      New archive members (never in CI): wifi_sae, wifi_sae_crypto (0432), wifi_wpa3, wifi_station_wire2 (0433), compiled -Oz
#      (boot_spare). CI members that MUST change and are rebuilt from the patched tree: wifi_station_transaction (-Oz), wifi_station_wire
#      (0433), wifi_ios_card (0434); wifi_ios_join (already rebuilt by the 0430 branch) is rebuilt again with -Oz. Everything else of the
#      Wi-Fi stack stays the prebuilt CI member: wifi_wpa2, wifi_wpa_crypto, wifi_wpa_owner, wifi_wpa_nst, wifi_station_transport,
#      wifi_normal_transport, wifi_normal_rx. The binders are not patched; sweet_touch.o must come out as in the reference build
#      (wifi-20261113/check_scan_path.py). wifi_sae_crypto.c includes BearSSL as "../../libkern/..." like the CI crypto member: the
#      pristine xnu-stage bsd/dev directory is added as an include directory for that path only.
SAE_NEW = {'0432': ['wifi_sae', 'wifi_sae_crypto'], '0433': ['wifi_wpa3', 'wifi_station_wire2']}
SAE_REBUILT = {'0433': [('wifi_station_transaction', ['--extra=-Oz']), ('wifi_station_wire', [])], '0434': [('wifi_ios_card', []), ('wifi_ios_join', ['--extra=-Oz'])]}
sae_new = []
if a.mode == 'patched' and any(n in series for n in ('0432', '0433', '0434')):
    assert all(n in series for n in ('0278', '0430', '0432', '0433', '0434')), 'needs 0278, 0430 and 0432..0434 together'
    assert '0431' not in series, 'not on the withdrawn 0431'
    XS = ROOT/'gpu/build/xnu-stage/bsd/dev'
    def compile_sae(n, extra):
        run(n+'-compile', ['nice', '-n', '10', 'python3', ROOT/'gpu/tools/xnu_staged_compile.py', srcroot/'bsd/dev'/(n+'.c'), '--out', out/(n+'.o'),
                           '--extra=-Wno-unsafe-buffer-usage'] + extra)
    for num, mem in SAE_NEW.items():
        for n in mem:
            assert members.count(n+'.o') == 0 and not (STAGE/'bsd/dev'/(n+'.c')).exists(), n
            compile_sae(n, ['--extra=-Oz'] + (['--extra=-I'+str(XS)] if n == 'wifi_sae_crypto' else []))
            run('ar-add-'+n, [AR, 'r', archive, out/(n+'.o')]); sae_new.append(n)
    for num, mem in SAE_REBUILT.items():
        for n, extra in mem:
            assert members.count(n+'.o') == 1, n
            assert sha(sc/'bsd/dev'/(n+'.c')) != sha(STAGE/'bsd/dev'/(n+'.c')), (n, 'not changed by the series')
            compile_sae(n, extra)
            run('ar-replace-'+n, [AR, 'r', archive, out/(n+'.o')]); sae_new.append(n)
    for n in ('wifi_wpa2', 'wifi_wpa_crypto', 'wifi_wpa_owner', 'wifi_wpa_nst', 'wifi_station_transport', 'wifi_normal_transport', 'wifi_normal_rx'):
        assert sha(sc/'bsd/dev'/(n+'.c')) == sha(STAGE/'bsd/dev'/(n+'.c')), (n, 'a CI member source changed without a rebuild')
    print('sae members (new + rebuilt):', ' '.join(sae_new))

# ---- 0436 (gpu/build/wifi-20261113): private Wi-Fi address. wifi_normal_transport.o stays the PREBUILT CI member; only its four
#      "continuation = exactly one scan command" branch clusters are replaced by NOPs in the archive copy (wifi_binpatch.py:
#      known member sha, pattern + context asserted). wifi_normal_rx.o is not touched. Never together with the 0431 diagnostic rebuild.
if a.mode == 'patched' and '0436' in series:
    assert '0431' not in series and '0430' in series and '0278' in series
    sys.path.insert(0, str(ROOT/'gpu/build/wifi-20261113')); import wifi_binpatch      # integ: the wifi-20261113 module (CI sha asserts)
    assert members.count('wifi_normal_transport.o') == 1
    bp = out/'binpatch-0436'; bp.mkdir()
    run('ar-x-wifi_normal_transport', [AR, 'x', archive, 'wifi_normal_transport.o'], cwd=bp)
    blob = (bp/'wifi_normal_transport.o').read_bytes(); new, sites = wifi_binpatch.patch(blob)
    (bp/'wifi_normal_transport.o').write_bytes(new)
    run('ar-r-wifi_normal_transport', [AR, 'r', archive, bp/'wifi_normal_transport.o'], cwd=bp)
    print('0436 binary patch: wifi_normal_transport.o %d branch words -> NOP (%s), sha %s' % (len(sites), ' '.join(hex(x) for x in sites), sha(bp/'wifi_normal_transport.o')[:16]))

# ---- 0450/0451 (gpu/build/wifi8021x-20261113): WPA2-Enterprise (802.1X). No prebuilt member is recompiled for it (wifi_ios_join.o
#      is the one 0430 already rebuilds) and sweet_touch.o is not changed. New objects, linked like Nord2Baseband.o:
#        wifi_8021x_wpa2/_stc/_owner/_nst.o  the e8_ instance of the station stack (wifi_8021x_rename.h)
#        wifi_8021x_glue.o                   wrappers nwn_begin/pump/event/peer_complete/cancel + EAPOL/PMK glue
#        Nord2WifiSupplicant.o (0451)        user client of eapolclient + wrapper niwl_create_client
#      and two symbol-NAME edits in the archive copy (same length, code bytes untouched, as 0296 does for sweet_usb.o):
#        wifi_wpa_nst.o        _nwn_{begin,pump,event,peer_complete,cancel} -> _nwc_*   (the CI entry points behind the wrappers)
#        wifi_ios_client.cpo   _niwl_create_client -> _niwc_create_client              (the CI client factory behind the wrapper)
n8x_new = []
def n8x_rename(member, pairs, tag):
    td = out/('rename-'+tag); td.mkdir()
    run('ar-x-'+tag, [AR, 'x', archive, member], cwd=td)
    blob = (td/member).read_bytes(); before = hashlib.sha256(blob).hexdigest()
    for old, new in pairs:
        o, n = b'\0_'+old.encode()+b'\0', b'\0_'+new.encode()+b'\0'
        assert len(o) == len(n) and blob.count(o) == 1 and n not in blob, (member, old, blob.count(o))
        blob = blob.replace(o, n)
    (td/member).write_bytes(blob)
    run('ar-r-'+tag, [AR, 'r', archive, td/member], cwd=td)
    return {'member': member, 'sha256_before': before, 'sha256_after': hashlib.sha256(blob).hexdigest(), 'renamed': pairs}
n8x_renamed = []
if a.mode == 'patched' and '0450' in series:
    assert '0430' in series and 'wifi_ios_join' in wifi_new, '0450 needs the wifi_ios_join.o rebuild of 0430'
    for n in ['wifi_8021x_wpa2', 'wifi_8021x_stc', 'wifi_8021x_owner', 'wifi_8021x_nst', 'wifi_8021x_glue']:
        assert members.count(n+'.o') == 0
        run(n+'-compile', ['nice', '-n', '10', 'python3', ROOT/'gpu/tools/xnu_staged_compile.py', srcroot/'bsd/dev'/(n+'.c'), '--out', objs/(n+'.o'),
                           '--extra=-Wno-unsafe-buffer-usage', '--extra=-Oz'])      # -Oz: control path, the boot image has little spare
        n8x_new.append(n)
    assert members.count('wifi_wpa_nst.o') == 1
    n8x_renamed.append(n8x_rename('wifi_wpa_nst.o', [('nwn_'+x, 'nwc_'+x) for x in ('begin', 'pump', 'event', 'peer_complete', 'cancel')], 'nst'))
    if '0451' in series:
        run('Nord2WifiSupplicant-compile', ['nice', '-n', '10', 'python3', ROOT/'gpu/tools/xnu_staged_compile.py', srcroot/'iokit/Kernel/arm/Nord2WifiSupplicant.cpp',
                                            '--out', objs/'Nord2WifiSupplicant.o', '--cxx', '--extra=-Wno-unsafe-buffer-usage', '--extra=-Oz'])
        n8x_new.append('Nord2WifiSupplicant')
        assert members.count('wifi_ios_client.cpo') == 1
        n8x_renamed.append(n8x_rename('wifi_ios_client.cpo', [('niwl_create_client', 'niwc_create_client')], 'client'))
    print('802.1X objects:', ' '.join(n8x_new), '| renamed entry points in', ' '.join(x['member'] for x in n8x_renamed))
else:
    assert not (a.mode == 'patched' and '0451' in series), '0451 needs 0450'

# ---- 0790 (gpu/build/autojoin-20261115): APPLE80211_IOC_RSSI_BOUNDS (GET 106) for wifid auto-join. New object wifi_rssi_bounds.o
#      (wrappers niw_decode / niwj_state_reply) and two symbol-NAME edits in the archive copy (same length, code bytes untouched):
#        wifi_ios_control.o       _niw_decode       -> _niv_decode
#        wifi_ios_station_view.o  _niwj_state_reply -> _niwk_state_reply
#      wifi_ios_client.cpo (CI) is not touched: its unchanged calls land in the wrappers.
if a.mode == 'patched' and '0790' in series:
    assert '0451' in series, '0790 is built on the 0450/0451 rename machinery'
    assert members.count('wifi_ios_control.o') == 1 and members.count('wifi_ios_station_view.o') == 1 and members.count('wifi_rssi_bounds.o') == 0
    run('wifi_rssi_bounds-compile', ['nice', '-n', '10', 'python3', ROOT/'gpu/tools/xnu_staged_compile.py', srcroot/'bsd/dev/wifi_rssi_bounds.c',
                                     '--out', objs/'wifi_rssi_bounds.o', '--extra=-Wno-unsafe-buffer-usage', '--extra=-Oz'])
    n8x_new.append('wifi_rssi_bounds')
    n8x_renamed.append(n8x_rename('wifi_ios_control.o', [('niw_decode', 'niv_decode')], 'control0790'))
    n8x_renamed.append(n8x_rename('wifi_ios_station_view.o', [('niwj_state_reply', 'niwk_state_reply')], 'stview0790'))
    print('0790 objects: wifi_rssi_bounds | renamed: wifi_ios_control.o, wifi_ios_station_view.o')

# ---- big window: the Wi-Fi layout checks compare boot_args.memSize with 0x3e000000 (movz w9, #0x3e00, lsl #16); patch the two archive members in the archive copy
import struct, tempfile, os
BIG_MEM = 0xec000000
# ram5088-20261113 (q10jgq): NORD2_PHYS_BASE=0x100000000 NORD2_MEM_SIZE=0x13e000000 selects the 5088 MiB window (ram5g-20261103 loader,
# booted on p79/p81/p82). Unset = the 3776 MiB window below, unchanged. Any other pair is refused.
_win = (int(os.environ.get('NORD2_PHYS_BASE', '0x142000000'), 0), int(os.environ.get('NORD2_MEM_SIZE', '0xec000000'), 0))
assert _win in ((0x142000000, 0xec000000), (0x100000000, 0x13e000000)), ('unsupported window', [hex(x) for x in _win])
if _win == (0x100000000, 0x13e000000):
    BIG_MEM = 0
    tmpd = Path(tempfile.mkdtemp(prefix='ram5088-', dir=out))
    for member in ('wifi_rom_xnu.o', 'sweet_touch.o'):
        run('ar-x-'+member, [AR, 'x', archive, member], cwd=tmpd)
        data = bytearray((tmpd/member).read_bytes())
        nphys = nmem = 0
        for i in range(4, len(data) - 8, 4):
            w, nx = struct.unpack_from('<II', data, i)
            prev = struct.unpack_from('<I', data, i-4)[0]
            if w == 0xf2c00029 and prev == 0xd2a84009 and nx == 0xeb09011f:    # movz x9,#0x4200,lsl16 ; movk x9,#1,lsl32 ; cmp x8,x9  (physBase 0x142000000)
                struct.pack_into('<I', data, i-4, 0xd2c00029)                   # movz x9,#1,lsl32  -> 0x100000000
                struct.pack_into('<I', data, i, 0xd503201f); nphys += 1          # nop
            if w == 0x52a7c009 and nx == 0xeb09011f:                            # movz w9,#0x3e00,lsl16 ; cmp x8,x9   (memSize / memSizeActual 0x3e000000)
                struct.pack_into('<I', data, i, 0x52b3e009)                     # movz w9,#0x9f00,lsl16 (0x9f000000)
                struct.pack_into('<I', data, i+4, 0xeb09051f); nmem += 1        # cmp x8,x9,lsl #1 -> exact 64-bit compare with 0x13e000000
        assert (nphys, nmem) == (1, 2), (member, nphys, nmem)
        (tmpd/member).write_bytes(bytes(data))
        run('ar-r-'+member, [AR, 'r', archive, tmpd/member], cwd=tmpd)
    print('5088 MiB window patched: wifi_rom_xnu.o, sweet_touch.o (physBase 0x100000000, memSize 0x13e000000, exact 64-bit compares)')
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
for n in lvts + sched_new + cpufreq_new + bb_new + call_new + n8x_new:                      # new objects: insert before Nord2GPU.o
    cmd.insert(cmd.index(str(objs/'Nord2GPU.o')), str(objs/(n+'.o')))
replaced = [x for x in cmd if x.startswith(str(objs))]
assert len(replaced) == len(names)+len(power)+len(lvts)+len(sched_new)+len(cpufreq_new)+len(bb_new)+len(call_new)+len(n8x_new)+2, replaced
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
    if '0280' in series: rec['audio_device.o'] = sha(out/'audio_device.o')
    if '0282' in series: rec['audio_install.o'] = sha(out/'audio_install.o')
    if '0330' in series: rec['audio_objs_0330'] = json.loads((out/'audio-objs-0330.log').read_text().splitlines()[-1])
    if '0279' in series: rec['SweetHID.cpo'] = sha(out/'SweetHID.cpo')
    if '0285' in series:
        rec['kern_exec.o'] = sha(out/'kern_exec.o'); rec['mem.o'] = sha(out/'mem.o'); rec['applog_name_default'] = a.applog_name_default
    if '0297' in series: rec['proc_info.o'] = sha(out/'proc_info.o')
    if '0291' in series:
        rec['kern_mib.o'] = sha(out/'kern_mib.o'); rec['hwmodel_default'] = a.hwmodel_default
for n in wifi_new: rec[n+'.o'] = sha(out/(n+'.o'))
for n in sae_new: rec[n+'.o'] = sha(out/(n+'.o'))
if n8x_renamed: rec['wifi_8021x_renamed_members'] = n8x_renamed
rec['size_flags_integ'] = OPT
(out/'receipt.json').write_text(json.dumps(rec, indent=2)+'\n')
print(json.dumps({k: v for k, v in rec.items() if k != 'objects'}, indent=2))
