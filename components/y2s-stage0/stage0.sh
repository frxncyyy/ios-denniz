#!/bin/bash
# Stage 0: cross-build y2sios' Mesa 25.0.7 (+ their patches, AIR frontend, libdrm stand-in) for the iOS userland from Linux.
# Everything below build/y2s-stage0; no device.  y2sios is MIT (NOTICE.md kept beside).
set -euo pipefail
G=$NORD2_WORK/gpu
B=$G/build/y2s-stage0; cd $B
VERSION=25.0.7; SHA256=592272df3cf01e85e7db300c449df5061092574d099da275d19e97ef0510f8a6
SDK=$G/build/apple-toolchain/sdks/iPhoneOS16.5.sdk
export PATH=$G/build/venv/bin:$PATH
[ -f mesa-$VERSION.tar.xz ] || curl -sL --fail -o mesa-$VERSION.tar.xz https://archive.mesa3d.org/mesa-$VERSION.tar.xz
echo "$SHA256  mesa-$VERSION.tar.xz" | sha256sum -c --quiet
if [ ! -d mesa-$VERSION ]; then
  tar xJf mesa-$VERSION.tar.xz
  for p in tools-mesa/*.patch; do echo "patch $p"; patch -s --no-backup-if-mismatch -d mesa-$VERSION -p1 < "$p"; done
fi
# Nord2: the userspace BO cache stays at the stock 64 MiB / 30 s until the growth of wired memory in backboardd and SpringBoard is understood (192 MiB / 180 s was tried: it made the pressure worse).
# 100-590 MB per 5 s through the 64 MiB budget, and the lag came back whenever the cache had gone cold (idle > 30 s).
sed -i -e 's/^#define REDWOOD_BO_IDLE_S .*/#define REDWOOD_BO_IDLE_S 30/' -e 's/^#define REDWOOD_BO_BUDGET .*/#define REDWOOD_BO_BUDGET (64ull << 20)/' mesa-$VERSION/src/gallium/drivers/panfrost/pan_bo.c
grep -q 'define REDWOOD_BO_BUDGET (64ull << 20)' mesa-$VERSION/src/gallium/drivers/panfrost/pan_bo.c
# Nord2: a failed IOSurface import (the surface is gone, the kernel refused it) is an ordinary failure, not a crash.  panfrost_bo_import dereferenced the
# NULL kmod BO (backboardd died with SIGSEGV in panfrost_bo_import+0x8c and SpringBoard restarted with it) and ignored the result of the VM bind (the
# texture would then sample from an uninitialised GPU address).  The caller already handles a NULL return (panfrost_resource_from_handle).
python3 - mesa-$VERSION/src/gallium/drivers/panfrost/pan_bo.c <<'PY'
import sys
p=sys.argv[1]; s=open(p).read()
if 'Nord2: import failed' not in s:
    old="""      bo->kmod_bo = pan_kmod_bo_import(dev->kmod.dev, fd, 0);
"""
    new="""      bo->kmod_bo = pan_kmod_bo_import(dev->kmod.dev, fd, 0);
      if (!bo->kmod_bo) {              /* Nord2: import failed */
         bo->dev = NULL;
         pthread_mutex_unlock(&dev->bo_map_lock);
         return NULL;
      }
"""
    assert old in s; s=s.replace(old,new,1)
    old2="""      ASSERTED int ret = pan_kmod_vm_bind(
         dev->kmod.vm, PAN_KMOD_VM_OP_MODE_IMMEDIATE, &vm_op, 1);
      assert(!ret);
"""
    new2="""      ASSERTED int ret = pan_kmod_vm_bind(
         dev->kmod.vm, PAN_KMOD_VM_OP_MODE_IMMEDIATE, &vm_op, 1);
      assert(!ret);
      if (ret) {                       /* Nord2: import failed */
         pan_kmod_bo_put(bo->kmod_bo);
         bo->kmod_bo = NULL;
         bo->dev = NULL;
         pthread_mutex_unlock(&dev->bo_map_lock);
         return NULL;
      }
"""
    assert old2 in s; s=s.replace(old2,new2,1)
    open(p,'w').write(s)
