#!/bin/bash
# usage: build_after.sh PREV NEXT : build NEXT (build only, never flash) once PREV's image exists
B=$NORD2_WORK/gpu/build
until [ -n "$(ls $B/device-mmu-$1-candidate/ui-sched/*.img 2>/dev/null)" ] || grep -q FAILED $B/device-mmu-$1-candidate/pack.done 2>/dev/null; do sleep 10; done
sleep 5; cd $B/device-mmu-$2-candidate; rm -rf kernelb ui-sched pack.done; ./build_$2.sh > build_$2.log 2>&1
