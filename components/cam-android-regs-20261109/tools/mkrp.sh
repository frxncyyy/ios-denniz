#!/bin/bash
# mkrp.sh <replay-env...>: alloc 2MB, map at IOVA in larb13 domain, replay, unmap, free  (all in one run)
IOVA=0x240000000
printf "SET 8 0xcc0\nSET 9 ${ORDER:-8}\nCALLR 3 &__get_free_pages\nNZ %%3\nLD 5 &memstart_addr\nADD 6 %%3 0x4000000000\nADD 7 %%6 %%5\n"
printf "SET 8 13\nCALLR 1 &pseudo_get_larbdev\nSET 8 %%1\nCALLR 2 &iommu_get_domain_for_dev\nSET 8 %%2\nSET 9 $IOVA\nSET 10 %%7\nSET 11 ${MAPSZ:-0x100000}\nSET 12 3\nCALLR 4 &iommu_map\nZ %%4\n"
env "$@" VA=0 PA=$((IOVA & 0xffffffff)) python3 "$(dirname "$0")/replay.py" | sed 's/^F 0x0 /F %3 /; s/^Q 0x0 /Q %3 /'
printf "SET 8 %%2\nSET 9 $IOVA\nSET 10 ${MAPSZ:-0x100000}\nCALLR 6 &iommu_unmap\nSET 8 %%3\nSET 9 ${ORDER:-8}\nCALLR 6 &free_pages\n"
