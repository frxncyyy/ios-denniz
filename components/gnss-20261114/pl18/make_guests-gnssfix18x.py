#!/usr/bin/env python3
"""gnss-20261114/pl4/make_guests.py (template; gen.sh writes make_guests-<variant>.py): gnssfix4 = SAFE: n2mnld gnssd7 drops every libmnl
write (logged, never reaches the kernel TX path that hung gnss5/gnss6) and swallows the post-boot 0xFE05 (= gnssd5, stable in gnss4).
gnssfix4x = EXPERIMENTAL: the same + the DSP answer recorded by the kernel S11 capture (postboot_reply.bin, 253 B) injected on libmnl's
read side when 0xFE05 is swallowed (no DSP command). Older doc: generates the guest scripts and payload dirs of gnssfix18x (= gnssfix18x with n2mnld gnssd5:
callback slot 0 = engine notification (the gnss3 SIGSEGV fix), slot 1 = NMEA/text, FIX SUMMARY console line every 10 s;
n2mnld job KeepAlive on crash (120 s throttle); config/log dir 0755 + log 0644; install over gnssfix18x in place and dump the
previous engine log first; undo removes only known files).
Original docstring follows: gnssfix18x (run on the host, writes only here).
gnssfix18x = gnssd4 (n2mnld: MediaTek libmnl hosted natively, kernel DSP boot K=31 on 0580/0581, NMEA -> FIFO) + locbridge FIFO
           (n2locbridge --fifo /private/var/run/n2gnss.nmea: every VALID fix -> locationd through CLSimulationManager).
gnssfix18x-undo     = both blocks undone (reverse order); prints the engine/bridge logs into the VM log first (GNSSFIX18X_LOG lines).
gnssfix18xroundtrip = manifest, install, install again (no-op), undo, undo again, manifests compared (rehearsal only).
gnsslog           = read-only dump of the logs (run in the offline VM on the mirror after any snapshot; changes nothing)."""
import hashlib, json, shutil, os
EXP = False; NOREAD = False; PMTK = "gnssfix18x".endswith('p')   # pl6: gnssfix6 = gnssd8, libmnl writes PASSED (incl. post-boot 0xFE05); gnssfix6p = + $PMTK103 cold start injected 20 s after pass-through   # gen.sh: gnssfix5 = safe gnssd7 (writes dropped), gnssfix5n = + NO device reads after the boot (hang discriminator)
from pathlib import Path
H = Path(__file__).resolve().parent
B = H.parent.parent                                             # gpu/build
NAT = B.parent / 'hwcodec/gnss/n2mnld/native'
sha = lambda b: hashlib.sha256(b).hexdigest()
FILES = {'n2mnld': NAT / 'build/n2mnld',
         **({'postboot_reply.bin': H / 'postboot_reply.bin'} if EXP else {}),
         'MNL_Config.xml': H / 'MNL_Config.xml',   # pl16: vendor config with L1Only enabled (this port has no L5 link)
         'MNL_Config.vendor.xml': B / 'apps-20261104/payload-gnssd3/MNL_Config.vendor.xml',
         'n2locbridge': B / 'apps-20261104/payload-locbridge1/n2locbridge'}
