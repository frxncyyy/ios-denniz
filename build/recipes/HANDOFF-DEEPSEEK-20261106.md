# Prompt di passaggio — progetto Nord2 (iOS su OnePlus Nord 2), stato al 6 novembre 2026

Sei un agente di sviluppo che continua il lavoro su un OnePlus Nord 2 (MT6893, nome in codice denniz) che esegue iOS 16.7.16 su un kernel XNU personalizzato.

- **Lingua:** rispondi all'utente SEMPRE in italiano.
- **Autorizzazioni:** l'utente ha dato piena autorizzazione per flash e test sul telefono.

## Regole fisse (obbligatorie)
- Non scrivere mai IMEI, IMSI, ICCID o numeri di telefono in note, log condivisi o risposte.
  - I log decodificati di iOS e i blob NVRAM li contengono.
  - Non pubblicarli e non copiarli in report.
- Non pubblicare sorgenti privati derivati da tegu. Non ridistribuire SDK Apple.
- Non usare MAI `pkill -f` o `pgrep -f` con pattern che possono corrispondere alla tua stessa shell.
  - Per uccidere un processo, trova il PID leggendo `/proc/<pid>/cmdline` ed escludi `$$`.
  - Per aspettare la fine di un ciclo, attendi un file DONE, non `pgrep`.
- **Non restare mai inattivo.** Prima di chiudere un turno in modalità autonoma, assicurati che ci sia un comando in background in attesa del prossimo evento (file DONE o riga di log). Una volta si sono perse 4 ore per questo.
- La riga di comando del kernel è PIENA: non si possono aggiungere boot-arg. I default vanno compilati nel codice.
- Il kernel non ha `strstr` e simili: usa gli helper `bb_strstr` / `bb_has_substr` in `Nord2Baseband.cpp`.
- Definisci le macro prima del loro primo uso.
- Lo spazio su disco è limitato: decodifica i log unificati di iOS filtrando con una pipe (`-o /dev/stdout | grep ...`), mai in un file completo.
- Non scrivere mai nella pstore RAM.
- Commit solo su richiesta, con la riga finale: `Co-Authored-By: ...`.

## Percorsi
- **W** (radice di lavoro): `$NORD2_WORK`
- **S** (scratchpad con i sorgenti delle patch): `/tmp/claude-1000/-home-user-Documents-Codex-2026-10-19-vedi-questa-repo-https-github-com-work-ios-sweet-research-nord2-gpu-overnight-20261024-work/7eab3ca4-47cd-4507-982a-63f3d778de8f/scratchpad`
  - È in `/tmp`: copialo subito in un posto persistente, per esempio `cp -a $S $W/gpu/build/scratchpad-backup-20261106`.
- **Strumenti fastboot/adb:** `$HOME/Android/Sdk/platform-tools/{fastboot,adb}`

## Albero delle patch del kernel
- **Serie di patch:** in `$W/gpu/build/kernel-p1-20261026` (fino a 0276).
- **Patch nuove**, nello scratchpad S:
  - `bb1/` (build di TEST): `SWEET_PERSIST_TEST 1`, cioè dopo 7 minuti il telefono si riavvia da solo in fastboot.
  - `bb1d/` (build DAILY): `SWEET_PERSIST_TEST 0`, nessun riavvio automatico. Per il resto i sorgenti sono identici a bb1.
  - Entrambe hanno `src/` (sorgenti modificati: `bsd/kern/sweet_amfi.c`, `iokit/Kernel/arm/Nord2Baseband.cpp`, `nord2_bb_qmi.h`, `bsd/kern/nord2_bb_kpi.h`, ...), `make_patch.sh` (rigenera `0277-nord2-baseband-bridge.patch` e `patched-sources-0277`) e `test/` (host test).
  - `callaudio/` (per bb1) e `callaudio-d/` (per bb1d) contengono:
    - 0280 audio in chiamata e 0281 hook del bridge;
    - 0282 aggiornamento del plugin HAL;
    - `make_0281.sh <dir-bb>`, da rilanciare dopo OGNI rigenerazione della 0277: `bash $S/callaudio/make_0281.sh $S/bb1` e `bash $S/callaudio-d/make_0281.sh $S/bb1d`.
  - `audiofix/` (0278), `slider/` (0279: alert slider come interruttore suoneria).
  - `installup/` (0282), `halplug/` (sorgenti e build locale del plugin RedwoodAudio, pacchetto attuale `halplug/out-c/audio.pkg`).
  - `ims/` (strumenti e `STATUS-0700.md`), `nvbuild/` (blob NVRAM), `vend/` (estrattore EROFS e librerie MTK estratte).
