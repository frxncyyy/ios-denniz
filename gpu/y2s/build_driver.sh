#!/bin/bash
# Build the y2sios Metal driver as the Nord2Metal bundle payload (no device, nothing installed).
# Needs gpu/build/y2s-stage0/build-nord2 from stage0.sh.  Output: $OUT/Nord2Metal.bundle (executable, Info.plist, libredwoodair.dylib)
set -euo pipefail
G=$NORD2_WORK/gpu
Y2=${Y2SIOS:?set Y2SIOS to a read-only clone of stormtroopxyz/y2sios}
B=$G/build/y2s-stage0; OUT=${OUT:-$B/payload}; SDK=$G/build/apple-toolchain/sdks/iPhoneOS16.5.sdk
BUNDLE=/System/Library/Extensions/Nord2Metal.bundle
rm -rf $OUT $B/src; mkdir -p $OUT/Nord2Metal.bundle $B/src $B/obj
for f in redwoodmetal.m redwooddrawable.m redwood_drawable.h m2m.h; do cp $Y2/userland/src/$f $B/src/; done
# the library lives in the bundle; log lines go to the kernel log (no /dev/m2m here)
sed -i "s|/redwood/lib/libredwoodair.dylib|$BUNDLE/libredwoodair.dylib|g" $B/src/redwoodmetal.m $B/src/redwooddrawable.m $G/y2s/nord2_rw_compat.h
# feature profile 1 (Apple1+Apple2): MPS, which SpringBoard's Siri/blur uses, asks for Apple2 at least (y2sios: metal_profile=1)
python3 -I - "$B/src/redwoodmetal.m" <<'PY'
import sys
p=sys.argv[1]; s=open(p).read()
old="\t\tprofile = 0;\n\t\tif (sysctlbyname(\"kern.bootargs\""
assert old in s
s=s.replace(old,"\t\tprofile = 1;\n\t\tif (sysctlbyname(\"kern.bootargs\"",1)
open(p,'w').write(s)
PY
python3 -I - "$B/src/m2m.h" <<'PY'
import sys
p=sys.argv[1]; s=open(p).read()
s=s.replace('''		printf("m2m-nodev: W M2M %s: %s; records follow on stdout\\n", M2M_DEVICE, strerror(errno));
		fflush(stdout);''','''		(void)0;''')
s=s.replace('''		printf("m2m-nodev: %s\\n", rec);
		fflush(stdout);''','''		nrw_log_line(rec, (size_t)n);''')
open(p,'w').write(s)
PY
cat >> $B/src/m2m.h <<'H'
H
CF="-target arm64-apple-ios16.0 -isysroot $SDK -O1 -Wno-deprecated-declarations -DRedwoodMetalDevice=TeguComputeDevice -include $G/y2s/nord2_rw_compat.h -I$B/src"
for f in redwoodmetal redwooddrawable; do clang $CF -c $B/src/$f.m -o $B/obj/$f.o; done
clang $CF -c $G/y2s/nord2_srcprobe.m -o $B/obj/nord2_srcprobe.o
clang $CF -c $G/y2s/nord2_fscache.m -o $B/obj/nord2_fscache.o
clang -target arm64-apple-ios16.0 -isysroot $SDK -fuse-ld=lld -bundle -Wl,-adhoc_codesign \
  $B/obj/redwoodmetal.o $B/obj/redwooddrawable.o $B/obj/nord2_srcprobe.o $B/obj/nord2_fscache.o -framework Foundation -framework IOKit -framework Metal -lobjc -o $OUT/Nord2Metal.bundle/Nord2Metal
# the compiler library, signed
L=$B/build-nord2/src/gallium/drivers/panfrost/libredwoodair.dylib
(cd $B/build-nord2 && $(ninja -t commands src/gallium/drivers/panfrost/libredwoodair.dylib | tail -1) -Wl,-adhoc_codesign -o $OUT/Nord2Metal.bundle/libredwoodair.dylib)
python3 -I - $OUT <<'PY'
import sys,plistlib
out=sys.argv[1]
plistlib.dump({'CFBundleExecutable':'Nord2Metal','CFBundleIdentifier':'org.nord2.Metal','CFBundleName':'Nord2Metal','CFBundlePackageType':'BNDL','CFBundleShortVersionString':'0.1','CFBundleSupportedPlatforms':['iPhoneOS'],'CFBundleVersion':'1','MinimumOSVersion':'16.0','NSPrincipalClass':'TeguComputeDevice'},open(out+'/Nord2Metal.bundle/Info.plist','wb'),fmt=plistlib.FMT_BINARY)
PY
ls -la $OUT/Nord2Metal.bundle
