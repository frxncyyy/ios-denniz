#!/bin/bash
# mkbp.sh <replay-env...>: alloc (DMA32), NO iommu mapping, larb port MMU off, physical addresses
printf "SET 8 ${GFP:-0xcc4}\nSET 9 ${ORDER:-6}\nCALLR 3 &__get_free_pages\nNZ %%3\nLD 5 &memstart_addr\nADD 6 %%3 0x4000000000\nADD 7 %%6 %%5\nADD 14 %%7 ${HDROFF:-0x30000}\nADD 15 %%7 0\n"
env "$@" VA=0 PA=0x11111000 MMU=0 python3 "$(dirname "$0")/replay.py" | sed 's/^F 0x0 /F %3 /; s/^Q 0x0 /Q %3 /; s/ 0x11111000$/ %7/; s/ 0x111'"${HDROFF#0x}"'$/ %14/' | sed "s/^W \(0x1a09[0-9a-f]424\) 0x0$/W \1 0x0/"
printf "SET 8 %%3\nSET 9 ${ORDER:-6}\nCALLR 6 &free_pages\n"
