# kill every instance, wait for the modem to be gated, run n2mdinit, report
for d in /proc/[0-9]*; do c=$(cat $d/comm 2>/dev/null); case $c in ccci_mdinit) kill -9 $(basename $d);; esac; done
sleep 6
echo "state before: $(cat /sys/kernel/ccci/boot | cut -d'|' -f1)"
(setsid nohup /tmp/n2mdinit > /tmp/n2.out 2>&1 &)
for i in $(seq 1 24); do sleep 5; s=$(cat /sys/kernel/ccci/boot | cut -d'|' -f1); echo "t=$((i*5)) $s fsops=$(grep -c '^fs' /tmp/n2.out) procs=$(for d in /proc/[0-9]*; do [ "$(cat $d/comm 2>/dev/null)" = ccci_mdinit ] && echo $(basename $d):$(cat $d/wchan 2>/dev/null):$(cut -d' ' -f3 $d/stat); done | tr '\n' ' ')"; case "$s" in "md1:4 "|"md1:5 ") break;; esac; done
grep -n "ffffffee\|fffffff7\|unknown" /tmp/n2.out | head -12