KPROBE = False
# pl9 (0588): libmnl boots the DSP itself (proxy off, writes passed, poll 1 ms) on the HOLD variants that mimic the kernel S11 loop:
# gnssfix9a = K=34 (no LNA before the boot + fresh 64 B RX arming before each TX + 2 s settle), gnssfix9b = K=35 (no LNA + settle),
# gnssfix9c = K=36 (fresh RX per TX + settle, LNA on). Needs q10jgdb (0583..0588).
# pl10 (n2mnld gnssd9): gnssfix10r = ladder K=35 (HOLD, DSP in ROM, no LNA) + n2mnld --replay-boot: n2mnld itself sends the blob
# frames through /dev/gpsdl0 (the pipe TX path), ack-paced, BEFORE libmnl starts (splits pipe path vs libmnl running); then libmnl
# behind the boot proxy, writes dropped (gnss5a-like). gnssfix10d = ladder K=31 deferred 20 s after libmnl started (--kboot-after 20),
# proxy on, writes dropped: the proven kernel boot while libmnl runs; kernel q10jgg (no LNA).
# pl11 (0589, kernel q10jgdc): gnssfix11r = ladder K=37 (no LNA, quiet pipe, S11-exact RX, GIC SPIs masked) + n2mnld --replay-boot
# (n2mnld boots the DSP through the pipe, then libmnl behind the proxy, writes dropped); gnssfix11g = the same on K=38 (+ GIC words
# traced around every pipe TX); gnssfix11l = K=37 with libmnl booting the DSP itself (proxy off, writes passed, poll 1 ms).
# pl12 (n2mnld gnssd10, kernel q10jgdc): every pipe-boot hang came 1-3 ms after a DSP ack that n2mnld logged with console write +
# file write + fsync (gnssd7+: fsync of every console line in the first 240 s); the kernel ladder boots with n2mnld blocked (no I/O).
# gnssfix12q = K=37 + n2mnld replay boot with NO log I/O at all during the boot (--quiet-boot) and no fsync afterwards;
# gnssfix12l = K=37, libmnl boots the DSP itself, logs kept but never fsync'ed (--no-fsync).
# pl13 (0590, kernel q10jgdd): gnssfix13r = K=39 (pipe TX = the ladder's n2gl_tx: readbacks + printf before the done poll) +
# n2mnld replay boot (gnssd10, no log I/O during the boot); gnssfix13s = K=40 (no-poll pipe TX: 200 us wait after START, sparse checks).
# pl14 (0591, kernel q10jgde): gnssfix14x = K=41 + n2mnld --xchg-boot (each boot frame = one ioctl 190 = the ladder's own n2gb_xchg in
# the kernel; only "return to user space between frames" differs from the working S11 loop); gnssfix14r = K=37 + n2mnld replay boot
# (the known pipe failure) on the 0591 kernel = post-mortem capture run (cpu id per trace line, kaddr line for the last-PC symbols).
# pl15 (0592, kernel q10jgdf): ring read byte-wise (the vectorised Device-memory copy in read() removed); boot by ioctl 190 (gnss14x
# proved it), then libmnl behind the boot proxy. gnssfix15x = writes dropped (does HOLD + reads now survive?); gnssfix15l = libmnl's
# runtime writes PASSED through the pipe (ladder n2gl_tx, K=43), read poll 1 ms: the engine can start.
# pl16 (q10jgdf, n2mnld gnssd12): libmnl boots the DSP itself through the fixed pipe (gnss15v proved it), MNL_Config.xml = vendor config
# with L1Only on (no L5 link here; the vendor default is L1+L5 and libmnl's L5 port never answers). gnssfix16a = K=32 (GPIO162 eLNA
# handed to the DSP, 0585) no PMTK; gnssfix16p = 16a + PMTK sequence (version query, NMEA RMC/GGA/GSA/GSV output, cold start) injected
# 30 s after the engine started, one every 3 s; gnssfix16n = 16p on K=37 (LNA NOT set) to separate the LNA.
# pl18 = pl17 with n2mnld gnssd14 (sys callback slots 6/8/16 decoded: slot 8 encode now fills its output, every slot call and its
# string data on the console). Older pl17 doc:
# pl17 (q10jgdg = q10jgdf + 0593 LNA ioctl, n2mnld gnssd13): K=37 (the boot that completed in gnss15v: quiet pipe, RX round closed on
# data; gnss16p on K=32 stalled at fragment 20 with rx stuck at exactly 256 B), libmnl boots the DSP, MNL_Config L1Only, engine text on
# the console (first 400 lines + keyword lines), PMTK 605 / 314 / 103 cold start / 740 UTC time from the system clock, 30 s after the
# engine start. gnssfix17n = + L1 eLNA handed to the DSP 20 s after libmnl's 52nd write (ioctl 192); gnssfix17x = no LNA at all.
V = "gnssfix18x"[-1]
KB = '37'
PMTK = '$PMTK605;$PMTK314,0,1,0,1,1,5,0,0,0,0,0,0,0,0,0,0,0,0,0;$PMTK103;$PMTK740,@UTC'
GNSSD_ARGS = ['--delay', '60', '--kboot', KB, '--no-proxy', '--tx-pass', '--poll-ms', '1', '--no-fsync', '--trace-seconds', '0',
              '--text-console', '400', '--pmtk', PMTK, '--pmtk-delay', '30'] + (['--lna-after', '20'] if V == 'n' else [])
