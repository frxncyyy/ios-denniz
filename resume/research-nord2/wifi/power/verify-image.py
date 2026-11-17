from pathlib import Path
import hashlib, importlib.util, struct, json, sys
spec=importlib.util.spec_from_file_location("pack", "tools/nord2-pack-xnu.py")
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
out=Path("research/nord2/wifi/power")
cpus=8
image=Path("shim/out/nord2-xnu/nord2-ios-wifi-power-fastlog-stop23.img")
stock=Path("backup/nord2/boot.img").read_bytes()
assert hashlib.sha256(stock).hexdigest()=="04e84c4a14e5c3cb77d0e587da93b5a7e720e8c5b2a3a51ab3ef4cda746e1f01"
rescue=Path("research/nord2/rescue/nord2-adb-rescue.img").read_bytes()
assert hashlib.sha256(rescue).hexdigest()=="d78bf9fe720a1d293b863ea0a8b30e0df4b5eafb097b1343a3c30e7c3d03bac5"
_,skg,_,sdt=m.stock_parts(stock);_,_,rrd,rdt=m.stock_parts(rescue)
si,sapp=m.unpack_stock_kernel(skg);size=struct.unpack_from("<Q",si,16)[0]
poff=(0x200000+size+0x3fff)&~0x3fff
data=image.read_bytes();assert len(data)<=33554432
_,kg,rd,dt=m.stock_parts(data);blob,app=m.unpack_stock_kernel(kg)
assert app==sapp and dt==sdt==rdt and rd==rrd
assert blob[0x200000:0x200000+len(si)]==si
assert blob[0x200000+len(si):0x200000+size]==bytes(size-len(si))
payload=blob[poff:];assert payload[:8]==b"SWEETXNU"
assert "FAILURES: none" in (out/"image-build.log").read_text()
cmd=payload[0x48:0x48+608].split(b"\0")[0]
assert cmd==f"debug=0xc cpus={cpus} keepsyms=1 -noprogress nord2stop=0 nord2vm=1 nord2fb=1 nord2stopat=23 rd=ufsroot0 ufsroot=userdata ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 wdt=-1 nord2display=1 nord2input=1 nord2wifi=2 nord2fastlog=1".encode()
(out/"payload.bin").write_bytes(payload)
report=dict(image=str(image),image_bytes=len(data),image_sha256=hashlib.sha256(data).hexdigest(),
    payload_sha256=hashlib.sha256(payload).hexdigest(),xnu_cpus=cpus,
    original_linux_kernel_and_dt_unchanged=True,tested_rescue_ramdisk_unchanged=True,
    root_mount="UFS userdata read/write",disk_writes_enabled=True,usb_console_enabled=True,
    point=23,usb_seconds=None,usb_log_capacity_bytes=4194304,display_enabled=True,input_enabled=True,lk_overlay_handoff=True,boot_spare_bytes=33554432-len(data),
    root_image_sha256="8b1d9de6775674afe790a161419ea23e824c5e045f21a66c6fc886125c991cb3",
    kernel_source="full Apple-clang CI39482559337, source67496ae, explicit generic ARMv8.2+crypto",
    hardware_tested=False,pmif_keys=True,mt6360_vtp_3v=True,
    wifi_ap_pmic_snapshot=True,probe_performs_conn_id_reads=True,radio_networking_enabled=False,
    radio_domain_temporary_probe=True,radio_state_restore_required=True,
    fast_framebuffer_console=True,gui_timing_marker=True)
(out/"manifest.json").write_text(json.dumps(report,indent=2)+"\n")
print(json.dumps(report,indent=2))
