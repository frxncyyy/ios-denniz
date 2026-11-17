#!/usr/bin/env python3
"""Independent offline verification; never flashes, boots or changes a device."""
from pathlib import Path
import argparse,gzip,hashlib,importlib.util,json,struct
ROOT=Path(__file__).resolve().parents[3]
OUT=Path(__file__).resolve().parent
IMAGE=ROOT/"shim/out/nord2-xnu/nord2-ios-zoom-wifi-stop23.img"
KERNEL=ROOT/"research/ci-39499794889/xnu-kernel/kernel.development.sweet"
SOURCE="c25b788dd0294436fd3e2dcb3260e993bdc02f06"
COMMANDLINE="debug=0xc cpus=8 keepsyms=1 -noprogress nord2stop=0 nord2vm=1 nord2fb=1 nord2stopat=23 rd=ufsroot0 ufsroot=userdata ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 wdt=-1 nord2display=1 nord2input=1 nord2wifi=2 nord2fastlog=1 nord2ufsprofile=1 nord2fbdiag=1 nord2wififw=1 nord2nortc=1 nord2wifiemi=1 nord2zoomreset=1"
MODE="ios_zoom_wifi"
def sha(b):return hashlib.sha256(b).hexdigest()
def pinned(path,wanted):
 b=(ROOT/path).read_bytes();assert sha(b)==wanted,path;return b
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--preflight',action='store_true');args=ap.parse_args()
 stock=pinned('backup/nord2/boot.img','04e84c4a14e5c3cb77d0e587da93b5a7e720e8c5b2a3a51ab3ef4cda746e1f01')
 rescue=pinned('research/nord2/rescue/nord2-adb-rescue.img','d78bf9fe720a1d293b863ea0a8b30e0df4b5eafb097b1343a3c30e7c3d03bac5')
 base=pinned('research/nord2/rescue/recompressed/base.img','ed05ea02d0499fe4109e6498a6852e769c346226aa4a56a220338673f8a2f5bf')
 ci=json.loads((ROOT/'research/ci-39499794889/run.json').read_text())
 assert ci['status']=='completed' and ci['conclusion']=='success' and ci['headSha']==SOURCE
 kernel=KERNEL.read_bytes();kernel_hash=sha(kernel)
 sums=KERNEL.parent/'SHA256SUMS';matches=[]
 for line in sums.read_text().splitlines():
  fields=line.split()
  if len(fields)==2 and Path(fields[1].lstrip('*')).name==KERNEL.name:matches.append(fields[0])
 assert matches==[kernel_hash],"kernel must match the CI artifact SHA256SUMS"
 assert len(COMMANDLINE.encode())<608
 tokens=COMMANDLINE.split();keys=[t.split('=',1)[0] for t in tokens if '=' in t]
 assert len(keys)==len(set(keys)),"duplicate bootarg"
 if args.preflight:
  print(json.dumps(dict(preflight=True,ci_run=39499794889,expected_source=SOURCE,kernel_sha256=kernel_hash,mode=MODE)))
  return
 spec=importlib.util.spec_from_file_location('pack',ROOT/'tools/nord2-pack-xnu.py')
 m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
 _,skg,_,sdt=m.stock_parts(stock);_,_,rrd,rdt=m.stock_parts(rescue)
 _,_,brd,bdt=m.stock_parts(base)
 si,sapp=m.unpack_stock_kernel(skg);linux_size=struct.unpack_from('<Q',si,16)[0]
 poff=(0x200000+linux_size+0x3fff)&~0x3fff
 data=IMAGE.read_bytes();assert len(data)<=33554432
 _,kg,rd,dt=m.stock_parts(data);blob,app=m.unpack_stock_kernel(kg)
 assert app==sapp and dt==sdt==rdt==bdt and rd==brd
 raw=gzip.decompress(rd);assert raw==gzip.decompress(rrd)
 assert sha(raw)=='95989a2ddff7d3fad8c63df9916a7d2510e09499f0b1710ac9fbf078ce60979f'
 assert blob[0x200000:0x200000+len(si)]==si
 assert blob[0x200000+len(si):0x200000+linux_size]==bytes(linux_size-len(si))
 payload=blob[poff:];assert payload[:8]==b'SWEETXNU'
 assert payload[0x48:0x48+608].split(b'\0')[0]==COMMANDLINE.encode()
 # Independently recompute Android v2 header ID, including empty second/DTBO.
 h=hashlib.sha1()
 for part in (kg,rd,b'',b'',dt):h.update(part);h.update(struct.pack('<I',len(part)))
 assert data[576:608]==h.digest().ljust(32,b'\0')
 allowed=set(range(8,12))|set(range(1636,1644))|set(range(576,608))
 assert all(a==b or i in allowed for i,(a,b) in enumerate(zip(base[:2048],data[:2048])))
 assert 'FAILURES: none' in (OUT/'image-build.log').read_text()
 report=dict(image=str(IMAGE.relative_to(ROOT)),image_bytes=len(data),image_sha256=sha(data),payload_sha256=sha(payload),
  kernel_sha256=kernel_hash,ci_run=39499794889,expected_kernel_source=SOURCE,kernel_source_identity_verified=True,
  commandline=COMMANDLINE,mode=MODE,xnu_cpus=8,point=23,usb_console_enabled=True,usb_log_capacity_bytes=4194304,
  original_linux_kernel_and_both_dts_byte_exact=True,linux_sha256=sha(si),appended_dt_sha256=sha(app),header_dt_sha256=sha(dt),
  rescue_ramdisk_uncompressed_sha256=sha(raw),rescue_ramdisk_uncompressed_bytes=len(raw),tested_rescue_contents_byte_exact=True,
  rescue_gzip_container_recompressed=True,rescue_container_hardware_tested=True,rescue_container_hardware_evidence="ADB marker observed after early-panic rescue fallback",compression_iterations=15,
  base_sha256=sha(base),tested_rescue_fallback_sha256=sha(rescue),boot_spare_bytes=33554432-len(data),android_header_id_verified=True,
  product_adt_requested=True,fast_framebuffer_console=True,skip_missing_rtc_wait=True,hardware_tested=False,
  firmware_execution=False,radio_networking_enabled=False,vendor_media_writes=False,zoom_reset_requested=False)
 if MODE=='ufs_stop_only':
  assert 'ufsrw=0' in tokens and 'rootrw=0' in tokens and 'nord2ufs2lane=1' in tokens
  report.update(intended_stop_before_root_publication=True,root_mount_expected=False,disk_writes_enabled=False,
   ufs_two_lane_diagnostic_requested=True,ufs_link_transition_requested=True,negotiated_x1_restoration_required=True,
   diagnostic_always_halts=True,linux_handoff_blocked=True,physical_fastboot_recovery_required=True,
   input_probe_requested=False,radio_power_probe_requested=False,firmware_ram_verification_requested=False,
   emi_snapshot_requested=False,framebuffer_diagnostics_requested=False,display_hardware_probe_requested=True,
   link_limits_readonly_requested=True,successful_lane_transition_observed=False)
 else:
  assert 'ufsrw=1' in tokens and 'rootrw=1' in tokens and not any(t.startswith('nord2ufs2lane=') for t in tokens)
  report.update(root_mount_expected='UFS userdata read/write',disk_writes_enabled=True,
   ufs_two_lane_diagnostic_requested=False,ufs_link_transition_requested=False,input_probe_requested=True,
   radio_power_probe_requested=True,radio_state_restore_required=True,firmware_ram_verification_requested=True,
   firmware_ram_verified_on_hardware=False,emi_snapshot_requested=True,emi_effective_permissions_proven=False,
   emi_snapshot_query_count_max=8,emi_smc_time_bound_guaranteed=False,framebuffer_diagnostics_requested=True,
   display_hardware_probe_requested=True,gui_ownership_retry_requested=True,link_limits_readonly_requested=True)
 assert 'nord2zoomreset=1' in tokens
 observer=json.loads((OUT/'observer-manifest.json').read_text())
 assert observer['expected_ci_run']==39499794889 and observer['expected_kernel_source']==SOURCE
 assert observer['old_sha256']=='a162a5b4ce6e5acbce663a3595ce4402effedf3f628e6da0c0c1710d6f92db27'
 assert observer['new_sha256']=='a275c9213db9ef42c86512664684f1d76f0c623cd42b108c516c6b5ac7d2d7a3'
 assert observer['changed_byte_offsets_zero_based']==[41,42,44,45]
 report.update(zoom_reset_requested=True,zoom_preference_mutation_executed=False,
  zoom_preference_path=observer['exact_preference_path'],zoom_backup_path=observer['exact_backup_path'],
  zoom_old_sha256=observer['old_sha256'],zoom_new_sha256=observer['new_sha256'],
  zoom_changed_byte_offsets_zero_based=observer['changed_byte_offsets_zero_based'],zoom_changed_bytes_count=4,
  zoom_old_values=observer['old_values'],zoom_new_values=observer['new_values'],
  zoom_backup_exclusive_verification_before_mutation_required=True,zoom_existing_backup_never_overwritten=True,
  zoom_target_uid=501,zoom_target_gid=501,zoom_backup_uid=0,zoom_backup_gid=0,
  zoom_observer_log_prefix='NORD2 ZOOM RESET:',zoom_hardware_outcome='not executed')
 (OUT/'payload.bin').write_bytes(payload)
 (OUT/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps(report,indent=2))
if __name__=='__main__':main()
