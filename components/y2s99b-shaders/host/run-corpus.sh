#!/bin/bash
# bash run-corpus.sh HARNESS OUTDIR : every library of ui-corpus-20261025 through the harness, 4 at a time, 600 s each
B=$NORD2_WORK/gpu/build
H=$1; O=$2; mkdir -p $O
ls $B/ui-corpus-20261025/libraries/*.metallib | xargs -P 4 -I{} sh -c 'f={}; b=$(basename $f .metallib); timeout 600 '$H' $f > '$O'/$b.out 2>&1; echo "exit $?" >> '$O'/$b.out'
echo CORPUS_DONE
