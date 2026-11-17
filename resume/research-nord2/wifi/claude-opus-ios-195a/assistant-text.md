Inizio leggendo AGENTS.md, l'inizio di NOTES.md e l'estratto delle prove.

Sto ancora leggendo il materiale: la richiesta iOS viene rifiutata dal campo flags a offset 4872. Ora cerco i file di analisi del dizionario di scansione.

Ho trovato che il bit 0x20 viene impostato dal builder quando la chiave `SCAN_LOW_LATENCY` vale true. Ora verifico il puntatore kCFBooleanTrue e cerco chi imposta quella chiave.

Il bit 0x20 è `SCAN_LOW_LATENCY` (builder IO80211 a 0x1d3ebb998–9bc). Ora cerco nel codice di wifid chi imposta quella chiave e con quale semantica.

Sto leggendo il log hardware attorno all'apertura dell'event pipe e alle richieste di scansione, per ricostruire la sequenza e i tempi.

Il log mostra che wifid arma l'event pipe solo tra t≈319 s e t≈490 s dopo il boot: è un indizio forte sul ritardo. Verifico cosa accade sul servizio Wi-Fi in quell'intervallo.

The Topaz reference has patches named "sweetbusy" and "iokit-quiesce": they match the IOKit-quiet hypothesis for the delay. Let me read them.

Sto verificando nel sorgente XNU che il kernel attuale mantenga l'hold "busy" sul registry root (causa probabile dell'attesa di ~300 s).

Ora torno al flag 0x20: cerco in wifid il produttore della chiave `SCAN_LOW_LATENCY`.

Point important : wifid arme un timer de 30 s (0x6FC23AC00 ns, à 0x100047db4–dd4) avant d'appeler ScanAsync. Je vérifie maintenant comment le framework convertit notre code de retour.

Je consulte la référence Redwood (oosexgang) pour la structure de scan à 5456 octets et le champ à 4872.

Redwood confirme flags u64 à 0x1308=4872. Je regarde comment Redwood consomme la requête de scan (admet-il les flags ?).

Sto verificando l'origine dei flag di canale della richiesta (audit channel-mode), per evitare un secondo rifiuto a valle del flag 0x20.

wifid contiene la stringa « Setting low latency to true for interactive client %@ ». Leggo le stringhe adiacenti per il contesto.

Verifico da dove CoreWiFi ricava `IEEE80211NetworkInterfacesNames`, per collegare (o no) il ritardo a configd/InterfaceNamer.

Ho abbastanza materiale sull'admission. Prima di scrivere la patch del client verifico cosa offre lo stub host `test-xnu.h` (tempo monotono, IOReturn).

Verifico quali test esistenti coprono `niwa_snapshot_prepare`, `nsb_add` e `niwb_bss_metadata`, perché la correzione cambia la semantica tutto-o-niente dello snapshot.

Ho tutti gli elementi per la patch. Controllo le firme di `wifi_ios_age.h` e `wifi_ios_cursor.h` usate nel nuovo test dello snapshot.

Verifico se i test client esistenti si aspettano il vecchio comportamento tutto-o-niente (es. una riga senza RSSI che fa fallire la cache).

Controllo se `test-lifecycle.cpp` verifica il codice di ritorno "busy" della scansione, che la patch cambia in EBUSY (16).

Verifico `wifi_ios_rates.h` (dichiarazione di `niwr_production_map`) per il nuovo test.

# Diagnosi Wi-Fi iOS su Nord2, immagine 195a7e17: perché Impostazioni non mostra reti e perché la pagina si apre dopo minuti

## Sintesi

I due sintomi hanno cause diverse.

**Reti assenti.** Ognuna delle 26 richieste reali SET10 viene rifiutata in un solo punto: il controllo `u32(p+4872)` in `shim/nord2-xnu/wifi_ios_policy.c:35`. Il valore loggato `flags_lo=32` è il bit 0x20, cioè `SCAN_LOW_LATENCY`. È un'indicazione di priorità e latenza rivolta al driver, non un filtro sui risultati. Tutti gli altri campi scalari del profilo passano già la policy attuale. Il ciclo sui canali non viene mai raggiunto, quindi l'ammissione dei canali non è mai stata provata su hardware.

Ammettere 0x20 da solo però non basta. Subito dopo, lo snapshot dei risultati è "tutto o niente": basta una beacon con caratteristiche comuni per far fallire l'intera scansione, e Impostazioni resterebbe di nuovo vuota. Le caratteristiche in questione sono tre: un selettore BSS membership tra i rate, una ricezione da canale adiacente, oppure IE più lunghi di 512 B.

**Pagina lenta.** Il ritardo di minuti non dipende dalla scansione. Durante tutto il periodo in cui la pagina non si apriva, il driver non ha ricevuto nessuna SET10. wifid apre i client a t≈25 s e poi resta muto fino a un momento compreso tra 319 s e 490 s, quando arma l'event pipe. L'ipotesi più forte è che il registry IOKit non diventi mai "quiet". Il riferimento Topaz, sulla stessa base, ha risolto lo stesso sintomo con la patch 0128.

Non ho compilato né eseguito nulla: avevo solo Read/Grep/Glob. Patch e test qui sotto vanno integrati e lanciati da Codex.

---

## 1. Il profilo di scansione realmente emesso

### Dati certi

Fonti: `scan-request-evidence.txt:2–3` e `hardware-snapshot.log:5398–6087`. Le 26 richieste sono identiche:

| Offset | Campo | Valore | Origine nel builder (IO80211, stesso IPSW) |
|---|---|---|---|
| 0 | version | 1 | `1d3ebb594–598` |
| 4 | bss_type | 3 | default `1d3ebbbc8` |
| 8–13 | BSSID | zero (`peer=0`) | scritto solo se c'è la chiave BSSID, `1d3ebb868–8b0` |
| 16 | ssid_len | 0 | nessuna SSID/short-SSID |
| 52 | scan_type | 1 | default `1d3ebbb90` |
| 56 | phy_mode | 1 | default `1d3ebbbfc` |
| 60 / 64 | dwell / rest | 0 / 0 | scritti solo se le chiavi esistono, `1d3ebbc04–c58` |
| 68 | channels | 13 | `1d3ebbf48` |
| 4872 (u64) | SCAN_FLAGS | 0x20 (alto 0) | `1d3ebb93c–baec` |
| 4880 | RSSI threshold | INT32_MIN | scrittura incondizionata, `1d3ebbaf8–afc` |
| 4884 | names | 0 | |
| 5448 | short_ssid | 0 | |

Dwell e rest a zero significano quindi "chiave assente, default del driver". La soglia INT32_MIN significa "nessuna soglia".

### Canali