PY
grep -q 'Nord2: import failed' mesa-$VERSION/src/gallium/drivers/panfrost/pan_bo.c
rm -rf mesa-$VERSION/src/redwood-drm mesa-$VERSION/src/compiler/air
cp -r tools-mesa/redwood-drm mesa-$VERSION/src/redwood-drm
cp -r tools-mesa/air mesa-$VERSION/src/compiler/air
# Nord2: the libdrm stand-in answers over the Nord2GPU transport (gpu/y2s) instead of RedwoodMali
Y=$G/y2s
cp $Y/nord2_drm.c mesa-$VERSION/src/redwood-drm/redwood_drm.c
cp $Y/nord2_pan.c $Y/nord2_pan.h $Y/nord2_pan_ios.c mesa-$VERSION/src/redwood-drm/
mkdir -p mesa-$VERSION/src/include && cp $G/include/nord2_gpu_transport.h mesa-$VERSION/src/redwood-drm/
sed -i 's|"../include/nord2_gpu_transport.h"|"nord2_gpu_transport.h"|' mesa-$VERSION/src/redwood-drm/nord2_pan.c
cat > cross.ini <<INI
[binaries]
c = ['clang', '-target', 'arm64-apple-ios16.0', '-isysroot', '$SDK']
cpp = ['clang++', '-target', 'arm64-apple-ios16.0', '-isysroot', '$SDK', '-stdlib=libc++', '-fuse-cxa-atexit']
objc = ['clang', '-target', 'arm64-apple-ios16.0', '-isysroot', '$SDK']
objcpp = ['clang++', '-target', 'arm64-apple-ios16.0', '-isysroot', '$SDK', '-stdlib=libc++']
c_ld = 'lld'
objc_ld = 'lld'
cpp_ld = 'lld'
objcpp_ld = 'lld'
ar = 'llvm-ar'
ranlib = 'llvm-ranlib'
strip = 'llvm-strip'
pkg-config = 'false'

[host_machine]
system = 'darwin'
subsystem = 'ios'
cpu_family = 'aarch64'
cpu = 'arm64'
endian = 'little'

[properties]
needs_exe_wrapper = true
INI
DRIVERS=softpipe,panfrost
BUILD=${BUILD:-build-softpipe-panfrost}
[ -f $BUILD/build.ninja ] || meson setup $BUILD mesa-$VERSION --cross-file cross.ini --wrap-mode=nofallback \
  --prefix=/redwood --libdir=lib --buildtype=release -Db_ndebug=true \
  -Dplatforms= -Dgallium-drivers="$DRIVERS" -Dvulkan-drivers= -Dglx=disabled -Degl=disabled -Dgbm=disabled \
  -Dosmesa=true -Dllvm=disabled -Dshared-glapi=enabled -Dgles1=disabled -Dgles2=enabled -Dopengl=true \
  -Dzstd=disabled -Dxmlconfig=disabled -Dexpat=disabled -Dshader-cache=disabled -Dvalgrind=disabled \
  -Dlibunwind=disabled -Dglvnd=disabled -Dvideo-codecs= -Dtools= -Dgallium-vdpau=disabled -Dgallium-va=disabled \
  -Dgallium-xa=disabled -Dlmsensors=disabled -Dbuild-tests=false > meson-setup.log 2>&1
[ -z "${CONFIGURE_ONLY:-}" ] || { echo CONFIGURED; exit 0; }
AIRDIR=src/gallium/drivers/panfrost
ninja -C $BUILD -j$(nproc) $AIRDIR/libredwoodair.dylib > ninja-air.log 2>&1
echo "libredwoodair.dylib: $(wc -c < $BUILD/$AIRDIR/libredwoodair.dylib) bytes"
ninja -C $BUILD -j$(nproc) src/gallium/targets/osmesa/libOSMesa.8.dylib > ninja-osmesa.log 2>&1
echo "libOSMesa.8.dylib: $(wc -c < $BUILD/src/gallium/targets/osmesa/libOSMesa.8.dylib) bytes"
echo STAGE0-DONE