LOCB_ARGS = ['--fifo', '/private/var/run/n2gnss.nmea']

COMMON = r'''import os, json, hashlib, plistlib, shutil
R='/work/root'; LP='/System/Library/xpc/launchd.plist'; MR='/private/var/db/nord2-metal/receipt.json'
sha=lambda b: hashlib.sha256(b).hexdigest(); rd=lambda p: open(R+p,'rb').read()
def put(path, data, mode, uid, gid):
    tmp=R+path+'.nord2tmp'
    if os.path.lexists(tmp): os.remove(tmp)
    with open(tmp,'xb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,mode); os.chown(tmp,uid,gid); os.replace(tmp,R+path)
    assert rd(path)==data
def job_set(key, job, tag):
    """add/replace (job) or remove (None) one LaunchDaemons entry; the Metal receipt's launchd sha is checked before, updated after"""
    raw=rd(LP); fmt=plistlib.FMT_BINARY if raw[:6]==b'bplist' else plistlib.FMT_XML; cfg=plistlib.loads(raw); LD=cfg['LaunchDaemons']
    rc=json.load(open(R+MR)); assert rc.get('installed_launchd_sha256')==sha(raw), tag+'_REFUSED launchd.plist differs from the Metal receipt: not touching it'
    if job is not None:
        assert not any(isinstance(j,dict) and j.get('Label')==job['Label'] for k,j in LD.items() if k!=key), tag+'_REFUSED label used elsewhere'
        if LD.get(key)==job: print(tag+'_JOB already'); return
        LD[key]=job
    else:
        if key not in LD: print(tag+'_JOB already absent'); return
        del LD[key]
    out=plistlib.dumps(cfg,fmt=fmt,sort_keys=False); lst=os.stat(R+LP)
    put(LP,out,lst.st_mode&0o7777,lst.st_uid,lst.st_gid); assert plistlib.loads(rd(LP))==cfg
    rc['installed_launchd_sha256']=sha(out); mst=os.stat(R+MR)
    put(MR,(json.dumps(rc,indent=2)+'\n').encode(),mst.st_mode&0o7777,mst.st_uid,mst.st_gid)
    print(tag+'_JOB', 'set '+json.dumps(job['ProgramArguments']) if job else 'removed '+key, 'launchd ->', rc['installed_launchd_sha256'][:12])
plan=json.load(open('/src/plan.json'))
'''

