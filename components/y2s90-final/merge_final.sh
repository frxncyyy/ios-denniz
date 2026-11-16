#!/bin/bash
# y2s90-final: the re-mergeable merge of the Nord2Metal driver tracks onto y2s83-thumbfix5.
#
#   bash merge_final.sh [--out DIR] [--build] [--force-out]
#
#   --out DIR    where the merged tree goes (DIR/src, and with --build DIR/obj + DIR/payload); default: this directory.
#                A later integration (y2s91 thumbnail grid integrity, ...) uses a NEW name: --out ../y2s9N-final2
#   --build      run the thumbfix5 build recipe (src/build.sh SRC OUT) and compare the compiler warnings with thumbfix5's
#   --force-out  allow replacing an existing DIR/src that this script did not make
#
# What it does, in order (every patch is --fuzz=0; any failure stops before DIR/src is touched):
#   1. y2s83-thumbfix5/src/redwoodmetal.m must be the base this merge was resolved on (merge/base.sha256);
#   2. the three track diffs upstream must still be the ones resolved here (merge/upstream.sha256, compared without their
#      header lines): y2s84-usecount/usecount-redwoodmetal.diff, y2s85-photosperf/photosperf1.diff, and the gamepipe diff
#      (y2s86-gamepipe/src/redwoodmetal.m against thumbfix5, made afresh). A track that moved stops the merge: resolve its new
#      diff onto the series stage before it and replace merge/series/NN-*.diff + merge/upstream (see README in this file's end);
#   3. a copy of thumbfix5's src/ gets merge/series/10-usecount, 20-photosperf, 30-gamepipe (both rebased: the conflicts are
#      resolved in them) and 40-final (no-final master switch + the version banner); the result must be merge/core.sha256;
#   4. then every diff listed in extra_diffs.txt, in order (dry run first, then for real). A diff whose only file is
#      redwoodmetal.m is applied to it whatever its path names; any other is applied in src/ with its listed -pN (default -p1).
#      A diff that does not apply as it is was made against plain thumbfix5 and meets the merged code: make a resolved copy of
#      it against the merge so far (DIR/.stage-fail holds that tree), list the resolved copy instead, and run again;
#   5. the stage replaces DIR/src; DIR/merge-manifest.txt names every diff applied (sha256) and the result.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); B=$(cd "$HERE/.." && pwd)
BASE=$B/y2s83-thumbfix5/src
OUT=$HERE; BUILD=0; FORCE=0
while [ $# -gt 0 ]; do
  case $1 in
    --out) mkdir -p "$2"; OUT=$(cd "$2" && pwd); shift 2;;
    --build) BUILD=1; shift;;
    --force-out) FORCE=1; shift;;
    *) echo "usage: merge_final.sh [--out DIR] [--build] [--force-out]"; exit 64;;
  esac
done
M=$HERE/merge
sha() { sha256sum "$1" | cut -c1-64; }
body() { sed '1,2d' "$1" | sha256sum | cut -c1-64; }     # a diff's sha256 without its ---/+++ lines (paths, dates)
fail() { echo "MERGE_FAILED: $*" >&2; exit 2; }

# 1. the base
[ "$(sha "$BASE/redwoodmetal.m")" = "$(cat "$M/base.sha256")" ] || fail "y2s83-thumbfix5/src/redwoodmetal.m is not the base of this merge"
echo "base   y2s83-thumbfix5 redwoodmetal.m $(cut -c1-12 "$M/base.sha256") ok"

# 2. the tracks upstream
GP=$(mktemp -p "$OUT" .gamepipe-XXXXXX.diff); trap 'rm -f "$GP"' EXIT
diff -u "$BASE/redwoodmetal.m" "$B/y2s86-gamepipe/src/redwoodmetal.m" > "$GP" || [ $? = 1 ]
while read -r name want path; do
  [ -n "$name" ] && [ "${name:0:1}" != "#" ] || continue
  f=$B/$path; [ "$path" = GENERATED ] && f=$GP
  [ -f "$f" ] || fail "track $name: $path missing"
  got=$(body "$f")
  [ "$got" = "$want" ] || fail "track $name moved upstream ($path: body $got, resolved here $want): resolve it onto the stage before merge/series/*-$name.diff, replace that diff, merge/upstream/ and its line in merge/upstream.sha256"
  echo "track  $name unchanged upstream (${want:0:12})"
done < "$M/upstream.sha256"

