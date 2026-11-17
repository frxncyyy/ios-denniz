#!/usr/bin/env python3
"""Independent offline verification; never flashes, boots or changes a device."""
from pathlib import Path
import argparse,gzip,hashlib,importlib.util,json,struct,re
ROOT=Path(__file__).resolve().parents[4]
OUT=Path(__file__).resolve().parent
IMAGE=ROOT/"shim/out/nord2-xnu/nord2-ios-wifi-capability-format-battery-stop23.img"
COMMANDLINE="debug=0xc cpus=8 keepsyms=1 -noprogress nord2stop=0 nord2vm=1 nord2fb=1 nord2stopat=23 rd=ufsroot0 ufsroot=userdata ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 wdt=-1 nord2display=1 nord2input=1 nord2wifi=2 nord2fastlog=1 nord2ufsprofile=1 nord2fbdiag=1 nord2wififw=1 nord2nortc=1 nord2wifiemi=0 nord2ufs2live=1 nord2wifipersist=1 nord2wifirom=1 nord2battery=1 nord2wifidma=1 nord2wifimain=1 nord2wificap=1"
MODE="ios_live_x2_wifi_main_battery"
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
def load_schema():
 cfg=json.loads((OUT/'native-schema-required.json').read_text())
 assert cfg.get('parent_verified') is True, 'BLOCKED: native telemetry/source pin not reviewed'
 for name in ('wfdma_native','main_native_v2','binding_telemetry',
              'preflight_v2_source','battery_fifo_low5_source','evidence_parser',
              'pool_map_source','state_candidate_source','drain_source',
              'txdone_transport_source','txdone_transport_header','txdone_coordinator_source',
              'txwriteback_transport_source','txwriteback_transport_header',
              'txwriteback_coordinator_source',
              'rxheader_transport_source','rxheader_transport_header',
              'rxheader_coordinator_source',
              'rxformat_transport_source','rxformat_phy_source',
              'rxformat_layout_source','rxformat_native_finish_source'):
  path=cfg.get(name+'_path');digest=cfg.get(name+'_sha256')
  assert isinstance(path,str) and path and not Path(path).is_absolute() and '..' not in Path(path).parts
  assert isinstance(digest,str) and re.fullmatch(r'[0-9a-f]{64}',digest)
  pinned(path,digest)
 assert cfg['pool_map_source_path']=='shim/nord2-xnu/pool_bind.inc'
 pool_source=pinned(cfg['pool_map_source_path'],cfg['pool_map_source_sha256'])
 assert b'ml_io_map_unmappable((vm_offset_t)base,(vm_size_t)bytes,VM_WIMG_IO)' in pool_source
 markers=cfg.get('required_kernel_markers')
 assert isinstance(markers,list) and markers and all(isinstance(x,str) and 0<len(x)<120 for x in markers)
 assert len(markers)==len(set(markers))
 new_markers=cfg.get('required_new_diagnostic_markers')
 assert isinstance(new_markers,list) and new_markers and all(isinstance(x,str) and 0<len(x)<120 for x in new_markers)
 assert len(new_markers)==len(set(new_markers)) and not set(new_markers)&set(markers)
 assert 'NORD2 WIFI MAIN DRAIN:' in new_markers
 assert b'NORD2 WIFI MAIN DRAIN:' in pinned(cfg['drain_source_path'],cfg['drain_source_sha256'])
 assert 'NORD2 WIFI MAIN TXDESC:' in new_markers
 assert b'NORD2 WIFI MAIN TXDESC:' in pinned(cfg['txdone_coordinator_source_path'],cfg['txdone_coordinator_source_sha256'])
 for marker in ('NORD2 WIFI MAIN TXSUBMIT:', 'NORD2 WIFI MAIN TXOBSERVED:'):
  assert marker in new_markers
  assert marker.encode() in pinned(cfg['txdone_coordinator_source_path'],cfg['txdone_coordinator_source_sha256'])
 assert 'NORD2 WIFI MAIN TXWB:' in new_markers
 assert b'NORD2 WIFI MAIN TXWB: constructed=%u observed=%u ddone=%u consumed_zero=%u software_used=%u' in pinned(cfg['txwriteback_coordinator_source_path'],cfg['txwriteback_coordinator_source_sha256'])
 for marker in ('NORD2 WIFI MAIN RXDESC: addr=%#llx buffer=%#llx didx=%u valid=%#x reason=%u', 'NORD2 WIFI MAIN RXWORDS: raw=%#x/%#x/%#x/%#x', 'NORD2 WIFI MAIN RXHEADER: expected=%#x/%u type=%#x event=%#x/%u status=%#x cid=%#x valid=%#x'):
  assert marker.split(':',1)[0]+':' in new_markers
  assert marker.encode() in pinned(cfg['rxheader_coordinator_source_path'],cfg['rxheader_coordinator_source_sha256'])
 owner=cfg.get('ownerdiag_required_markers')
 assert owner==['NORD2 WIFI MAIN GUARD:'] and all(x in new_markers for x in owner)
 assert cfg.get('ownerdiag_candidate_manifest_path')=='research/nord2/wifi/next-stage/main-fw-transport/owner-guard-diagnostics-v2/manifest.json'
 assert cfg.get('ownerdiag_candidate_manifest_sha256')=='9a8b2a3f5ea81613bf1825ddf830ff7c2816da79883425075e688b25cf88c6a0'
 assert sha((ROOT/cfg['ownerdiag_candidate_manifest_path']).read_bytes())==cfg['ownerdiag_candidate_manifest_sha256']
 reviewed=json.loads((ROOT/cfg['ownerdiag_candidate_manifest_path']).read_text())
 assert cfg.get('ownerdiag_normalization_path')=='research/nord2/wifi-main-ownerdiag/focused/normalization.json'
 assert cfg.get('ownerdiag_normalization_sha256')=='6560500e276e4782fb981726fc44c6ba6b8e18c2f6305193dd9833b9907511ad'
 normalized=json.loads(pinned(cfg['ownerdiag_normalization_path'],cfg['ownerdiag_normalization_sha256']))
 assert normalized['reviewed_manifest']==cfg['ownerdiag_candidate_manifest_sha256']
 assert normalized['source']=='shim/nord2-xnu/wifi_main_wfdma_bind.inc'
 assert normalized['before_sha256']==reviewed['files']['wifi_main_wfdma_bind.inc']
 reviewed_binding=pinned(str(Path(cfg['ownerdiag_candidate_manifest_path']).parent/'wifi_main_wfdma_bind.inc'),normalized['before_sha256'])
 # The executed ownerdiag binding is historical evidence. Its canonical path
 # can change in this capability image; capability source pins below must cover it.
 assert normalized['after_sha256']==cfg['ownerdiag_source_pins'][1]['sha256']
 expected=('wifi_main_wfdma_native.h','wifi_main_wfdma_bind.inc','wifi_main_bind.inc')
 assert tuple(reviewed['changed_runtime'])==expected
 pins=cfg.get('ownerdiag_source_pins')
 assert isinstance(pins,list) and len(pins)==3
 assert tuple(Path(x['path']).name for x in pins)==expected
 for item in pins:
  assert item['path']=='shim/nord2-xnu/'+Path(item['path']).name
  assert isinstance(item['sha256'],str) and re.fullmatch(r'[0-9a-f]{64}',item['sha256'])
  expected_sha=reviewed['files'][Path(item['path']).name]
  if Path(item['path']).name=='wifi_main_wfdma_bind.inc':
   assert item['sha256']==normalized['after_sha256']
  else:
   assert item['sha256']==expected_sha
 guard=b'NORD2 WIFI MAIN GUARD: reason=%u source=%u stage=%u error=%u valid=%u addr=%#x raw=%#x mask=%#x expected=%#x'
 assert guard in pinned(str(Path(cfg['ownerdiag_candidate_manifest_path']).parent/'wifi_main_bind.inc'),reviewed['files']['wifi_main_bind.inc'])
 # Executed SWINT policy is historical evidence. The capability source and
 # telemetry contract remain pending, so inherited pins cannot satisfy them.
 candidate_path=cfg.get('swint_candidate_manifest_path')
 candidate_sha=cfg.get('swint_candidate_manifest_sha256')
 assert isinstance(candidate_path,str) and candidate_path.startswith('research/nord2/') and '..' not in Path(candidate_path).parts
 assert isinstance(candidate_sha,str) and re.fullmatch(r'[0-9a-f]{64}',candidate_sha)
 swint_reviewed=json.loads(pinned(candidate_path,candidate_sha))
 assert swint_reviewed.get('changed_runtime')==['wifi_main_wfdma_bind.inc']
 assert swint_reviewed.get('unchanged_header_coordinator') is True
 assert swint_reviewed['baseline']['wifi_main_wfdma_bind.inc']==normalized['after_sha256']
 assert swint_reviewed['baseline']['wifi_main_wfdma_native.h']==pins[0]['sha256']
 assert swint_reviewed['baseline']['wifi_main_bind.inc']==pins[2]['sha256']
 swint_pins=cfg.get('swint_source_pins')
 assert isinstance(swint_pins,list) and swint_pins
 for item in swint_pins:
  assert isinstance(item,dict) and item.get('path','').startswith('shim/nord2-xnu/')
  assert Path(item['path']).name==item['path'].split('/')[-1]
  assert isinstance(item.get('sha256'),str) and re.fullmatch(r'[0-9a-f]{64}',item['sha256'])
  # The executed SWINT pin is historical; capability source pins below
  # identify the new canonical files, which may replace it.
 assert len(swint_pins)==1 and swint_pins[0]['path']=='shim/nord2-xnu/wifi_main_wfdma_bind.inc'
 assert swint_pins[0]['sha256']==swint_reviewed['files']['wifi_main_wfdma_bind.inc']
 assert pinned(str(Path(candidate_path).parent/'wifi_main_wfdma_bind.inc'),swint_pins[0]['sha256'])
 swint_markers=cfg.get('swint_required_markers')
 assert isinstance(swint_markers,list) and len(swint_markers)==len(set(swint_markers))
 assert all(isinstance(x,str) and x in new_markers for x in swint_markers)
 for marker in swint_markers:
  assert any(marker.encode() in pinned(str(Path(candidate_path).parent/Path(item['path']).name),item['sha256']) for item in swint_pins)
 # A capability receipt parser and actual emitting source are prerequisites
 # for opening the schema gate; no guessed firmware-ready or scan result.
 drain_path=cfg.get('capability_failure_drain_manifest_path')
 drain_sha=cfg.get('capability_failure_drain_manifest_sha256')
 assert drain_path=='research/nord2/wifi/next-stage/after-fw-ready/runtime-failure-drain/manifest.json'
 assert drain_sha=='32a1e5d024e2bab5df2602d1fcfab7b168bea235946388d0193298772c66f71f'
 pinned(drain_path,drain_sha)
 cap_path=cfg.get('capability_candidate_manifest_path')
 cap_sha=cfg.get('capability_candidate_manifest_sha256')
 assert cap_path=='research/nord2/wifi/next-stage/after-fw-ready/production-candidate/manifest.json'
 assert cap_sha=='75fc0de8baa1a8573b54a9afb5f4fe1cad5e557d267b3119483680d970c6a415'
 promoted=json.loads(pinned(cap_path,cap_sha))
 names=('wifi_capability.c','wifi_capability.h','wifi_capability_bind.inc','wifi_capability_dispatcher.c','wifi_capability_dispatcher.h')
 generated='research/ci-39158169212/buildtree/xnu/'
 # Historical candidate binds only the unchanged hook/registration/generator.
 expected={}
 for local,private in ((generated+'bsd/dev/sweet_touch.c','proposed/bsd/dev/sweet_touch.c'),(generated+'bsd/conf/files','proposed/bsd/conf/files'),('tools/nord2-mkpatch.sh','proposed/tools/nord2-mkpatch.sh')):
  expected[local]=promoted['files'][private]
 format_path=cfg.get('capability_format_manifest_path')
 assert format_path=='research/nord2/wifi/next-stage/after-fw-ready/capability-source-parity/manifest.json'
 format_sha=cfg.get('capability_format_manifest_sha256')
 assert format_sha=='efb352b1072826ad066461750ca7b87709106499690ca2ec00711e2d38d05427'
 format_manifest=json.loads(pinned(format_path,format_sha))
 assert format_manifest['choice']=='B_SOURCE_PARITY_PLUS_CAPTURE'
 for name in names:
  digest=format_manifest['files'][name]
  private=pinned(str(Path(format_path).parent/name),digest)
  expected['shim/nord2-xnu/'+name]=digest
 cap_pins=cfg.get('capability_source_pins')
 assert isinstance(cap_pins,list) and len(cap_pins)==len(expected)
 assert {item['path']:item['sha256'] for item in cap_pins}==expected
 for path,digest in expected.items():pinned(path,digest)
 generated_pins=cfg.get('capability_generated_source_pins')
 assert isinstance(generated_pins,list) and len(generated_pins)==len(names)
 assert [item['path'] for item in generated_pins]==[generated+'bsd/dev/'+name for name in names]
 for name,item in zip(names,generated_pins):
  emitted=pinned(item['path'],item['sha256'])
  assert item['sha256']==hashlib.sha256((b'#include <arm64/sweet_early.h>\n#if defined(NORD2)\n'+(ROOT/('shim/nord2-xnu/'+name)).read_bytes()+b'\n#endif /* NORD2 */\n') if name.endswith('.c') else (ROOT/('shim/nord2-xnu/'+name)).read_bytes()).hexdigest()
  canonical=pinned('shim/nord2-xnu/'+name,expected['shim/nord2-xnu/'+name])
  assert emitted==(b'#include <arm64/sweet_early.h>\n#if defined(NORD2)\n'+canonical+b'\n#endif /* NORD2 */\n' if name.endswith('.c') else canonical)
 touch=pinned(generated+'bsd/dev/sweet_touch.c',expected[generated+'bsd/dev/sweet_touch.c'])
 assert b'#include "wifi_capability_bind.inc"' in touch
 assert touch.count(b'ncd_schedule();')==1 and touch.index(b'nwfn_after_pool();')<touch.index(b'ncd_schedule();')
 registration=pinned(generated+'bsd/conf/files',expected[generated+'bsd/conf/files'])
 for name in ('wifi_capability.c','wifi_capability_dispatcher.c'):
  assert len(re.findall(rb'^bsd/dev/'+name.encode().replace(b'.',rb'\.')+rb'\s+standard$',registration,re.M))==1
 cap_markers=cfg.get('capability_required_markers')
 assert cap_markers==['NORD2 WIFI CAP'+('' if name=='RESULT' else ' '+name)+':' for name in ('ARM','RESULT','RX','DESCRIPTOR','TLV','OWNER','EVENT','EVENTHEADER','WORKER')]
 assert all(marker in new_markers for marker in cap_markers)
 emitter=pinned('shim/nord2-xnu/wifi_capability_bind.inc',expected['shim/nord2-xnu/wifi_capability_bind.inc'])
 for marker in cap_markers:assert marker.encode() in emitter
 parser_path=cfg.get('capability_receipt_parser_path')
 parser_sha=cfg.get('capability_receipt_parser_sha256')
 assert parser_path=='research/nord2/wifi-capability-format/ios/parse-capability.py'
 assert isinstance(parser_sha,str) and re.fullmatch(r'[0-9a-f]{64}',parser_sha)
 pinned(parser_path,parser_sha)
 dep_path=cfg.get('capability_receipt_dependency_path')
 assert dep_path=='research/nord2/wifi-capability-format/ios/parser-dependency.json'
 dep=json.loads(pinned(dep_path,cfg['capability_receipt_dependency_sha256']))
 assert dep['path']=='research/nord2/wifi-capability/ios/parse-capability.py'
 assert dep['sha256']=='7c5bdf4f2d6b3fafad6fbbb3d59a56e67825cdb4045e6735cc629132a6131232'
 pinned(dep['path'],dep['sha256'])
 assert cfg.get('main_evidence_manifest_path')=='research/nord2/wifi-main-swint/evidence-v2/manifest.json'
 assert cfg.get('main_evidence_manifest_sha256')=='2e9052aba2a1fc7904b55e0546411d01e5b7ecee4891c614a65c40977fb0726c'
 evidence=json.loads(pinned(cfg['main_evidence_manifest_path'],cfg['main_evidence_manifest_sha256']))
 assert cfg.get('main_evidence_review_path')=='research/nord2/wifi-main-swint/evidence-v2/independent-review.json'
 assert cfg.get('main_evidence_review_sha256')=='eb87a9b997e03ddfd5db560c87cae9a781feb2aa43f5803db671706635aae3f1'
 review=json.loads(pinned(cfg['main_evidence_review_path'],cfg['main_evidence_review_sha256']))
 assert review['result']=='PASS_OFFLINE_PARSER_REVIEW' and review['candidate_manifest_sha256']==cfg['main_evidence_manifest_sha256']
 assert cfg['evidence_parser_path']=='research/nord2/wifi-main-swint/evidence-v2/parse-evidence.py'
 assert cfg['evidence_parser_sha256']==evidence['files']['parse-evidence.py']==review['parser_sha256']
 for path,digest in evidence['dependencies'].items():pinned(path,digest)
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
  firmware_execution=True,radio_networking_enabled=False,rf_enabled=True,vendor_media_writes=False,
  wifi_dma_pool_probe_requested=True,wifi_dma_enabled=True,wifi_rom_requested=True,
  wifi_main_firmware_requested=True,wifi_phy_force_cal_requested=True,
  wifi_capability_query_requested=True,wifi_capability_query_limit=1,
  wifi_fw_ready_observation_requested=True,wifi_scan_requested=False,
  wifi_network_interface_requested=False,battery_observer_requested=True,
  wifi_battery_fifo_low5_required=True,wifi_dma_count_zero_or_observed512_required=True,
  wifi_wfsys_requested=False,wifi_wfsys_balanced_expected=False,wifi_wfsys_cpu_held=False,
  wifi_wfsys_releases_common=False,wifi_wfsys_byte_exact_restore=False,
  wifi_persistent_requested=True,wifi_persistent_source_phase_expected=10,
  wifi_persistent_handoff_expected=2,wifi_persistent_retains_power_ownership=True,
  wifi_persistent_outer_restore_expected=False,wifi_persistent_network_ready=False,
  wifi_common_diagnostic_requested=False,wifi_adie_id_diagnostic_requested=False,
  wifi_emi_legacy_requested=False,wifi_emi_requested=False,
  firmware_ram_verification_requested=True,firmware_ram_verified_on_hardware=False,
  emi_snapshot_requested=False,emi_effective_permissions_proven=False,
  input_probe_requested=True,display_hardware_probe_requested=True,disk_writes_enabled=True,
  measured_boot_speedup=None,measured_cpu_clock_hz=None,zoom_reset_requested=False)

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--preflight',action='store_true');ap.add_argument('--kernel-path',action='store_true');ap.add_argument('--commandline',action='store_true');ap.add_argument('--check-existing',action='store_true');args=ap.parse_args()
 cfg=load_identity();schema=load_schema();SOURCE=cfg['source_commit'];EXPECTED_KERNEL_SHA256=cfg['kernel_sha256'];KERNEL=ROOT/cfg['kernel_path'];CI_RUN=cfg['ci_run']
 assert re.fullmatch(r'[0-9a-f]{64}',EXPECTED_KERNEL_SHA256), 'build blocked: parent must supply verified exact CI kernel SHA256'
 pinned(PRODUCT_ADT,PRODUCT_ADT_SHA256)
 stock=pinned('backup/nord2/boot.img','04e84c4a14e5c3cb77d0e587da93b5a7e720e8c5b2a3a51ab3ef4cda746e1f01')
 rescue=pinned('research/nord2/rescue/nord2-adb-rescue.img','d78bf9fe720a1d293b863ea0a8b30e0df4b5eafb097b1343a3c30e7c3d03bac5')
 base=pinned('research/nord2/rescue/recompressed/base.img','ed05ea02d0499fe4109e6498a6852e769c346226aa4a56a220338673f8a2f5bf')
 ci=json.loads((ROOT/cfg['ci_run_json']).read_text())
 assert ci['status']=='completed' and ci['conclusion']=='success' and ci['headSha']==SOURCE
 kernel=KERNEL.read_bytes();kernel_hash=sha(kernel)
 previous=json.loads((ROOT/'research/nord2/wifi-dma/ios/manifest.json').read_text())
 assert previous['image_sha256']=='e0d3a50f7a706f27a8d9fb19bbd6951c85f016e8b179b47b726c331331ec65d5'
 executed=json.loads((ROOT/'research/nord2/wifi-main-swint/ios/manifest.json').read_text())
 assert executed['image_sha256']=='fa13be9baa232b26c89994998cd566fa2ec4e0d96748b5420f1c6504ba203be7'
 assert executed['commandline']==previous['commandline']+' nord2wifimain=1'
 assert COMMANDLINE==executed['commandline']+' nord2wificap=1'
 required_markers=(b'nord2wifirom',b'NORD2 WIFI ROM:',b'NORD2 WIFI ROM OWNER:',b'NORD2 WIFI ROM RECEIPT:',b'NORD2 WIFI ROM SMC ARM:',b'NORD2 WIFI ROM CPU ARM:',b'NORD2 BAT RAW:',b'NORD2 BAT I2C:',b'NORD2 BAT PREFLIGHT:',b'nord2wifidma',b'nord2-wifi-dma-claim',b'NORD2 WIFI DMA PREP:',b'NORD2 WIFI DMA ARM:',b'NORD2 WIFI DMA SMC ARM:',b'NORD2 WIFI DMA:',b'NORD2 WIFI DMA SET:',b'NORD2 WIFI DMA RAW:',b'NORD2 WIFI DMA OWNER:',b'NORD2 WIFI DMA LIVE:',b'NORD2 WIFI DMA CFG:',b'NORD2 WIFI DMA RING:',b'nord2wifimain',b'nord2wificap')+tuple(x.encode() for x in schema['required_kernel_markers']+schema['required_new_diagnostic_markers'])
 assert all(marker in kernel for marker in required_markers), 'CI kernel lacks exact ROM/DMA/BAT native markers'
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
 assert "nord2wifiemi=0" in tokens and "nord2wifipersist=1" in tokens and "nord2wifirom=1" in tokens and "nord2battery=1" in tokens
 assert "nord2cpuclock" not in keys and "nord2wifiwake" not in keys and "nord2wifiwfsys" not in keys
 assert not any(t.startswith(("nord2wificommon=","nord2wifiadieid=","nord2wifiemilegacy=")) for t in tokens)
 assert 'nord2wifidma=1' in tokens and 'nord2wifimain=1' in tokens and 'nord2wificap=1' in tokens
 assert 'nord2ufs2live=1' in tokens and not any(t.startswith('nord2ufs2lane=') for t in tokens)
 if args.kernel_path:
  print(str(KERNEL));return
 if args.commandline:
  print(COMMANDLINE);return
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
 assert sha(data)!='fa13be9baa232b26c89994998cd566fa2ec4e0d96748b5420f1c6504ba203be7', 'executed SWINT image cannot be reused'
 _,kg,rd,dt=m.stock_parts(data);blob,app=m.unpack_stock_kernel(kg)
 assert app==sapp and dt==sdt==rdt==bdt and rd==brd
 raw=gzip.decompress(rd);assert raw==gzip.decompress(rrd)
 assert sha(raw)=='95989a2ddff7d3fad8c63df9916a7d2510e09499f0b1710ac9fbf078ce60979f'
 # Independently rebuild loader from current sources and compare every byte,
 # including zero padding, against its exact packed 2MiB reserve.
 import subprocess,tempfile
 with tempfile.TemporaryDirectory(prefix='rom-loader-verify-') as ld:
  subprocess.run(['sh',str(ROOT/'tools/nord2-build-loader.sh'),ld],check=True,cwd=ROOT)
  expected_loader=(Path(ld)/'loader-Image').read_bytes()
 assert blob[:0x200000]==expected_loader.ljust(0x200000,b'\0'),'packed loader mismatch'
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
 baseline_adt=adt.serialize(am.build(str(ROOT/PRODUCT_ADT)))
 fallback=json.loads((OUT/'baseline.json').read_text())
 prior_payload=(ROOT/'research/nord2/wifi-dma/ios/payload.bin').read_bytes()
 old_off,old_size=struct.unpack_from('<QQ',prior_payload,0x20)
 assert old_off+old_size<=len(prior_payload)
 actual_old_adt=prior_payload[old_off:old_off+old_size]
 assert old_size==fallback['fallback_payload_adt_bytes'] and sha(actual_old_adt)==fallback['fallback_payload_adt_sha256']
 assert baseline_adt==actual_old_adt,'new generated ADT differs from executed e0d3 baseline before calibration'
 calpath=ROOT/'research/nord2/wifi/calibration/WIFI'
 cal=calpath.read_bytes()
 assert len(cal)==6146 and sha(cal)=='0000000000000000000000000000000000000000000000000000000000000000'
 ins=importlib.util.spec_from_file_location('nord2_main_cal',ROOT/'research/nord2/wifi/next-stage/main-fw-transport/main-fw-native-v2/inject_cal_adt.py')
 injector=importlib.util.module_from_spec(ins);ins.loader.exec_module(injector)
 parsed,consumed=adt.parse(baseline_adt);assert consumed==len(baseline_adt)
 expected_adt=adt.serialize(injector.add(parsed,cal))
 assert payload[adt_off:adt_off+adt_size]==expected_adt,'target ADT/private calibration differs'
 target,consumed=adt.parse(expected_adt);assert consumed==len(expected_adt)
 chosen=[x for x in target.children if x.name=='chosen'];assert len(chosen)==1
 props=[v for k,_,v in chosen[0].props if k=='nord2-wifi-calibration'];assert props==[cal]
 chosen[0].props=[x for x in chosen[0].props if x[0]!='nord2-wifi-calibration']
 assert adt.serialize(target)==baseline_adt,'non-calibration ADT changed'
 # Static ADT is intentionally unclaimed. Loader patches only the current
 # runtime copy after exact opt-in and checked fixed reservation selection.
 assert b'nord2-conn-claim' in expected_adt
 sys.path.insert(0,str(ROOT/'research/nord2/wifi-dma/ios'))
 import rom_claim_static_check
 rom_claim_static_check.verify(expected_adt)

 with tempfile.TemporaryDirectory(prefix='nord2-independent-verify-') as temporary:
  stripped=Path(temporary)/'kernel.strip'
  subprocess.run([str(ROOT/'../toolchains/cctools/bin/aarch64-apple-darwin-strip'),'-S','-x',str(KERNEL),'-o',str(stripped)],check=True)
  expected=bytearray(stripped.read_bytes())
 mach=mkpayload.parse_macho(expected);mkpayload.apply_chained_fixups(expected,mach)
 segments=[seg for seg in mach['segs'] if seg['vmsize']]
 embedded=b''.join(expected[seg['fileoff']:seg['fileoff']+seg['filesize']] for seg in segments)
 assert all(marker in embedded for marker in required_markers), 'loaded kernel segments lack persistent markers'
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
  product_adt_requested=True,product_adt_sha256=PRODUCT_ADT_SHA256,
  private_calibration_adt_sha256=sha(expected_adt),private_wifi_calibration_sha256=sha(cal),
  native_schema_pin_sha256=sha((OUT/'native-schema-required.json').read_bytes()),
  fast_framebuffer_console=True,skip_missing_rtc_wait=True,hardware_tested=False,measured_boot_speedup=None,measured_cpu_clock_hz=None,lane_transition_observed=False,
  kernel_payload_segments_byte_verified=True)
 assert 'ufsrw=1' in tokens and 'rootrw=1' in tokens and 'nord2ufs2live=1' in tokens
 report.update(request_metadata(MODE))
 if args.check_existing:
  assert json.loads((OUT/'manifest.json').read_text())==report,'existing prepared image manifest differs'
  assert (OUT/'payload.bin').read_bytes()==payload,'existing payload differs'
  print(json.dumps(dict(existing_prepared_image_verified=True,image_sha256=report['image_sha256'],kernel_sha256=kernel_hash)))
  return
 assert not (OUT/'payload.bin').exists() and not (OUT/'manifest.json').exists(), 'preserve existing evidence explicitly'
 (OUT/'payload.bin').write_bytes(payload)
 (OUT/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps(report,indent=2))
if __name__=='__main__':main()
