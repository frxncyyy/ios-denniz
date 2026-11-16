#!/bin/bash
# memrun.sh CMD...: under the heavy lock, 1.5 GB virtual cap, 120 s; prints the peak RSS (VmHWM) at the end
flock $HOME/nord2-heavy.lock bash -c '
ulimit -v 1500000
timeout 120 "$@" & p=$!; hwm=0
while kill -0 $p 2>/dev/null; do v=$(awk "/VmHWM/{print \$2}" /proc/$p/status 2>/dev/null); [ -n "$v" ] && hwm=$v; sleep 0.2; done
wait $p; rc=$?; echo "MEMRUN rc=$rc peak_rss_kb=$hwm"' _ "$@"
