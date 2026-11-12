#!/bin/bash
# 0401: builds the n2fbhwctl fork (build_ios.sh: host test + iOS link) and writes payload dirs for autocycle/txn_g.sh into apps-20261104
# (guest = guest-fbiommu.sh, undo = guest-fbhwctl-undo.sh / payload-fbhwctl-undo, catalog = owned-fbhwctl.json: same paths as fbhwctl):
#   payload-fbiommuprobe  "modes=0 period=0 delay=60 probe=1"   READ-ONLY: M4U0/larb/clock/GIC dump at job start, CPU path, nothing armed
#   payload-fbiommucycle  "modes=3,3,3,3,3 iommu=0,1,2,3,0 probe=1 iotest=1 hwtest=6 period=120 delay=90 pool=1 final=0"
#                         one boot: pool-only HW (iommu 0) -> dry run (1) -> single-layer IOVA (2, + scattered colour test 6) -> all (3) -> 0
#   payload-fbiommustage  "modes=0 period=0 delay=5 probe=1 stages=1,2,0,3 stagegap=20"  (0403) staged arming, last 'stage' line = last stage reached
#   payload-fbiommumode3  "modes=3 iommu=3 period=0 delay=60 pool=1 probe=1"   soak: HW mode 3 with every layer through the IOMMU
# Then apps-20261104/rehearse-fbiommu.sh must print REHEARSAL_OK.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); A=$H/../../apps-20261104
"$H/build_ios.sh" >/dev/null; X=$H/build/n2fbhwctl; S=$(sha256sum "$X" | cut -c1-64)
mk() { # name config purpose
  local d=$A/payload-$1; rm -rf "$d"; mkdir -p "$d"; cp "$X" "$d/n2fbhwctl"
  python3 - "$d/plan.json" "$1" "$2" "$3" "$S" <<'PY'
import json,sys
p,name,cfg,purpose,sha=sys.argv[1:]
json.dump({'name':name,'purpose':purpose,'guest':'guest-fbiommu.sh','undo':'guest-fbhwctl-undo.sh (payload-fbhwctl-undo)',
           'files':{'n2fbhwctl':sha},'config':cfg,
           'needs':['a kernel with 0401 (kern.n2fbiommu*) on 0394..0400 -- e.g. q10jfj + 0401; without 0401 the iommu lines log rc=2 and the fbhw schedule still runs']},open(p,'w'),indent=1)
PY
  echo "payload-$1: $2"; }
mk fbiommuprobe "modes=0 period=0 delay=60 probe=1" "READ-ONLY probe boot: kern.n2fbiommu_probe=1 at job start (M4U0, larb0/1 ports, MMSYS clocks, GIC line), CPU display path, IOMMU never armed"
mk fbiommucycle "${CYCLE_CONFIG:-modes=3,3,3,3,3 iommu=0,1,2,3,0 probe=1 iotest=1 hwtest=6 period=120 delay=90 pool=1 final=0}" "one boot: HW mode 3 with IOMMU 0 (pool only), 1 (dry run), 2 (single-layer IOVA + scattered colour test), 3 (all layers), 0 again; probe dump at start and after every phase"
mk fbiommustage "modes=0 period=0 delay=5 probe=1 stages=1,2,0,3 stagegap=20" "0403 staged arming (kernel with 0403): stage 1 arm only, stage 2 MMU_EN on one OVL port (identity) 2 s + watchdog, 0 off, stage 3 both ports 2 s; 20 s apart, probe after each; CPU display path"
mk fbiommumode3 "modes=3 iommu=3 period=0 delay=60 pool=1 probe=1" "soak: HW mode 3, every eligible layer through the IOMMU, for the whole boot"
sha256sum "$X" $A/payload-fbiommu*/plan.json
