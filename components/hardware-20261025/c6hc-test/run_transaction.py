from pathlib import Path
import json,subprocess,sys,datetime,argparse
ap=argparse.ArgumentParser();ap.add_argument("--resume-after-offline",action="store_true");resume=ap.parse_args().resume_after_offline
p=Path.cwd();e=p/'gpu/build/hardware-20261025/c6hc-test';old=p/'gpu/build/hardware-20261025/c6gc-test'
a=json.loads((e/'authorization.json').read_text());main=Path(a['artifacts']['rescue']['path']).parents[3]
assert a['authorization']=='user-20261026-claude-c6h-compositor-install-and-ui'
assert a['artifacts']['addon']['sha256']=='028d2d66b8a68876a16549d7ff8756c787f1b39cda131c8a97151ad11a4e8111'
def run(stage,cmd):
 (e/(stage+'-command.json')).write_text(json.dumps(cmd,indent=2)+'\n')
 (e/'transaction-status.json').write_text(json.dumps({'stage':stage,'time':datetime.datetime.now(datetime.timezone.utc).isoformat()})+'\n')
 print(stage,flush=True)
 with (e/(stage+'.log')).open('w') as log:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True)
if not resume:
 run('snapshot',[sys.executable,str(p/'gpu/tools/snapshot_current_root.py'),'--main',str(main),'--seed',str(old/'offline-update/metal-rootfs.img'),'--output',str(e/'snapshot')])
 s=json.loads((e/'snapshot/verified.json').read_text());assert s['independent_full_hash_matches']
 run('offline-update',[sys.executable,str(p/'gpu/tools/update_metal_offline_vm.py'),'--main',str(main),'--snapshot',str(e/'snapshot/current-rootfs.img'),'--snapshot-sha256',s['sha256'],'--addon',a['artifacts']['addon']['path'],'--addon-sha256',a['artifacts']['addon']['sha256'],'--output',str(e/'offline-update'),'--journal-oracle',str(p/'gpu/build/hardware-20261024/hfs-source/oracle'),'--vm-kernel',str(p/'gpu/build/vm-runtime/vmlinuz-7.2.9-arch1-1')])
run('catalog',[sys.executable,str(e/'check_update_catalog.py')])
c=json.loads((e/'catalog-preservation.json').read_text());assert not c['outside_owned_paths']
run('device-install',[sys.executable,str(p/'gpu/tools/commit_root_delta.py'),'--main',str(main),'--before',str(e/'snapshot/current-rootfs.img'),'--after',str(e/'offline-update/metal-rootfs.img'),'--output',str(e/'device-install'),'--expected-addon-sha256',a['artifacts']['addon']['sha256'],'--commit','--skip-final-full-hash'])
i=json.loads((e/'device-install/verified.json').read_text());assert i['all_device_preimages_verified'] and i['changed_ranges_readback_verified']
(e/'transaction-status.json').write_text(json.dumps({'stage':'installed-verified-awaiting-manual-fastboot','time':datetime.datetime.now(datetime.timezone.utc).isoformat(),'expected_after_sha256':i['after'],'verification':'changed-ranges-readback','final_full_hash_skipped':i['final_full_hash_skipped']})+'\n')
print('Installed; changed ranges read back and verified. Final full hash skipped at user request. No reboot issued.',flush=True)
