#!/system/bin/sh
# pw.sh r ADDR | pw.sh w ADDR VAL   (MT6359P via PWRAP SWINF2)
B=0x10026
st(){ echo $(( ($(devmem ${B}ca8 4) >> 1) & 7 )); }
waitf(){ i=0; while [ $(st) -ne $1 ]; do i=$((i+1)); [ $i -gt 200 ] && { echo TIMEOUT; return 1; }; done; }
if [ "$1" = r ]; then waitf 0 || exit 1; devmem ${B}c80 4 $2; waitf 6 || exit 1; v=$(devmem ${B}c94 4); devmem ${B}ca4 4 1; printf "0x%04x\n" $((v & 0xffff));
else waitf 0 || exit 1; devmem ${B}c84 4 $3; devmem ${B}c80 4 $(( (1<<29) | $2 )); waitf 0 || exit 1; echo ok; fi
