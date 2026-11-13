#!/bin/bash
# Rebuild work/b from work/a (copy of the q10jgw stage Wi-Fi sources) + work/new + the edit scripts.
set -euo pipefail
cd "$(dirname "$0")"; rm -rf b; cp -a a b; cp -a new/. b/
python3 edit_core.py; [ ! -f edit_bind.py ] || python3 edit_bind.py
