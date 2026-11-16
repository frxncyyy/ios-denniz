#!/usr/bin/env bash
# Regenerate inventory.json and the appendix of API-SURFACE.md (everything from "## Appendix" on).
set -euo pipefail
cd "$(dirname "$0")"
python3 api_inventory.py --json inventory.json >/dev/null
python3 - <<'PY'
s=open('API-SURFACE.md').read()
i=s.find('## Appendix: per-protocol coverage')
if i>=0: s=s[:i].rstrip()+'\n'
open('API-SURFACE.md','w').write(s)
PY
python3 gen_table.py >> API-SURFACE.md