Il numero 13 è un dato. Quali 13 canali siano non è loggato. L'owner ha 22 canali: 1–13 a 2,4 GHz e 36–48 e 149–165 a 5 GHz (`SCANRFCHANNEL`, log 1233–1256).

L'ipotesi più semplice è che siano i canali 1–13 con flag 0x0a. Il motivo: `niws_channels` pubblica 2|8 (`wifi_ios_scalar.c:37`), e wifid ricopia `CHANNEL_FLAGS` tramite CWFChannel senza ricodificarlo (`channel-mode-audit/selector-resolution/README.md`). Resta un'inferenza. La patch aggiunge al log i valori min/max dei canali e l'OR dei flag, così la conferma arriva dallo stesso test.

### Significato del bit 0x20

**Produttore.**
- Il builder legge `CFSTR 'SCAN_LOW_LATENCY'` a `1d3ebb99c` e confronta il risultato con il booleano in x19, caricato a `1d3ebb964–96c` dallo slot GOT `0x1daca9b68`. Solo se sono uguali esegue `orr x8,x8,#0x20` sul u64 a 4872 (`1d3ebb9b4–9bc`).
- Lo stesso schema produce LOW_PRIORITY 0x2, PRIO_OVERRIDE 0x40, IS_THROTTLED 0x80 e INC_OFFCHANNEL_BSS 0x8. I bit 0x100–0x800 dipendono da una capability (`1d3ebba38–4c`). I nomi CFSTR sono annotati in `scan-and-initialization/conversion-constants.txt:260–303`.
- Il binario wifid (`research/nord2/wifi/ios-abi/wifid`) contiene sia le chiavi sia queste stringhe di log:
  - `"%s: Setting low latency to true for interactive client %@"`
  - `"Normal priority, low-latency scan request from \"%@\""`
  - `"Low priority, non-low-latency scan request from \"%@\""`
  - `"Low-latency scan request: shouldBlockScans=%d, no throttling"`

  Che a impostare 0x20 sia il client interattivo (Impostazioni) è quindi provato dal testo delle stringhe, non dal flusso delle istruzioni.
- Limite: non ho risolto in questa sessione l'identità dello slot GOT come `kCFBooleanTrue`. La deduco dallo schema, ripetuto identico per ogni chiave booleana.

**Consumatore.**
- `Apple80211ScanAsync` copia la maschera in ctx+120 (`1d3ebe948–94c`).
- In `IO80211-disassembly.txt` le uniche letture di ctx+120/121 sono nel collector dei risultati: `1d3ede568` (bit 0x200) e `1d3ede690` (bit 0x800). La ricerca è un grep sulle load con quell'offset, quindi un accesso con calcolo diverso dell'indirizzo potrebbe sfuggire.
- Conclusione: nel framework 0x20 non ha alcun consumatore lato risultati. È diretto solo al driver.
- Redwood fa lo stesso: logga `flags` e li ignora (`redwood_apple80211.c:493–495`).

**Conclusione.** 0x20 chiede di non rimandare o throttlare la scansione. Il nostro owner è dedicato, passivo, senza associazione né traffico, e serve le richieste in FIFO partendo subito. Rispetta quindi la richiesta per costruzione. Non è un filtro che stiamo ignorando.

### Durata e cache

- **wifid.** Prima di `Apple80211ScanAsync` (`0x100047de8`) arma un timer: `dispatch_time(0, 0x6FC23AC00 = 30 s)` e `dispatch_source_set_timer` (`0x100047dac–dd4`, import verificati in `wifid-imports.json`). Il wrapper `0x10014c668` ritenta fino a 6 volte, con `usleep(500000)`, solo sui codici 16/61/-3905 (`0x10014c69c–6d8`). Qualsiasi altro codice diverso da zero fallisce subito verso chi ha chiamato.
- **Framework.** Dopo l'event 10 chiama GET11 in ciclo fino a errno `0xe0820446` (oppure 5), a `1d3ede548–65c`. Su EBUSY ritenta con un timer, fino a 4 volte (`1d3ede5c0–630`).
- **Nostro client.** Lo snapshot è immutabile per ogni generazione e viene scartato alla SET10 successiva (`wifi_ios_client.cpp:239`).
- **Backend.** Ha un timeout di servizio di 30 s (`wifi_scan_service_bind.inc:63`), uguale al watchdog di wifid. La scansione di boot su 22 canali usa circa 41 unità per canale, ben sotto il limite.

---

## 2. La correzione proposta

### a. Ammissione

