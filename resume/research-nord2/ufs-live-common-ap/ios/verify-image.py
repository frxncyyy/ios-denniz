#!/usr/bin/env python3
"""Independent offline verification; never flashes, boots or changes a device."""
from pathlib import Path
import argparse,gzip,hashlib,importlib.util,json,struct,re
ROOT=Path(__file__).resolve().parents[4]
OUT=Path(__file__).resolve().parent
IMAGE=ROOT/"shim/out/nord2-xnu/nord2-ios-ufs2-wifi-ap-stop23.img"
COMMANDLINE="debug=0xc cpus=8 keepsyms=1 -noprogress nord2stop=0 nord2vm=1 nord2fb=1 nord2stopat=23 rd=ufsroot0 ufsroot=userdata ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 wdt=-1 nord2display=1 nord2input=1 nord2wifi=2 nord2fastlog=1 nord2ufsprofile=1 nord2fbdiag=1 nord2wififw=1 nord2nortc=1 nord2wifiemi=0 nord2wificommon=1 nord2ufs2live=1"
MODE="ios_live_x2_common_ap"
PRODUCT_ADT="research/nord2/ipsw/d22ap.adt"
PRODUCT_ADT_SHA256="9e670987c80493bd69aba7be62e3179c04663324d9249b0973675baac175dbf1"
def sha(b):return hashlib.sha256(b).hexdigest()
def pinned(path,wanted):
 b=(ROOT/path).read_bytes();assert sha(b)==wanted,path;return b
def load_identity():
 cfg=json.loads((OUT/'identity-required.json').read_text())
 assert cfg.get('parent_verified') is True, 'BLOCKED: parent has not supplied and verified CI identity'
 assert isinstance(cfg.get('ci_run'),int) and cfg['ci_run']>0, 'BLOCKED: missing CI run'
 assert isinstance(cfg.get('source_commit'),str) and re.fullmatch(r'[0-9a-f]{40}',cfg['source_commit']), 'BLOCKED: missing fullsourceSHA'
 assert isinstance(cfg.get('kernel_sha256'),str) and re.fullmatch(r'[0-9a-f]{64}',cfg['kernel_sha256']), 'BLOCKED: missingkernelSHA256'
 for key in ('kernel_path','ci_run_json'):
  assert isinstance(cfg.get(key),str) and cfg[key] and not Path(cfg[key]).is_absolute() and '..' not in Path(cfg[key]).parts, 'BLOCKED: missing/untrusted '+key
 assert f"research/ci-{cfg['ci_run']}/" in cfg['kernel_path'] and f"research/ci-{cfg['ci_run']}/" in cfg['ci_run_json'], 'BLOCKED: CI pathidentity mismatch'
 return cfg

