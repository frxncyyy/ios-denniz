#!/usr/bin/env python3
"""Replay refused corpus compute modules through one compiler; record packet facts."""
import ctypes as C, json, struct, subprocess, sys, hashlib
from pathlib import Path
lib, listing, out = sys.argv[1], sys.argv[2], Path(sys.argv[3])
if len(sys.argv) > 4:  # child: compile one module
    L = C.CDLL(lib)
    class Blob(C.Structure): _fields_ = [('data', C.c_void_p), ('size', C.c_size_t)]
    f = L.tegu_mali_compile_compute_v2
    f.argtypes = [C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t, C.POINTER(Blob), C.c_char_p, C.c_size_t]
    d = Path(sys.argv[4]).read_bytes(); b = Blob(); e = C.create_string_buffer(300)
    rc = f(d, len(d), None, 0, C.byref(b), e, 300)
    r = dict(rc=rc, error=e.value.decode())
    if rc == 0:
        p = C.string_at(b.data, b.size); h = struct.unpack('<32I', p[:128])
        r.update(size=b.size, w8=h[8], w16=h[16], w19=h[19], push=h[5], sha256=hashlib.sha256(p).hexdigest(),
                 manifest=[list(struct.unpack('<16I', p[128+64*i:192+64*i])[:7]) for i in range(h[13])])
    print(json.dumps(r)); sys.exit(0)
rows = json.load(open(listing)); res = []
for r in rows:
    try:
        c = subprocess.run([sys.executable, __file__, lib, listing, str(out), r['path']], capture_output=True, text=True, timeout=1200)
        x = json.loads(c.stdout.strip().splitlines()[-1]) if c.returncode == 0 and c.stdout.strip() else dict(rc=None, exit=c.returncode)
    except subprocess.TimeoutExpired:
        x = dict(rc=None, exit='timeout')
    x.update(sha256_spv=r['sha256'], before=r['refusal'], path=r['path']); res.append(x)
    out.write_text(json.dumps(res, indent=0))  # incremental (thermal throttling)
