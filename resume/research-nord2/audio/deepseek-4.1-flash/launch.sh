#!/bin/sh
set -eu
cd '$NORD2_REPO/research/nord2/audio/deepseek-4.1-flash'
exec script -q -f -c '$NORD2_REPO/research/nord2/audio/deepseek-4.1-flash/run-agent.sh' '$NORD2_REPO/research/nord2/audio/deepseek-4.1-flash/terminal.typescript'