GNSSD = r'''# gnssd (gnssfix18x block 1): /usr/local/libexec/n2mnld = gnssd4 (gpu/hwcodec/gnss/n2mnld/native: MediaTek libmnl.so embedded and run
# natively; gnssd3 + console milestones "first DSP bytes" / "first libmnl frame written", engine log fsync'ed with every stats line,
# first stats after 3 s) + config dir /private/var/db/n2gnss (0700: MNL_Config.xml EMPTY = the harness config that produced the
# kernel boot blob; MNL_Config.vendor.xml kept for later) + launchd job com.nord2.gnssd (RunAtLoad, one run per boot, gate
# /private/var/db/nord2-gnssd): 60 s after boot it uploads the DSP code and runs kernel ladder K=31 (0580: boot + HOLD), then
# starts libmnl; NMEA goes to the FIFO /private/var/run/n2gnss.nmea. Kernel: 0580 (+0581) = q10jcs. A gnssstep probe gate (old
# bring-up job, absent on the 14 Nov mirror) is moved away so nothing else opens /dev/gpsdl0. Receipt /private/var/db/nord2-gnssd.json.
BIN='/usr/local/libexec/n2mnld'; DIR='/private/var/db/n2gnss'; GATE='/private/var/db/nord2-gnssd'; REC='/private/var/db/nord2-gnssd.json'
SGATE='/private/var/db/nord2-gnssstep'; SOFF=SGATE+'.off-gnssd1'
LABEL='com.nord2.gnssd'; KEY='/System/Library/LaunchDaemons/'+LABEL+'.plist'
if not UNDO:
    F={n:open('/src/'+n,'rb').read() for n in ('n2mnld','MNL_Config.xml','MNL_Config.vendor.xml')}
    for n,b in F.items(): assert sha(b)==plan['files'][n], 'GNSSFIX18X_REFUSED %s is not the planned file'%n
    exe=F['n2mnld']; assert exe[:4]==b'\xcf\xfa\xed\xfe' and b'__MNLT' in exe and b'start gnssd14' in exe, 'GNSSFIX18X_REFUSED not the gnssd14 n2mnld'
    JOB={'Label':LABEL,'UserName':'root','RunAtLoad':True,'KeepAlive':{'SuccessfulExit':False},'ThrottleInterval':120,
         'ProcessType':'Interactive','EnablePressuredExit':False,
         'ProgramArguments':[BIN,'--gate',GATE]+[str(a) for a in plan['gnssd_args']]}
    if os.path.exists(R+BIN): assert sha(rd(BIN))==sha(exe) or os.path.exists(R+REC), 'GNSSFIX18X_REFUSED a different n2mnld without our receipt'
    if os.path.exists(R+DIR): assert os.path.isdir(R+DIR) and (os.path.exists(R+REC) or not os.listdir(R+DIR)), 'GNSSFIX18X_REFUSED n2gnss dir without our receipt'
    old=json.load(open(R+REC)) if os.path.exists(R+REC) else {}
    if not (os.path.exists(R+BIN) and sha(rd(BIN))==sha(exe)): put(BIN, exe, 0o755, 0, 0)
    dir_made=old.get('dir_made', not os.path.exists(R+DIR))
    if not os.path.isdir(R+DIR): os.makedirs(R+DIR)
    os.chmod(R+DIR,0o755); os.chown(R+DIR,0,0)
    for n in ('n2mnld.log','n2mnld.log.1'):
        if os.path.isfile(R+DIR+'/'+n): os.chmod(R+DIR+'/'+n,0o644)
    for n in ('MNL_Config.xml','MNL_Config.vendor.xml'):
        p=DIR+'/'+n
        if not (os.path.exists(R+p) and rd(p)==F[n]): put(p, F[n], 0o600, 0, 0)
    if 'postboot_reply.bin' in plan['files']:
        rb=open('/src/postboot_reply.bin','rb').read(); assert sha(rb)==plan['files']['postboot_reply.bin'], 'GNSSFIX18X_REFUSED reply file'
        if not (os.path.exists(R+DIR+'/postboot_reply.bin') and rd(DIR+'/postboot_reply.bin')==rb): put(DIR+'/postboot_reply.bin', rb, 0o644, 0, 0)
    elif os.path.exists(R+DIR+'/postboot_reply.bin'): os.remove(R+DIR+'/postboot_reply.bin')
    moved=old.get('gnssstep_gate_moved', False)
    if os.path.exists(R+SGATE):
        if os.path.exists(R+SOFF): os.remove(R+SOFF)
        os.rename(R+SGATE, R+SOFF); moved=True; print('GNSSFIX18X_GNSSSTEP gate moved away')
    job_set(KEY, JOB, 'GNSSFIX18X')
    rec=(json.dumps({'bin':BIN,'sha256':sha(exe),'dir':DIR,'dir_made':dir_made,'job':KEY,'gnssstep_gate_moved':moved,'payload':'gnssfix18x'},indent=1)+'\n').encode()
    if not (os.path.exists(R+REC) and rd(REC)==rec): put(REC,rec,0o644,0,0)
    if not os.path.exists(R+GATE): put(GATE,b'run\n',0o644,0,0)
    print('GNSSFIX18X_GNSSD_OK', BIN, sha(exe)[:12], 'args', json.dumps(JOB['ProgramArguments'][1:]))
else:
    rec=json.load(open(R+REC)) if os.path.exists(R+REC) else {}
    job_set(KEY, None, 'GNSSFIX18X_UNDO')
    if os.path.exists(R+SOFF) and (rec.get('gnssstep_gate_moved') or not rec):
        if not os.path.exists(R+SGATE): os.rename(R+SOFF, R+SGATE); print('GNSSFIX18X_UNDO gnssstep gate restored')
        else: os.remove(R+SOFF)
    for p in (GATE, BIN):
        if os.path.lexists(R+p): os.remove(R+p); print('GNSSFIX18X_UNDO removed', p)
    KNOWN=('postboot_reply.bin','MNL_Config.xml','MNL_Config.vendor.xml','n2mnld.log','n2mnld.log.1','MNL_Config_Tmp.xml','mtkgps.dat','mtkgps.dat.bak','mtkgps.dat.crc',
           'mtkgps.dat.ver','cache.dat','almanac.dat','EPO.DAT','QEPO.DAT','EPO_GR_3_1.DAT','mnl.prop','gps.conf','debuglog')
    if os.path.isdir(R+DIR) and rec.get('dir_made', True):
        for n in KNOWN:
            if os.path.lexists(R+DIR+'/'+n): os.remove(R+DIR+'/'+n)
        left=os.listdir(R+DIR)
        if left: print('GNSSFIX18X_UNDO kept', DIR, 'with unknown engine files (not in the owned list):', left)
        else: os.rmdir(R+DIR); print('GNSSFIX18X_UNDO removed', DIR)
    if os.path.exists(R+REC): os.remove(R+REC)
    print('GNSSFIX18X_GNSSD_UNDONE')
os.sync()
'''

