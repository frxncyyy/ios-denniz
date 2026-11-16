#!/bin/bash
cd $NORD2_WORK/gpu
for v in base speed; do until ! /tmp/claude-1000/-home-user-Documents-Codex-2026-10-19-vedi-questa-repo-https-github-com-work-ios-sweet-research-nord2-gpu-overnight-20261024-work/0549114f-d223-4e6c-bedc-9119774ffaf5/scratchpad/enc/busy.sh; do sleep 30; done; nice -n 10 python3 tools/audit_shader_pipeline_inputs.py run --output $NORD2_WORK/gpu/build/compile-stall-20261026/census2-$v --workers 4 --allow-compiler-change > $NORD2_WORK/gpu/build/compile-stall-20261026/census2-$v.run.log 2>&1; echo "$v rc=$? $(date +%T)"; done
echo CENSUS2-DONE