def request_metadata(mode=MODE):
 assert mode==MODE
 return dict(prepared_only=True,hardware_tested=False,successful_lane_transition_observed=False,
  strategy_cost_counters_requested=True,strategy_cost_report_expected=True,
  ufs_live_x2_requested=True,ufs_two_lane_diagnostic_requested=False,
  ufs_live_retains_x2_on_verified_success=True,negotiated_x1_restoration_required=False,
  root_mount_expected='UFS userdata read/write ONLY after verified live phase5 proof',
  root_allowed_only_after=dict(phase=5,commands=60,sets=18,reads=2,verified=1,rollback=0,original=0),
  linux_handoff_permanently_blocked_from_arming=True,manual_physical_reset_required=True,
  automatic_rescue_fallback_allowed=False,rescue_contents_present=True,
  rescue_recovery='manual fastboot flash of known rescue after physical reset',
  runtime_io_failure_action='retain DMA/ownership panic; manual reset only',
  profile_link_and_limits_scope='pre-transition x1 baseline only',
  firmware_execution=False,radio_networking_enabled=False,rf_enabled=False,vendor_media_writes=False,
  wifi_common_init_requested=True,wifi_common_stage_requested=2,
  wifi_common_register_mutation_requested=True,wifi_common_restore_every_touched_register_required=True,
  wifi_common_outer_power_restore_required=True,wifi_common_dirty_restore_blocks_handoff=True,
  wifi_common_ap_observability_requested=True,wifi_common_runtime_result=None,
  firmware_ram_verification_requested=True,firmware_ram_verified_on_hardware=False,
  emi_snapshot_requested=False,emi_effective_permissions_proven=False,
  input_probe_requested=True,display_hardware_probe_requested=True,disk_writes_enabled=True,
  measured_boot_speedup=None,measured_cpu_clock_hz=None,zoom_reset_requested=False)

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--preflight',action='store_true');ap.add_argument('--kernel-path',action='store_true');args=ap.parse_args()
 cfg=load_identity();SOURCE=cfg['source_commit'];EXPECTED_KERNEL_SHA256=cfg['kernel_sha256'];KERNEL=ROOT/cfg['kernel_path'];CI_RUN=cfg['ci_run']
 assert re.fullmatch(r'[0-9a-f]{64}',EXPECTED_KERNEL_SHA256), 'build blocked: parent must supply verified exact CI kernel SHA256'
 pinned(PRODUCT_ADT,PRODUCT_ADT_SHA256)
 stock=pinned('backup/nord2/boot.img','04e84c4a14e5c3cb77d0e587da93b5a7e720e8c5b2a3a51ab3ef4cda746e1f01')
 rescue=pinned('research/nord2/rescue/nord2-adb-rescue.img','d78bf9fe720a1d293b863ea0a8b30e0df4b5eafb097b1343a3c30e7c3d03bac5')
 base=pinned('research/nord2/rescue/recompressed/base.img','ed05ea02d0499fe4109e6498a6852e769c346226aa4a56a220338673f8a2f5bf')
 ci=json.loads((ROOT/cfg['ci_run_json']).read_text())
 assert ci['status']=='completed' and ci['conclusion']=='success' and ci['headSha']==SOURCE
 kernel=KERNEL.read_bytes();kernel_hash=sha(kernel)
 assert kernel_hash==EXPECTED_KERNEL_SHA256,"kernel differs from parent-pinned SHA256"
 sums=KERNEL.parent/'SHA256SUMS';matches=[]
 for line in sums.read_text().splitlines():
  fields=line.split()
  if len(fields)==2 and Path(fields[1].lstrip('*')).name==KERNEL.name:matches.append(fields[0])
 assert matches==[kernel_hash],"kernel must match the CI artifact SHA256SUMS"
 assert len(COMMANDLINE.encode())<608
 tokens=COMMANDLINE.split();keys=[t.split('=',1)[0] for t in tokens if '=' in t]
 assert len(keys)==len(set(keys)),"duplicate bootarg"
 assert "nord2zoomreset" not in keys,"zoom reset must be absent, not zero"
 assert "nord2wifiemi=0" in tokens
 assert "nord2cpuclock" not in keys
 assert "nord2wificommon=1" in tokens
 assert 'nord2ufs2live=1' in tokens and not any(t.startswith('nord2ufs2lane=') for t in tokens)
 if args.kernel_path:
  print(str(KERNEL));return
 if args.preflight:
  print(json.dumps(dict(preflight=True,ci_run=CI_RUN,expected_source=SOURCE,kernel_sha256=kernel_hash,mode=MODE)))
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
 assert payload[0x48:0x48+608]==COMMANDLINE.encode().ljust(608,b'\0')
 import sys,subprocess,tempfile
 sys.path.insert(0,str(ROOT/'tools'))
 import mkpayload
 import adt
 adt_off,adt_size=struct.unpack_from('<QQ',payload,0x20)
 assert adt_off+adt_size<=len(payload)
 asp=importlib.util.spec_from_file_location('nord2_expected_adt',ROOT/'tools/mkadt-nord2.py')
 am=importlib.util.module_from_spec(asp);asp.loader.exec_module(am)
 expected_adt=adt.serialize(am.build(str(ROOT/PRODUCT_ADT)))
 assert payload[adt_off:adt_off+adt_size]==expected_adt,'AppleADT/product differs from exact expectedNord2ADT'
 with tempfile.TemporaryDirectory(prefix='nord2-independent-verify-') as temporary:
  stripped=Path(temporary)/'kernel.strip'
  subprocess.run([str(ROOT/'../toolchains/cctools/bin/aarch64-apple-darwin-strip'),'-S','-x',str(KERNEL),'-o',str(stripped)],check=True)
  expected=bytearray(stripped.read_bytes())
 mach=mkpayload.parse_macho(expected);mkpayload.apply_chained_fixups(expected,mach)
 segments=[seg for seg in mach['segs'] if seg['vmsize']]
 assert struct.unpack_from('<II',payload,8)==(1,len(segments))
 assert struct.unpack_from('<Q',payload,0x18)[0]==mach['entry']
 for i,seg in enumerate(segments):
  va,vm,off,size=struct.unpack_from('<4Q',payload,0x2a8+32*i)
  assert (va,vm,size)==(seg['vmaddr'],seg['vmsize'],seg['filesize'])
  assert off+size<=len(payload)
  assert payload[off:off+size]==expected[seg['fileoff']:seg['fileoff']+size]

 # Independently recompute Android v2 header ID, including empty second/DTBO.
 h=hashlib.sha1()
 for part in (kg,rd,b'',b'',dt):h.update(part);h.update(struct.pack('<I',len(part)))
 assert data[576:608]==h.digest().ljust(32,b'\0')
 allowed=set(range(8,12))|set(range(1636,1644))|set(range(576,608))
 assert all(a==b or i in allowed for i,(a,b) in enumerate(zip(base[:2048],data[:2048])))
 assert 'FAILURES: none' in (OUT/'image-build.log').read_text()
 report=dict(image=str(IMAGE.relative_to(ROOT)),image_bytes=len(data),image_sha256=sha(data),payload_sha256=sha(payload),
  kernel_sha256=kernel_hash,ci_run=CI_RUN,expected_kernel_source=SOURCE,kernel_source_identity_verified=True,
  commandline=COMMANDLINE,mode=MODE,xnu_cpus=8,point=23,usb_console_enabled=True,usb_log_capacity_bytes=4194304,
  original_linux_kernel_and_both_dts_byte_exact=True,linux_sha256=sha(si),appended_dt_sha256=sha(app),header_dt_sha256=sha(dt),
  rescue_ramdisk_uncompressed_sha256=sha(raw),rescue_ramdisk_uncompressed_bytes=len(raw),tested_rescue_contents_byte_exact=True,
  rescue_gzip_container_recompressed=True,rescue_container_hardware_tested=True,rescue_container_hardware_evidence="ADB marker observed after early-panic rescue fallback",compression_iterations=15,
  base_sha256=sha(base),manual_recovery_rescue_sha256=sha(rescue),boot_spare_bytes=33554432-len(data),android_header_id_verified=True,
  product_adt_requested=True,product_adt_sha256=PRODUCT_ADT_SHA256,fast_framebuffer_console=True,skip_missing_rtc_wait=True,hardware_tested=False,measured_boot_speedup=None,measured_cpu_clock_hz=None,lane_transition_observed=False,
  kernel_payload_segments_byte_verified=True)
 assert 'ufsrw=1' in tokens and 'rootrw=1' in tokens and 'nord2wificommon=1' in tokens and 'nord2ufs2live=1' in tokens
 report.update(request_metadata(MODE))
 assert not (OUT/'payload.bin').exists() and not (OUT/'manifest.json').exists(), 'preserve existing evidence explicitly'
 (OUT/'payload.bin').write_bytes(payload)
 (OUT/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps(report,indent=2))
if __name__=='__main__':main()
