"""Compile the actual regenerated owning units before full CI. Offline only."""
from pathlib import Path
import json, subprocess, hashlib
P = Path(__file__).resolve().parent
R = next(p for p in P.parents if (p / 'AGENTS.md').is_file())
B = R / 'research/ci-39158169212/buildtree/xnu'
O = P / 'native-compile'
O.mkdir(exist_ok=False)
old = json.loads((R / 'research/nord2/wifi-vcn33/ios/focused/commands.json').read_text())
template = next(x for x in old if x['name'] == 'sweet_touch')
results = []
for name in ('wifi_station_wire', 'wifi_station_tx', 'wifi_station_transport', 'wifi_live_control', 'sweet_touch'):
    src = B / 'bsd/dev' / (name + '.c')
    cmd = [x for x in template['argv'] if x != '-I' + str(R / 'shim/nord2-xnu')]
    cmd[cmd.index('-c') + 1] = str(src)
    cmd[cmd.index('-o') + 1] = str(O / (name + '.o'))
    if '-MF' in cmd: cmd[cmd.index('-MF') + 1] = str(O / (name + '.d'))
    assert '-mcpu=generic' in cmd and '-march=armv8.2-a+crypto' in cmd
    with (O / (name + '.log')).open('xb') as log:
        p = subprocess.run(cmd, cwd=template['cwd'], stdout=log, stderr=subprocess.STDOUT)
    results.append(dict(name=name, exit=p.returncode, source_sha256=hashlib.sha256(src.read_bytes()).hexdigest(), argv=cmd))
    (O / 'report.json').write_text(json.dumps(results, indent=2) + '\n')
    print(name, p.returncode, flush=True)
    if p.returncode:
        print((O / (name + '.log')).read_text()[-4000:])
        raise SystemExit(p.returncode)
