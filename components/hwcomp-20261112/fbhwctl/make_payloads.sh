#!/bin/bash
# Builds n2fbhwctl (build_ios.sh: host test + iOS link) and writes the payload dirs for autocycle/txn_g.sh into apps-20261104:
#   payload-fbhwmode0      config "modes=0 period=0 delay=20 pool=0"           baseline (CPU path, no pool)
#   payload-fbhwmode1..3   config "modes=N period=0 delay=20 pool=1"           fixed mode N from 20 s after the job starts, pool at once
#   payload-fbhwcycle      config "modes=0,1,2,3 period=120 delay=90 pool=1 final=0"   one boot = 4 x 120 s comparison, then mode 0
#   payload-fbhwctl-undo   (plan.json only)                                    for guest-fbhwctl-undo.sh
#   owned-fbhwctl.json     owned-gnssd2.json + the paths this payload writes (the catalog check refuses anything else)
# CYCLE_CONFIG / FIXED_DELAY override the configs. Then apps-20261104/rehearse-fbhwctl.sh must print REHEARSAL_OK.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); A=$H/../../apps-20261104
"$H/build_ios.sh" >/dev/null; X=$H/build/n2fbhwctl; S=$(sha256sum "$X" | cut -c1-64)
mk() { # name config purpose
  local d=$A/payload-$1; rm -rf "$d"; mkdir -p "$d"; cp "$X" "$d/n2fbhwctl"
  python3 - "$d/plan.json" "$1" "$2" "$3" "$S" <<'PY'
import json,sys
p,name,cfg,purpose,sha=sys.argv[1:]
json.dump({'name':name,'purpose':purpose,'guest':'guest-fbhwctl.sh','undo':'guest-fbhwctl-undo.sh (payload-fbhwctl-undo)',
           'files':{'n2fbhwctl':sha},'config':cfg,
           'needs':['a kernel with 0396 (kern.n2fbhw) -- q10jfi or q10jfj; 0394 for pool=0/1 (kern.n2fbhw_pool)']},open(p,'w'),indent=1)
PY
  echo "payload-$1: $2"; }
D=${FIXED_DELAY:-20}
mk fbhwmode0 "modes=0 period=0 delay=$D pool=0" "baseline: CPU path, no pool (kern.n2fbhw=0, kern.n2fbhw_pool=0) = q10jfh behaviour"
for m in 1 2 3; do mk fbhwmode$m "modes=$m period=0 delay=$D pool=1" "fixed display HW composition mode $m (kern.n2fbhw=$m from $D s after the job starts; kern.n2fbhw_pool=1 at once), for the whole boot"; done
mk fbhwcycle "${CYCLE_CONFIG:-modes=0,1,2,3 period=120 delay=90 pool=1 final=0}" "one boot compares modes 0,1,2,3 (120 s each after a 90 s settle), then mode 0; run hwcomp-20261112/phone/fbhw_scenario.py --cycle meanwhile"
rm -rf $A/payload-fbhwctl-undo; mkdir -p $A/payload-fbhwctl-undo
echo '{"name":"fbhwctl-undo","guest":"guest-fbhwctl-undo.sh","files":{}}' > $A/payload-fbhwctl-undo/plan.json
python3 - $A <<'PY'
import json,sys
A=sys.argv[1]; o=json.load(open(A+'/owned-gnssd2.json'))
add=['/usr/local/libexec/n2fbhwctl','/private/var/db/nord2-fbhw-mode','/private/var/db/nord2-fbhwctl.json']
json.dump(o+[x for x in add if x not in o],open(A+'/owned-fbhwctl.json','w'))
print('owned-fbhwctl.json', len(o), '+', len(add))
PY
sha256sum "$X" $A/payload-fbhw*/plan.json
