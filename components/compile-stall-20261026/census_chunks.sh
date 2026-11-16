#!/bin/bash
# Resumable all-family census in chunks of 250 variants, yielding the heavy lease between chunks.
cd $NORD2_WORK/gpu
for v in base fold; do
  for i in $(seq 1 16); do
    n=$(ls $NORD2_WORK/gpu/build/compile-stall-20261026/censusall-$v/runs 2>/dev/null | wc -l)
    [ "$n" -ge 3459 ] && break
    until ! /tmp/claude-1000/-home-user-Documents-Codex-2026-10-19-vedi-questa-repo-https-github-com-work-ios-sweet-research-nord2-gpu-overnight-20261024-work/0549114f-d223-4e6c-bedc-9119774ffaf5/scratchpad/enc/busy.sh; do sleep 30; done
    nice -n 10 python3 tools/audit_shader_pipeline_inputs.py run --output $NORD2_WORK/gpu/build/compile-stall-20261026/censusall-$v --workers 1 --limit $((250*i)) --allow-compiler-change >> $NORD2_WORK/gpu/build/compile-stall-20261026/censusall-$v.run.log 2>&1
    echo "$v chunk $i runs=$(ls $NORD2_WORK/gpu/build/compile-stall-20261026/censusall-$v/runs | wc -l) $(date +%T)"
  done
done
echo CENSUS-DONE