LOCB = r'''# locbridge (gnssfix18x block 2): /usr/local/libexec/n2locbridge (payload-locbridge1 binary e0950d56, ad-hoc signed with
# com.apple.locationd.simulation; phone-run 12 Nov: "N2LOC sink CoreLocation ... CLSimulationManager=1", "simulation started" in replay
# mode) in FIFO mode: reads NMEA from /private/var/run/n2gnss.nmea (n2mnld writes it) and hands every VALID RMC/GGA fix to locationd
# (CLSimulationManager append/flush/start). No fix -> nothing is sent (the Wi-Fi/cached position stays). Job com.nord2.locbridge
# (RunAtLoad, KeepAlive on failure, 30 s throttle, gate /private/var/db/nord2-locbridge), log /private/var/db/nord2-locbridge.log.
BIN='/usr/local/libexec/n2locbridge'; GATE='/private/var/db/nord2-locbridge'; REC='/private/var/db/nord2-locbridge.json'
LOGF='/private/var/db/nord2-locbridge.log'; FIFO='/private/var/run/n2gnss.nmea'
LABEL='com.nord2.locbridge'; KEY='/System/Library/LaunchDaemons/'+LABEL+'.plist'
if not UNDO:
    exe=open('/src/n2locbridge','rb').read()
    assert sha(exe)==plan['files']['n2locbridge'], 'GNSSFIX18X_REFUSED n2locbridge is not the planned file'
    assert exe[:4]==b'\xcf\xfa\xed\xfe' and b'com.apple.locationd.simulation' in exe, 'GNSSFIX18X_REFUSED n2locbridge without its entitlement'
    JOB={'Label':LABEL,'UserName':'root','RunAtLoad':True,'KeepAlive':{'SuccessfulExit':False},'ThrottleInterval':30,
         'ProcessType':'Interactive','EnablePressuredExit':False,
         'ProgramArguments':[BIN,'--gate',GATE,'--log',LOGF]+[str(a) for a in plan['locbridge_args']]}
    if os.path.exists(R+BIN): assert sha(rd(BIN))==sha(exe) or os.path.exists(R+REC), 'GNSSFIX18X_REFUSED a different n2locbridge without our receipt'
    if not (os.path.exists(R+BIN) and sha(rd(BIN))==sha(exe)): put(BIN,exe,0o755,0,0)
    job_set(KEY, JOB, 'GNSSFIX18X_LOCB')
    rec=(json.dumps({'bin':BIN,'sha256':sha(exe),'job':KEY,'payload':'gnssfix18x'},indent=1)+'\n').encode()
    if not (os.path.exists(R+REC) and rd(REC)==rec): put(REC,rec,0o644,0,0)
    if not os.path.exists(R+GATE): put(GATE,b'run\n',0o644,0,0)
    print('GNSSFIX18X_LOCBRIDGE_OK', BIN, sha(exe)[:12], 'args', json.dumps(JOB['ProgramArguments'][1:]))
else:
    job_set(KEY, None, 'GNSSFIX18X_UNDO_LOCB')
    for p in (GATE, BIN, REC, LOGF, FIFO):
        if os.path.lexists(R+p): os.remove(R+p); print('GNSSFIX18X_UNDO removed', p)
    print('GNSSFIX18X_LOCBRIDGE_UNDONE')
os.sync()
'''

