#!/bin/bash
# Shared by run_night.sh and run_night_resume.sh (sourced). Window loop: capture (stress_capture.py, rewinds) + stress_run.py,
# loss/reboot handling, state file, verdict.  Not meant to be run directly.
H=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
B=$(dirname "$H")
PHONE="$B/autotest/phone.py"
WINDOWS=${WINDOWS:-40}                 # 40 x ~9.3 min ~ 6.2 h
WIN_MIN=${WIN_MIN:-8.5}                # stress minutes per window (<= 9.0: capture window <= 600 s incl. margins)
SEED=${SEED:-1}
PLANS=${PLANS:-"default;default;subway:400,photos:110,idle:20"}   # per window, cycled; 'default' = all phases scaled to WIN_MIN
LOST_WAIT=${LOST_WAIT:-90}
DRY=${DRY:-0}
export WINDOWS WIN_MIN SEED PLANS

log() { echo "$(date -Is) $*" | tee -a "$ST/night.events"; }

phone_up() {   # $1 = seconds to wait
  [ "$DRY" = 1 ] && return 0
  timeout $(( $1 + 15 )) python3 "$PHONE" wait "$1" >/dev/null 2>&1
}

save_state() {  # next_window last_rc lost
  python3 - "$ST/night.json" "$1" "$2" "$3" <<'EOF'
import json, sys, os
f, nxt, rc, lost = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
d = json.load(open(f)) if os.path.exists(f) else {}
d.update(next_window=nxt, last_rc=rc, lost=lost == '1', windows=int(os.environ.get('WINDOWS', 40)),
         seed=int(os.environ.get('SEED', 1)), win_min=float(os.environ.get('WIN_MIN', 8.5)), tag=os.environ.get('TAG'),
         img=os.environ.get('IMG', d.get('img')), plans=os.environ.get('PLANS'))
json.dump(d, open(f, 'w'), indent=1)
EOF
}

plan_args() {   # window index -> --plan ... (or nothing for 'default')
  local n p; IFS=';' read -ra P <<< "$PLANS"; n=${#P[@]}; p=${P[$(( ($1 - 1) % n ))]}
  [ "$p" = default ] || [ -z "$p" ] || echo "--plan $p"
}

# run_window K [first]   first=txn: window 1 inside txn_g.sh's capture (log = ui-usb/xnu-live.log, minutes given in $3)
#                        first=boot: capture from boot (flash-only): wait for the UI in our own capture first
run_window() {
  local k=$1 first=${2:-} d CP SR=0 CR=0 minutes=$WIN_MIN logf extra
  d=$ST/w$(printf %02d "$k"); mkdir -p "$d"
  extra="$(plan_args "$k") ${COORDS:+--coords $COORDS}"
  if [ "$DRY" = 1 ]; then
    lose=""; [ "${DRY_LOSE_WINDOW:-0}" = "$k" ] && lose="--dry-lose-after 250"
    python3 "$H/stress_run.py" "$d" --dry-run --minutes "$minutes" --seed "$SEED" --window "$k" $extra $lose > "$d/run.out" 2>&1; SR=$?
    python3 "$H/test/synth.py" --fill "$d" ${DRY_BAD:+--bad} >/dev/null
    log "window $k (dry): stress rc $SR"; return $SR
  fi
  if [ "$first" = txn ]; then
    logf=$RUN/ui-usb/xnu-live.log; minutes=$3
    python3 "$H/stress_run.py" "$d" --log "$logf" --minutes "$minutes" --seed "$SEED" --window "$k" $extra > "$d/run.out" 2>&1; SR=$?
    log "window $k (txn capture): stress rc $SR, $(tail -1 "$d/run.out")"; return $SR
  fi
  logf=$d/console.log
  local secs=$(python3 -c "print(int(float('$minutes')*60) + 45)")
  [ "$first" = boot ] && secs=600
  python3 "$H/stress_capture.py" "$logf" --state "$ST/capture-state.json" --seconds "$secs" --stop-file "$d/stop" \
      --lost-wait "$LOST_WAIT" ${INIT_FROM:+--init-from $INIT_FROM} > "$d/capture.out" 2>&1 &
  CP=$!
  sleep 3
  if ! kill -0 $CP 2>/dev/null; then wait $CP; CR=$?; log "window $k: capture ended at once (rc $CR): $(tail -2 "$d/capture.out" | tr '\n' ' ')"; return $(( CR ? CR : 3 )); fi
  if [ "$first" = boot ]; then          # flash-only boot: wait for the first present (UI up), then fit the stress into the window
    local t0=$SECONDS
    until grep -aq 'NORD2 FB PRESENT' "$logf" 2>/dev/null || [ $((SECONDS - t0)) -gt 300 ]; do sleep 2; done
    sleep "${UI_SETTLE:-20}"
    minutes=$(python3 -c "print(round(min(float('$WIN_MIN'), (600 - 3 - $((SECONDS - t0)) - 40) / 60), 2))")
    log "window $k: UI up after $((SECONDS - t0)) s, stress $minutes min"
  fi
  python3 "$H/stress_run.py" "$d" --log "$logf" --minutes "$minutes" --seed "$SEED" --window "$k" $extra > "$d/run.out" 2>&1; SR=$?
  sleep 4; touch "$d/stop"; wait $CP; CR=$?
  log "window $k: stress rc $SR, capture rc $CR, log $(stat -c %s "$logf" 2>/dev/null) B, $(tail -1 "$d/run.out")"
  [ $SR -ne 0 ] && return $SR
  return $CR
}

# loop K0..WINDOWS; stops on phone loss (3) or reboot (4) unless CONTINUE_AFTER_REBOOT=1
window_loop() {
  local k=$1 rc
  while [ "$k" -le "$WINDOWS" ]; do
    if ! phone_up "$LOST_WAIT"; then
      log "PHONE LOST before window $k (no console for $LOST_WAIT s)"
      echo "PHONE LOST before window $k (no console for $LOST_WAIT s); last capture: $(ls -t "$ST"/w*/console.log 2>/dev/null | head -1)" > "$ST/PHONE_LOST.txt"
      save_state "$k" 3 1; return 3
    fi
    run_window "$k"; rc=$?
    INIT_FROM=""
    if [ $rc -eq 3 ]; then log "PHONE LOST in window $k"; cp "$ST/w$(printf %02d "$k")/PHONE_LOST.txt" "$ST/PHONE_LOST.txt" 2>/dev/null; save_state $((k + 1)) 3 1; return 3; fi
    if [ $rc -eq 4 ]; then
      log "PHONE REBOOTED in window $k"; save_state $((k + 1)) 4 0
      [ "${CONTINUE_AFTER_REBOOT:-0}" = 1 ] || return 4
    else
      save_state $((k + 1)) $rc 0
    fi
    k=$((k + 1))
  done
  return 0
}

verdict() {
  python3 "$H/stress_verdict.py" "$ST" --json "$ST/verdict.json" ${COORDS:+--coords $COORDS} ${VERDICT_ARGS:-} > "$ST/verdict.txt" 2>&1; local rc=$?
  cat "$ST/verdict.txt"
  echo "report: $ST/verdict.txt  json: $ST/verdict.json  screenshots: $ST/w*/shots/"
  return $rc
}
