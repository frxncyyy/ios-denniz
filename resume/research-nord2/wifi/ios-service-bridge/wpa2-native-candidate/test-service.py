from pathlib import Path
import subprocess,tempfile
P=Path(__file__).resolve().parent;R=next(x for x in P.parents if (x/'shim/nord2-xnu').exists());W=P.parent/'station-transport-live-candidate';C=P.parent/'connection-bridge/v2/proposed/shim/nord2-xnu';F=P.parent/'wpa2-engine-candidate/followup';B=R/'research/ci-39398655023/buildtree/xnu/libkern/crypto/sweet_bearssl'
with tempfile.TemporaryDirectory(prefix='njs-owner-service-') as td:
 files=[P/'service-test.c',P/'wifi_wpa_nst.c',P/'wifi_wpa_owner.c',P/'wifi_station_transaction.c',P/'wifi_wpa2.c',C/'wifi_ios_join.c',W/'wifi_station_wire.c',P/'wifi_wpa_crypto.c']+[B/'src'/x for x in ['hash/sha1.c','codec/dec32be.c','codec/enc32be.c','symcipher/aes_ct.c','symcipher/aes_ct_dec.c','symcipher/aes_ct_cbcdec.c']]
 a=['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all','-pthread']+['-I'+str(x) for x in [P,W,C,P.parent/'ifnet-native-candidate',R/'shim/nord2-xnu',F,B/'inc',B/'src']]+list(map(str,files))+['-o',td+'/test'];subprocess.run(a,check=True);subprocess.run([td+'/test'],check=True)
