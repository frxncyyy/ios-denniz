#!/usr/bin/env python3
"""0433 host test: the real station transaction against an independent access point (ref.py).
usage: run.py HARNESS_BINARY"""
import subprocess, sys, struct, os, hashlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ref
SELF, AP = bytes.fromhex('021122334455'), bytes.fromhex('00aabbccddee')
SSID, PW = b'nord2 test', b'correct horse battery'
K = dict(PREPARE=1, CHANNEL=2, AUTH=3, ASSOC=4, CONFIGURE=5, EAPOL=6, PTK=7, GTK=8, DATA=9, DEAUTH=10, DEL_PTK=11, DEL_GTK=12, RETIRE=13, ABORT=14, GROUP=15, REKEY=16, IGTK=17, ACTION=18)
CONNECTED, DONE = 15, 21
def rsn(akms, caps, mgmt=True):
    b = bytes([1, 0, 0, 0x0f, 0xac, 4, 1, 0, 0, 0x0f, 0xac, 4]) + struct.pack('<H', len(akms)) + b''.join(bytes([0, 0x0f, 0xac, a]) for a in akms) + struct.pack('<H', caps)
    if mgmt: b += bytes([0, 0, 0, 0x0f, 0xac, 6])
    return bytes([48, len(b)]) + b
LEGACY_RSN = rsn([2], 0, False)
RSNXE = bytes([244, 1, 0x20])
def hdr(fc): return struct.pack('<HH', fc, 0) + SELF + AP + AP + bytes(2)

