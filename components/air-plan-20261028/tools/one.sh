#!/bin/bash
# one.sh LIB CASEID [env...]: run one UI corpus case through plan_corpus (fresh run dir), print row + stderr
D=$(dirname $0)/..; id=$1; lib=$2; shift 2
rm -rf $D/runs/one; mkdir -p $D/runs/one; echo $id > $D/runs/one/ids
env "$@" nice -n 10 python3 $D/tools/plan_corpus.py --run one --lib $lib --classes all --filter $D/runs/one/ids >/dev/null 2>&1
python3 -c "import json;[print(json.dumps({k:v for k,v in json.loads(l).items() if k in ('status','error','diffs')},indent=0)[:3000]) for l in open('$D/runs/one/results.jsonl')]"
