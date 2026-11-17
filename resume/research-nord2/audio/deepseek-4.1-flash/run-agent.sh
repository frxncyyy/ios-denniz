#!/bin/sh
set -eu
cd '$NORD2_REPO/research/nord2/audio/deepseek-4.1-flash/work'
printf '%s\n' "$$" > '$NORD2_REPO/research/nord2/audio/deepseek-4.1-flash/agent.pid'
prompt=$(cat '$NORD2_REPO/research/nord2/audio/deepseek-4.1-flash/PROMPT.md')
exec opencode mini --standalone --model opencode-go/deepseek-v4.1-flash --prompt "$prompt"
