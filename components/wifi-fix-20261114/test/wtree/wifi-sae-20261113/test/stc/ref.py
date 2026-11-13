"""Independent reference for the 0432..0434 host tests: SAE group 19 (hunting-and-pecking, hash-to-element),
the AKM 2/6/8 four-way handshake and group rekey as an access point would run them. Pure Python integers
for the curve, hashlib/hmac for the hashes, 'cryptography' for AES-CMAC and RFC 3394 key wrap. No code is
shared with the C sources."""
import hashlib, hmac, os, struct
from cryptography.hazmat.primitives import cmac as _cmac
from cryptography.hazmat.primitives.ciphers import algorithms
from cryptography.hazmat.primitives.keywrap import aes_key_wrap

P = 0xffffffff00000001000000000000000000000000ffffffffffffffffffffffff
N = 0xffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551
A = P - 3
Bc = 0x5ac635d8aa3a93e7b3ebbd55769886bc651d06b0cc53b0f63bce3c3e27d2604b

def hm(key, *parts): return hmac.new(key, b''.join(parts), hashlib.sha256).digest()
def kdf(key, label, context, bits):
    out = b''; i = 1
    while len(out) * 8 < bits:
        out += hm(key, struct.pack('<H', i), label, context, struct.pack('<H', bits)); i += 1
    return out[:bits // 8]
def add(p, q):
    if p is None: return q
    if q is None: return p
    if p[0] == q[0]:
        if (p[1] + q[1]) % P == 0: return None
        l = (3 * p[0] * p[0] + A) * pow(2 * p[1], -1, P) % P
    else:
        l = (q[1] - p[1]) * pow(q[0] - p[0], -1, P) % P
    x = (l * l - p[0] - q[0]) % P
    return (x, (l * (p[0] - x) - p[1]) % P)
def mul(k, p):
    r = None
    while k:
        if k & 1: r = add(r, p)
        p = add(p, p); k >>= 1
    return r
def on_curve(p): return (p[1] * p[1] - (p[0] ** 3 + A * p[0] + Bc)) % P == 0
def sqrt(v):
    y = pow(v, (P + 1) // 4, P)
    return y if y * y % P == v % P else None
def b32(v): return v.to_bytes(32, 'big')
def point(b): return (int.from_bytes(b[:32], 'big'), int.from_bytes(b[32:64], 'big'))
def pbytes(p): return b32(p[0]) + b32(p[1])

def pwe_loop(a, b, password):
    key = max(a, b) + min(a, b); found = None
    for counter in range(1, 41):
        seed = hm(key, password, bytes([counter]))
        x = int.from_bytes(kdf(seed, b'SAE Hunting and Pecking', b32(P), 256), 'big')
        if x < P and found is None:
            y = sqrt((x ** 3 + A * x + Bc) % P)
            if y is not None:
                if (y & 1) != (seed[-1] & 1): y = P - y
                found = (x, y)
    return found
def sswu(u):
    z = P - 10
    m = (z * z * pow(u, 4, P) + z * u * u) % P
    t = pow(m, P - 2, P)
    x1 = Bc * pow(z * A, -1, P) % P if m == 0 else (-Bc * pow(A, -1, P)) * (1 + t) % P
    gx1 = (x1 ** 3 + A * x1 + Bc) % P
    x2 = z * u * u * x1 % P
    gx2 = (x2 ** 3 + A * x2 + Bc) % P
    if pow(gx1, (P - 1) // 2, P) == 1: x, v = x1, gx1
    else: x, v = x2, gx2
    y = sqrt(v)
    if (u & 1) != (y & 1): y = P - y
    return (x, y)
def hkdf_expand(prk, info, n):
    out = b''; t = b''; i = 1
    while len(out) < n:
        t = hm(prk, t, info, bytes([i])); out += t; i += 1
    return out[:n]
def pt(ssid, password, identifier=b''):
    seed = hm(ssid, password, identifier)
    u1 = int.from_bytes(hkdf_expand(seed, b'SAE Hash to Element u1 P1', 48), 'big') % P
    u2 = int.from_bytes(hkdf_expand(seed, b'SAE Hash to Element u2 P2', 48), 'big') % P
    return add(sswu(u1), sswu(u2))
def pwe_pt(ptv, a, b):
    val = int.from_bytes(hm(bytes(32), max(a, b), min(a, b)), 'big') % (N - 1) + 1
    return mul(val, ptv)

class Sae:
    """One SAE party (used as the access point)."""
    def __init__(self, own, peer, password, ssid, h2e, rand=None, mask=None):
        self.own, self.peer, self.h2e = own, peer, h2e
        self.pwe = pwe_pt(pt(ssid, password), own, peer) if h2e else pwe_loop(own, peer, password)
        self.rand = rand or (int.from_bytes(os.urandom(32), 'big') % (N - 2) + 2)
        mask = mask or (int.from_bytes(os.urandom(32), 'big') % (N - 2) + 2)
        self.scalar = (self.rand + mask) % N
        e = mul(mask, self.pwe); self.element = (e[0], P - e[1])
        self.sc = 0
    def commit(self): return struct.pack('<H', 19) + b32(self.scalar) + pbytes(self.element)
    def peer_commit(self, body):
        assert struct.unpack('<H', body[:2])[0] == 19 and len(body) >= 98
        self.ps = int.from_bytes(body[2:34], 'big'); self.pe = point(body[34:98])
        assert 1 < self.ps < N and on_curve(self.pe)
        k = mul(self.rand, add(mul(self.ps, self.pwe), self.pe))
        seed = hm(bytes(32), b32(k[0]))
        val = b32((self.scalar + self.ps) % N)
        keys = kdf(seed, b'SAE KCK and PMK', val, 512)
        self.kck, self.pmk, self.pmkid = keys[:32], keys[32:], val[:16]
    def confirm(self):
        self.sc += 1; sc = struct.pack('<H', self.sc)
        return sc + hm(self.kck, sc, b32(self.scalar), pbytes(self.element), b32(self.ps), pbytes(self.pe))
    def peer_confirm(self, body):
        return hmac.compare_digest(body[2:34], hm(self.kck, body[:2], b32(self.ps), pbytes(self.pe), b32(self.scalar), pbytes(self.element)))

def prf_sha1(key, label, data, n):
    out = b''; i = 0
    while len(out) < n:
        out += hmac.new(key, label + b'\0' + data + bytes([i]), hashlib.sha1).digest(); i += 1
    return out[:n]
def cmac(key, data):
    c = _cmac.CMAC(algorithms.AES(key)); c.update(data); return c.finalize()

class Authenticator:
    """EAPOL-Key authenticator for AKM 2 (version 2), 6 (version 3) and 8 (version 0), CCMP, optional PMF."""
    def __init__(self, akm, pmk, aa, spa, ap_rsn, ap_rsnxe=b'', pmf=False):
        self.akm, self.pmk, self.aa, self.spa, self.ap_rsn, self.ap_rsnxe, self.pmf = akm, pmk, aa, spa, ap_rsn, ap_rsnxe, pmf
        self.version = {2: 2, 6: 3, 8: 0}[akm]; self.replay = 0
        self.anonce = os.urandom(32); self.gtk = os.urandom(16); self.igtk = os.urandom(16); self.gtk_id = 1; self.igtk_id = 4
    def mic(self, frame):
        z = frame[:81] + bytes(16) + frame[97:]
        return hmac.new(self.kck, z, hashlib.sha1).digest()[:16] if self.akm == 2 else cmac(self.kck, z)
    def frame(self, info, nonce, data, mic=True, rsc=bytes(8)):
        self.replay += 1
        body = bytes([2]) + struct.pack('>HH', info, 16) + struct.pack('>Q', self.replay) + nonce + bytes(16) + rsc + bytes(8) + bytes(16) + struct.pack('>H', len(data)) + data
        f = bytes([2, 3]) + struct.pack('>H', len(body)) + body
        if mic: m = self.mic(f); f = f[:81] + m + f[97:]
        return f
    def m1(self, pmkid=None):
        data = b'' if pmkid is None else bytes([0xdd, 20, 0, 0x0f, 0xac, 4]) + pmkid
        return self.frame(0x0088 | self.version, self.anonce, data, mic=False)
    def m2(self, f, expect_data):
        info, = struct.unpack('>H', f[5:7]); assert info == (0x0108 | self.version), hex(info)
        assert struct.unpack('>Q', f[9:17])[0] == self.replay
        snonce = f[17:49]
        ctx = min(self.aa, self.spa) + max(self.aa, self.spa) + min(self.anonce, snonce) + max(self.anonce, snonce)
        ptk = prf_sha1(self.pmk, b'Pairwise key expansion', ctx, 48) if self.akm == 2 else kdf(self.pmk, b'Pairwise key expansion', ctx, 384)
        self.kck, self.kek, self.tk = ptk[:16], ptk[16:32], ptk[32:48]
        assert self.mic(f) == f[81:97], 'M2 MIC'
        n, = struct.unpack('>H', f[97:99]); assert f[99:99 + n] == expect_data, (f[99:99 + n].hex(), expect_data.hex())
    def kdes(self, igtk=True, extra=b''):
        d = self.ap_rsn + self.ap_rsnxe + bytes([0xdd, 22, 0, 0x0f, 0xac, 1, self.gtk_id, 0]) + self.gtk
        if self.pmf and igtk: d += bytes([0xdd, 28, 0, 0x0f, 0xac, 9]) + struct.pack('<H', self.igtk_id) + bytes(6) + self.igtk
        return d + extra
    def wrap(self, d):
        d += b'\xdd' if len(d) % 8 else b''
        while len(d) % 8: d += b'\0'
        if len(d) < 16: d += bytes(16 - len(d))
        return aes_key_wrap(self.kek, d)
    def m3(self, igtk=True, extra=b''):
        return self.frame(0x13c8 | self.version, self.anonce, self.wrap(self.kdes(igtk, extra)))
    def m4(self, f):
        info, = struct.unpack('>H', f[5:7]); assert info == (0x0308 | self.version), hex(info)
        assert self.mic(f) == f[81:97], 'M4 MIC'
    def group1(self, new_igtk=False):
        self.gtk = os.urandom(16); self.gtk_id = 3 - self.gtk_id
        if new_igtk: self.igtk = os.urandom(16); self.igtk_id = 9 - self.igtk_id
        d = bytes([0xdd, 22, 0, 0x0f, 0xac, 1, self.gtk_id, 0]) + self.gtk
        if self.pmf: d += bytes([0xdd, 28, 0, 0x0f, 0xac, 9]) + struct.pack('<H', self.igtk_id) + bytes(6) + self.igtk
        return self.frame(0x1380 | self.version, os.urandom(32), self.wrap(d))
    def group2(self, f):
        info, = struct.unpack('>H', f[5:7]); assert info == (0x0300 | self.version), hex(info)
        assert self.mic(f) == f[81:97], 'Group 2 MIC'

if __name__ == '__main__':
    h = bytes.fromhex
    a1, a2 = h('4d3f2fffe387'), h('a5d8aa958e3c')
    s = Sae(a1, a2, b'mekmitasdigoat', b'byteme', False, int('992465fd3daa3c60aa6565b7f62a2a7f2e12dd12f198faf4fbed89d7ff1ace94', 16), int('9507a90f777a044d6a0830b91ea3d5dd70bece44e1acffb86983b5e1bf9fb322', 16))
    assert s.commit().hex() == '13002e2c0f0db52440ad146d967114ce005ce1eab0aa2c2e5c2871b774f6c2575c65d5ad9e00829707aa36ba8b859738fc961d08243505f47c035376d7ac4bc8d7b95083bf43827d0fc31ed778dd3671fd21a46d1091d64b6f9a1e1272621325dbe1'
    s.peer_commit(h('1300591b96f3397fb945100848e7b550543b6720d88337ee93fc49fd6df7e08b5223e71b9bb048d3873f20556953a96c91536fd8ee6ca9b4a68a148b056a909be03e83ae208f60f8ef5537858074db06687032399862999b511e0a1552a5fea317c2'))
    assert s.kck.hex() == '1e733f6d9bd53256287304338831b09a39406d121017073a5c30db36f36cb81a' and s.pmk.hex() == '4e4dfab1a2dd8ac1a91790f953faaa452ae5c6873ab75b63605ba663f8a7fe59' and s.pmkid.hex() == '8747a600eea3f9f22475df58ca1e5498'
    w = pwe_pt(pt(b'byteme', b'mekmitasdigoat', b'psk4internet'), h('00095b66ec1e'), h('000b6bd90246'))
    assert pbytes(w).hex() == 'c93049b9e64000f848201649e999f2b5c22dea69b5632c9df4d633b8aa1f6c1e73634e94b53d82e7383a8d258199d9dc1a5ee8269d060382ccbf33e614ff59a0'
    print('PASS reference implementation reproduces IEEE 802.11-2020 Annex J.10 (commit, KCK, PMK, PMKID, H2E PWE)')
