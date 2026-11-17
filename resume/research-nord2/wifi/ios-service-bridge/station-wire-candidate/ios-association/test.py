from pathlib import Path
import subprocess,hashlib,re,json
P=Path(__file__).resolve().parent;B=P/'_build';B.mkdir(exist_ok=True)
R=next(p for p in P.parents if (p/'shim/nord2-xnu').is_dir())
reference=R/'research/reference/oosexgang/hackios-xnu-kernel/xnu/files/bsd/dev/redwood_apple80211.c'
raw=reference.read_bytes()
assert hashlib.sha256(raw).hexdigest()=='a6a61421c24e8ac649095093729e68c9e443855060dc9b94143815edb9e82240'
layout=re.search(r'struct apple80211_assoc \{.*?\n\};',raw.decode(),re.S).group()
evidence=subprocess.run(['python3',str(P/'evidence.py')],check=True,text=True,capture_output=True)
rsn=bytes.fromhex(json.loads((P/'rsn-oracle.json').read_text())['rsn22_hex'])
(B/'reference-layout.h').write_text('/* Exact pinned original reference struct and original-cache RSN suites. */\n'+layout+'\nstatic const unsigned char oracle_rsn[22]={'+','.join(map(str,rsn))+'};\n')
subprocess.run(['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I',str(B),str(P/'association.c'),str(P/'test.c'),'-o',str(B/'test')],check=True)
result=subprocess.run([str(B/'test')],check=True,text=True,capture_output=True)
(P/'verification.log').write_text(evidence.stdout+evidence.stderr+result.stdout+result.stderr)
print(evidence.stdout+result.stdout,end='')