- **Dopo ogni modifica ai sorgenti in bb1:**
  1. copia i file in `bb1d/src`;
  2. rimetti `SWEET_PERSIST_TEST 0` in `bb1d/src/bsd/kern/sweet_amfi.c`;
  3. esegui `make_patch.sh` in entrambe;
  4. esegui `make_0281.sh` in entrambe le cartelle callaudio.

## Compilare ed eseguire
- **Script di build per immagine:** `$W/gpu/build/device-mmu-<TAG>-candidate/build_<TAG>.sh`.
  - Modello DAILY: `device-mmu-q10dx-candidate/build_q10dx.sh` (bb1d, callaudio-d, audiofix, slider, serie fino a 0282, `--audio-package $S/halplug/out-c/audio.pkg`).
  - Modello TEST: `device-mmu-q10cx-candidate/build_q10cx.sh` (bb1, callaudio).
  - Per una nuova build, copia `build_p1_candidate.py` e `pack_with_args.py` dalla cartella modello e sostituisci il tag con sed.
  - L'output è `ui-sched/*.img`; `pack.done` contiene `done`, `FAILED` o `PACKFAILED`.
  - Se fallisce, cerca gli errori di compilazione con `grep " error:" kernelb/*-compile.log`.
  - PACKFAILED con EOFError significa che `/tmp` è pieno (c'è una quota): libera spazio.
- **Automazione:** cartella `$W/gpu/build/autocycle/`.
  - `build_and_run.sh TAG` aspetta la build, flasha, cattura il log del kernel in `runs/TAG/ui-usb/xnu-live.log` e scrive `runs/TAG/DONE`.
  - `run_cycle.sh IMG TAG` flasha un'immagine già pronta.
  - `mk_test.sh TAG` crea ed esegue una build di test partendo dal modello q10cx.
  - `to_fastboot.sh`, `flash_boot.sh`: dal rescue Android il fastboot si raggiunge da solo (bit RTC + sysrq). Da una build DAILY serve che l'utente metta il telefono in fastboot a mano. Le build TEST tornano in fastboot da sole dopo 7 minuti.
  - `fetch_ulog2.sh TAG`: rescue, snapshot e estrazione dei log unificati di iOS in `$W/gpu/build/cc-fetch-TAG/arc`. Decodifica con `/tmp/claude-1000/ulog/examples/target/release/unifiedlog_iterator -m log-archive -i ARC -o /dev/stdout | grep ...`.
  - `txn.sh` modifica il rootfs di iOS (env PAYLOAD/GUEST/OWNED, esempi in `$W/gpu/build/apps-20261104/`).
  - Backup: `install_nvx.sh BLOB` installa un blob NVRAM in userdata+64GiB; `backup_ios.sh` / `restore_ios.sh` fanno il backup e il ripristino di iOS (16 GiB).
- **Immagini salvate:**
  - `autocycle/q10dx-DAILY-image/` è la daily attuale: VoLTE, SMS, dati, audio dall'auricolare, slider.
  - `q10dw-DAILY-VOLTE-image/` è la daily precedente con VoLTE.
  - `q10cv-image/` è senza IMS (chiamate in 3G).

## Stato attuale (tutto verificato sul telefono, salvo dove indicato)
- **Funziona:**
  - dati 4G;
  - SMS in invio e in ricezione, con banner;
  - chiamate VoLTE in uscita e in entrata, con la rete sempre in 4G;
  - audio dall'auricolare in chiamata e microfono;
  - suoneria e alert slider;
  - IMS registrato (`+CIREGU: 1,1`);
  - NVRAM del modem: blob Android completo in userdata+64GiB (sha 389ab76d…), servito dal server FS del kernel (0277ay e bb).
- **Chiavi di VoLTE**, ricavate dal log di Android in `$W/gpu/build/ANDROID-IMS-LOG/`:
  - sequenza di avvio come il RIL MTK (EBOOT, EDSBP=2, EGRAT, EFUN=1, ERAT, ...);
  - tabella APN tra `EAPNLOCK=1/0`, con `bearer_bitmask=4294836223` (0 significa nessuna rete radio e provocava l'errore 5873);
  - `EIAAPN` con 7 campi;
  - dati via `EAPNACT` default (cid 0);
  - conferma `EIMSPDN` con stato 0 e risposte `EIMSPDIS`;
  - chiamate tracciate con `+ECPI` (`+CLCC` è in ritardo per IMS); `EAIC` approva usando il campo 5;
  - le risposte di errore del server FS devono avere lo stesso numero di valori di quelle riuscite (altrimenti il modem va in assert).
- **NON funziona ancora:**
  1. Pulsante muto in chiamata (il microfono resta attivo).
  2. Pulsante vivavoce grigio nell'app Telefono.
  3. Lo slider del volume in chiamata non cambia il livello.
  4. MMS: implementato con `EAPNACT` di tipo "mms" (0277bc), NON ANCORA PROVATO. L'utente può provare un solo MMS perché costa: fai tutto il possibile prima.

## Prossimo lavoro: audio (punti 1–3)
- **Log di iOS** dell'avvio q10dx, con il plugin v2:
  - archivio in `$W/gpu/build/PARKED-audio-logs-q10dx/arc`;
  - estratto filtrato in `PARKED-audio-logs-q10dx/audio.jsonl` (solo righe audio).
  - Prendi il `boot_uuid` più recente.
- **Plugin HAL:**
  - sorgente: patch `$S/halplug/nord2-receiver-port-v2.patch` sopra upstream `oosexgang/hackios-xnu-userland@a79ff07`, file `userland/src/audio_plugin.c`;
  - build locale: `$S/halplug/build_local.sh`, che produce `out-*/audio.pkg`;
  - installazione: tramite il kernel (0282, la generazione sale da sola quando cambia l'hash). Non sostituire mai il plugin direttamente nel rootfs: il controllo del kernel bloccherebbe l'avvio.
- **Lato kernel:**
  - `/dev/redwood-audio` accetta gli ioctl `'r'` 2 (route 0 = auricolare, 1 = altoparlante), 3 (volume per porta) e 4 (muto uplink);
  - sysctl `kern.n2call.*`.
- **Ipotesi del lavoro precedente:** VirtualAudio crea la porta ricevitore (`prec`) se il device "Codec" ha una proprietà mute sull'elemento di uscita 3, e il vivavoce dipende da quella porta. Il muto di iOS passa da `vaemSetUplinkMuteOnDevice` / `[mute/inpt/0]` sull'input. Verifica nei log se iOS ha creato `prec` e se il plugin ha ricevuto i comandi (righe `redwood:` in syslog).

## Metodo che ha funzionato
- Confronta sempre con Android quando possibile: i log in `ANDROID-IMS-LOG` sono la verità di riferimento.
- Per il firmware del modem: `/tmp/claude-1000/md1x/` (md1rom, `syms.pkl`, `mdis.py` con capstone 6 per nanoMIPS).
- Su hardware fai iterazioni brevi: una build TEST circa ogni 8 minuti, leggendo `runs/TAG/ui-usb/xnu-live.log`.

## Aggiornamento 17:05
- **Analisi dei log q10dx:** VirtualAudio crea la porta `prec`. Però la sessione PhoneCall non si attiva: "input can't be made routable, hasInputDevice=NO".
  - Il microfono `pmbi` (sul device Codec) non entra nel device di default per due errori: `!siz` su `[ssrc/inpt/0]` e la mancanza di un controllo volume in input.
  - Ne derivano il vivavoce grigio, il volume in chiamata applicato alla categoria Audio/Video e il muto che finisce su un device senza input.
- **Plugin v3** (`$S/halplug/out-d/audio.pkg`, sha ffd34ce3…; patch `nord2-receiver-port-v3.patch`):
  - aggiunge i controlli di volume e muto in input sul Codec;
  - accetta qualsiasi dato su `ssrc`;
  - il muto viene combinato nello ioctl `'r'` 4.
- **Build:** daily q10dy = q10dx + `--audio-package out-d`, immagine in `autocycle/q10dy-DAILY-image/`, NON ANCORA PROVATA.
- **Se non basta:** guardare le proprietà baseband↔codec dell'iPhone X (`bb2w`/`w2bb`) e la PRECONDITION FAILURE di CoreTelephony sulle info del vocoder.

## Aggiornamento 17:30 — COMPITO IMMEDIATO PER DEEPSEEK
**Stato:** la daily q10dy (plugin v3, generazione 3 installata, IMS registrato) è stata provata dall'utente. Muto, vivavoce e slider del volume NON funzionano ancora; l'audio dall'auricolare funziona.

**Log di iOS di q10dy:** archivio in `$W/gpu/build/PARKED-audio-logs-q10dy/arc`.
- Se esiste già `audio.jsonl` in quella cartella, è un estratto filtrato (righe audio di mediaserverd, audiomxd, callservicesd, InCallService e CommCenter).
- Altrimenti decodificalo così, filtrando sempre con una pipe perché il disco ha poco spazio:
```
X=$W/gpu/build/PARKED-audio-logs-q10dy
/tmp/claude-1000/ulog/examples/target/release/unifiedlog_iterator -m log-archive -i $X/arc -o /dev/stdout 2>/dev/null \
 | LC_ALL=C grep -a -E '"process":"[^"]*(mediaserverd|audiomxd|callservicesd|InCallService|CommCenter)"' \
 | LC_ALL=C grep -a -i -E 'redwood|VirtualAudio|vaem|receiver|prec|pmbi|mute|volume|route|speaker|routable|interruption|CMSession|ssrc|bb2w|w2bb|vocoder|PRECONDITION' > $X/audio.jsonl
```
- Se il decoder non c'è più (sta in `/tmp`), ricompilalo da https://github.com/mandiant/macos-UnifiedLogs (examples/unifiedlog_iterator, `cargo build --release`).
- Il boot giusto è il `boot_uuid` che ha più righe `redwood`: `grep -a redwood audio.jsonl | grep -ao '"boot_uuid":"[^"]*"' | sort | uniq -c`.
- **Kernel log** dello stesso avvio: `$W/gpu/build/autocycle/runs/q10dy/ui-usb/xnu-live.log` (righe `NORD2 CALL`).

**Cosa verificare nel log:**
1. Sono spariti gli errori `!siz` su `[ssrc/inpt/0]` e "No control present for (Codec pmbi)"?
2. Durante la chiamata il device di default ha `input ports: { pmbi }`?
3. C'è ancora "Skipping begin interruption … input can't be made routable, hasInputDevice=NO"?
4. In che categoria finiscono i comandi del volume (PhoneCall o Audio/Video)?
5. Quali righe `redwood:` compaiono: data source, muto in ingresso, volume del ricevitore?
6. Se la sessione salta ancora con `hasInputDevice=NO`, studia cosa si aspetta VirtualAudio su iPhone X (D22AP) per il microfono nella sessione PhoneCall:
   - le proprietà baseband↔codec `bb2w`/`w2bb` sul Codec;
   - le info vocoder di CoreTelephony (PRECONDITION FAILURE in CoreTelephonyInterface.cpp:732);
   - eventualmente un device "Baseband" o un uplink che VirtualAudio deve trovare.
   - VirtualAudio e i plugin si estraggono dal dyld cache e da `$MAIN/research/nord2/ipsw/root-images/OS.dmg`. Nel lavoro precedente erano stati estratti in `$S/hal/` e `$S/dsc/`, se esistono ancora.

**Poi:**
1. correggi il plugin (v4) partendo da `halplug/nord2-receiver-port-v3.patch` e ricompila con `halplug/build_local.sh` (nuovo `out-e/audio.pkg`);
2. fai una daily con `--audio-package` che punta al nuovo pacchetto (modello `device-mmu-q10dy-candidate/build_q10dy.sh`);
3. falla provare all'utente.

**Ultimo punto aperto:** MMS (0277bc, non provato; l'utente può fare un solo tentativo).

**Nota 17:32:** `PARKED-audio-logs-q10dy/audio.jsonl` è già decodificato e filtrato (290 MB). Il boot di q10dy è `089E66BE1F1E4672A82F39D5DB95A7F4`: estrailo con `grep -a "\"boot_uuid\":\"089E66BE1F1E4672A82F39D5DB95A7F4\""`. Il telefono è di nuovo sulla daily q10dy, avviata alle 17:27.

## Aggiornamento 8 novembre 17:55 (Claude) — causa vera di muto/vivavoce/volume
- `/tmp` è stato cancellato dal riavvio: lo scratchpad S non esiste più. Usa SOLO `gpu/build/scratchpad-backup-20261106/` (bb1d, audiofix, slider, callaudio-d, halplug). I plugin v5/v6 di DeepSeek (out-f/out-g in /tmp) sono persi.
- Decoder log iOS ricompilato in `~/.local/share/ulog-src/examples/target/release/unifiedlog_iterator`. Filtro compatto: `$S_NEW/compact.py` non è persistente; il formato è una riga per messaggio (boot, tempo, processo, libreria, messaggio).
- **Catena del difetto** (verificata nei binari):
  1. MediaExperience `cmsShouldSkipBeginInterruption` → `hasInputDevice` = proprietà VAEM `VAEM_InputDeviceIsAvailableForRouteConfiguration` (PhoneCall/Default).
  2. Questa chiede a VirtualAudio `'cprc'` (kVirtualAudioPlugInPropertyConnectedPortsForRouteConfiguration, scope `inpt`): lista vuota.
  3. In VirtualAudio, `RoutingHandler_Call_GenericConfig2` crea `AggregateDevice_Call` da Input Device (Codec/Digital Mic), Output Device (Speaker/Codec) e **Baseband Device = dispositivo HAL con UID "Baseband Voice"** (`Device_Baseband_Voice_Aspen`, porte `pdln` "Baseband Downlink" e `puln` "Baseband Uplink"). Il plugin non lo aveva → nessuna rotta PhoneCall.
- **Plugin v7** (`halplug/c/`, pacchetto `halplug/out-g/audio.pkg`, sha c04acd50…; test `halplug/test/t7.c`): aggiunge il device "Baseband Voice" (1 canale in/out, rate 8/16/24/32/48 kHz, mai default; downlink = silenzio, uplink scartato) e memorizza/notifica il data source del Codec.
- **Daily q10eb** = kernel di q10ea invariato + plugin v7 (solo reimpacchettamento: `device-mmu-q10eb-candidate/build_q10eb.sh`). Immagine in `autocycle/q10eb-DAILY-image/`. Flashata 17:50, generazione audio 6 installata.
- Ancora aperto: CommCenter "Could not get vocoder type ... QMI_ERR_NOT_SUPPORTED" (il bridge QMI del kernel non risponde al tipo di vocoder); VirtualAudio usa la frequenza del vocoder per il Baseband Voice. Se la rotta parte ma qualcosa resta strano, è il prossimo indiziato.

## Aggiornamento 8 novembre 18:35 (Claude) — q10eb provata, blocco in chiamata = prossimità
- q10eb (plugin v7): la rotta della chiamata PARTE (log iOS: "baseband voice link runs (8000 Hz)", ricevitore, volume ricevitore → kernel "iOS call volume receiver"). Prima volta.
- Ma rispondendo il telefono "si blocca": callservicesd chiede a backboardd proximityDetectionMode=active (audio sul ricevitore) → backboardd mette il digitizer in modalità faceExpected → i tocchi non passano più. Non c'è sensore di prossimità.
- Fix **0283**: patch binaria di callservicesd (sopra 0277u), `gpu/build/prox-0283/patch_csd_prox.py`: a 0x10001a880 `mov x19,x1` → `mov x19,#0` (modalità prossimità sempre 0). sha 408f228d…; installata con txn.sh (payload-prox, guest-prox.sh, owned-prox.json; backup callservicesd.pre0283). Transazione cc-txn-q10ec, kernel q10eb.
- Il 3G del test q10eb: IMS mai registrato perché il telefono era partito con un'altra SIM (PIN richiesto dal modem stesso: "SIM 2 PIN-lock 1") e la Very inserita dopo. Con la Very all'avvio: "+CIREGU: 1,1" (q10ec).

## Aggiornamento 8 novembre 19:50 (Claude) — chiamate OK, sensori via SCP (0284)
- **q10ed** (bridge: risposta/chiusura chiamata con coda AT urgente `nord2_at_query_urgent`, niente setup AT durante chiamata): l'utente conferma la chiamata FUNZIONA. Resta il blocco touch per la prossimità.
- **Sensori Nord 2** (dtbo sensor_devinfo + dmesg rescue): TCS3701 (prox/ALS, SCP I2C0 0x39, EINT4), BMI160/LSM6DSO (acc/gyro, SCP SPI2), magnetometro AK09918/MMC5603. Tutti dietro l'**SCP** (MT6893 tinysys RISC-V, mailbox IPI). Android (rescue) mette `scp_share` a **0x89000000+0x320000**; LK riserva il firmware a 0x9fa00000+5MB. Note e dump: `gpu/build/scp-probe-20261108/` (scp1.img = partizione SCP, privata: non pubblicare).
- **Patch 0284** (`scratchpad-backup-20261106/sensors/`, `make_patch.sh`, a/=stage q10ed, b/=patch): `bsd/dev/nord2_scp.inc` (thread: step1 lettura, clock scp_sel, GPR1/2 DRAM, release core0, READY mbox1 bit10, SET_CONFIG FIFO, nanohub ConfigCmd prox/light/acc/gyro/mag, drain FIFO ogni 20 ms, log "NORD2 SCP:"), hook in `sweet_touch.c` (nord2_input_init), `SweetHID.cpp`: servizi Apple-vendor 0xFF00 usage 0x08 prox, 0x04 ALS, 0x03 acc, 0x09 gyro, 0x0A compass. Prox parte "far" e lo ripete a ogni setProperties di backboardd; "near" inoltrato solo dopo aver visto almeno un "far". boot-arg off: nord2scp=0 / nord2sensors=0.
- Riferimenti: driver HakiOS MT6768 (MIT, ~/work/ext-hakios/repo), sorgenti OnePlus mt6893 OSS (GPL, solo lettura per i layout) in ~/work/ext-hakios/op.
- Build **q10ee** = q10ed + 0284 (`device-mmu-q10ee-candidate`).

## Aggiornamento 8 novembre 21:00 (Claude) — stato hub SCP / sensori (interrotto per limite d'uso)
- Telefono: ultima daily stabile flashata = **q10ed** (chiamate OK). q10ee/q10ef (sensori HID) = backboardd in crash loop: il servizio HID di prossimità 0xFF00/0x08, anche da solo, fa andare backboardd in eccezione (`-[BKIOHIDServiceMatcher _lock_servicesAdded:]` → unrecognized selector). Default ora `N2_SENSOR_MASK 0`.
- Hub SCP: avvio del core0 → il telefono si blocca (watchdog → fastboot in ~39 s nelle build TEST). PMIC letto: VGPU11 SSHUB_EN=0, vosel AP 0x38 (~750 mV). Nelle prove q10ei/j/k (avvio rimandato 60 s) la morte avviene esattamente a 60 s dopo "step 2 in 60 s", prima che la riga "2a" (subito dopo `ml_io_map(0x89000000)`) arrivi sulla console USB → sospetto: `ml_io_map` della DRAM 0x89000000 o la scrittura dell'header FIFO, oppure la console non fa in tempo. Prossimo test: stampare PRIMA di ml_io_map, provare senza toccare la DRAM, poi rilascio core0 senza FIFO.
- Firmware SCP (scp1.img) ha un proprio driver PWRAP (`pwrap_swinf_tinysys`) e richieste bus/26M/APSRC verso SPM: altri sospetti per il blocco.
- Strumenti: `autocycle/test_run.sh TAG` (build + flash TEST + cattura, autonomo), patch dir `sensors-t` (modo 2, test) e `sensors` (modo 1, daily). Ultima build test: device-mmu-q10ek-candidate.

## Aggiornamento 9 novembre 01:20 (Claude) — hub sensori FUNZIONANTE, prossimità da collegare a iOS
- Causa dei blocchi SCP: NON l'SCP ma lo spazio io_map del kernel (8 MB, quasi pieno): mappature grandi/prima del Wi-Fi → panic del Wi-Fi. Fix: mappature accorpate (topck+infra, 5 mbox in una) e fatte 90 s dopo il boot; FIFO 16 KB a 0x8904c000.
- Hub: core0+core1 partono, SCP_INIT_DONE, FIFO funziona: accelerometro (handle 0), magnetometro (1), giroscopio (3) ~50 Hz. Record = handle MTK (tipo Android − 1).
- Prossimità TCS3701: serve **VIBR 3.0 V** (LED IR; Android infrared_set_power): VIBR_ANA_CON0 0x1fae VOSEL=11, LDO_VIBR_CON0 0x1d52 EN; scritto dal thread input (unico utente PMIF). Record handle 7: campo1 contatore, **campo2 stato 1=vicino 0=lontano** (verificato coprendo). Calibrazione OPLUS inviata (cmd 3). Luce (handle 4): ancora nessun dato.
- iOS: backboardd ignora la prossimità inviata dal servizio touch; un servizio 0xFF00/0x08 separato fa crashare backboardd con `-[NSNull applyUIMode:]` (wrapper di prossimità non istanziato). Tentativo: proprietà `ProxDetectionMode` sul servizio → **daily q10f1** (autocycle/q10f1-DAILY-image, NON flashata): hub on (modo 2), servizio prossimità (mask 1), plugin v8 (out-h: niente notifiche senza cambio valore; ha tolto il rimbalzo ricevitore/altoparlante).
- Rootfs: callservicesd attuale = 0277u (0283 rimossa con txn q10ez). Se q10f1 crasha backboardd: rimettere q10ed o mask 0.
