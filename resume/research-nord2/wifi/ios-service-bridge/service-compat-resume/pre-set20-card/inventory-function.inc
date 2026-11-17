/* Compiled native module implements all six operations for this checked
 * WPA2-PSK/CCMP profile. Bits describe installed handlers, not reached keys,
 * current carrier, all RSN authentication modes or a hardware-test result. */
int nord2_wifi_join_card_inventory(struct niwc_manifest*out){
 if(!out)return -1;
 if(!nss_available())return 1;
 IOLockLock(nss_native.lock);
 if(nss_native.failed||!njs_native.notice_lock){IOLockUnlock(nss_native.lock);return 1;}
 const struct niwc_manifest installed={2,{
  {NIWC_CCMP,NIWC_END_TO_END},{NIWC_RSN_IE,NIWC_END_TO_END}}};
 *out=installed;IOLockUnlock(nss_native.lock);return 0;
}
