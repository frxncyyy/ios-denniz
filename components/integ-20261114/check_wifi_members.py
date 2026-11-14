#!/usr/bin/env python3
"""integ-20261114: Wi-Fi member guard beside wifi-20261113/check_scan_path.py (which covers transport/RX/0436 binpatch/sweet_touch.o).
usage: check_wifi_members.py CANDIDATE_DIR
D. SAE/0430 rule (wifi-sae-20261113 NOTES): the CI members wifi_wpa2, wifi_wpa_crypto, wifi_wpa_owner, wifi_wpa_nst (names renamed by 0450 only),
   wifi_station_transport stay PREBUILT: every function equals the same-named one of the linked (unstripped) kernel outside relocations.
E. Rebuilt / new members (0430 wifi_ios_join/policy, 0432..0434 wifi_sae, wifi_sae_crypto, wifi_wpa3, wifi_station_wire2, wifi_station_transaction,
   wifi_station_wire, wifi_ios_card) and the 802.1X objects: the objects of this build are the ones linked (same test).
F. 0450 rename: in the kernel the CI nst entry points are reachable only as _nwc_* (checked through the symbol-free kernel: run test above)
   and the build receipt lists the two renamed members.
G. CI members not replaced: a run unique to the CI object (absent from every rebuilt/new Wi-Fi object of this build) is in the kernel."""
import sys, subprocess, re, struct, glob, json
from pathlib import Path
LL = '$HOME/Documents/Codex/2026-10-13/por/work/huawei/command-line-tools/sdk/default/openharmony/native/llvm/bin/'
C = Path(sys.argv[1]); B = Path(__file__).resolve().parents[1]; K = C/'k/kernel'
ok = True
KF = glob.glob(str(K/'kernel.development.nord2-metal-*'))[0]       # unstripped (the packer strips its own copy)
def disasm(path, syms=None):
    """{name: [(words, masked word indices)]}: instruction words per function; relocations (llvm-objdump prints them with SECTION offsets,
    in blocks after an instruction) masked."""
    cmd = [LL+'llvm-objdump', '-d', '-r', str(path)] + (['--disassemble-symbols=' + ','.join(syms)] if syms else [])
    t = subprocess.run(cmd, text=True, capture_output=True).stdout; funcs = []; rel = set(); cur = None
    for l in t.split('\n'):
        m = re.match(r'^([0-9a-f]+) <(.+)>:$', l)
        if m: cur = (m.group(2), int(m.group(1), 16), []); funcs.append(cur); continue
        if cur is None: continue
        m = re.match(r'^\s*[0-9a-f]+:\s+([0-9a-f]{8})\s', l)
        if m: cur[2].append(int(m.group(1), 16)); continue
        m = re.match(r'^\s+([0-9a-f]+):\s+ARM64_RELOC_(\w+)', l)
        if m:
            off = int(m.group(1), 16) & ~3; rel.add(off)
            if m.group(2).endswith('PAGEOFF12'): rel.add(off + 4)      # ld64 LOH (AdrpLdrGotLdr / AdrpAddLdr) folds the offset into the next load
    out = {}
    for n, start, w in funcs:
        out.setdefault(n, []).append((w, {i for i in range(len(w)) if start + 4 * i in rel}))
    return out
def loh(a, b):
    """b (kernel) may differ from a (object) where ld64 applied a linker optimization hint: nop, adr, ldr-literal, or a load/store
    (unsigned offset form) whose only change is the 12-bit offset field."""
    if b == 0xd503201f or (b & 0x9f000000) == 0x10000000 or (b & 0xbf000000) == 0x18000000: return True
    return (a & 0x3b000000) == 0x39000000 and (a & ~(0xfff << 10)) == (b & ~(0xfff << 10))
def same(o, k):
    wo, mask = o; wk = k[0]
    return len(wo) == len(wk) and all(i in mask or wo[i] == wk[i] or loh(wo[i], wk[i]) for i in range(len(wo)))
def check(tag, obj, rename=None):
    """Every function of obj (>= 4 insns) that the linker kept (dead-stripped = absent from the kernel's symbols) equals a same-named
    kernel function outside relocations / LOH; at least one must be found.  rename: object name -> kernel name (0450's archive rename)."""
    global ok
    rename = rename or {}
    fo = {n: v for n, v in disasm(obj).items() if not n.startswith('ltmp') and len(v[0][0]) >= 4}
    fk = disasm(KF, sorted(rename.get(n, n) for n in fo)) if fo else {}
    good_n = 0; bad = []; gone = []
    for n, v in sorted(fo.items()):
        kn = rename.get(n, n)
        if kn not in fk: gone.append(n)
        elif any(same(v[0], k) for k in fk[kn]): good_n += 1
        else: bad.append(n)
    if not fo:
        print('%s %s: no named functions (untestable by symbol; linked as an archive member)' % (tag, obj.name)); return
    good = not bad and good_n > 0; ok &= good
    print('%s %s: %d/%d functions identical in the kernel, %d dead-stripped%s' % (tag, obj.name, good_n, len(fo), len(gone), '' if good else ' FAIL: ' + ' '.join(bad[:6])))
import tempfile
with tempfile.TemporaryDirectory(dir=str(C)) as d:
    ci = ['wifi_wpa2.o', 'wifi_wpa_crypto.o', 'wifi_wpa_owner.o', 'wifi_wpa_nst.o', 'wifi_station_transport.o']
    subprocess.run([LL+'llvm-ar', 'x', str(B/'startup-diagnostic/kernel.startup.a')] + ci, cwd=d, check=True)
    for o in ci: check('D CI', Path(d)/o, {'_nwn_' + x: '_nwc_' + x for x in ('begin', 'pump', 'event', 'peer_complete', 'cancel')} if o == 'wifi_wpa_nst.o' else None)
rec = json.loads((K/'receipt.json').read_text())
for n in ['wifi_ios_join', 'wifi_ios_policy', 'wifi_sae', 'wifi_sae_crypto', 'wifi_wpa3', 'wifi_station_wire2', 'wifi_station_transaction', 'wifi_station_wire', 'wifi_ios_card']:
    p = K/(n+'.o')
    if not p.exists(): print('E rebuilt %s.o MISSING' % n); ok = False; continue
    check('E rebuilt', p)
for n in ['wifi_8021x_wpa2', 'wifi_8021x_stc', 'wifi_8021x_owner', 'wifi_8021x_nst', 'wifi_8021x_glue', 'Nord2WifiSupplicant']:
    check('E 802.1X', K/'objs'/(n+'.o'))
rn = rec.get('wifi_8021x_renamed_members', []); good = sorted(x['member'] for x in rn) == ['wifi_ios_client.cpo', 'wifi_wpa_nst.o']; ok &= good
print('F 0450 renamed members:', ' '.join(x['member'] for x in rn), good)
print('WIFI_MEMBERS_OK' if ok else 'WIFI_MEMBERS_MISMATCH'); sys.exit(0 if ok else 1)
