#!/bin/bash
# 0402b: builds the n2fbhwctl fork (build_ios.sh: host test + iOS link) and writes the payloads for autocycle/txn_g.sh into apps-20261104
# (guest = guest-fbm0402.sh, undo = guest-fbm0402-undo.sh / payload-fbm0402cycle-undo, catalog = owned-fbm0402.json = owned-fbhwctl.json):
#   payload-fbm0402cycle  ONE boot, mode 3 throughout (pool on), one 0402b feature per phase, 180 s each:
#     A baseline (all defaults = the 0401 frame path)   B kern.n2fbscaling=1 + backboardd restart   C kern.n2fbh6=1 (scaling off again + restart)
#     D kern.n2fbhwlayers=4 (4L)                          E all three (+ restart)                     end: defaults back, kern.n2fbhw=0
# Then apps-20261104/rehearse-fbm0402.sh must print REHEARSAL_OK.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); A=$H/../../apps-20261104
"$H/build_ios.sh" >/dev/null; X=$H/build/n2fbhwctl; S=$(sha256sum "$X" | cut -c1-64)
CFG=${CYCLE_CONFIG:-"modes=3,3,3,3,3 scal=0,1,0,0,1 h6=0,0,1,0,1 layers=2,2,2,4,4 bbd=0,1,1,0,1 period=180 delay=90 pool=1 final=0"}
d=$A/payload-fbm0402cycle; rm -rf "$d"; mkdir -p "$d"; cp "$X" "$d/n2fbhwctl"
python3 - "$d/plan.json" "$CFG" "$S" <<'PY'
import json,sys
p,cfg,sha=sys.argv[1:]
json.dump({'name':'fbm0402cycle','guest':'guest-fbm0402.sh','undo':'guest-fbm0402-undo.sh (payload-fbm0402cycle-undo)','files':{'n2fbhwctl':sha},'config':cfg,
           'purpose':'one boot, kern.n2fbhw=3 + pool: phase A defaults, B kern.n2fbscaling=1 (+backboardd restart), C kern.n2fbh6=1 (+restart, scaling off), D kern.n2fbhwlayers=4, E all (+restart); 180 s each; the last N2FBHWCTL phase line before a lockup names the feature set',
           'needs':['a kernel with 0402b (kern.n2fbscaling/n2fbh6/n2fbhwlayers) on 0394..0401; without them the feat lines log rc=2 and only the mode cycle runs']},open(p,'w'),indent=1)
PY
u=$A/payload-fbm0402cycle-undo; rm -rf "$u"; mkdir -p "$u"; echo '{"name":"fbm0402cycle-undo","guest":"guest-fbm0402-undo.sh","files":{}}' > "$u/plan.json"
echo "payload-fbm0402cycle: $CFG"; sha256sum "$X" "$d/plan.json"
