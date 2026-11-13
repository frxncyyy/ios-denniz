#!/bin/bash
# Rebuild work/b from work/a (copy of the q10jgw stage Wi-Fi sources) + work/new + the edit scripts.
set -euo pipefail
cd "$(dirname "$0")"
python3 gen.py