DUMP = r'''# read-only: tails of the GNSS engine log and the bridge log into the VM log (GNSSFIX18X_LOG lines), file list of the config dir
import os
R='/work/root'
for p,n in (('/private/var/db/n2gnss/n2mnld.log.1',60),('/private/var/db/n2gnss/n2mnld.log',400),('/private/var/db/nord2-locbridge.log',80)):
    if os.path.exists(R+p):
        L=open(R+p,'rb').read().decode('utf-8','replace').splitlines()
        print('GNSSFIX18X_LOG ==', p, len(L), 'lines; last', min(n,len(L)))
        for l in L[-n:]: print('GNSSFIX18X_LOG', l[:300])
    else: print('GNSSFIX18X_LOG ==', p, 'absent')
d='/private/var/db/n2gnss'
if os.path.isdir(R+d): print('GNSSFIX18X_LOG files', sorted((n, os.path.getsize(R+d+'/'+n)) for n in os.listdir(R+d)))
'''

HEAD = 'set +e\n# %s (generated by gnss-20261114/pl/make_guests.py; see the blocks there for what and why)\nfsck.hfsplus -f -n /dev/vda | tail -2\nmount -t hfsplus -o %s,force /dev/vda /work/root || exit 1\n'
TAIL = "sync\numount /work/root\nfsck.hfsplus -f -n /dev/vda\necho INSTALL_DONE\n"
def py(code, undo): return "python3 - <<'GUEST'\nUNDO=%s\n%s%sGUEST\n" % (undo, COMMON, code)
def guest(name, parts, ro=False, tail=True, mark=None):
    s = HEAD % (name, 'ro' if ro else 'rw')
    for code, undo in parts: s += py(code, undo) if code is not DUMP else "python3 - <<'GUEST'\n%sGUEST\n" % DUMP
    if mark: s += "echo '%s'\n" % mark
    if not ro: s += "echo 'CC_JOB_ADDED %s (com.nord2.gnssd + com.nord2.locbridge)'\necho 'CC_RECEIPT_UPDATED metal receipt launchd sha'\n" % name
    return s + (TAIL if tail else TAIL.replace('echo INSTALL_DONE\n', ''))

