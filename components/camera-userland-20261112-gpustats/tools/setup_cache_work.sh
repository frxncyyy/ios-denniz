#!/bin/bash
# One-time preparation of $RWCAM_WORK for verify_cache.py / the cachetools. Needs the LOCAL iOS 16.7 shared cache (private) and apfs-fuse's source
# (its LZBITMAP decoder). Writes only into $RWCAM_WORK; reads the cache read-only.
#   usage: RWCAM_WORK=/some/dir setup_cache_work.sh [cache dir] [apfs-fuse dir]
# The cache's two LINKEDIT subcaches (.28 / .42 .dyldlinkedit) and the .symbols file are APFS-compressed with LZBITMAP (decmpfs type 14, in the
# resource fork `<file>:com.apple.ResourceFork`, 64 KiB chunks): the plain files are empty, which is why the export tables looked unavailable.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
: "${RWCAM_WORK:?set RWCAM_WORK}"
CACHE=${1:-$HOME/dsc-work/sys/System/Library/Caches/com.apple.dyld}
AFS=${2:-$HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/toolchains/apfs-fuse}
mkdir -p "$RWCAM_WORK/lz" "$RWCAM_WORK/dsc2"
# 1. LZBITMAP extractor = apfs-fuse's DecompressLZBITMAP + a 40-line main
awk 'NR>=563' "$AFS/ApfsLib/Util.cpp" | awk '{print} /^}/{exit}' | sed 's/^size_t DecompressLZBITMAP/static size_t DecompressLZBITMAP/' > "$RWCAM_WORK/lz/fn.inc"
cp "$HERE/cachetools/lzbitmap_extract.cpp" "$RWCAM_WORK/lz/"
g++ -O2 -o "$RWCAM_WORK/lz/extract" "$RWCAM_WORK/lz/lzbitmap_extract.cpp"
# 2. decompress: <resource fork> <out> <uncompressed size = the decmpfs header's last 8 bytes>
size_of() { python3 -c "import struct,sys;print(struct.unpack('<IIQ',open(sys.argv[1],'rb').read(16))[2])" "$1"; }
for n in dyld_shared_cache_arm64.28.dyldlinkedit dyld_shared_cache_arm64.42.dyldlinkedit dyld_shared_cache_arm64.symbols; do
  out="$RWCAM_WORK/lz/$(echo $n | sed 's/.*arm64\.//').bin"
  [ -f "$out" ] || "$RWCAM_WORK/lz/extract" "$CACHE/$n:com.apple.ResourceFork" "$out" "$(size_of "$CACHE/$n:com.apple.decmpfs")"
done
# 3. a cache directory with the decompressed subcaches in place of the empty ones
for f in "$CACHE"/dyld_shared_cache_arm64*; do b=$(basename "$f"); case $b in *:*) ;; *.28.dyldlinkedit|*.42.dyldlinkedit|*.symbols) ;; *) ln -sf "$f" "$RWCAM_WORK/dsc2/$b";; esac; done
ln -sf "$RWCAM_WORK/lz/28.dyldlinkedit.bin" "$RWCAM_WORK/dsc2/dyld_shared_cache_arm64.28.dyldlinkedit"
ln -sf "$RWCAM_WORK/lz/42.dyldlinkedit.bin" "$RWCAM_WORK/dsc2/dyld_shared_cache_arm64.42.dyldlinkedit"
ln -sf "$RWCAM_WORK/lz/symbols.bin" "$RWCAM_WORK/dsc2/dyld_shared_cache_arm64.symbols"
ln -sf "$RWCAM_WORK/lz/symbols.bin" "$RWCAM_WORK/lz/sym.bin"
# 4. CMCapture's ObjC method table
(cd "$HERE/cachetools" && RWCAM_WORK="$RWCAM_WORK" python3 methods.py "$RWCAM_WORK/cmcap.pkl")
echo "ready: RWCAM_WORK=$RWCAM_WORK $HERE/verify_cache.py"
