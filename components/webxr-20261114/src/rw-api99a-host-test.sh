#!/bin/bash
# y2s99a: rw_api99a_pure.h on the build machine.  Nothing is installed, the phone is not touched, the mirror is only read.
#   bash rw-api99a-host-test.sh [PATHS]   ASan/UBSan -Werror test ('rw_api99a: ok ...'); PATHS = a list of the mirror's
#                                         *.pipelinelib/* files (y2s99a-api-gaps/audit/plib-paths.txt) checked too;
#                                         then 10 hand mutants of the header, each of which must make the test fail
#                                         (UBSan only, so the mutants run under 'ulimit -v').
# Run it under the RAM rules: flock $HOME/nord2-heavy.lock bash rw-api99a-host-test.sh ...  (RSS a few MB).
set -euo pipefail
D=$(cd "$(dirname "$0")" && pwd)
S=$(mktemp -d); trap 'rm -rf "$S"' EXIT
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -o "$S/t" "$D/rw_api99a_host_test.c"
"$S/t" "$@"
mut() {  # mut NAME OLD NEW
  mkdir -p "$S/m"; cp "$D/rw_api99a_host_test.c" "$S/m/"
  python3 -I - "$D/rw_api99a_pure.h" "$S/m/rw_api99a_pure.h" "$2" "$3" <<'PY'
import sys
s = open(sys.argv[1]).read(); assert s.count(sys.argv[3]) >= 1, sys.argv[3]
open(sys.argv[2], 'w').write(s.replace(sys.argv[3], sys.argv[4], 1))
PY
  cc -std=c11 -O1 -w -fsanitize=undefined -o "$S/m/t" "$S/m/rw_api99a_host_test.c"
  if (ulimit -v 1500000; "$S/m/t" "$@" > /dev/null 2>&1); then echo "MUTANT SURVIVED: $1"; exit 1; fi
  caught=$((caught+1))
}
caught=0
mut dance-release   'ops->release(ctx, self);'                      ';'
mut dance-delete    'ops->delete_cache(ctx, cache);'                ';'
mut subrect-equal   '(want_w == surf_w && want_h == surf_h)'        '0'
mut subrect-rows    'if ((uint64_t)want_w * bpp > row_bytes) {'     'if (0) {'
mut faces           'return (type == 5 || type == 6) ? 6u : 1u;'    'return (type == 3 || type == 4) ? 6u : 1u;'
mut cache-desc      '&& strcmp(slash + 1, "pipelines.desc") != 0'   ''
mut excluded-vk     '"/VectorKit.framework/" }'                     '"/NoSuch.framework/" }'
mut event-ge        'if (get(ctx) >= value) {'                      'if (get(ctx) > value) {'
mut fam-dedupe      'if (s->key[i] == key) {'                       'if (0) {'
mut pow2            'return n != 0 && (n & (n - 1)) == 0;'          'return n != 0;'
echo "rw_api99a mutants: $caught of 10 caught"
