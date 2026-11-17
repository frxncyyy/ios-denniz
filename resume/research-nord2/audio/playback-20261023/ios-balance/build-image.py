"""Offline complete speaker and rollback image builds. No device access."""
from pathlib import Path
import argparse,hashlib,json,struct,subprocess
P=Path(__file__).resolve().parent
R=next(p for p in P.parents if(p/'AGENTS.md').is_file())
if (P/'PACKING_PAUSED').exists():raise SystemExit('Packing paused for final source-backed pin-state review')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def run(label,args):
 with(P/(label+'.log')).open('xb')as f:subprocess.run(list(map(str,args)),cwd=R,stdout=f,stderr=subprocess.STDOUT,check=True)
s=json.loads((P/'stage.json').read_text());ci=R/('research/ci-'+str(s['ci_run']));record=json.loads((ci/'run.json').read_text())
assert record['status']=='completed' and record['conclusion']=='success' and record['headSha']==s['source_commit']
subprocess.run(['git','diff','--exit-code',s['source_commit'],'--','shim','tools','xnu','.github'],cwd=R,check=True)
base=R/'research/nord2/rescue/recompressed/base.img';assert sha(base)=='ed05ea02d0499fe4109e6498a6852e769c346226aa4a56a220338673f8a2f5bf'
kernel=ci/'xnu-kernel/kernel.development.sweet'
khash=sha(kernel)
expected=[l.split()[0]for l in(kernel.parent/'SHA256SUMS').read_text().splitlines()if len(l.split())==2 and Path(l.split()[1].lstrip('*')).name==kernel.name];assert expected==[khash]
baseline=R/'research/nord2/wifi-ios-scan-cancel/ios/baseline.adt'
old=json.loads((R/'research/nord2/wifi-ios-scan-cancel/ios/build-result.json').read_text());assert sha(baseline)==old['baseline_adt_sha256']
board=P.parent/'profile-sol/tfa98xx.cnt';assert sha(board)=='635300fbc96f258ff9a35f82435f2e9d3ff6f1370c156067b40146e011038692'
hal=ci/'nord2-audio-hal';provenance=json.loads((hal/'provenance.json').read_text());assert provenance['commit']=='a79ff0772a3454fd1ef127e181e5b3bce6c9ce18'
for name,h in provenance['outputs'].items():assert sha(hal/name)==h
# A committed mode1 install must use the same executable/plist as the phone.
assert sha(hal/'audio.pkg')=='82253445dad22fee379af4c141d6883aa91e322322bbe38dd62959a34af8b4aa'
run('package-check',['python3','tools/nord2-audio-package.py','verify',hal/'audio.pkg'])
run('adt',['python3','tools/nord2-audio-adt.py',baseline,hal/'audio.pkg',board,P/'playback.adt'])
run('isa',['python3','tools/nord2-check-rcpc2.py',kernel])
run('strip',[R/'../toolchains/cctools/bin/aarch64-apple-darwin-strip','-S','-x',kernel,'-o',P/'kernel.strip'])
run('loader',['sh','tools/nord2-build-loader.sh',P/'loader'])
reports=[]
for name,adt,extra in [('playback',P/'playback.adt','nord2audio=1 nord2audioinstall=1'),('rollback',baseline,'nord2audioinstall=2')]:
 cmd=old['commandline']+' '+extra;assert len(cmd)<608
 payload=P/(name+'-payload.bin');image=R/('shim/out/nord2-xnu/nord2-ios-audio-balance-'+name+'-stop23.img')
 run(name+'-payload',['python3','tools/mkpayload.py',P/'kernel.strip',adt,payload,'--cmdline',cmd])
 assert not image.exists(), 'Refuse to overwrite any existing boot image'
 run(name+'-pack',['python3','tools/nord2-pack-xnu.py',base,P/'loader/loader-Image',payload,image,'--zopfli',R/'../toolchains/zopfli/zopfli'])
 run(name+'-check',['python3','tools/nord2-check-xnu.py',image,payload,'--stock',base])
 assert 'FAILURES: none'in(P/(name+'-check.log')).read_text()
 d={'role':name,'image':str(image.relative_to(R)),'image_bytes':image.stat().st_size,'image_sha256':sha(image),'payload':str(payload.relative_to(R)),'payload_sha256':sha(payload),'adt_sha256':sha(adt),'commandline':cmd,'boot_spare_bytes':33554432-image.stat().st_size}
 assert d['boot_spare_bytes']>=0;reports.append(d)
report={'source_commit':s['source_commit'],'ci_run':s['ci_run'],'ci_attempt':record['attempt'],'kernel_sha256':khash,'baseline_adt_sha256':sha(baseline),'board_sha256':sha(board),'hal_package_sha256':sha(hal/'audio.pkg'),'images':reports,'authorized':False,'hardware_tested':False}
(P/'build-result.json').open('x').write(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2),flush=True)
