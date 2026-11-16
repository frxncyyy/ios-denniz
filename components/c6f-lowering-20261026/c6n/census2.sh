#!/usr/bin/env bash
# fsjobs census of the c6n tree with draw reorder off / in-pass / cross-pass (host only).
cd $NORD2_WORK/gpu/build/c6f-lowering-20261026
P=$NORD2_WORK/gpu/build/deepseek-parallel-20261025/board/post.py
busy(){ pgrep -x 'clang|clang-23|cc1|rustc|cargo|ninja|ld|ld.lld' >/dev/null || pgrep -f '^(timeout [0-9]+ )?python3 .*(frame_bench|ca_backboardd_host_test|metal_resource_integration|host_test_vs_texture|producer_plan|fsjobs|probe_host)' >/dev/null || pgrep -f '^bash tools/run_host_tests' >/dev/null || pgrep -f 'c6n/gates.sh' >/dev/null; }
until ! busy && sleep 5 && ! busy; do sleep 15; done
python3 $P post --from lowering-opus --to all --subject "HEAVY TAKE: lowering-opus fsjobs census x3 (reorder 0/1/2), nice10 jobs1" --body "own dir fsjobs/build-c6n" >/dev/null
for r in 0 1 2; do
 extra=""; [ $r = 0 ] && extra="--rebuild"
 TEGU_DRAW_REORDER=$r nice -n 10 python3 fsjobs/fsjobs.py $extra --tag c6n --preframe 1 > c6n/census-r$r.log 2>&1; echo "r$r rc=$?" >> c6n/census-r$r.log
 cp fsjobs/census-c6n-pf1.json c6n/census-r$r.json; cp fsjobs/fsjobs-c6n-pf1.json c6n/fsjobs-r$r.json
done
python3 $P post --from lowering-opus --to all --subject "HEAVY RELEASE: lowering-opus fsjobs census x3 done" --body "-" >/dev/null
echo CENSUS2_DONE > c6n/census2.done
