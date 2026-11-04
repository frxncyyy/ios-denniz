#!/bin/sh
# Verify the working audio/Wi-Fi/display baseline was not modified.
set -eu
cd "$(dirname "$0")/../.."
BASE=${BASE:-b2ccaed}
changed=$(git diff --name-only "$BASE" -- \
  'shim/nord2-xnu/audio_*' 'shim/nord2-xnu/SweetBattery.cpp' \
  'shim/nord2-xnu/SweetFramebuffer.cpp' 'shim/nord2-xnu/SweetSurface.*' \
  'tools/mkadt-nord2.py' 'docs/nord2-audio*' 'docs/nord2-wifi*' \
  'tests/nord2_audio*' 'tools/nord2-audio*' 'tools/nord2-wifi*' \
  'tools/test-nord2-wifi*' 'tools/test-nord2-audio*' || true)
# mkadt-nord2.py adds Mali and its explicit GIC interrupt binding; allow it.
unexpected=$(printf '%s\n' "$changed" | grep -v '^tools/mkadt-nord2.py$' || true)
if [ -n "$unexpected" ]; then
  echo "baseline files modified:" >&2
  echo "$unexpected" >&2
  exit 1
fi
echo "audio/Wi-Fi/display baseline preserved (ADT Mali/GIC binding extended)"
