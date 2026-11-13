#!/bin/bash
# usage: phone_chain.sh IMG TAG   -- phone in fastboot. Runs three rootfs transactions back to back with the SAME kernel image IMG:
#   1. vt17  (Nord2Metal: Photos shader dump, VT cache)   2. still4096 (camera plist)   3. vcodec (vcodecd + vendor libs + probe gates)
# Between transactions it waits for the autotest channel and resets the phone to fastboot (phone.py reset fastboot).
# The last boot is the encode test: watch runs/TAG-final/live.log (logtail) for N2VCDT / N2VCDP / NORD2 VCODEC lines.
set -u
W=$NORD2_WORK
B=$W/gpu/build; A=$B/autotest; D=$B/autocycle; P=$B/apps-20261104
IMG=${1:?image}; TAG=${2:?tag}
step() { # name payload guest owned
  echo "$(date -Is) === $1"
  env PAYLOAD=$2 GUEST=$3 OWNED=$4 timeout 900 bash $D/txn.sh $IMG $TAG-$1 > $D/runs-$TAG-$1.out 2>&1
  tail -2 $B/cc-txn-$TAG-$1/events
}
back_to_fastboot() {
  timeout 200 python3 $A/phone.py wait 200 > /dev/null 2>&1
  sleep 20
  timeout 60 python3 $A/phone.py reset fastboot | tail -1
  sleep 20
  ~/Android/Sdk/platform-tools/fastboot devices | head -1
}
step vt17 payload-vt17 guest-vt17.sh $P/owned-vt17.json
back_to_fastboot
step still4096 payload-still4096 guest-still4096.sh $P/owned-still4096.json
back_to_fastboot
step vcodec payload-vcodec guest-vcodec.sh $P/owned-vcodec.json
echo "$(date -Is) chain done; the last boot runs the vcodec self-test and the encode probe (gates). Read: python3 $A/phone.py logs OUT.log | grep -a 'N2VCD\|NORD2 VCODEC'"
