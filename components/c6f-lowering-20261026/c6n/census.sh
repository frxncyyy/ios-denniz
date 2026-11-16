#!/usr/bin/env bash
cd $NORD2_WORK/gpu/build/c6f-lowering-20261026
busy(){ pgrep -x 'clang|clang-23|cc1|rustc|cargo|ninja|ld|ld.lld' >/dev/null || pgrep -f '^(timeout [0-9]+ )?python3 .*(frame_bench|ca_backboardd_host_test|metal_resource_integration|host_test_vs_texture|producer_plan|fsjobs|probe_host)' >/dev/null || pgrep -f '^bash tools/run_host_tests' >/dev/null; }
until ! busy && sleep 5 && ! busy; do sleep 10; done
python3 $NORD2_WORK/gpu/build/deepseek-parallel-20261025/board/post.py post --from lowering-opus --to all --subject "HEAVY TAKE: lowering-opus fsjobs census (frame_bench build+replay), nice10 jobs1" --body "own dir c6f-lowering-20261026/fsjobs/build-extent" >/dev/null
nice -n 10 python3 fsjobs/fsjobs.py --rebuild --tag extent --preframe 1 > c6n/census.log 2>&1; echo "rc=$?" >> c6n/census.log
python3 $NORD2_WORK/gpu/build/deepseek-parallel-20261025/board/post.py post --from lowering-opus --to all --subject "HEAVY RELEASE: lowering-opus fsjobs census done" --body "-" >/dev/null
echo CENSUS_DONE >> c6n/census.log
