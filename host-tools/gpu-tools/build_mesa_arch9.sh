#!/bin/sh
# Reproduce the pinned host Mesa/arch9 compiler used by the Nord 2 tests.
# Network is only used for the Python build dependencies; Mesa itself is the
# exact read-only reference snapshot, copied into the ignored gpu/build tree.
set -eu
cd "$(dirname "$0")/.."
ROOT=$PWD
MESA=$ROOT/build/mesa-src
BUILD=$ROOT/build/mesa-build
VENV=$ROOT/build/venv
REF=${MESA_REFERENCE:-$ROOT/../../references/mesa}
PIN=81daea66703eeea1a232446e3a36902353fc3045

mkdir -p "$ROOT/build"
if [ ! -d "$MESA" ]; then
  cp -a "$REF" "$MESA"
  chmod -R u+w "$MESA"
fi
test "$(git -C "$MESA" rev-parse HEAD)" = "$PIN" || {
  echo "Mesa checkout is not the pinned revision" >&2; exit 1; }

if [ ! -x "$VENV/bin/meson" ]; then
  python3 -m venv "$VENV"
  "$VENV/bin/pip" install mako pyyaml setuptools meson ninja
fi

cp "$ROOT/tools/nord2_mali_compile.c" \
   "$MESA/src/panfrost/clc/nord2_mali_compile.c"
if ! grep -q "nord2_mali_compile" "$MESA/src/panfrost/clc/meson.build"; then
  cat >> "$MESA/src/panfrost/clc/meson.build" <<'TARGET'

# Nord 2 host probe: arch9 Mali-G77 compiler/reflection.  Local build only.
executable('nord2_mali_compile', ['nord2_mali_compile.c'],
  link_with : [libpanfrost_compiler],
  include_directories : [inc_include, inc_src],
  c_args : [c_msvc_compat_args, no_override_init_args],
  dependencies : [idep_vtn, idep_nir, idep_mesautil, libpanfrost_model_dep],
  build_by_default : false)
TARGET
fi

if [ ! -f "$BUILD/build.ninja" ]; then
  PATH="$VENV/bin:$PATH" "$VENV/bin/meson" setup "$BUILD" "$MESA" \
    -Dgallium-drivers=softpipe -Dvulkan-drivers= -Dtools=panfrost \
    -Dplatforms= -Dglx=disabled -Degl=disabled -Dgbm=disabled \
    -Dopengl=true -Dgles1=disabled -Dgles2=disabled -Dllvm=disabled \
    -Dmesa-clc=auto -Dprecomp-compiler=auto -Dpanfrost-rust=false \
    -Dbuild-tests=false -Dzstd=disabled -Dzlib=disabled -Dexpat=disabled \
    -Dshader-cache=disabled
fi
PATH="$VENV/bin:$PATH" ninja -C "$BUILD" -j1 \
  src/panfrost/compiler/libpanfrost_compiler.a \
  src/panfrost/clc/nord2_mali_compile
echo "arch9 compiler: $BUILD/src/panfrost/clc/nord2_mali_compile"
