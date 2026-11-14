#!/bin/bash
# usage: sym.sh SLIDE ADDR...   (prints unslid symbol per address; caches)
IPSW=~/dsc-work/ipsw-dl/x/ipsw; DSC=~/dsc-work/dsc-full/dyld_shared_cache_arm64
SL=$1; shift
for a in "$@"; do u=$(printf '0x%x' $(( a - SL ))); r=$(grep -m1 "^$u:" $(dirname $0)/sym.cache 2>/dev/null); if [ -z "$r" ]; then r=$(timeout 120 $IPSW dyld a2s $DSC $u 2>&1 | tail -1); echo "$r" >> $(dirname $0)/sym.cache; fi; echo "$a $r"; done
