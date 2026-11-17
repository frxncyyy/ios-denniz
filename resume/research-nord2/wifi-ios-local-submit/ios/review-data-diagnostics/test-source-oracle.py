#!/usr/bin/env python3
"""Compile original selected source struct in temporary storage; retain no source copy."""
from pathlib import Path
import hashlib,json,re,subprocess,tempfile
P=Path(__file__).resolve().parent;R=next(x for x in P.parents if (x/'AGENTS.md').is_file());A=R.parent/'nord2-current-kernel';H=A/'drivers/misc/mediatek/connectivity/wlan/core/gen4m/include/nic_cmd_event.h'
commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=A,text=True).strip();assert commit=='04d00c2565764c3922c9b4826b5acff1106e533f'
src=H.read_text();body=re.search(r'struct EVENT_TX_DONE \{.*?\n\};',src,re.S).group(0);enum=re.search(r'enum ENUM_TXS_CONTROL_FLAG \{.*?\n\};',src,re.S).group(0)
fields={'ucPacketSeq':0,'ucStatus':1,'u2SequenceNumber':2,'ucWlanIndex':4,'ucTxCount':5,'u2TxRate':6,'ucFlag':8,'ucTid':9,'ucFlushReason':14,'aucRawTxS':28,'aucReserved1':56}
code='#include <stdint.h>\n#include <stddef.h>\n'+body+'\n'+enum+'\n_Static_assert(sizeof(struct EVENT_TX_DONE)==88,"extent");\n'
for k,v in fields.items():code+=f'_Static_assert(offsetof(struct EVENT_TX_DONE,{k})=={v},"{k}");\n'
code+='_Static_assert(TXS_WITH_ADVANCED_INFO==0,"advanced flag bit");\nint main(void){return 0;}\n'
with tempfile.TemporaryDirectory() as d:
 f=Path(d)/'oracle.c';f.write_text(code);subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror',str(f),'-o',d+'/oracle'],check=True);subprocess.run([d+'/oracle'],check=True)
out={'passed':True,'android_commit':commit,'source_path':str(H.relative_to(A)),'source_sha256':hashlib.sha256(H.read_bytes()).hexdigest(),'checks':14,'extent':88,'fields':fields,'advanced_bit':0,'scope':'Original EVENT_TX_DONE layout only; no RF/status availability proof. Source extracted in temporary storage only.'}
(P/'source-oracle.json').write_text(json.dumps(out,indent=2)+'\n');print('PASS 14 original-source ABI extent/offset/flag checks')
