#!/bin/bash
# Keep the phone's kernel log on the PC's disk while somebody uses the phone, so the last seconds before a crash are not lost
# (the live log is RAM only; nothing of it can be read after a reset: runs/crash1).
#   logtail.sh NAME        appends to runs/NAME/live.log until stopped (Ctrl-C / kill); start it in the background:
#                          nohup gpu/build/autotest/logtail.sh user5 > /dev/null 2>&1 &
# Every round reads what the kernel printed since the last one (phone.py logs, which rewinds the kernel's 4 MiB buffer when it has
# read 1 MiB, so it never fills) and waits one second. When the phone leaves USB (crash, reset, fastboot) a line with the time is
# written to runs/NAME/events.log and the loop waits for the channel to come back; the new boot's log goes on in the same file after
# a "===== boot" line. It uses its own offset file, so `phone.py logs` / scenarios run by hand at the same time still work, but
# each reader then sees only part of the log once a rewind happens: do not run two readers during a test you care about.
A=$(cd "$(dirname "$0")" && pwd); N=${1:?usage: logtail.sh NAME}; R=$A/runs/$N; mkdir -p "$R"
export NORD2_PHONE_STATE=$R/.logtail-state
ev(){ echo "$(date -Is) $*" >> "$R/events.log"; }
up=0; boot=""
ev "logtail started"
while :; do
  if st=$(timeout 10 python3 "$A/phone.py" status 2>/dev/null); then
    b=$(echo "$st" | sed -n 's/.*"boot": \([0-9]*\).*/\1/p')
    if [ "$up" = 0 ] || [ "$b" != "$boot" ]; then
      ev "channel up, boot id $b"; echo "===== boot $b $(date -Is) =====" >> "$R/live.log"; up=1; boot=$b
    fi
    timeout 60 python3 "$A/phone.py" logs "$R/live.log" --seconds 5 2>> "$R/events.log" || true
  else
    [ "$up" = 1 ] && ev "CHANNEL LOST (crash, reset or cable): last log line: $(tail -c 300 "$R/live.log" | tr -d '\000' | tail -1 | cut -c1-200)"
    up=0; sleep 1
  fi
done
