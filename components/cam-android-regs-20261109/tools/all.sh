#!/bin/bash
# all.sh <name>: full camera register snapshot into out-<name>/
cd "$(dirname "$0")"; n=$1; rm -rf out; mkdir -p out
for f in g1 g2 g4 s4 s5 s6 s7 s8 s9 sa sb ana ana2; do python3 n2d.py $f.txt $f >/dev/null; done
python3 n2d.py g3.txt g3 >/dev/null
mkdir -p out-$n; for f in out/*.log; do python3 sum.py $f > out-$n/$(basename $f .log).txt; cp $f out-$n/; done
