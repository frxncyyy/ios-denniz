#!/bin/bash
# usage: build_and_run.sh TAG  (builder ../device-mmu-TAG-candidate already started) -> waits, runs the cycle, prints the modem summary
B=$NORD2_WORK/gpu/build
T=$1; C=$B/device-mmu-$T-candidate
until [ -n "$(ls $C/ui-sched/*.img 2>/dev/null)" ] || grep -q FAILED $C/pack.done 2>/dev/null; do sleep 10; done
grep -q FAILED $C/pack.done 2>/dev/null && { echo BUILD FAILED; grep -a "error:" $C/kernelb/*.log | head -5; tail -5 $C/kernelb.stdout; exit 1; }
I=$(ls $C/ui-sched/*.img); sha256sum $I | cut -c1-64
$B/autocycle/run_cycle.sh $I $T
cat $B/autocycle/runs/$T/events
grep -a "NORD2 MODEM" $B/autocycle/runs/$T/ui-usb/xnu-live.log | grep -av "early\|late \|sram\[" | sed -n '/start: begin/,$p' | cut -c1-170 | head -60
date -Is > $B/autocycle/runs/$T/DONE
