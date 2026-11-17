#!/usr/bin/env bash
# Fetch the kernel build inputs exactly as the CI workflow (.github/workflows/xnu.yml) does.
#
# The kernel is built on a GitHub macOS runner (macos-14, Xcode 15.x) with blacktop/darwin-xnu-build pinned
# to the commit below; its build.sh downloads Apple's open-source xnu for MACOS_VERSION=13.5 (xnu-8796)
# itself. This script reproduces the "Fetch build script" step on a Mac (or just prepares the tree to read):
#   tools/fetch-xnu-build.sh [DIR]      (default DIR: ./dxb)
# Then on macOS 14 with Xcode 15: (cd DIR && MACOS_VERSION=13.5 KERNEL_CONFIG=DEVELOPMENT MACHINE_CONFIG=SWEET bash build.sh)
# The normal way is the workflow itself: fork the repository and run
#   gh workflow run xnu.yml -R <you>/<fork> ; gh run download <run-id> -R <you>/<fork> -n xnu-kernel
set -euo pipefail
DIR=${1:-dxb}
DXB_REPO=https://github.com/blacktop/darwin-xnu-build
DXB_SHA=663906528d1cd5ea8fde7de6f16de73db9511549   # same pin as .github/workflows/xnu.yml
ROOT=$(cd "$(dirname "$0")/.." && pwd)
git init -q "$DIR"
git -C "$DIR" fetch -q --depth 1 "$DXB_REPO" "$DXB_SHA"
git -C "$DIR" checkout -q FETCH_HEAD
[ "$(git -C "$DIR" rev-parse HEAD)" = "$DXB_SHA" ] || { echo "unexpected commit"; exit 1; }
cp "$ROOT"/xnu/patches/*.patch "$DIR"/patches/ 2>/dev/null || true
cp "$ROOT"/xnu/patches/*.sh "$DIR"/patches/ 2>/dev/null || true
if [ "$(uname)" = Darwin ]; then
    sed -i '' 's/ARCHS="x86_64 arm64e" VALID_ARCHS="x86_64 arm64e"/ARCHS="x86_64 arm64e arm64" VALID_ARCHS="x86_64 arm64e arm64"/' "$DIR"/build.sh
else
    sed -i 's/ARCHS="x86_64 arm64e" VALID_ARCHS="x86_64 arm64e"/ARCHS="x86_64 arm64e arm64" VALID_ARCHS="x86_64 arm64e arm64"/' "$DIR"/build.sh
fi
echo "XNU_BUILD_TREE_OK $DIR (blacktop/darwin-xnu-build $DXB_SHA + xnu/patches)"
echo "Note: the Nord 2 line was then completed by the local integration step (recompile of the pieces 0394+"
echo "against the CI object archive, see build/recipes and docs/COMPLETENESS.md). The archive comes from a CI run."
