import json, os, sys, hashlib
from pathlib import Path
out=Path(sys.argv[1]); (out/'apps').mkdir(parents=True)
apps=[('Terraria-v1-4-52.ipa','Terraria.app','com.505games.terraria'),('Subway-Surfers-IPAOMTK.COM.ipa','SubwaySurf.app','com.kiloo.subwaysurfers')]
plan={'apps':[]}
for ipa,bundle,bid in apps:
    src=Path.home()/'Downloads'/ipa
    os.link(src,out/'apps'/ipa)
    plan['apps'].append({'ipa':ipa,'bundle':bundle,'bundle_id':bid,'sha256':hashlib.sha256(src.read_bytes()).hexdigest()})
(out/'plan.json').write_text(json.dumps(plan,indent=1))
