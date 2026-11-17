#!/usr/bin/env python3
"""Compile actual pinned Android declarations/macros, independently of codec.
No Android sources are copied into the candidate. Generated files stay here.
"""
import hashlib, json, pathlib, re, subprocess
HERE=pathlib.Path(__file__).resolve().parent
ROOT=HERE.parents[4]
G=ROOT.parent/'nord2-current-kernel/drivers/misc/mediatek/connectivity/wlan/core/gen4m'
PIN='04d00c2565764c3922c9b4826b5acff1106e533f'

def decl(path, name,kind='struct'):
    s=re.sub(r'/\*.*?\*/|//[^\n]*','', (G/path).read_text(),flags=re.S)
    m=re.search(kind+' '+name+r'\s*\{',s)
    assert m, name
    depth=1; i=m.end()
    while depth:
        depth+=(s[i]=='{')-(s[i]=='}'); i+=1
    return s[m.start():s.index(';',i)+1]

def function(path,name):
    s=(G/path).read_text();m=re.search(r'\n'+name+r'\(',s);assert m
    begin=m.start()+1;opening=s.index('{',m.end());depth=1;i=opening+1
    while depth:
        depth+=(s[i]=='{')-(s[i]=='}');i+=1
    return 'static void '+s[begin:i]

STRUCTS={
'CMD_802_11_KEY':'ucAddRemove ucTxKey ucKeyType ucIsAuthenticator aucPeerAddr ucBssIdx ucAlgorithmId ucKeyId ucKeyLen ucWlanIndex ucMgmtProtection aucKeyMaterial aucKeyRsc',
'CMD_SET_BSS_RLM_PARAM':'ucBssIndex ucRfBand ucPrimaryChannel ucErpProtectMode ucUseShortPreamble ucUseShortSlotTime ucTxNss ucRxNss',
'CMD_SET_BSS_INFO':'ucBssIndex ucConnectionState ucCurrentOPMode ucSSIDLen aucSSID aucBSSID ucIsQBSS ucVersion u2OperationalRateSet u2BSSBasicRateSet ucStaRecIdxOfAP u2HwDefaultFixedRateCode ucNonHTBasicPhyType ucAuthMode ucEncStatus ucPhyTypeSet ucBMCWlanIndex ucDisconnectDetectThreshold u4PrivateData rBssRlmParam ucDBDCBand ucWmmSet ucNss',
'CMD_UPDATE_STA_RECORD':'ucStaIndex ucStaType aucMacAddr u2AssocId u2ListenInterval ucBssIndex ucDesiredPhyTypeSet u2DesiredNonHTRateSet u2BSSBasicRateSet ucStaState ucVersion ucRCPI ucNeedResp ucWlanIndex ucBMCWlanIndex ucRtsPolicy u2HwDefaultFixedRateCode',
'CMD_CH_PRIVILEGE':'ucBssIndex ucTokenID ucAction ucPrimaryChannel ucRfSco ucRfBand ucRfChannelWidth ucReqType ucDBDCBand u4MaxInterval',
}
def main():
    assert subprocess.check_output(['git','-C',str(ROOT.parent/'nord2-current-kernel'),'rev-parse','HEAD'],text=True).strip()==PIN
    manifest=HERE/'manifest.json'
    if manifest.exists():
        for name,digest in json.loads(manifest.read_text())['original_source_sha256'].items():
            assert hashlib.sha256((G/name).read_bytes()).hexdigest()==digest, name+' source drift'
    pre='#include <stdint.h>\n#include <stddef.h>\n#include <stdio.h>\n#define MAC_ADDR_LEN 6\n#define HE_OP_BYTE_NUM 3\n#define HE_MAC_CAP_BYTE_NUM 6\n#define HE_PHY_CAP_BYTE_NUM 11\n#define CFG_SUPPORT_802_11AX 1\n#define CFG_SUPPORT_802_11BE 0\n'
    st=['CMD_802_11_KEY','CMD_SET_BSS_RLM_PARAM','CMD_SET_BSS_INFO','CMD_HTVHT_BA_SIZE','CMD_HE_BA_SIZE','CMD_EHT_BA_SIZE','CMD_UPDATE_STA_RECORD']
    source=pre+'\n'.join(decl(pathlib.Path('include/wsys_cmd_handler_fw.h'),x) for x in st)+'\n'+decl(pathlib.Path('include/nic_cmd_event.h'),'CMD_CH_PRIVILEGE')
    source+='\nint main(void) {\n'
    for s, fields in STRUCTS.items():
        source+=f'printf("#define SW_SIZE_{s} %zu\\n",sizeof(struct {s}));\n'
        for f in fields.split(): source+=f'printf("#define SW_OFF_{s}_{f} %zu\\n",offsetof(struct {s},{f}));\n'
    source+='return 0;}\n'
    build=HERE/'_build'; build.mkdir(exist_ok=True)
    (build/'layout.c').write_text(source)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(build/'layout.c'),'-o',str(build/'layout')],check=True)
    generated=subprocess.check_output([str(build/'layout')],text=True)
    path=HERE/'wifi_station_source_layout.h'
    if path.exists(): assert path.read_text()==generated, 'Original-source ABI drift'
    else: path.write_text(generated)
    # Compile a second harness against original structures AND actual header macros.
    tx=(G/'include/nic/nic_connac2x_tx.h').read_text()
    # Entire macro/header body has only one precomp include; no replacement macros.
    tx=re.sub(r'^\s*#include[^\n]*','',tx,flags=re.M)
    harness=pre+'\n#define CFG_SUPPORT_CONNAC2X 1\n#define BIT(n) (UINT32_C(1)<<(n))\n#define BITS(a,b) ((UINT32_MAX>>(31-(b))) & (UINT32_MAX<<(a)))\n#define TX_DESC_SET_FIELD(d,v,m,s) ((d)=((d)&~(m))|(((uint32_t)(v)<<(s))&(m)))\n#define MSEC_TO_USEC(x) ((x)*1000)\n#define USEC_PER_TU 1024\n#define TX_DESC_TX_TIME_NO_LIMIT 0\n'+tx+'\n'
    # Reuse actual declarations, not candidate structs, in oracle function bodies.
    harness+=source[:source.index('int main(void)')].replace(pre,'')
    for name in ['ENUM_PARAM_AUTH_MODE','ENUM_WEP_STATUS','ENUM_PARAM_MEDIA_STATE']:
        harness+=decl(pathlib.Path('include/wsys_cmd_handler_fw.h'),name,'enum')+'\n'
    harness+=decl(pathlib.Path('include/nic_cmd_event.h'),'ENUM_RTS_POLICY','enum')+'\n'
    harness+='#define STA_ROLE_BASE_INDEX 4\n'
    for name in ['ENUM_STA_TYPE_INDEX','ENUM_STA_ROLE_INDEX','ENUM_PHY_TYPE_INDEX','ENUM_SW_RATE_INDEX','ENUM_DBDC_BN']:
        harness+=decl(pathlib.Path('include/nic/wlan_def.h'),name,'enum')+'\n'
    for name,path,size in [('EVENT_TX_DONE','include/nic_cmd_event.h',88),('CMD_RX_PACKET_FILTER','include/nic_cmd_event.h',68),('EVENT_CH_PRIVILEGE','include/wsys_cmd_handler_fw.h',24),('EVENT_ACTIVATE_STA_REC','include/wsys_cmd_handler_fw.h',8),('EVENT_ADD_KEY_DONE_INFO','include/wsys_cmd_handler_fw.h',8),('EVENT_BSS_BEACON_TIMEOUT','include/wsys_cmd_handler_fw.h',4),('CMD_REMOVE_STA_RECORD','include/wsys_cmd_handler_fw.h',4),('CMD_INDICATE_PM_BSS_CONNECTED','include/wsys_cmd_handler_fw.h',12),('CMD_INDICATE_PM_BSS_ABORT','include/wsys_cmd_handler_fw.h',4)]:
        harness+=decl(pathlib.Path(path),name)+f'\n_Static_assert(sizeof(struct {name})=={size},"{name}");\n'
    harness+='\n#include <string.h>\n#include <assert.h>\n#define IN\n#define __KAL_ATTRIB_PACKED__ __attribute__((packed))\n#define COPY_MAC_ADDR(d,s) memcpy(d,s,6)\n#define ASSERT(x) assert(x)\n'
    mac=(G/'include/nic/mac.h').read_text()
    for name in ['MAC_FRAME_TYPE_MGT','MAC_FRAME_AUTH','AUTH_ALGORITHM_NUM_SHARED_KEY','AUTH_TRANSACTION_SEQ_1','AUTH_TRANSACTION_SEQ_2','AUTH_TRANSACTION_SEQ_3','AUTH_TRANSACTION_SEQ_4','MASK_FC_PROTECTED_FRAME']:
        match=re.search(r'^#define\s+'+name+r'\s+[^\n]*',mac,re.M);assert match,name
        harness+=match.group(0)+'\n'
    harness+=decl(pathlib.Path('include/nic/mac.h'),'WLAN_AUTH_FRAME')+'\n'
    harness+=function(pathlib.Path('mgmt/auth.c'),'authComposeAuthFrameHeaderAndFF')+'\n'
    harness+='\n#include "wifi_station_wire.h"\n#include "test_oracle.inc"\n'
    (build/'oracle.c').write_text(harness)
    subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(HERE),str(build/'oracle.c'),str(HERE/'wifi_station_wire.c'),'-o',str(build/'tests')],check=True)
    subprocess.run([str(build/'tests')],check=True)
    print('PASS: pinned source layout, actual TXD macros, command payload oracle, bounds')
if __name__=='__main__': main()
