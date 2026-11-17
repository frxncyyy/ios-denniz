from pathlib import Path
import hashlib,json,shutil
R=Path(__file__).resolve().parents[4];P=Path(__file__).resolve().parent;S=R/'shim/nord2-xnu';B=R/'research/ci-39158169212/buildtree/xnu'
manifest=json.loads((P/'integrated-source/manifest.json').read_text())
for name,row in manifest.items():
    source=P/'integrated-source'/name
    assert hashlib.sha256(source.read_bytes()).hexdigest()==row['native_sha256']
    assert not (S/name).exists(),name
for name in manifest:shutil.copyfile(P/'integrated-source'/name,S/name)
c_names=sorted(Path(n).stem for n in manifest if n.endswith('.c'))
cpp_names=sorted(n for n in manifest if n.endswith('.cpp'))
p=R/'tools/nord2-mkpatch.sh';text=p.read_text()
text=text.replace('MAIN_FILES="','MAIN_FILES="'+' '.join(c_names)+' ',1)
text=text.replace('for name in wifi_vcn33_bind.inc','for name in wifi_scan_service.h wifi_scan_service_bind.inc wifi_vcn33_bind.inc',1)
anchor='\n\ntmp=$(mktemp -d)'
assert anchor in text
new='\n# Nord 2 compatible Wi-Fi scan service; no proprietary IO80211 implementation.\nfor name in '+' '.join(cpp_names)+'; do\n    NEW_FILES="$NEW_FILES iokit/Kernel/arm/$name"\n    {\n        printf \'#include <arm64/sweet_early.h>\\n#if defined(NORD2)\\n\'\n        cat "shim/nord2-xnu/$name"\n        printf \'\\n#endif /* NORD2 */\\n\'\n    } > "$BT/iokit/Kernel/arm/$name"\ndone\n'
text=text.replace(anchor,new+anchor);p.write_text(text)
p=B/'bsd/conf/files';text=p.read_text();text+='\n# Nord2 compatible Wi-Fi scan service helpers (empty elsewhere)\n'+''.join('bsd/dev/'+n+'.c\t\t\tstandard\n' for n in c_names);p.write_text(text)
p=B/'iokit/conf/files.arm64';text=p.read_text();text+='\n# Nord2 compatible Wi-Fi controller/user client (empty elsewhere)\n'+''.join('iokit/Kernel/arm/'+n+' optional iokitcpp\n' for n in cpp_names);p.write_text(text)
print('Integrated',len(manifest),'bridge source files and build lists')