- Accettare SCAN_FLAGS uguale a 0, a 0x2 (LOW_PRIORITY) oppure a 0x20 (LOW_LATENCY), sempre con la parte alta a zero.
- 0x2 rientra nella stessa classe di "solo scheduling": su un owner inattivo una priorità più bassa non ha nulla a cui cedere il passo.
- Restano rifiutati: la combinazione 0x22, INC_OFFCHANNEL_BSS (cambia l'insieme dei risultati), PRIO_OVERRIDE e IS_THROTTLED (semantica non ricostruita), le opzioni 6 GHz e qualsiasi altro bit.
- I rifiuti ora hanno codici distinti, e l'ADMISSION logga l'OR dei flag di canale e il min/max dei canali. Se il prossimo blocco fosse sui canali, si legge direttamente dal log, senza un'altra build.

### b. EBUSY vero

Con una scansione pendente, o con la coda piena, il client restituisce il valore grezzo 16 invece di `kIOReturnBusy` (`client.cpp:236,242`).
- È coerente con il retry di wifid descritto sopra e con Redwood (`set_scan`: «EBUSY … the framework tries again»).
- Limite: il pass-through del valore grezzo in errno per RawSet non è provato, lo è solo per RawGet. Nel caso peggiore il comportamento resta quello di oggi, un errore immediato.

### c. Snapshot non più tutto-o-niente

Oggi una sola riga non convertibile annulla l'intera scansione: in quel caso si invia l'event 10 con status 5 e le righe sono zero (`wifi_ios_snapshot.c:23–28`). Ci sono quattro trigger realistici:

| Trigger | Dove nel codice | Correzione proposta |
|---|---|---|
| Selettori BSS membership tra i rate (HT 0xFF, SAE-H2E 0xFB, …) | Rifiutati come rate sconosciuti (`wifi_ios_mapping.c:15–17`, `wifi_ios_rates.c:5–8`) | Non sono rate: si saltano. Gli IE originali restano intatti nel risultato. |
| Beacon ricevuta da un canale adiacente | `channel_mismatch` (`wifi_scan_bss.c:48`); `nsb_add` sostituisce sempre la copia precedente (`:82–85`) | Una ricezione "off-channel" non sostituisce più una copia on-channel. |
| IE totali > 512 B | `ies_truncated` (`wifi_scan_bss.h:5`) | `NSB_IE_BYTES` diventa 1024, cioè lo spazio IE del risultato iOS da 1164 B. |
| Riga senza RSSI osservato | `wifi_ios_snapshot.c:5` | Riga omessa e contata (vedi sotto). |

Le righe che restano non rappresentabili vengono omesse, contate in `omitted` e classificate in `omit_mask`. Non si inventa nessun valore. Partial e timeout non diventano mai una cache pronta. Il caso INC_OFFCHANNEL_BSS è coerente: il chiamante non ha chiesto le BSS off-channel, quindi è corretto non riportarle.

Costo memoria: circa 8 KiB in più per ogni copia di `nsb_scan` e circa 512 B di stack in più nel worker.

### d. Rami successivi: rischio esplicito

La continuazione `nss_execute` (`wifi_scan_service_bind.inc:54–76`) è coperta da test host ma non è mai stata eseguita su hardware. Un esito FATAL porta al `panic("…manual reset required")` di `:94`. Il test hardware va preparato con il recovery già pronto.

### Patch

```diff
--- a/shim/nord2-xnu/wifi_ios_policy.h
+++ b/shim/nord2-xnu/wifi_ios_policy.h
@@
 #include "wifi_scan_service.h"
 #include "wifi_ios_mapping.h"
+/* niwi_scan_admit: 0 admits; distinct negative refusals are numeric
+ * diagnostics only (no request bytes are reported). */
+enum {NIWI_ADMIT_ARGUMENT=-1,NIWI_ADMIT_PROFILE=-2,NIWI_ADMIT_OWNER=-3,NIWI_ADMIT_OPTION=-4,
+ NIWI_ADMIT_FLAGS=-5,NIWI_ADMIT_PEER=-6,NIWI_ADMIT_CHANNEL=-7,NIWI_ADMIT_CHANNEL_FLAGS=-8,NIWI_ADMIT_ALLOWLIST=-9};
+/* SCAN_FLAGS scheduling hints, same-IPSW builder 0x1d3ebb970..9bc. */
+#define NIWI_SCAN_LOW_PRIORITY 0x2u
+#define NIWI_SCAN_LOW_LATENCY 0x20u
 int niwi_set_shape(unsigned,const void*,size_t,const char[16],uint32_t*);
```

`wifi_ios_policy.c`, funzione completa (sostituisce le righe 17–61):

```c
/* Passive discovery only. Raw1/2 nondirected/default-timing requests are served by our explicit passive
 * discovery policy; source raw2 means passive. Channels may be a retained-owner
 * subset. Active probes are not claimed. Directed BSS/SSID, dwell/rest and
 * filter/override scan flags refuse. info must be the authoritative native owner snapshot. */
int niwi_scan_admit(const uint8_t*p,size_t n,const struct nss_info*info,struct nss_request*out){
 if(!p||n!=5456||!info||!info->count||info->count>NSS_CHANNELS||!out)return NIWI_ADMIT_ARGUMENT;
 uint32_t mode=u32(p+52),count=u32(p+68),options=u32(p+4872);
 if(u32(p)!=1||u32(p+4)!=3||u32(p+56)!=1||count>NSS_CHANNELS||
    (mode!=1&&mode!=2))return NIWI_ADMIT_PROFILE;
 for(unsigned j=0;j<info->count;j++){
  if((info->channels[j].band!=1&&info->channels[j].band!=2)||!info->channels[j].channel)return NIWI_ADMIT_OWNER;
  for(unsigned k=0;k<j;k++)if(info->channels[j].band==info->channels[k].band&&
      info->channels[j].channel==info->channels[k].channel)return NIWI_ADMIT_OWNER;
 }
 /* Compare semantic fields, not padding or inactive array storage. The actual
  * framework dictionary builder initializes RSSI threshold to INT32_MIN.
  * Zero is retained for the existing null/default request profile. Explicit
  * thresholds, timing, named/short SSIDs are not implemented. */
 if(u32(p+16)||p[60]||p[61]||u32(p+64)||
    (u32(p+4880)!=0&&u32(p+4880)!=0x80000000u)||u32(p+4884)||u32(p+5448))return NIWI_ADMIT_OPTION;
 /* u64 SCAN_FLAGS at 4872: CFNumber32 then boolean ORs LOW_PRIORITY2,
  * INC_OFFCHANNEL_BSS8, LOW_LATENCY0x20, PRIO_OVERRIDE0x40, IS_THROTTLED0x80,
  * capability-gated 0x100..0x800 (builder 0x1d3ebb93c..baec). wifid pairs
  * "Normal priority, low-latency" (interactive) and "Low priority" requests.
  * Either hint alone is met by this idle single-owner FIFO, which starts each
  * request at once; IO80211 result collection reads only 0x200/0x800
  * (0x1d3ede568, 0x1d3ede690). Filters, overrides and 6GHz options refuse. */
 if(u32(p+4876)||(options&&options!=NIWI_SCAN_LOW_PRIORITY&&options!=NIWI_SCAN_LOW_LATENCY))return NIWI_ADMIT_FLAGS;
 unsigned zero=1,broadcast=1;
 for(unsigned i=8;i<14;i++){if(p[i])zero=0;if(p[i]!=255)broadcast=0;}
 if(!zero&&!broadcast)return NIWI_ADMIT_PEER; /* Neither directed peer nor fabricated filter. */
 struct nss_request result={0};
 for(unsigned j=0;j<count;j++){
  const uint8_t*c=p+72+12*j;uint32_t channel=u32(c+4),flags=u32(c+8);unsigned band=0;
  if(u32(c)!=1||!channel||channel>255)return NIWI_ADMIT_CHANNEL;
  /* ACTIVE0x80 grants channel permission; it does not require probe TX.
   * We remain passive. DFS/other widths and unknown attributes refuse. */
  flags&=~128u;
  if(flags==8||flags==10)band=1;
  else if(flags==16||flags==18)band=2;
  else if(flags!=0&&flags!=2)return NIWI_ADMIT_CHANNEL_FLAGS;
  unsigned found=info->count;
  for(unsigned k=0;k<info->count;k++)if(info->channels[k].channel==channel&&
      (!band||info->channels[k].band==band)){
   if(found!=info->count)return NIWI_ADMIT_ALLOWLIST;found=k;
  }
  if(found==info->count)return NIWI_ADMIT_ALLOWLIST;
  for(unsigned k=0;k<j;k++)if(result.channels[k].band==info->channels[found].band&&
      result.channels[k].channel==info->channels[found].channel)return NIWI_ADMIT_ALLOWLIST;
  result.channels[j]=info->channels[found];
 }
 result.count=count;*out=result;return 0;
}
```

```diff
--- a/shim/nord2-xnu/wifi_ios_client.cpp
+++ b/shim/nord2-xnu/wifi_ios_client.cpp
@@ -24,1 +24,13 @@
 static uint32_t niw_u32(const uint8_t*p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
+/* Monotonic milliseconds for numeric boot/UI ordering only. */
+static unsigned long long niw_ms(void){
+#ifdef NIWL_HOST_TEST
+ return 0;
+#else
+ uint64_t now=0,ns=0;clock_get_uptime(&now);absolutetime_to_nanoseconds(now,&ns);return ns/1000000u;
+#endif
+}
+/* Raw EBUSY: wifid's ScanAsync wrapper 0x10014c69c..6d8 sleeps 500ms and
+ * retries 16/61/-3905 up to six times; any other nonzero fails the caller. */
+static const IOReturn kNiwScanBusy=16;
@@ -182 +194 @@
-...if(selector>=1&&selector<=5&&pipeLogs++<32)printf("NORD2 WIFI IOS PIPE: selector=%u phase=%u status=%#x output=%u refs=%u\n",selector,(unsigned)phase,(unsigned)result,a?(unsigned)a->structureOutputSize:0,a?a->asyncReferenceCount:0);...
+...if(selector>=1&&selector<=5&&pipeLogs++<32)printf("NORD2 WIFI IOS PIPE: selector=%u phase=%u status=%#x output=%u refs=%u t_ms=%llu\n",selector,(unsigned)phase,(unsigned)result,a?(unsigned)a->structureOutputSize:0,a?a->asyncReferenceCount:0,niw_ms());...
@@ -229 +241,6 @@
- *handler=c;printf("NORD2 WIFI IOS CLIENT: opened=1 type=0\n");return kIOReturnSuccess;
+#ifdef NIWL_HOST_TEST
+ unsigned rootBusy=0;
+#else
+ unsigned rootBusy=(unsigned)IOService::getServiceRoot()->getBusyState();
+#endif
+ *handler=c;printf("NORD2 WIFI IOS CLIENT: opened=1 type=0 t_ms=%llu root_busy=%u\n",niw_ms(),rootBusy);return kIOReturnSuccess;
@@ scanLocked
- if(trace)printf("NORD2 WIFI IOS SCANREQ: ... short_ssid=%u peer=%u\n",...,peer);
+ if(trace)printf("NORD2 WIFI IOS SCANREQ: ... short_ssid=%u peer=%u t_ms=%llu\n",...,peer,niw_ms());
  if(phase!=Armed||!ownsReference)return kIOReturnNotReady;
- if(pendingTicket||scanGeneration==UINT64_MAX)return kIOReturnBusy;
+ if(scanGeneration==UINT64_MAX)return kIOReturnBusy;
+ if(pendingTicket)return kNiwScanBusy;
  nss_info info={};nss_request request={};if(nord2_wifi_scan_info(&info))return kIOReturnNotReady;
- int admission=niwi_scan_admit(p,n,&info,&request);if(trace)printf("NORD2 WIFI IOS ADMISSION: status=%d owner_channels=%u requested_channels=%u\n",admission,(unsigned)info.count,admission?0:(unsigned)request.count);if(admission)return kIOReturnUnsupported;
+ int admission=niwi_scan_admit(p,n,&info,&request);
+ if(trace){uint32_t listed=n>=72?niw_u32(p+68):0,flags=0,low=0,high=0; /* numeric channel words only */
+  for(uint32_t j=0;j<listed&&j<400&&n>=84+12*(size_t)j;j++){uint32_t ch=niw_u32(p+76+12*j);flags|=niw_u32(p+80+12*j);if(!j||ch<low)low=ch;if(ch>high)high=ch;}
+  printf("NORD2 WIFI IOS ADMISSION: status=%d owner_channels=%u requested_channels=%u channel_flags_or=%#x channel_min=%u channel_max=%u\n",admission,(unsigned)info.count,admission?0:(unsigned)request.count,flags,low,high);}
+ if(admission)return kIOReturnUnsupported;
@@
- if(result){nils_finish(&cacheLife,&cacheTicket,nullptr);release();return result==2?kIOReturnBusy:kIOReturnNotReady;}
+ if(result){nils_finish(&cacheLife,&cacheTicket,nullptr);release();return result==2?kNiwScanBusy:kIOReturnNotReady;}
@@ completed
- ..."NORD2 WIFI IOS COMPLETE: ... cache_status=%#x rows=%u\n",...,current?c->snapshot->count:0);
+ ..."NORD2 WIFI IOS COMPLETE: ... cache_status=%#x rows=%u omitted=%u omit_mask=%#x\n",...,current?c->snapshot->count:0,current?c->snapshot->omitted:0,current?c->snapshot->omit_mask:0);
```

Le parti `...` sono invariate rispetto alle righe 182/234/259 originali.

```diff
--- a/shim/nord2-xnu/wifi_ios_snapshot.h
+++ b/shim/nord2-xnu/wifi_ios_snapshot.h
 enum niwa_status {NIWA_PENDING=1,NIWA_PARTIAL,NIWA_TIMEOUT,NIWA_COMPLETE};
-struct niwa_snapshot {enum niwa_status status;unsigned entered,count,backend_count,dropped;
+enum {NIWA_OMIT_RSSI=1u,NIWA_OMIT_CHANNEL=2u,NIWA_OMIT_IES=4u,NIWA_OMIT_RATES=8u,NIWA_OMIT_OTHER=16u};
+struct niwa_snapshot {enum niwa_status status;unsigned entered,count,backend_count,dropped,omitted,omit_mask;
@@
- * as a permission boolean. Unknown RSSI/rates/truncated IEs refuse encoding.
+ * as a permission boolean. A record without observed RSSI, heard off its own
+ * channel, with IEs beyond the retained/ABI room or unmappable rates is omitted
+ * and counted in omit_mask; it never fails other rows and nothing is guessed.
```

`wifi_ios_snapshot.c`, file completo:

```c
#include "wifi_ios_snapshot.h"
#include "wifi_ios_bss.h"
_Static_assert(NSB_IE_BYTES<=NIWS_MAX_IE,"retained IEs fit the 1164-byte result element room");
/* One record that cannot be encoded from observed data is omitted and counted;
 * it never fails the other rows of a completed scan. Off-channel receptions
 * are omitted: INC_OFFCHANNEL_BSS (0x8) is refused at admission. */
static unsigned row(const struct nsb_record*s,uint32_t age,const struct niwp_rate_map*m,size_t n,uint8_t*encoded){
 struct niwb_bss b;struct niws_result r={0};size_t rates=0;
 if(s->channel_mismatch)return NIWA_OMIT_CHANNEL;
 if(s->ies_truncated||s->ie_bytes!=s->ie_total)return NIWA_OMIT_IES;
 if(niwb_bss_metadata(s,&b))return NIWA_OMIT_OTHER;
 if(!b.rssi_valid)return NIWA_OMIT_RSSI;
 if(b.rate_count>NIWS_MAX_RATES||(b.rate_count&&niwp_rates(m,n,b.ie_rates,b.rate_count,r.rates,NIWS_MAX_RATES,&rates)))return NIWA_OMIT_RATES;
 r.channel=b.channel;r.channel_flags=b.band==1?8u:16u;r.age=age;r.rssi=b.rssi_dbm;
 r.beacon_interval=b.interval;r.capabilities=b.capabilities;r.rate_count=(uint8_t)rates;
 r.ssid_bytes=b.ssid_bytes;r.ie_bytes=b.ie_bytes;
 for(unsigned i=0;i<6;i++)r.bssid[i]=b.bssid[i];
 for(unsigned i=0;i<b.ssid_bytes;i++)r.ssid[i]=b.ssid[i];
 for(unsigned i=0;i<b.ie_bytes;i++)r.ie[i]=b.ies[i];
 return niws_encode_result(&r,encoded,NIWS_SCAN_RESULT_BYTES)?NIWA_OMIT_OTHER:0;
}
int niwa_snapshot_prepare(const struct nsb_scan*s,uint64_t generation,const uint32_t*age,size_t ages,
 const struct niwp_rate_map*m,size_t n,struct niwa_snapshot*out){
 if(!s||!out||out->entered||!generation||!s->entered||s->count>NSB_RESULTS||s->count>s->frames||
    s->last<s->started||s->deadline<=s->started||(s->active&&s->done)||(s->timed_out&&!s->partial))return -1;
 enum niwa_status state=NIWA_PENDING;
 if(s->timed_out)state=NIWA_TIMEOUT;
 else if(s->partial||s->dropped)state=NIWA_PARTIAL;
 else if(s->done){if(s->completion.sequence!=s->sequence||s->completion.state!=7)return -1;state=NIWA_COMPLETE;}
 if(state==NIWA_COMPLETE&&(ages!=s->count||(ages&&!age)))return -1;
 out->entered=1;out->status=state;out->generation=generation;out->backend_count=s->count;out->dropped=s->dropped;
 out->completion_state=s->completion.state;out->completed_channels=s->completion.completed_channels;
 if(state!=NIWA_COMPLETE)return 1; /* never mark partial/timeout/pending cache READY */
 for(unsigned i=0;i<s->count;i++){unsigned why=row(&s->records[i],age[i],m,n,out->rows[out->count]);
  if(why){out->omitted++;out->omit_mask|=why;}else out->count++;} /* encoder writes nothing on refusal */
 if(niwc_begin(&out->cursor,generation)||niwc_complete(&out->cursor,generation,&out->rows[0][0],
    (size_t)out->count*NIWS_SCAN_RESULT_BYTES,out->count))return -1;
 return 0;
}
```

```diff
--- a/shim/nord2-xnu/wifi_ios_bss.c
+++ b/shim/nord2-xnu/wifi_ios_bss.c
@@ -15 +15,3 @@
-   for(unsigned j=0;j<z;j++){uint8_t x=s->ies[i+j]&127;if(!x)return -1;unsigned k;for(k=0;k<r.rate_count;k++)if(r.ie_rates[k]==x)break;if(k==r.rate_count){if(k==16)return -1;r.ie_rates[r.rate_count++]=x;}}
+   for(unsigned j=0;j<z;j++){uint8_t v=s->ies[i+j],x=v&127;
+    if((v&128)&&x>=121)continue; /* BSS membership selector (EHT121..HT127), not a rate */
+    if(!x)return -1;unsigned k;for(k=0;k<r.rate_count;k++)if(r.ie_rates[k]==x)break;if(k==r.rate_count){if(k==16)return -1;r.ie_rates[r.rate_count++]=x;}}
--- a/shim/nord2-xnu/wifi_scan_bss.h
+++ b/shim/nord2-xnu/wifi_scan_bss.h
-#define NSB_IE_BYTES 512u
+#define NSB_IE_BYTES 1024u /* = NIWS_MAX_IE, the 1164-byte iOS result's element room */
--- a/shim/nord2-xnu/wifi_scan_bss.c
+++ b/shim/nord2-xnu/wifi_scan_bss.c
@@ -84 +84,3 @@
-  s->records[i]=r;return 0;
+  /* Adjacent-channel leakage must not replace an on-channel copy. */
+  if(!r.channel_mismatch||s->records[i].channel_mismatch)s->records[i]=r;
+  return 0;
```

---

## 3. Il ritardo di minuti della pagina

### Cosa si dimostra dal log 195a

- configd e wifid partono alle righe 860/862, a circa 20 s.
- wifid apre tre client IOUC tra 24,5 s e 25,5 s (righe 1329/1372/1440, ancorate ai `time_us` di VCN33). L'ultimo esegue GET96/GET97, cioè il costruttore `initWithInterfaceName:`.
- Da lì fino alla riga 4995 non c'è nessuna riga `NORD2 WIFI IOS`.
- Preferences viene eseguito quattro volte (righe 3995, 4306, 4615, 4795, a partire da circa 134 s), senza alcun traffico sul driver Wi-Fi.
- L'event pipe (selettori 5/3/2) si arma tra il timestamp 16:05:19 (t=319 s, riga 4945) e 16:08:10 (t=490 s, riga 5290).
- La prima SCANREQ è alla riga 5398, dopo t≈491 s.
- a199 mostra lo stesso schema: client a 1595, pipe a 4446, collocabile tra 135 s e 451 s.

Durante tutto il periodo in cui "la schermata non era apribile" non esisteva quindi nessuna richiesta di scansione. Il fix su RSSI o sui flag non può spiegare il ritardo e non lo risolverà.

### Ipotesi più forte (non dimostrata)

- In `StartIOKitMatching` resta, invariato, `if (OSKext::iokitDaemonAvailable()) adjustBusy(1)` (contesto in `xnu/patches/0082-nord2-early-probe.patch:6284–6285`).
- Nel log non compare nessun exec di un daemon IOKit, quindi `iokitDaemonLaunched()` non rilascerebbe mai quell'hold e il registry root resterebbe busy.
- Topaz, sulla stessa base, documenta lo stesso effetto («IOKit never quiesced, configd's InterfaceNamer never said quiet, wifid … waited») e lo corregge con `research/reference/ios-topaz-topaz/xnu/patches/0128-topaz-iokit-quiesce.patch`.
- Il README del progetto elencava già questo punto come aperto («Validate IOKit busy/quiesce and InterfaceNamer», `ios-service-bridge/README.md:32`).
- I numeri sono coerenti: 20 s più un timeout di InterfaceNamer di circa 300 s danno circa 320 s, dentro la finestra 319–490 s. Il valore 300/240 s lo ricordo dal sorgente open-source di configd; non l'ho verificato su questo IPSW.

**Controindicazione onesta.** Già a ~25 s configd tocca en0 (riga 1402, `delete address 192.0.0.2`), quindi una parte di SystemConfiguration vede l'interfaccia presto. L'anello esatto tra InterfaceNamer e `IEEE80211NetworkInterfacesNames` (`1abdb5b28`) non è ricostruito.

### Misure minime mancanti

Non serve una sonda separata: le due misure sono già incluse nella patch del client.
- `t_ms` sulle righe CLIENT, PIPE e SCANREQ, per ordinare gli eventi nel tempo.
- `root_busy` all'apertura del client. Se vale ≥1, l'ipotesi è confermata. Se vale 0 e il pipe arriva comunque a ~300 s, l'ipotesi è smentita e il sospetto successivo è la classificazione SC.

### Patch kernel proposta (stessa build, separabile)

`xnu/patches/0083-nord2-iokit-quiesce.patch`. Va rigenerata con `nord2-mkpatch` dopo la 0082, perché i numeri di riga sono indicativi.

```diff
--- a/iokit/Kernel/IOStartIOKit.cpp
+++ b/iokit/Kernel/IOStartIOKit.cpp
@@ -222,7 +222,17 @@
 #endif
 
 #if !NO_KEXTD
+#if defined(NORD2)
+	/*
+	 * NORD2: no IOKit daemon is launched on this root, so iokitDaemonLaunched()
+	 * would never drop this hold; the registry root stays busy and IOKit-quiet
+	 * waiters only proceed after their own timeouts (Topaz 0128). Take no hold;
+	 * IOService.cpp skips the matching decrement.
+	 */
+	if (false) {
+#else
 	if (OSKext::iokitDaemonAvailable()) {
+#endif
 		/* Add a busy count to keep the registry busy until the IOKit daemon has
--- a/iokit/Kernel/IOService.cpp
+++ b/iokit/Kernel/IOService.cpp
@@ IOService::iokitDaemonLaunched(void)
 	IOServiceTrace(IOSERVICE_KEXTD_READY, 0, 0, 0, 0);
 	startDeferredMatches();
+#if !defined(NORD2)
 	getServiceRoot()->adjustBusy(-1);
+#endif
 	IOService::publishUserResource(gIOResourceIOKitKey);
```

Se l'ipotesi è giusta, la sequenza che oggi parte a ~300 s parte semplicemente prima. Non si introduce un comportamento nuovo.

### Altre osservazioni (non bloccano la scansione)

- Una SET19 con `state=0` (OFF) viene rifiutata (riga 5049). È un rifiuto corretto, e wifid poi scansiona comunque.
- `proto_plumb returned 102` su en0 (righe 1335–1340) riguarda il futuro data path.
- I respawn continui di bluetoothd e fairplayd: nessun legame dimostrato con questi sintomi.

---

## 4. Test

### `tools/fixtures/nord2-wifi-ios-contract/test-caller.c` (sostituzione completa)

Contiene il fixture del chiamante reale con flags 0x20.

```c
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "wifi_ios_policy.h"
static unsigned checks;
static void put(unsigned char*p,unsigned v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
/* Actual 195a7e17 Settings SET10 numeric profile (26 identical requests):
 * version1 BSS3 scan1 PHY1, dwell/rest0, 13 channels, SCAN_FLAGS 0x20
 * (LOW_LATENCY, builder 0x1d3ebb998..9bc), RSSI INT32_MIN, no SSID, names,
 * short SSID or peer. Channel words were not logged: records 1..13 use this
 * driver's own GET27 flags 20MHz|2GHz, which CWFChannel round-trips. */
static void caller(unsigned char*p,unsigned channel_flags,unsigned scan_flags){memset(p,0,5456);put(p,1);put(p+4,3);put(p+52,1);put(p+56,1);put(p+68,13);put(p+4872,scan_flags);put(p+4880,0x80000000u);for(unsigned i=0;i<13;i++){put(p+72+12*i,1);put(p+76+12*i,i+1);put(p+80+12*i,channel_flags);}}
static void yes(unsigned char*p,struct nss_info*i){struct nss_request r;assert(!niwi_scan_admit(p,5456,i,&r)&&r.count==13);for(unsigned k=0;k<13;k++)assert(r.channels[k].band==1&&r.channels[k].channel==k+1);checks++;}
static void no(unsigned char*p,struct nss_info*i,int why){struct nss_request r,b;memset(&r,0xa5,sizeof r);b=r;assert(niwi_scan_admit(p,5456,i,&r)==why&&!memcmp(&r,&b,sizeof r));checks++;}
int main(void){unsigned char p[5456],base[5456],power[24]={0};struct nss_info info={0};
 /* Hardware owner_channels=22: 2.4GHz 1..13 plus non-DFS 5GHz (SCANRFCHANNEL). */
 static const unsigned five[9]={36,40,44,48,149,153,157,161,165};info.band5=1;info.count=22;
 for(unsigned i=0;i<13;i++){info.channels[i].band=1;info.channels[i].channel=i+1;}
 for(unsigned i=0;i<9;i++){info.channels[13+i].band=2;info.channels[13+i].channel=five[i];}
 caller(base,10,NIWI_SCAN_LOW_LATENCY);memcpy(p,base,sizeof p);yes(p,&info);
 caller(p,10|128,NIWI_SCAN_LOW_LATENCY);yes(p,&info);caller(p,0,NIWI_SCAN_LOW_LATENCY);yes(p,&info);
 caller(p,10,0);yes(p,&info);caller(p,10,NIWI_SCAN_LOW_PRIORITY);yes(p,&info);
 caller(p,10,NIWI_SCAN_LOW_PRIORITY|NIWI_SCAN_LOW_LATENCY);no(p,&info,NIWI_ADMIT_FLAGS);
 /* Every other SCAN_FLAGS bit, alone and with the real hint, and the high word. */
 for(unsigned b=0;b<32;b++){if(b==1||b==5)continue;caller(p,10,1u<<b);no(p,&info,NIWI_ADMIT_FLAGS);caller(p,10,NIWI_SCAN_LOW_LATENCY|1u<<b);no(p,&info,NIWI_ADMIT_FLAGS);}
 for(unsigned b=0;b<32;b++){caller(p,10,NIWI_SCAN_LOW_LATENCY);put(p+4876,1u<<b);no(p,&info,NIWI_ADMIT_FLAGS);}
 /* Nonsemantic fields/inactive storage are independently poisoned one byte at
  * a time. This is robustness coverage, not a claim the real caller poisons. */
 for(unsigned j=0;j<5456;j++){if((j>=14&&j<16)||(j>=20&&j<52)||(j>=62&&j<64)||(j>=72+13*12&&j<4872)||(j>=4888&&j<5448)||j>=5452){memcpy(p,base,sizeof p);p[j]=0xa5;yes(p,&info);}}
 memcpy(p,base,sizeof p);memset(p+8,255,6);yes(p,&info);p[8]=254;no(p,&info,NIWI_ADMIT_PEER);
 static const unsigned fields[]={0,4,16,52,56,60,64,68,4872,4876,4880,4884,5448};
 static const int why[]={NIWI_ADMIT_PROFILE,NIWI_ADMIT_PROFILE,NIWI_ADMIT_OPTION,NIWI_ADMIT_PROFILE,NIWI_ADMIT_PROFILE,NIWI_ADMIT_OPTION,NIWI_ADMIT_OPTION,NIWI_ADMIT_PROFILE,NIWI_ADMIT_FLAGS,NIWI_ADMIT_FLAGS,NIWI_ADMIT_OPTION,NIWI_ADMIT_OPTION,NIWI_ADMIT_OPTION};
 for(unsigned i=0;i<sizeof fields/sizeof *fields;i++){memcpy(p,base,sizeof p);put(p+fields[i],0x12345678);no(p,&info,why[i]);}
 for(unsigned i=0;i<13;i++){memcpy(p,base,sizeof p);put(p+76+12*i,255);no(p,&info,NIWI_ADMIT_ALLOWLIST);memcpy(p,base,sizeof p);put(p+80+12*i,4);no(p,&info,NIWI_ADMIT_CHANNEL_FLAGS);
  memcpy(p,base,sizeof p);put(p+80+12*i,16);no(p,&info,NIWI_ADMIT_ALLOWLIST);memcpy(p,base,sizeof p);put(p+72+12*i,2);no(p,&info,NIWI_ADMIT_CHANNEL);}
 put(power,1);put(power+4,1);put(power+8,1);assert(!niwi_power_on_admit(power,24));checks++;
 for(unsigned j=12;j<24;j++){power[j]=0xa5;assert(!niwi_power_on_admit(power,24));checks++;}
 put(power+8,0);assert(niwi_power_on_admit(power,24)<0);checks++;put(power+8,1);put(power+4,2);assert(niwi_power_on_admit(power,24)<0);checks++;
 for(unsigned n=0;n<24;n++){assert(niwi_power_on_admit(power,n)<0);checks++;}
 printf("PASS %u actual-caller(flags0x20)/hint/semantic/atomic/reason/power checks; synthetic only\n",checks);}
```

### `tools/fixtures/nord2-wifi-ios-contract/test-snapshot.c` (nuovo)

Usa frame RXD+beacon sintetici che passano dal decoder vero.

```c
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "wifi_ios_age.h"
#include "wifi_ios_rates.h"
/* Synthetic RXD(group4 RXV)+beacon frames through the canonical decoder, dedup,
 * metadata, production rate map, age plan and 1164-byte encoder. Identities
 * are locally administered placeholders; no device frame is used. */
static unsigned checks;static uint8_t buf[3840];static uint64_t now;
static void put32(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static size_t ie(uint8_t*p,unsigned id,const uint8_t*v,size_t n){p[0]=(uint8_t)id;p[1]=(uint8_t)n;if(n)memcpy(p+2,v,n);return n+2;}
static size_t pad(uint8_t*p,size_t bytes){static const uint8_t v[250]={0x00,0x10,0x18};size_t used=0;
 while(used<bytes){size_t left=bytes-used,n=left>252?250:left-2;if(left==253)n=248;used+=ie(p+used,221,v,n);}return used;}
/* HT-required 0xff and SAE-H2E-only 0xfb selectors sit in the rate IEs. */
static size_t frame(unsigned tuned,unsigned ds,uint8_t id,int rxv,size_t extra){
 static const uint8_t ssid[3]={'n','e','t'},rates[5]={0x82,0x84,0x8b,0x96,0xff},ext[9]={0x0c,0x12,0x18,0x24,0x30,0x48,0x60,0x6c,0xfb};
 size_t m=rxv?32:24,at=m+36;uint8_t channel=(uint8_t)ds;memset(buf,0,sizeof buf);
 buf[2]=0x01;buf[3]=0x38;put32(buf+4,rxv?4u<<11:0);put32(buf+8,12u<<8);buf[13]=(uint8_t)tuned;
 if(rxv){buf[28]=100;buf[29]=90;}
 buf[m]=0x80;buf[m+10]=2;buf[m+15]=id;buf[m+16]=2;buf[m+21]=id;buf[m+32]=100;buf[m+34]=1;
 at+=ie(buf+at,0,ssid,3);at+=ie(buf+at,1,rates,5);at+=ie(buf+at,3,&channel,1);at+=ie(buf+at,50,ext,9);
 if(extra)at+=pad(buf+at,extra);
 buf[0]=(uint8_t)at;buf[1]=(uint8_t)(at>>8);return at;
}
static void add(struct nsb_scan*s,unsigned tuned,unsigned ds,uint8_t id,int rxv,size_t extra){size_t n=frame(tuned,ds,id,rxv,extra);now+=10;assert(!nsb_add(s,buf,n,now));checks++;}
static void finish(struct nsb_scan*s){s->active=0;s->done=1;s->completion.sequence=s->sequence;s->completion.state=7;}
int main(void){static struct nsb_scan scan;static struct niwa_snapshot snap;uint8_t out[NIWS_SCAN_RESULT_BYTES];size_t mapn=0;
 const struct niwp_rate_map*map=niwr_production_map(&mapn);
 now=1000;assert(!nsb_begin(&scan,1,now,1000000));
 add(&scan,6,6,1,1,0);                          /* A on-channel, selectors in both rate IEs */
 add(&scan,1,1,2,1,0);uint64_t first=now;        /* B on-channel */
 add(&scan,2,1,2,1,0);                          /* B again through adjacent-channel leakage */
 assert(scan.count==2&&!scan.records[1].channel_mismatch&&scan.records[1].observed_us==first);checks++;
 add(&scan,1,1,2,1,0);assert(scan.records[1].observed_us==now);checks++; /* newer on-channel copy replaces */
 add(&scan,5,6,3,1,0);                          /* C heard only off its channel */
 add(&scan,11,11,4,1,674);                      /* D 700-byte IEs: beyond old 512 retention */
 add(&scan,11,11,5,1,1074);                     /* E 1100-byte IEs: beyond the iOS 1024 room */
 add(&scan,3,3,6,0,0);                          /* F no RX vector: no observed RSSI */
 assert(scan.count==6&&scan.frames==8&&scan.records[3].ie_total==700&&!scan.records[3].ies_truncated&&scan.records[4].ies_truncated);checks++;
 finish(&scan);assert(!niwaa_snapshot_prepare(&scan,1,now,now,map,mapn,&snap));
 assert(snap.status==NIWA_COMPLETE&&snap.backend_count==6&&snap.count==3&&snap.omitted==3&&
   snap.omit_mask==(NIWA_OMIT_CHANNEL|NIWA_OMIT_IES|NIWA_OMIT_RSSI));checks++;
 static const unsigned channel[3]={6,1,11},ies[3]={26,26,700};
 for(unsigned i=0;i<3;i++){struct niws_result r;assert(!niws_decode_result(snap.rows[i],sizeof snap.rows[i],&r));
  assert(r.channel==channel[i]&&r.channel_flags==8&&r.ie_bytes==ies[i]&&r.rssi==-60&&r.rate_count==12&&
   r.rates[0]==1&&r.rates[2]==5&&r.rates[4]==6&&r.rates[11]==54);checks++;}
 for(unsigned i=0;i<3;i++){assert(niwc_take(&snap.cursor,1,out,sizeof out)==1&&!memcmp(out,snap.rows[i],sizeof out));checks++;}
 assert(!niwc_take(&snap.cursor,1,out,sizeof out));checks++;
 /* A completed scan with nothing encodable is a real empty list, not failure. */
 memset(&scan,0,sizeof scan);memset(&snap,0,sizeof snap);now=5000;assert(!nsb_begin(&scan,2,now,1000000));
 add(&scan,5,6,7,1,0);finish(&scan);assert(!niwaa_snapshot_prepare(&scan,2,now,now,map,mapn,&snap));
 assert(snap.status==NIWA_COMPLETE&&!snap.count&&snap.omitted==1&&snap.omit_mask==NIWA_OMIT_CHANNEL&&!niwc_take(&snap.cursor,2,out,sizeof out));checks++;
 /* Partial/timeout scans still never become a ready cache. */
 memset(&scan,0,sizeof scan);memset(&snap,0,sizeof snap);now=9000;assert(!nsb_begin(&scan,3,now,1000000));
 add(&scan,6,6,8,1,0);scan.active=0;scan.partial=1;assert(niwa_snapshot_prepare(&scan,3,0,0,map,mapn,&snap)==1&&snap.status==NIWA_PARTIAL&&!snap.count);checks++;
 printf("PASS %u snapshot checks: selectors not rates, on-channel dedup, per-row omission, 1024B IEs, empty-complete, partial-not-ready; synthetic only\n",checks);return 0;}
```

### Altri ritocchi ai test

- **`tools/test-nord2-wifi-ios-contract.py`**: aggiungere `'snapshot'` alle choices di `--only` e ai suites, con i sorgenti `('wifi_ios_snapshot.c','wifi_ios_age.c','wifi_ios_bss.c','wifi_ios_rates.c','wifi_ios_mapping.c','wifi_ios_scan_contract.c','wifi_ios_cursor.c','wifi_scan_bss.c')`.
- **`tools/fixtures/nord2-wifi-ios-client/test-integrated.cpp`**:
  - Alla riga 25 aggiungere `put(carrier+8+4872,0x40);assert(call(c,0,a)==kIOReturnUnsupported&&!active);put(carrier+8+4872,0x20); /* real interactive LOW_LATENCY; PRIO_OVERRIDE refuses */`. Così il flusso IOUC completo (enqueue, cache, event10, GET11) gira con il profilo reale.
  - Alla riga 26 sostituire `==kIOReturnBusy` con `==(IOReturn)16`.

### Suite da far girare a Codex

- `test-nord2-wifi-ios-contract.py`
- `test-nord2-wifi-ios-client.py`
- `test-nord2-wifi-scan-service.py` e `test-nord2-wifi-normal.py`: toccano `wifi_scan_bss.*` e richiedono la calibrazione privata.

Codex deve anche:
- rigenerare 0082 e 0083;
- ripetere i controlli ISA/LDAPUR consueti;
- adattare il parser del log ai nuovi campi in coda (`t_ms`, `root_busy`, ADMISSION, `omitted`).

---

## 5. Un unico test hardware cumulativo (proposta; nessuna autorizzazione delegata)

**Immagine e procedura.** L'immagine contiene tutte le patch sopra e si avvia boot-only. Collezione completa dal boot. Procedura umana:
1. Non toccare il telefono per 60 s.
2. Aprire Impostazioni → Wi-Fi una volta e restare sulla pagina 60 s.
3. Riferire tre cose: il tempo approssimativo di apertura, quante reti compaiono (solo il numero) e lo stato dei pulsanti.

**Lettura del log** (solo metadati numerici):
- **Ritardo.**
  - `CLIENT … root_busy=` e la differenza di `t_ms` tra il primo CLIENT e `PIPE selector=2`: dovrebbe essere di secondi, non di circa 300 s.
  - Se `root_busy=0` e il ritardo resta, l'ipotesi IOKit-quiet è smentita.
- **Ammissione.**
  - Il risultato atteso è `status=0 requested_channels=13`, insieme a `channel_min/max` e `channel_flags_or`.
  - Un codice di rifiuto diverso identifica da solo il campo successivo.
- **Consegna.**
  - Righe attese: `ENQUEUE`, poi `SCAN: status=0 done=1 partial=0`, poi `COMPLETE … cache_status=0 rows=N omitted=M omit_mask=…`, poi `EVENT event=10 notified=1`, infine N righe `RESULT status=1` seguite da `status=0`.
  - Tra SCANREQ ed EVENT devono passare meno di 30 000 ms di `t_ms`.
- **Esito reale.** Contano N righe e le reti visibili. I 7 AP della scansione di boot non sono un esito.

**Prerequisito.** Prima del test bisogna avere il recovery pronto: questa è la prima esecuzione hardware della continuazione di scansione, e un esito FATAL provoca panic con reset manuale.

---

## Limiti residui

- Non ho compilato né eseguito nulla.
- Lo slot GOT 0x1daca9b68 non è risolto.
- Il pass-through di EBUSY tramite RawSet non è provato.
- La lista e i flag dei 13 canali reali non sono loggati: li darà il prossimo log.
- La catena InterfaceNamer → `IEEE80211NetworkInterfacesNames` resta un'ipotesi.
- Associazione e data path restano non implementati.
