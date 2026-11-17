#!/bin/bash
# No canonical writes/CI/device action. Exported buildtree and final source are explicit.
set -euo pipefail
if [[ $# != 3 ]]; then
 echo 'usage: compile-final-runtime.sh EXPORTED_BUILDTREE FINAL_SWEET_TOUCH_C NEW_PRIVATE_OUTPUT_DIR' >&2
 exit 2
fi
repo_dir=$(cd "$(dirname "$0")/../../../../.." && pwd)
buildtree=$(cd "$1" && pwd)
touch_source=$(realpath "$2")
mkdir "$3"
output_dir=$(cd "$3" && pwd)
python3 "$repo_dir/tools/nord2-xnu-cc.py" "$buildtree" bsd \
 "$repo_dir/shim/nord2-xnu/audio_device.c" "$output_dir/audio_device.o" -DNORD2=1 -Wall -Wextra -Werror
python3 "$repo_dir/tools/nord2-xnu-cc.py" "$buildtree" bsd \
 "$touch_source" "$output_dir/sweet_touch.o" -DNORD2=1
# Source identity belongs in review receipt; these are private objects, not a linked kernel.
sha256sum "$repo_dir/shim/nord2-xnu/audio_device.c" "$touch_source" \
 "$output_dir/audio_device.o" "$output_dir/sweet_touch.o" > "$output_dir/SHA256SUMS"
file "$output_dir/audio_device.o" "$output_dir/sweet_touch.o"
