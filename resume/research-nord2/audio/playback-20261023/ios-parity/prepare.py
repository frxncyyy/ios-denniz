"""Offline receipt after both independent image reviews; never authorizes/flashes."""
from pathlib import Path
import json, hashlib, sys
if sys.flags.optimize:raise SystemExit('Python -O is not supported')
P=Path(__file__).resolve().parent
R=next(p for p in P.parents if(p/'AGENTS.md').is_file())
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
b=json.loads((P/'build-result.json').read_text())
c=json.loads((P/'ci-verification.json').read_text())
assert c['source_commit']==b['source_commit'] and c['ci_run']==b['ci_run']
assert c['kernel_sha256']==b['kernel_sha256']
images={}
for item in b['images']:
 role=item['role'];assert role in ('playback','rollback') and role not in images
 image=R/item['image'];assert sha(image)==item['image_sha256']
 assert image.stat().st_size==item['image_bytes']<=33554432
 review=P.parent/'profile-sol/image-review'/('parity-'+role)/'report.json'
 r=json.loads(review.read_text())
 assert r['verdict']=='PASS' and r['source']==b['source_commit']
 assert r['ci_run']==b['ci_run'] and r['mode']==role
 assert r['image_sha256']==item['image_sha256'] and r['hardware_tested'] is False
 images[role]={**item,'review':str(review.relative_to(R)), 'review_sha256':sha(review)}
assert set(images)=={'playback','rollback'}
f=json.loads((P/'fallback-verification.json').read_text())['fallbacks'][:2]
for i in f:assert sha(R/i['path'])==i['sha256']
out={
 'state':'PREPARED_VERIFIED_NOT_AUTHORIZED',
 'source_commit':b['source_commit'],'ci_run':b['ci_run'],'ci_attempt':b['ci_attempt'],
 'kernel_sha256':b['kernel_sha256'],'images':images,
 'ci_verification_sha256':sha(P/'ci-verification.json'),
 'host_validation_sha256':sha(P/'host-validation.json'),
 'runner_sha256':sha(P/'run-authorized.py'),
 'test_plan_sha256':sha(P/'hardware-test-plan.json'),
 'scope':'boot-and-ios-hal-with-backup',
 'persistent_changes':['Reuse installed package exactly; validate original System HAL in HAL.nord2-original',
  'Retain exact previously verified RedwoodAudio.driver',
  'Restore original VirtualAudio.plugin from HAL.disabled',
  'Durable journal and package-independent rollback mode2'],
 'features':['48kHz S16 stereo speaker PCM via MT6885 AFE/I2S3 and both TFA9874 amps',
  'Pinned unmodified HackiOS/Topaz HAL integration',
  'USB live status/enable/stop/fixed quiet tone/silent DMA test'],
 'fallbacks':f,'old_kernel_alone_does_not_rollback_hal':True,
 'limits':['No physical audio success proven',
  'Microphone and telephony audio not implemented',
  'Initial hardware digital gain limited to256/32768',
  'Pinned Nord2 A16 board container; exact handset container identity not dumped'],
 'exact_image_authorized':False,'hardware_tested':False}
(P/'prepared.json').open('x').write(json.dumps(out,indent=2)+'\n')
print(json.dumps({k:out[k]for k in('state','source_commit','ci_run','images')},indent=2))
