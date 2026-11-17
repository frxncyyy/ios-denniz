#!/bin/sh
set -eu
exec foot -T 'Nord2 Audio | DeepSeek 4.1 Flash' -a nord2-audio-deepseek \
 -D '$NORD2_REPO/research/nord2/audio/deepseek-4.1-flash/work' -H '$NORD2_REPO/research/nord2/audio/deepseek-4.1-flash/launch.sh' \
 2> '$NORD2_REPO/research/nord2/audio/deepseek-4.1-flash/foot.stderr.log'
