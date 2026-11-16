B=$NORD2_WORK/gpu/build
cd $B/y2s99b-shaders/host
for f in $(grep -L '^exit' corpus-new1/*.out); do b=$(basename $f .out); timeout 600 ./h-run1/metallib_compile_host_test $B/ui-corpus-20261025/libraries/$b.metallib > $f 2>&1; echo "exit $?" >> $f; done
echo RERUN_DONE