class Station:
    def __init__(self, harness, allow, flags, ap_rsn, ap_ies, password=PW, pmk=None):
        self.pmk = pmk or hashlib.pbkdf2_hmac('sha1', PW, SSID, 4096, 32)
        self.p = subprocess.Popen([harness, str(allow), str(flags), password.decode(), SSID.decode(), self.pmk.hex(), ap_rsn.hex(), ap_ies.hex()],
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        self.logs = []; self.actions = []; self.state = None; self.began = None
        self._read()
        assert self.began == 1, ('begin refused', self.logs)
    def _read(self):
        while True:
            l = self.p.stdout.readline()
            assert l, 'harness died: ' + ' | '.join(self.logs[-4:])
            l = l.rstrip('\n')
            if l.startswith('A '): k, h = (l.split(' ') + [''])[1:3]; self.actions.append((int(k), bytes.fromhex(h)))
            elif l.startswith('S '): self.state = [int(x) for x in l.split()[1:]]; return
            elif l.startswith('V '): self.value = int(l[2:])
            elif l.startswith('B '): self.began = int(l[2:])
            else: self.logs.append(l)
    def cmd(self, c):
        self.p.stdin.write(c + '\n'); self.p.stdin.flush(); self._read(); return self
    def take(self, kind):
        assert self.actions, ('no action, expected', kind, self.state, self.logs[-3:])
        k, b = self.actions.pop(0); assert k == K[kind], ('action', k, 'expected', kind, self.logs[-3:]); return b
    def none(self): assert not self.actions, self.actions
    def ok(self): return self.cmd('R 1')
    def mgmt(self, f): return self.cmd('M ' + f.hex())
    def eapol(self, f): return self.cmd('E ' + f.hex())
    def tick(self, ms): return self.cmd('T %d' % ms)
    def again(self): self.actions = []; self.logs = []; self.cmd('N'); assert self.began == 1; return self
    phase = property(lambda s: s.state[0]); outcome = property(lambda s: s.state[1])
    def log(self, text): return any(text in l for l in self.logs)
    def close(self): self.p.stdin.close(); self.p.wait()

def to_auth(s):
    s.take('PREPARE'); s.ok(); s.take('CHANNEL'); s.ok(); s.cmd('G'); return s.take('AUTH')
def sae_exchange(s, h2e, password=PW, token=False):
    f = to_auth(s)
    assert f[:2] == b'\xb0\x00' and f[4:10] == AP and f[10:16] == SELF and struct.unpack('<HHH', f[24:30]) == (3, 1, 126 if h2e else 0), f[:30].hex()
    if token:
        tok = os.urandom(32); s.ok()
        s.mgmt(hdr(0xb0) + struct.pack('<HHH', 3, 1, 76) + struct.pack('<H', 19) + ((bytes([255, 33, 93]) + tok) if h2e else tok))
        g = s.take('AUTH'); assert s.log('anti-clogging token received')
        if h2e: assert g[30:128] == f[30:128] and g[128:] == bytes([255, 33, 93]) + tok
        else: assert g[30:32] == f[30:32] and g[32:64] == tok and g[64:] == f[32:]; g = g[:32] + g[64:]
        f = g
    ap = ref.Sae(AP, SELF, password, SSID, h2e); ap.peer_commit(f[30:128])
    s.ok(); s.mgmt(hdr(0xb0) + struct.pack('<HHH', 3, 1, 126 if h2e else 0) + ap.commit())
    c = s.take('AUTH'); assert struct.unpack('<HHH', c[24:30]) == (3, 2, 0) and len(c) == 64
    return ap, c
def associate(s, expect_rsn, expect_rsnxe=b'', comeback=False):
    f = s.take('ASSOC'); assert f[:2] == b'\x00\x00'
    ies = f[28:]; found = {}
    while ies: found[ies[0]] = ies[:2 + ies[1]]; ies = ies[2 + ies[1]:]
    assert found[0][2:] == SSID and found[48] == expect_rsn, (found[48].hex(), expect_rsn.hex())
    assert found.get(244, b'') == expect_rsnxe
    s.ok()
    if comeback:
        s.mgmt(hdr(0x10) + struct.pack('<HHH', 0x11, 30, 0) + bytes([1, 4, 0x82, 0x84, 0x8b, 0x96, 56, 5, 3]) + struct.pack('<I', 200)); s.none()
        assert s.log('association comeback'); s.tick(100); s.none(); s.tick(130); assert s.take('ASSOC') == f; s.ok()
    s.mgmt(hdr(0x10) + struct.pack('<HHH', 0x11, 0, 0xc001) + bytes([1, 4, 0x82, 0x84, 0x8b, 0x96]))
    s.take('CONFIGURE'); s.ok(); assert s.phase == 9, s.state
def handshake(s, auth, expect_m2, pmf, flags, pmkid=None, extra=b''):
    s.eapol(auth.m1(pmkid)); m2 = s.take('EAPOL'); auth.m2(m2, expect_m2); s.ok()
    s.eapol(auth.m3(extra=extra)); m4 = s.take('EAPOL'); auth.m4(m4); s.ok()
    k = s.take('PTK'); assert len(k) == 64 and k[0] == 1 and k[1] == 1 and k[2] == 1 and k[4:10] == AP and k[11] == 4 and k[13] == 16 and k[14] == 5 and k[16:32] == auth.tk, 'PTK command'
    assert k[15] == (1 if pmf and flags & 1 else 0), ('ucMgmtProtection', k[15])
    s.ok(); s.cmd('K')
    g = s.take('GTK'); assert g[0] == 1 and g[2] == 0 and g[11] == 4 and g[12] == auth.gtk_id and g[14] == 4 and g[16:32] == auth.gtk, 'GTK command'
    s.ok(); s.take('DATA'); s.ok(); assert s.phase == CONNECTED and s.outcome == 1, s.state
def teardown(s, first=True):
    if first:
        s.cmd('C'); d = s.take('DEAUTH'); assert d == struct.pack('<HH', 0xc0, 0) + AP + SELF + AP + bytes(2) + bytes([3, 0]), d.hex()
        s.ok()
    s.take('DEL_PTK'); s.ok(); s.take('DEL_GTK'); s.ok()
    while s.actions and s.actions[0][0] == K['DEL_GTK']: s.take('DEL_GTK'); s.ok()        # one per installed GTK slot (rekey)
    s.take('RETIRE'); s.ok(); s.take('ABORT'); s.ok(); assert s.phase == DONE, s.state
def open_auth(s):
    f = to_auth(s); assert f[24:30] == struct.pack('<HHH', 0, 1, 0) and len(f) == 30; s.ok(); s.mgmt(hdr(0xb0) + struct.pack('<HHH', 0, 2, 0))
def legacy_join(s, ap_rsn):
    open_auth(s); associate(s, LEGACY_RSN); auth = ref.Authenticator(2, s.pmk, AP, SELF, ap_rsn); handshake(s, auth, LEGACY_RSN, False, 0); return auth

def main(harness):
    n = 0; ALL, REQ = 7, 15
    # --- WPA3-only (SAE, MFPR), both PWE methods, with and without an anti-clogging round and association comeback
    for h2e in (0, 1):
        for token in (False, True):
            flags = 1 if token else 0
            ap_rsn = rsn([8], 0xc0); ies = ap_rsn + (RSNXE if h2e else b'')
            s = Station(harness, 12, flags, ap_rsn, ies)
            assert s.log('profile akm=8 pmf=1 required=1 h2e=%d' % h2e), s.logs
            ap, c = sae_exchange(s, h2e, token=token); assert ap.peer_confirm(c[30:]), 'AP rejects the station confirm'
            s.ok(); s.mgmt(hdr(0xb0) + struct.pack('<HHH', 3, 2, 0) + ap.confirm())
            assert s.log('confirm ok') and s.log('PMK done')
            sta_rsn = rsn([8], 0xc0); associate(s, sta_rsn, RSNXE if h2e else b'', comeback=token)
            auth = ref.Authenticator(8, ap.pmk, AP, SELF, ap_rsn, RSNXE if h2e else b'', pmf=True)
            handshake(s, auth, sta_rsn + (RSNXE if h2e else b''), True, flags, pmkid=ap.pmkid, extra=bytes([0xdd, 5, 0x50, 0x6f, 0x9a, 0x20, 1]))
            assert s.log('authorized (akm=8 pmf=1 igtk=1)')
            s.eapol(auth.group1(new_igtk=True)); g = s.take('REKEY'); assert g[12] == auth.gtk_id and g[16:32] == auth.gtk; s.ok(); auth.group2(s.take('GROUP')); s.ok(); assert s.phase == CONNECTED
            if h2e: s.mgmt(hdr(0xc0) + bytes([7, 0])); assert s.outcome == 5 and s.log('unprotected deauth reason 7 honoured'); teardown(s, first=False)
            else: teardown(s)
            s.close(); n += 1
    # --- PMF flag 2: unprotected deauth/disassoc ignored once the pairwise key is installed; link-loss still ends the link
    ap_rsn = rsn([8], 0xc0); s = Station(harness, 12, 2, ap_rsn, ap_rsn); ap, c = sae_exchange(s, 0); s.ok()
    s.mgmt(hdr(0xb0) + struct.pack('<HHH', 3, 1, 0) + ap.commit()); s.none()                      # repeated AP commit is ignored in the confirm step
    s.mgmt(hdr(0xb0) + struct.pack('<HHH', 3, 2, 0) + ap.confirm()); associate(s, ap_rsn)
    auth = ref.Authenticator(8, ap.pmk, AP, SELF, ap_rsn, pmf=True); handshake(s, auth, ap_rsn, True, 2)
    s.mgmt(hdr(0xc0) + bytes([7, 0])); s.mgmt(hdr(0xa0) + bytes([6, 0])); s.none(); assert s.phase == CONNECTED and s.log('unprotected disassoc reason 6 ignored')
    s.cmd('L'); assert s.outcome == 5; s.close(); n += 1
    # --- transition AP (AKM 2+8, MFPC): SAE when requested; a failed SAE attempt makes the next one use WPA2-PSK, then SAE again
    ap_rsn = rsn([2, 8], 0x80); s = Station(harness, REQ, 0, ap_rsn, ap_rsn); assert s.log('profile akm=8 pmf=1 required=0 h2e=0')
    ap, c = sae_exchange(s, 0, password=b'not the password'); s.ok(); s.mgmt(hdr(0xb0) + struct.pack('<HHH', 3, 2, 0) + ap.confirm())
    assert s.outcome == 2 and s.log('confirm mismatch') and not ap.peer_confirm(c[30:])
    s.again(); assert s.log('prefers the WPA2 alternative') and s.log('profile akm=2 pmf=0'); legacy_join(s, ap_rsn); s.cmd('L')
    s.again(); assert s.log('profile akm=8 pmf=1') and not s.log('prefers'); ap, c = sae_exchange(s, 0); s.ok(); s.mgmt(hdr(0xb0) + struct.pack('<HHH', 3, 2, 0) + ap.confirm())
    sta_rsn = rsn([8], 0x80); associate(s, sta_rsn); auth = ref.Authenticator(8, ap.pmk, AP, SELF, ap_rsn, pmf=True); handshake(s, auth, sta_rsn, True, 0); teardown(s); s.close(); n += 1
    # --- transition AP, SAE possible but not requested: the pre-0432 WPA2-PSK profile through the CI engine; WPA3-only AP: SAE anyway
    s = Station(harness, ALL, 0, ap_rsn, ap_rsn); assert s.log('profile akm=2 pmf=0'); legacy_join(s, ap_rsn); s.close()
    ap3 = rsn([8], 0xc0); s = Station(harness, ALL, 0, ap3, ap3); assert s.log('profile akm=8 pmf=1 required=1'); s.close(); n += 2
    # --- refusals
    s = Station(harness, 12, 0, ap3, ap3); to_auth(s); s.ok()
    s.mgmt(hdr(0xb0) + struct.pack('<HHH', 3, 1, 77) + struct.pack('<H', 19)); assert s.outcome == 2 and s.log('status 77'); s.close()
    s = Station(harness, 12, 0, ap3, ap3); f = to_auth(s); s.ok()
    s.tick(530); assert s.take('AUTH') == f; s.ok(); s.tick(530); assert s.take('AUTH') == f; s.ok(); s.tick(530); assert s.outcome == 4 and s.log('no answer to commit'); s.close()
    s = Station(harness, 12, 0, ap3, ap3); ap, c = sae_exchange(s, 0); s.ok()
    s.mgmt(hdr(0xb0) + struct.pack('<HHH', 3, 2, 0) + ap.confirm()); associate(s, ap3)
    auth = ref.Authenticator(8, ap.pmk, AP, SELF, ap3, pmf=True)
    s.eapol(auth.m1()); auth.m2(s.take('EAPOL'), ap3); s.ok(); s.eapol(auth.m3(igtk=False)); assert s.outcome == 2 and s.log('EAPOL-Key refused'), 'M3 without IGTK must fail under PMF'; s.close()
    p = subprocess.run([harness, '4', '0', PW.decode(), SSID.decode(), '00' * 32, LEGACY_RSN.hex(), LEGACY_RSN.hex()], input='', capture_output=True, text=True)
    assert 'B -1' in p.stdout and 'no acceptable AKM' in p.stdout                                 # SAE-only request, WPA2-only AP
    n += 4
    # --- WPA2-PSK-SHA256 (AKM 6) and WPA2-PSK with PMF required (AKM 2, MFPR)
    for akm in (6, 2):
        ap_rsn = rsn([akm], 0xc0); s = Station(harness, 6, 1, ap_rsn, ap_rsn); assert s.log('profile akm=%d pmf=1 required=1' % akm)
        open_auth(s); associate(s, ap_rsn); auth = ref.Authenticator(akm, s.pmk, AP, SELF, ap_rsn, pmf=True); handshake(s, auth, ap_rsn, True, 1)
        s.eapol(auth.group1()); s.take('REKEY'); s.ok(); auth.group2(s.take('GROUP')); s.ok(); assert s.phase == CONNECTED
        teardown(s); s.close(); n += 1
    # --- AKM 6 without PMF at the AP
    ap_rsn = rsn([6], 0, False); s = Station(harness, 3, 0, ap_rsn, ap_rsn); open_auth(s)
    associate(s, ap_rsn); auth = ref.Authenticator(6, s.pmk, AP, SELF, ap_rsn); handshake(s, auth, ap_rsn, False, 0)
    s.mgmt(hdr(0xc0) + bytes([3, 0])); assert s.outcome == 5; s.close(); n += 1
    # --- no secret record: the unchanged legacy entry (CI engine), with group rekey
    s = Station(harness, 0, 0, LEGACY_RSN, LEGACY_RSN); auth = legacy_join(s, LEGACY_RSN)
    s.eapol(auth.group1()); s.take('REKEY'); s.ok(); auth.group2(s.take('GROUP')); s.ok()
    s.mgmt(hdr(0xc0) + bytes([3, 0])); assert s.outcome == 5; s.close(); n += 1
    print('PASS station transaction against the independent AP: %d scenarios (SAE H&P/H2E, token, comeback, AKM 8/6/2 four-way with CMAC/SHA1, IGTK KDE, PMF flags, SAE retreat, refusals, legacy)' % n)
if __name__ == '__main__': main(sys.argv[1])
