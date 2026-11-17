from pathlib import Path
import hashlib,json
p=Path(__file__).resolve().parent;n=p.parent;c=n.parent
names=lambda s:s
sources={'harness-core.c':c/'completion-test.c','harness-adapter.c':n/'v2/test-native-build3.c','harness-pmic.c':n/'v2/pmic-host.c'}
m={}
for out,src in sources.items():
 s=src.read_text().replace('"completion.h"','"wifi_persistent.h"').replace('"../adapter.h"','"wifi_persistent_adapter.h"');s=names(s);(p/out).write_text(s)
 m[out]={'source':str(src),'input_sha256':hashlib.sha256(src.read_bytes()).hexdigest(),'output_sha256':hashlib.sha256(s.encode()).hexdigest(),'changes':'include names only; all symbols unchanged'}
(p/'test-mapping.json').write_text(json.dumps(m,indent=2)+'\n')
