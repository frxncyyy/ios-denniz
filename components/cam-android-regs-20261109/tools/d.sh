#!/bin/bash
# d.sh <sym> [len]: disassemble a symbol of the live Android kernel
D=$(dirname "$0")
a=$(grep -m1 " $1\$" "$D/../kallsyms.txt" | cut -d' ' -f1)
o=$(( 0x$a - 0xffffff93c3c80000 ))
l=${2:-512}
llvm-objdump -d --no-show-raw-insn --triple=aarch64 --adjust-vma=0xffffff93c3c80000 --start-address=$((o)) --stop-address=$((o + l)) "$D/img.elf" | tail -n +7
