#include <assert.h>
#include <string.h>
static const uint8_t fake_ap[6]={2,0,0,0,0,2};
static const struct sw_peer peer={{2,0,0,0,0,1},{2,0,0,0,0,2}};
static void same(const uint8_t *a,const void *b,size_t n) {assert(memcmp(a,b,n)==0);}
static void guard(const uint8_t *b,size_t n) {size_t i;for(i=0;i<n;i++)assert(b[i]==0xa5);}
static void oracle_rlm(struct CMD_SET_BSS_RLM_PARAM *d,const struct sw_rlm *p) {
    memset(d,0,sizeof(*d));d->ucBssIndex=p->bss;d->ucRfBand=p->band;
    d->ucPrimaryChannel=p->channel;d->ucErpProtectMode=p->erp_protect;
    d->ucUseShortPreamble=p->short_preamble;d->ucUseShortSlotTime=p->short_slot;
    d->ucTxNss=p->tx_nss;d->ucRxNss=p->rx_nss;
}
static void oracle_txd(struct HW_MAC_CONNAC2X_TX_DESC *d,const struct sw_txd *p,size_t n,int sec,uint16_t fc) {
    memset(d,0,sizeof(*d));
    HAL_MAC_CONNAC2X_TXD_SET_TX_BYTE_COUNT(d,32+n);
    HAL_MAC_CONNAC2X_TXD_SET_ETHER_TYPE_OFFSET(d,(p->pse_header_len+(sec?12:24))>>1);
    HAL_MAC_CONNAC2X_TXD_SET_QUEUE_INDEX(d,p->queue);
    HAL_MAC_CONNAC2X_TXD_SET_WLAN_INDEX(d,p->wlan);
    HAL_MAC_CONNAC2X_TXD_SET_HEADER_FORMAT(d,sec?0:2);
    HAL_MAC_CONNAC2X_TXD_SET_LONG_FORMAT(d);
    if(sec) {HAL_MAC_CONNAC2X_TXD_SET_ETHERNET_II(d);if(sec==2)HAL_MAC_CONNAC2X_TXD_SET_VTA(d,1);}
    else {
        HAL_MAC_CONNAC2X_TXD_SET_VTA(d,1);
        HAL_MAC_CONNAC2X_TXD_SET_802_11_HEADER_LENGTH(d,12);
        HAL_MAC_CONNAC2X_TXD_SET_TYPE(d,(fc&12)>>2);
        HAL_MAC_CONNAC2X_TXD_SET_SUB_TYPE(d,(fc&0xf0)>>4);
        HAL_MAC_CONNAC2X_TXD7_SET_TYPE(d,(fc&12)>>2);
        HAL_MAC_CONNAC2X_TXD7_SET_SUB_TYPE(d,(fc&0xf0)>>4);
        HAL_MAC_CONNAC2X_TXD_SET_SPE_IDX(d,p->spe_index);
    }
    HAL_MAC_CONNAC2X_TXD_SET_HEADER_PADDING(d,0);
    HAL_MAC_CONNAC2X_TXD_SET_TID(d,0);
    HAL_MAC_CONNAC2X_TXD_SET_REMAINING_LIFE_TIME_IN_MS(d,p->lifetime_ms);
    HAL_MAC_CONNAC2X_TXD_SET_REMAINING_TX_COUNT(d,p->retry_limit);
    HAL_MAC_CONNAC2X_TXD_SET_FR_RATE(d,p->fixed_rate);
    HAL_MAC_CONNAC2X_TXD_SET_FIXED_RATE_ENABLE(d);
    HAL_MAC_CONNAC2X_TXD_SET_PKT_FORMAT(d,2);
    HAL_MAC_CONNAC2X_TXD_SET_OWN_MAC_INDEX(d,p->own_mac);
    HAL_MAC_CONNAC2X_TXD_SET_PID(d,p->pid);
    HAL_MAC_CONNAC2X_TXD_SET_TXS_TO_MCU(d);
    if(p->protected_frame)HAL_MAC_CONNAC2X_TXD_SET_PROTECTION(d);
}
static void commands(void) {
    uint8_t out[256],secret[16];size_t used,i;
    struct sw_key k={.bss=2,.wlan=17,.pairwise=1,.key_id=0,.key=secret,.key_len=16};
    struct CMD_802_11_KEY kd;
    struct sw_channel c={.bss=2,.token=23,.band=1,.channel=6,.dbdc_band=4,.interval_ms=2000};
    struct CMD_CH_PRIVILEGE cd;
    struct sw_bss bs={.rlm={2,1,6,1,1,1,1,1},.ssid=(const uint8_t *)"fixture",.ssid_len=7,
        .connected=1,.ap_sta=3,.bmc_wlan=18,.nonht_phy=1,.phy_set=3,.dbdc_band=4,.wmm_set=1,
        .rates=0x3fcf,.basic_rates=0xf,.fixed_rate=4,.private_data=0x12345678};
    struct CMD_SET_BSS_INFO bd;
    struct sw_sta st={.bss=2,.sta=3,.wlan=17,.bmc_wlan=255,.sta_type=65,.state=3,
        .need_response=1,.phy_set=3,.rcpi=80,.rts_policy=3,.aid=123,.listen_interval=10,
        .rates=0x3fcf,.basic_rates=0xf,.fixed_rate=4,.version=1};
    struct CMD_UPDATE_STA_RECORD sd;
    struct CMD_SET_BSS_RLM_PARAM rd;
    for(i=0;i<16;i++)secret[i]=(uint8_t)i; /* public fixture, never a credential */
    memcpy(k.ap,fake_ap,6);memcpy(bs.ap,fake_ap,6);memcpy(st.ap,fake_ap,6);
    for(k.pairwise=0;k.pairwise<=1;k.pairwise++) {
        k.key_id=k.pairwise?0:2;
        memset(&kd,0,sizeof(kd));kd.ucAddRemove=1;kd.ucTxKey=k.pairwise;kd.ucKeyType=k.pairwise;
        memcpy(kd.aucPeerAddr,k.ap,6);kd.ucBssIdx=k.bss;kd.ucAlgorithmId=4;kd.ucKeyId=k.key_id;
        kd.ucKeyLen=16;kd.ucWlanIndex=k.wlan;memcpy(kd.aucKeyMaterial,secret,16);
        assert(sw_key_add(out,sizeof(out),&used,&k)==0&&used==sizeof(kd));same(out,&kd,used);
        memset(&kd,0,sizeof(kd));kd.ucKeyType=k.pairwise;memcpy(kd.aucPeerAddr,k.ap,6);
        kd.ucBssIdx=k.bss;kd.ucKeyId=k.key_id;kd.ucWlanIndex=k.wlan;
        assert(sw_key_remove(out,sizeof(out),&used,&k)==0&&used==sizeof(kd));same(out,&kd,used);
    }
    for(c.abort=0;c.abort<=1;c.abort++) {
        memset(&cd,0,sizeof(cd));cd.ucBssIndex=c.bss;cd.ucTokenID=c.token;cd.ucAction=c.abort;
        cd.ucDBDCBand=c.dbdc_band;
        if(!c.abort) {cd.ucPrimaryChannel=c.channel;cd.ucRfBand=c.band;cd.u4MaxInterval=c.interval_ms;}
        assert(sw_channel(out,sizeof(out),&used,&c)==0&&used==sizeof(cd));same(out,&cd,used);
    }
    oracle_rlm(&rd,&bs.rlm);assert(sw_rlm(out,sizeof(out),&used,&bs.rlm)==0);same(out,&rd,sizeof(rd));
    memset(&bd,0,sizeof(bd));bd.ucBssIndex=bs.rlm.bss;bd.ucConnectionState=MEDIA_STATE_CONNECTED;bd.ucSSIDLen=7;
    memcpy(bd.aucSSID,bs.ssid,7);memcpy(bd.aucBSSID,bs.ap,6);
    bd.u2OperationalRateSet=bs.rates;bd.u2BSSBasicRateSet=bs.basic_rates;
    bd.ucStaRecIdxOfAP=bs.ap_sta;bd.u2HwDefaultFixedRateCode=bs.fixed_rate;
    bd.ucNonHTBasicPhyType=bs.nonht_phy;bd.ucAuthMode=AUTH_MODE_WPA2_PSK;bd.ucEncStatus=ENUM_ENCRYPTION3_ENABLED;
    bd.ucPhyTypeSet=bs.phy_set;bd.ucBMCWlanIndex=bs.bmc_wlan;bd.u4PrivateData=bs.private_data;
    bd.rBssRlmParam=rd;bd.ucDBDCBand=bs.dbdc_band;bd.ucWmmSet=bs.wmm_set;bd.ucNss=1;
    assert(sw_bss(out,sizeof(out),&used,&bs)==0&&used==sizeof(bd));same(out,&bd,used);
    bs.connected=0;bd.ucConnectionState=MEDIA_STATE_DISCONNECTED;assert(sw_bss(out,sizeof(out),&used,&bs)==0);same(out,&bd,used);
    memset(&sd,0,sizeof(sd));sd.ucStaIndex=st.sta;sd.ucStaType=BIT(STA_TYPE_LEGACY_INDEX)|BIT(STA_ROLE_AP_INDEX);
    memcpy(sd.aucMacAddr,st.ap,6);sd.u2AssocId=st.aid;sd.u2ListenInterval=st.listen_interval;
    sd.ucBssIndex=st.bss;sd.ucDesiredPhyTypeSet=st.phy_set;sd.u2DesiredNonHTRateSet=st.rates;
    sd.u2BSSBasicRateSet=st.basic_rates;sd.ucStaState=st.state;sd.ucRCPI=st.rcpi;
    sd.ucNeedResp=st.need_response;sd.ucWlanIndex=st.wlan;sd.ucBMCWlanIndex=st.bmc_wlan;
    sd.ucRtsPolicy=st.rts_policy;sd.u2HwDefaultFixedRateCode=st.fixed_rate;sd.ucVersion=st.version;
    assert(sw_sta(out,sizeof(out),&used,&st)==0&&used==sizeof(sd));same(out,&sd,used);
    st.version=0;sd.ucVersion=0;assert(sw_sta(out,sizeof(out),&used,&st)==0);same(out,&sd,used);
    /* Every insufficient capacity preserves output, including secret commands. */
    k.pairwise=1;k.key_id=0;c.abort=0;
#define CAPS(fn,input,n) for(i=0;i<(n);i++){memset(out,0xa5,sizeof(out));used=999;assert(fn(out,i,&used,input)==SW_CAPACITY);assert(!used);guard(out,sizeof(out));}
    CAPS(sw_key_add,&k,sizeof(kd));CAPS(sw_key_remove,&k,sizeof(kd));CAPS(sw_channel,&c,sizeof(cd));
    CAPS(sw_rlm,&bs.rlm,sizeof(rd));CAPS(sw_bss,&bs,sizeof(bd));CAPS(sw_sta,&st,sizeof(sd));
    memset(out,0xa5,sizeof(out));k.key_len=17;
    assert(sw_key_add(out,sizeof(out),&used,&k)==SW_INVALID&&!used);guard(out,sizeof(out));
    st.phy_set=16;assert(sw_sta(out,sizeof(out),&used,&st)==SW_INVALID&&!used);
    bs.ssid_len=33;assert(sw_bss(out,sizeof(out),&used,&bs)==SW_INVALID&&!used);
    bs.ssid_len=SIZE_MAX;assert(sw_bss(out,sizeof(out),&used,&bs)==SW_INVALID&&!used);
    c.channel=0;assert(sw_channel(out,sizeof(out),&used,&c)==SW_INVALID&&!used);
}
static void frames(void) {
    uint8_t out[256],auth[30],assoc[110],eth[64]={0},oracle_auth[32]={0};size_t used,alen,i;int j;
    const uint8_t rates[]={0x82,0x84,0x8b,0x96,12,18,24,36,48,72,96,108};
    struct sw_assoc a={peer,(const uint8_t *)"fixture",7,rates,sizeof(rates),0x431,10};
    struct sw_txd t={.wlan=17,.own_mac=2,.queue=32,.pid=3,.retry_limit=30,
                     .fixed_rate=4,.pse_header_len=0,.lifetime_ms=2000};
    struct HW_MAC_CONNAC2X_TX_DESC d;
    assert(sw_auth(auth,sizeof(auth),&used,&peer)==0&&used==30);
    authComposeAuthFrameHeaderAndFF(oracle_auth,(uint8_t *)peer.ap,(uint8_t *)peer.self,0,1,0);
    same(auth,oracle_auth,30);
    assert(auth[0]==0xb0&&auth[26]==1&&auth[24]==0&&auth[28]==0);
    same(auth+4,peer.ap,6);same(auth+10,peer.self,6);same(auth+16,peer.ap,6);
    assert(sw_assoc(assoc,sizeof(assoc),&alen,&a)==0&&alen==28+9+10+6+22);
    assert(assoc[24]==0x31&&assoc[25]==4&&assoc[26]==10);
    assert(assoc[28]==0&&assoc[29]==7&&assoc[37]==1&&assoc[38]==8);
    assert(assoc[47]==50&&assoc[48]==4&&assoc[53]==48&&assoc[54]==20);
    same(assoc+4,peer.ap,6);same(assoc+10,peer.self,6);
    CAPS(sw_auth,&peer,sizeof(auth));CAPS(sw_assoc,&a,alen);
    eth[12]=0x88;eth[13]=0x8e;eth[14]=2;eth[15]=3;eth[17]=sizeof(eth)-18;
    for(i=0;i<256;i++) {
        t.wlan=(uint8_t)(i%255);t.own_mac=(uint8_t)(i%64);t.queue=(uint8_t)(i%128);
        t.pid=(uint8_t)(1+i%127);t.retry_limit=(uint8_t)(1+i%31);
        t.fixed_rate=(uint16_t)((i*53)%0x4000);t.pse_header_len=(uint16_t)(2*(i%116));t.spe_index=(uint8_t)(i%32);
        t.lifetime_ms=(uint32_t)(66+i*200);
        oracle_txd(&d,&t,sizeof(auth),0,0xb0);
        assert(sw_management_txd(out,sizeof(out),&used,&t,auth,sizeof(auth))==0&&used==32);same(out,&d,32);
        oracle_txd(&d,&t,alen,0,0);
        assert(sw_management_txd(out,sizeof(out),&used,&t,assoc,alen)==0);same(out,&d,32);
        t.protected_frame=(uint8_t)(i%2);oracle_txd(&d,&t,sizeof(eth),1,0);
        assert(sw_security_txd(out,sizeof(out),&used,&t,eth,sizeof(eth))==0);same(out,&d,32);
        t.protected_frame=0;
        uint8_t deauth[26]={0xc0};deauth[24]=3;
        oracle_txd(&d,&t,sizeof(deauth),0,0xc0);
        assert(sw_management_txd(out,sizeof(out),&used,&t,deauth,sizeof(deauth))==0);same(out,&d,32);
        eth[12]=8;eth[13]=0;t.protected_frame=1;oracle_txd(&d,&t,sizeof(eth),2,0);
        assert(sw_data_txd(out,sizeof(out),&used,&t,eth,sizeof(eth))==0);same(out,&d,32);
        eth[12]=0x88;eth[13]=0x8e;t.protected_frame=0;
    }
    for(i=0;i<32;i++) {
        memset(out,0xa5,sizeof(out));assert(sw_management_txd(out,i,&used,&t,auth,sizeof(auth))==SW_CAPACITY&&!used);guard(out,sizeof(out));
        assert(sw_security_txd(out,i,&used,&t,eth,sizeof(eth))==SW_CAPACITY&&!used);guard(out,sizeof(out));
    }
    for(i=0;i<18;i++)assert(sw_security_txd(out,sizeof(out),&used,&t,eth,i)==SW_INVALID&&!used);
    for(i=0;i<30;i++)assert(sw_management_txd(out,sizeof(out),&used,&t,auth,i)==SW_INVALID&&!used);
    for(i=0;i<14;i++) {a.rates_len=i; j=sw_assoc(out,sizeof(out),&used,&a);assert((i>=1&&i<=12)?j==0:j==SW_INVALID);}
    {uint8_t bad[]={2,2};a.rates=bad;a.rates_len=2;assert(sw_assoc(out,sizeof(out),&used,&a)==SW_INVALID);
     bad[1]=0;assert(sw_assoc(out,sizeof(out),&used,&a)==SW_INVALID);}
    a.rates=rates;a.rates_len=12;a.ssid_len=SIZE_MAX;assert(sw_assoc(out,sizeof(out),&used,&a)==SW_INVALID);
    a.ssid_len=7;a.capability=0x4000;assert(sw_assoc(out,sizeof(out),&used,&a)==SW_INVALID);
    t.pid=0;assert(sw_security_txd(out,sizeof(out),&used,&t,eth,sizeof(eth))==SW_INVALID);
    t.pid=1;t.lifetime_ms=1;assert(sw_security_txd(out,sizeof(out),&used,&t,eth,sizeof(eth))==SW_INVALID);
    assert(sw_auth(NULL,30,&used,&peer)==SW_INVALID);
    assert(sw_auth(out,sizeof(out),NULL,&peer)==SW_INVALID);
}
int main(void) {assert(RTS_POLICY_LEGACY==3);assert(ENUM_BAND_AUTO==4);assert(PHY_TYPE_OFDM_INDEX==3);assert(RATE_6M_SW_INDEX==6);assert(RATE_54M_SW_INDEX==13);commands();frames();puts("PASS original structs + 1280 actual-macro TXD comparisons; bounded hostile inputs");return 0;}