plan = {'purpose': ('gnssfix18x: gnssd14 (decoded callbacks) K=37, libmnl boots the DSP, L1Only, engine text on console, PMTK '+PMTK+(', eLNA after the boot' if V == 'n' else ', no LNA'))+' + n2locbridge FIFO. Kernel q10jgdg (0583..0593)',
        'files': {n: sha(p.read_bytes()) for n, p in FILES.items()}, 'gnssd_args': GNSSD_ARGS, 'locbridge_args': LOCB_ARGS,
        'blocks_sha256': {'gnssd': sha(GNSSD.encode()), 'locbridge': sha(LOCB.encode()), 'common': sha(COMMON.encode())}}
for name, files in (('gnssfix18x', True), ('gnssfix18x-undo', False), ('gnssfix18xroundtrip', True), ('gnsslog', False)):
    d = H / f'payload-{name}'
    if d.exists(): shutil.rmtree(d)
    d.mkdir()
    if files:
        for n, p in FILES.items(): shutil.copy2(p, d / n)
    (d / 'plan.json').write_text(json.dumps(dict(plan, guest=name), indent=1, sort_keys=True) + '\n')
(H / 'guest-gnssfix18x.sh').write_text(guest('gnssfix18x', [(DUMP, None), (GNSSD, False), (LOCB, False)], mark='GNSSFIX18X_OK'))
(H / 'guest-gnssfix18x-undo.sh').write_text(guest('gnssfix18x-undo', [(DUMP, None), (LOCB, True), (GNSSD, True)], mark='GNSSFIX18X_UNDO_OK'))
(H / 'guest-gnsslog.sh').write_text(guest('gnsslog', [(DUMP, None)], ro=True, mark='GNSSLOG_DONE').replace('fsck.hfsplus -f -n /dev/vda\n', ''))

