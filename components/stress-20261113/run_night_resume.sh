#!/bin/bash
# ORCHESTRATOR ONLY. Continue an overnight run after a phone loss / reboot, without reflashing, once the phone is back up.
#   run_night_resume.sh TAG [--dry-run]
# Reads autocycle/runs/TAG/stress/night.json (next_window, windows, seed, plans), waits up to RESUME_WAIT s (300) for the console,
# then runs the remaining windows (stress_capture.py notices a reboot by the boot id and logs a REBOOT event: the verdict keeps that
# FAIL, the night already lost the phone once) and re-runs the verdict over ALL windows.
# If the phone is not back: exit 3 (bring it up, e.g. run_night.sh IMG NEWTAG with FLASH_ONLY=1, or reflash by hand).
# Env overrides as run_night.sh (WINDOWS can be raised to extend the night).
set -uo pipefail
TAG=${1:-}; [ -n "$TAG" ] || { sed -n '2,9p' "$0"; exit 2; }
[ "${2:-}" = --dry-run ] && export DRY=1
H0=$(cd "$(dirname "$0")" && pwd)
if [ "${DRY:-0}" = 1 ]; then RUNS=${RUNS:-$H0/test/dry-runs}; else RUNS=${RUNS:-$(dirname "$H0")/autocycle/runs}; fi
ST=$RUNS/$TAG/stress
[ -f "$ST/night.json" ] || { echo "no $ST/night.json"; exit 2; }
eval "$(python3 - "$ST/night.json" <<'EOF'
import json, sys, shlex, os
d = json.load(open(sys.argv[1]))
for k, env in (('windows', 'WINDOWS'), ('seed', 'SEED'), ('win_min', 'WIN_MIN'), ('plans', 'PLANS'), ('img', 'IMG')):
    if d.get(k) is not None and env not in os.environ: print('export %s=%s' % (env, shlex.quote(str(d[k]))))
print('NEXT=%d' % d.get('next_window', 1))
EOF
)"
export TAG
source "$H0/night_lib.sh"
RUN=$RUNS/$TAG
log "resume $TAG at window $NEXT of $WINDOWS"
rm -f "$ST/PHONE_LOST.txt.resumed"; [ -f "$ST/PHONE_LOST.txt" ] && mv "$ST/PHONE_LOST.txt" "$ST/PHONE_LOST.txt.resumed"
if ! phone_up "${RESUME_WAIT:-300}"; then
  log "phone not back after ${RESUME_WAIT:-300} s: bring it up (FLASH_ONLY=1 run_night.sh IMG NEWTAG, or reflash), then resume"
  mv "$ST/PHONE_LOST.txt.resumed" "$ST/PHONE_LOST.txt" 2>/dev/null; exit 3
fi
window_loop "$NEXT"; LR=$?
verdict; VR=$?
[ -f "$ST/PHONE_LOST.txt.resumed" ] && echo "NOTE: the night lost the phone before this resume ($ST/PHONE_LOST.txt.resumed)"
[ $LR -ne 0 ] && { log "stopped early again (rc $LR)"; exit $LR; }
exit $VR