# 3. the series
STAGE=$(mktemp -d -p "$OUT" .stage-XXXXXX); trap 'rm -f "$GP"; rm -rf "$STAGE"' EXIT
cp -p "$BASE"/* "$STAGE"/
: > "$STAGE/.manifest"
apply() {  # apply DIFF LABEL [pN]
  local d=$1 label=$2 p=${3:--p1} nfiles first
  nfiles=$(grep -c '^+++ ' "$d" || true); first=$(grep -m1 '^+++ ' "$d" | awk '{print $2}')
  if [ "$nfiles" = 1 ] && [ "$(basename "$first")" = redwoodmetal.m ]; then
    set -- patch --fuzz=0 --no-backup-if-mismatch -s -f "$STAGE/redwoodmetal.m"
  else
    set -- patch --fuzz=0 --no-backup-if-mismatch -s -f -d "$STAGE" "$p"
  fi
  if ! "$@" --dry-run < "$d" > "$STAGE/.dry" 2>&1; then
    cat "$STAGE/.dry" >&2
    rm -rf "$OUT/.stage-fail"; cp -a "$STAGE" "$OUT/.stage-fail"; rm -f "$OUT/.stage-fail/.dry"
    fail "$label ($d) does not apply to the merge so far (tree kept in $OUT/.stage-fail; nothing else changed)"
  fi
  "$@" < "$d"
  printf '%s  %s  %s\n' "$(sha "$d")" "$label" "$d" >> "$STAGE/.manifest"
  echo "apply  $label ok"
}
for d in "$M"/series/*.diff; do apply "$d" "series $(basename "$d" .diff)"; done
CORE=$(sha "$STAGE/redwoodmetal.m")
[ "$CORE" = "$(cat "$M/core.sha256")" ] || fail "the series gave $CORE, not the resolved merge $(cat "$M/core.sha256")"
echo "core   redwoodmetal.m ${CORE:0:12} = the resolved y2s90 merge"

# 4. the extra diffs (later tracks)
n=0
while read -r path p rest; do
  [ -n "${path:-}" ] && [ "${path:0:1}" != "#" ] || continue
  case $path in /*) f=$path;; *) f=$B/$path;; esac
  [ -f "$f" ] || fail "extra diff $path missing"
  apply "$f" "extra $path" "${p:--p1}"; n=$((n + 1))
done < "$HERE/extra_diffs.txt"
echo "extra  $n diff(s) from extra_diffs.txt"

# 5. into place
if [ -d "$OUT/src" ] && [ ! -f "$OUT/src/.merged-by-merge_final" ] && [ $FORCE = 0 ]; then
  fail "$OUT/src exists and was not made by merge_final.sh (--force-out to replace it)"
fi
{ echo "# merge_final.sh $(date -Is)"; echo "base $(cat "$M/base.sha256")  y2s83-thumbfix5/src/redwoodmetal.m"; cat "$STAGE/.manifest";
  echo "result $(sha "$STAGE/redwoodmetal.m")  src/redwoodmetal.m (core $CORE, $n extra)"; } > "$OUT/merge-manifest.txt"
rm -f "$STAGE/.manifest" "$STAGE/.dry"; touch "$STAGE/.merged-by-merge_final"
rm -rf "$OUT/src.old"; [ -d "$OUT/src" ] && mv "$OUT/src" "$OUT/src.old"
mv "$STAGE" "$OUT/src"; rm -rf "$OUT/src.old" "$OUT/.stage-fail"
echo "MERGED $OUT/src/redwoodmetal.m $(sha "$OUT/src/redwoodmetal.m")"

# 6. build (the thumbfix5 recipe) and the warnings against thumbfix5's
if [ $BUILD = 1 ]; then
  ( time bash "$OUT/src/build.sh" "$OUT/src" "$OUT" ) > "$OUT/build.log" 2>&1 || { tail -20 "$OUT/build.log"; fail "build"; }
  cat "$OUT/build.log"
  w() { grep -oE 'warning: .*\[-W[^]]+\]' "$1" | sed -E 's/.*\[(-W[^]]+)\]/\1/' | sort | uniq -c; }
  echo "warnings (count by flag): thumbfix5 | this build"
  diff <(w "$B/y2s83-thumbfix5/obj/redwoodmetal.err") <(w "$OUT/obj/redwoodmetal.err") && echo "  same warning set as thumbfix5" || echo "  (differences above)"
  grep -c 'error:' "$OUT/obj/redwoodmetal.err" | sed 's/^/errors: /'
fi
exit 0
# README
# Re-merging after a track changed (example: y2s85 ships photosperf2.diff): apply merge/series up to 10-usecount on thumbfix5,
# patch the new photosperf diff in, resolve its rejects by hand, write the new 20-photosperf.diff (diff -u, labels
# a/redwoodmetal.m b/redwoodmetal.m), then re-check 30/40 against it the same way; update upstream.sha256 (body sha = `sed 1,2d
# FILE | sha256sum`) and core.sha256, and run this script. New tracks do not need that: list their diff in extra_diffs.txt.