MAN = r'''m(){
mount -t hfsplus -o ro,force /dev/vda /work/root && python3 - "$1" <<'PY'
import os,sys,hashlib,json,plistlib
R='/work/root'; out={}
for top in ['/private/var/db','/usr/local/libexec','/private/var/run']:
    for b,ds,fs in os.walk(R+top):
        if '/nord2-metal' in b and top=='/private/var/db': pass
        for n in ['.']+ds+fs:
            p=os.path.normpath(os.path.join(b,n)); st=os.lstat(p)
            if p.endswith('/nord2-metal/receipt.json'): continue
            out[p[len(R):]]=[st.st_uid,st.st_gid,st.st_mode,st.st_size if not os.path.isdir(p) else 0]
            if os.path.isfile(p) and not os.path.islink(p) and st.st_size<16<<20: out['sha:'+p[len(R):]]=hashlib.sha256(open(p,'rb').read()).hexdigest()
cfg=plistlib.loads(open(R+'/System/Library/xpc/launchd.plist','rb').read())
out['launchd:jobs']=hashlib.sha256(repr(sorted((k,repr(v)) for k,v in cfg['LaunchDaemons'].items())).encode()).hexdigest()
out['launchd:keys']=sorted(k for k in cfg['LaunchDaemons'] if 'nord2' in k)
rc=json.load(open(R+'/private/var/db/nord2-metal/receipt.json'))
out['receipt:matches-launchd']=rc.get('installed_launchd_sha256')==hashlib.sha256(open(R+'/System/Library/xpc/launchd.plist','rb').read()).hexdigest()
out['receipt:others']=hashlib.sha256(repr(sorted((k,repr(v)) for k,v in rc.items() if k!='installed_launchd_sha256')).encode()).hexdigest()
json.dump(out,open('/tmp/'+sys.argv[1],'w')); print('MANIFEST',sys.argv[1],len(out),'nord2 jobs',out['launchd:keys'],'receipt ok',out['receipt:matches-launchd'])
PY
umount /work/root
}
'''
CMP = r'''python3 - <<'PY'
import json
L=lambda n: json.load(open('/tmp/'+n))
m0,m1,m2,m3,m4=(L('m%d'%i) for i in range(5))
d=lambda a,b: sorted(k for k in set(a)|set(b) if a.get(k)!=b.get(k))
print('RT install changed', d(m0,m1)); print('RT reinstall changed', d(m1,m2)); print('RT undo vs start', d(m0,m3)); print('RT re-undo vs undo', d(m3,m4))
ok = not d(m1,m2) and not d(m0,m3) and not d(m3,m4) and m1['receipt:matches-launchd'] and m3['receipt:matches-launchd'] and \
     'com.nord2.gnssd' in str(m1['launchd:keys']) and 'com.nord2.locbridge' in str(m1['launchd:keys'])
print('ROUNDTRIP_OK' if ok else 'ROUNDTRIP_FAILED')
PY
'''
inst = guest('gnssfix18x', [(DUMP, None), (GNSSD, False), (LOCB, False)], mark='GNSSFIX18X_OK', tail=False).replace('set +e\n', '', 1) + 'sync\numount /work/root\n'
undo = guest('gnssfix18x-undo', [(DUMP, None), (LOCB, True), (GNSSD, True)], mark='GNSSFIX18X_UNDO_OK', tail=False).replace('set +e\n', '', 1) + 'sync\numount /work/root\n'
pre = (H.parent / 'pl17/guest-gnssfix17n-undo.sh').read_text().replace('set +e\n', '', 1).replace('echo INSTALL_DONE\n', '')   # the mirror (= phone) has gnssfix1: clean it first
rt = 'set +e\nmkdir -p /tmp\n' + MAN + pre + 'm m0\n' + inst + 'm m1\n' + inst + 'm m2\n' + undo + 'm m3\n' + undo + 'm m4\n' + CMP + 'fsck.hfsplus -f -n /dev/vda | tail -2\necho INSTALL_DONE\n'
(H / 'guest-gnssfix18xroundtrip.sh').write_text(rt)

owned = ['/private/var/db/n2gnss/postboot_reply.bin', '/System/Library/xpc/launchd.plist', '/private/var/db/nord2-metal/receipt.json',
         '/usr/local/libexec/n2mnld', '/private/var/db/n2gnss', '/private/var/db/n2gnss/MNL_Config.xml', '/private/var/db/n2gnss/MNL_Config.vendor.xml',
         '/private/var/db/nord2-gnssd', '/private/var/db/nord2-gnssd.json', '/private/var/db/nord2-gnssstep', '/private/var/db/nord2-gnssstep.off-gnssd1',
         '/usr/local/libexec/n2locbridge', '/private/var/db/nord2-locbridge', '/private/var/db/nord2-locbridge.json']
runtime = ['/private/var/db/n2gnss/' + n for n in ('n2mnld.log', 'n2mnld.log.1', 'MNL_Config_Tmp.xml', 'mtkgps.dat', 'mtkgps.dat.bak', 'mtkgps.dat.crc',
           'mtkgps.dat.ver', 'cache.dat', 'almanac.dat', 'EPO.DAT', 'QEPO.DAT', 'EPO_GR_3_1.DAT', 'mnl.prop', 'gps.conf', 'debuglog')] + \
          ['/private/var/db/nord2-locbridge.log', '/private/var/run/n2gnss.nmea']
(H / 'owned-gnssfix18x.json').write_text(json.dumps(owned + runtime, indent=0) + '\n')
(H / 'owned-gnssfix18x-undo.json').write_text(json.dumps(owned + runtime, indent=0) + '\n')
print('generated: guests gnssfix18x, gnssfix18x-undo, gnssfix18xroundtrip, gnsslog; plan files', json.dumps(plan['files']))
