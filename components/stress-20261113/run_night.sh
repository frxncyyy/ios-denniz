#!/bin/bash
# ORCHESTRATOR ONLY (phone transaction). One-command overnight stress + verdict.
#   run_night.sh IMG TAG [--dry-run]
# 1. flash IMG: autocycle/txn_g.sh (needs PAYLOAD/GUEST/OWNED as for run_fbhw_txn.sh; READS <= 600) -- or FLASH_ONLY=1
#    (phone.py reset fastboot + to_fastboot.sh + flash_boot.sh IMG, no payload) -- or NOFLASH=1 (phone already runs IMG);
# 2. window 1 = the stress inside txn_g.sh's own capture (after the UI is up: first 'NORD2 FB PRESENT' + UI_SETTLE s);
# 3. windows 2..WINDOWS: stress_capture.py (continues the kernel log where window 1 stopped, rewinds the 4 MiB buffer) +
#    stress_run.py (<= 540 s each); stops on phone loss / reboot (run_night_resume.sh continues);
# 4. stress_verdict.py -> autocycle/runs/TAG/stress/verdict.txt (+ .json); exit = verdict (0 PASS, 1 FAIL, 3 WARN, 2 no data)
#    or 3/4 after a loss/reboot (the verdict is still printed).
# Env: WINDOWS (40) WIN_MIN (8.5) SEED (1) PLANS ("default;default;subway:400,photos:110,idle:20") COORDS (json) READS (600)
#      UI_SETTLE (20) LOST_WAIT (90) CONTINUE_AFTER_REBOOT (0) VERDICT_ARGS ("--allow-crash parsecd" ...) RUNS (autocycle/runs)
# --dry-run: no phone, no flash: prints the transaction it would start, runs WINDOWS (default 2) simulated windows
#      (stress_run --dry-run + synthetic console via test/synth.py --fill) under stress-20261113/test/dry-runs/TAG, then the verdict.
#      DRY_BAD=1 injects faults; DRY_LOSE_WINDOW=K simulates a phone loss in window K.
set -uo pipefail
IMG=${1:-}; TAG=${2:-}
[ -n "$IMG" ] && [ -n "$TAG" ] || { sed -n '2,20p' "$0"; exit 2; }
[ "${3:-}" = --dry-run ] && export DRY=1 && export WINDOWS=${WINDOWS:-2}
export IMG TAG
source "$(cd "$(dirname "$0")" && pwd)/night_lib.sh"
if [ "$DRY" = 1 ]; then RUNS=${RUNS:-$H/test/dry-runs}; else RUNS=${RUNS:-$B/autocycle/runs}; fi
RUN=$RUNS/$TAG; ST=$RUN/stress
[ ! -e "$RUN" ] || { echo "$RUN exists (use run_night_resume.sh $TAG to continue)"; exit 2; }
[ "$DRY" = 1 ] || [ -f "$IMG" ] || { echo "no image $IMG"; exit 2; }
python3 -c "import sys; sys.exit(0 if float('$WIN_MIN') * 60 <= 540 else 1)" || { echo "WIN_MIN $WIN_MIN > 9 min (capture window <= 600 s)"; exit 2; }
mkdir -p "$ST"; save_state 1 0 0
log "run_night $TAG img $IMG ($( [ "$DRY" = 1 ] && echo dry || sha256sum "$IMG" | cut -c1-16)) windows $WINDOWS x $WIN_MIN min seed $SEED plans $PLANS"

READS=${READS:-600}; [ "$READS" -le 600 ] || { echo "READS $READS > 600 (console tool limit)"; exit 2; }
next=1
if [ "$DRY" = 1 ]; then
  log "dry: would run: READS=$READS PAYLOAD=${PAYLOAD:-?} GUEST=${GUEST:-?} OWNED=${OWNED:-?} $B/autocycle/txn_g.sh $IMG $TAG"
elif [ "${NOFLASH:-0}" = 1 ]; then
  log "NOFLASH: phone expected up with $IMG"
elif [ "${FLASH_ONLY:-0}" = 1 ]; then
  timeout 20 python3 "$PHONE" reset fastboot >/dev/null 2>&1 || true
  "$B/autocycle/to_fastboot.sh" 600 && "$B/autocycle/flash_boot.sh" "$IMG" | tee -a "$ST/night.events" || { log "flash failed"; exit 1; }
  phone_up 600 || { log "phone did not come up after the flash"; exit 3; }
  run_window 1 boot; rc=$?; save_state 2 $rc $([ $rc -eq 3 ] && echo 1 || echo 0); next=2
  [ $rc -eq 3 ] && { verdict; exit 3; }
else
  : "${PAYLOAD:?txn_g.sh needs PAYLOAD (or FLASH_ONLY=1 / NOFLASH=1)}" "${GUEST:?}" "${OWNED:?}"
  READS=$READS PAYLOAD=$PAYLOAD GUEST=$GUEST OWNED=$OWNED "$B/autocycle/txn_g.sh" "$IMG" "$TAG" > "$RUNS/../runs-$TAG.txn.out" 2>&1 &
  TX=$!; LOGF=$RUN/ui-usb/xnu-live.log; cap0=""
  for i in $(seq 1 1200); do                 # snapshot + offline + flash + boot: max 40 min
    sleep 2; kill -0 $TX 2>/dev/null || { log "txn ended before the UI was up: $(tail -3 "$RUNS/../runs-$TAG.txn.out" | tr '\n' ' ')"; break; }
    [ -z "$cap0" ] && [ -f "$LOGF" ] && cap0=$SECONDS
    [ -f "$LOGF" ] && grep -aq 'NORD2 FB PRESENT' "$LOGF" && break
  done
  if kill -0 $TX 2>/dev/null; then
    sleep "${UI_SETTLE:-20}"
    m=$(python3 -c "print(round(min(float('$WIN_MIN'), ($READS - ($SECONDS - ${cap0:-$SECONDS}) - 40) / 60), 2))")
    if python3 -c "import sys; sys.exit(0 if $m >= 1.5 else 1)"; then run_window 1 txn "$m"; rc=$?; else log "window 1: only $m min left in the txn capture: boot log only"; rc=0; fi
    wait $TX; log "txn: $(tail -1 "$RUNS/../runs-$TAG.txn.out")"
    mkdir -p "$ST/w01"; [ -f "$ST/w01/summary.json" ] || echo "{\"window\": 1, \"rc\": 0, \"why\": \"boot capture only\", \"log\": \"$LOGF\"}" > "$ST/w01/summary.json"
    [ -f "$ST/w01/steps.jsonl" ] || echo '{"kind": "event", "event": "start", "t": 0, "log_offset": 0}' > "$ST/w01/steps.jsonl"
    save_state 2 $rc $([ $rc -eq 3 ] && echo 1 || echo 0); next=2
    export INIT_FROM=$LOGF
    [ $rc -eq 3 ] && { verdict; exit 3; }
  else
    wait $TX; log "no UI: see $RUNS/../runs-$TAG.txn.out"; exit 1
  fi
fi
window_loop $next; LR=$?
verdict; VR=$?
[ $LR -ne 0 ] && { log "night stopped early (rc $LR): resume with run_night_resume.sh $TAG"; exit $LR; }
exit $VR
