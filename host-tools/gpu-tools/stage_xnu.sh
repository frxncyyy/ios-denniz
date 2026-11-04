#!/bin/sh
# Stage a mutable XNU source+headers tree for the cached CI compile recipe.
# The default source is the audio-calibrated CI tree (it already carries
# patches 0001-0083); it is read-only, so copy it into the ignored build dir.
set -eu
cd "$(dirname "$0")/.."
SRC=${XNU_SOURCE:-$NORD2_REPO/research/nord2/audio/playback-20261023/install-sol/private-ci39969463181}
test -d "$SRC/xnu" || { echo "missing XNU source $SRC/xnu" >&2; exit 1; }
rm -rf build/xnu-stage build/xnu-obj
cp -a "$SRC/xnu" build/xnu-stage
cp -a "$SRC/build" build/xnu-obj
cp -a "$SRC/fakeroot" build/fakeroot
cp -a "$SRC/sdk-kernel-platform" build/sdk-kernel-platform
chmod -R u+w build/xnu-stage build/xnu-obj build/fakeroot build/sdk-kernel-platform
ln -sfn xnu-stage build/xnu
ln -sfn xnu-obj build/build
# Pre-0084 copies for the three edited files.
cp build/xnu-stage/bsd/conf/files build/xnu-stage/bsd/conf/files.nord2orig
cp build/xnu-stage/iokit/conf/files.arm64 build/xnu-stage/iokit/conf/files.arm64.nord2orig
cp build/xnu-stage/iokit/KernelConfigTables.cpp build/xnu-stage/iokit/KernelConfigTables.cpp.nord2orig
./tools/apply_stage_overlay.sh
echo "staged XNU under gpu/build/xnu-stage; regenerate 0084 with tools/regenerate_patch.sh"
