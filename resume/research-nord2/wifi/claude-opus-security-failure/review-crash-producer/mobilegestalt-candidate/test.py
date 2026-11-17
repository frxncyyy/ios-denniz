from pathlib import Path
import tempfile,shutil,subprocess,sys
P=Path(__file__).resolve().parent
R=P.parents[5]
with tempfile.TemporaryDirectory(prefix="nord2-mg-identity-") as t:
 root=Path(t);arm=root/'iokit/Kernel/arm';bsd=root/'bsd/dev';arm.mkdir(parents=True);bsd.mkdir(parents=True)
 for h in (R/'shim/nord2-xnu').glob('*.h'):shutil.copy2(h,bsd/h.name)
 shutil.copy2(R/'shim/nord2-xnu/wifi_ifnet_link.inc',bsd)
 shutil.copy2((R/'shim/nord2-xnu/wifi_ios_registry.cpp') if '--baseline' in sys.argv else P/'proposed/shim/nord2-xnu/wifi_ios_registry.cpp',arm)
 for f in (P/'proposed/tools/fixtures/nord2-wifi-ios-registry').iterdir():shutil.copy2(f,arm/f.name)
 subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(arm/'test-registry.cpp'),'-o',str(root/'test')],check=True)
 subprocess.run([str(root/'test')],check=True)
