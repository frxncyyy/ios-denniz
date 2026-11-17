Il proprietario ha chiesto esplicitamente di passare a Claude questo caso Wi-Fi e di vederne il lavoro sul desktop3. Sei l'analista principale di questa diagnosi; scegli una correzione coerente dell'intero problema, non una nuova build per ogni bit.

Progetto locale: $NORD2_REPO
Leggi AGENTS.md, la parte iniziale di NOTES.md e questo handoff. Lavoro limitato ad analisi dei sorgenti e dei log salvati. Nessun accesso a USB/telefono, flash, build, git commit o file canonici; Codex coordina integrazione e test. Hai Read/Grep/Glob e sorgenti/disassembly locali. Emetti diagnosi e proposta di patch nel tuo risultato finale: verrà salvato automaticamente qui. Nessun bisogno di contattare terzi. Non leggere credenziali o config account. UFS/boot speed sono conclusi; kmod/audio vanno preservati.

Stato reale:
- Nord2/MT6893 avvia iOS16 con8core, UFSx2, USB live, display/touch/buttons; scansione hardware di boot ora trova7AP su22canali senzaerrori.
- Attuale immagine195a7e174c750f1e0b2bd54894c615ce9e008068b8a52e609923246f6a3ed09b, source27717fffcbbd0221334a4b18df6565551f877ab3, CI39750474116, boot-only flash autorizzato e riuscito. Nessuna associazione/data networking ancora implementata. Non scambiare i7AP di boot con reti consegnate alle Impostazioni.
- Resoconto umano sull'ultimo test: "no, ci sono voluti comunque diversi minuti prima che la schermata fosse apribile. e le reti ancora non spuntano". Non ha confermato di nuovo tutti i pulsanti.
- Snapshot completo privato: $NORD2_REPO/research/nord2/wifi/claude-opus-ios-195a/hardware-snapshot.log; hash/contatori $NORD2_REPO/research/nord2/wifi/claude-opus-ios-195a/evidence.json; estratto numerico $NORD2_REPO/research/nord2/wifi/claude-opus-ios-195a/scan-request-evidence.txt. Il collector70804 prosegue a cura di Codex; non aprirne altri.
- Il log ORA contiene richieste iOS reali SET10:5456B, phase3 Armed, version1,BSS3,scan_type1,PHY1,dwell/rest0,channels13,ssid_length0,names0,flags_lo32/flags_hi0,RSSI -2147483648,short_ssid0,peer0. ADMISSION=-1,owner_channels22; nessuna ENQUEUE. Questo è un dato, non inferenza.
- La build precedente a199cd0c riceveva richieste ma non loggava tutti i campi. Abbiamo corretto la precedente zero-byte sweep che rifiutava il defaultRSSI INT32_MIN documentato dall'esatto framework. Ora niwi_scan_admit lo ammette ma continua a rifiutare tutti i flags. Test host e review linkata PASS erano limitati al profilo conosciuto; NON dimostrano che tutto il profilo reale fosse ammesso. Il nuovo flags32 è un altro rifiuto concreto. Non limitarti a whitelistare32 senza capirne semantica.

Materiale chiave:
- shim/nord2-xnu/wifi_ios_policy.c/h, wifi_ios_client.cpp, wifi_ios_registry.cpp, wifi_ios_scan_contract.*, wifi_ios_scalar.*, wifi_ios_event.*, wifi_scan_service_bind.inc, wifi_scan_service.h, wifi_normal_rx.c, wifi_normal_transport.c.
- tools/test-nord2-wifi-ios-contract.py, tools/test-nord2-wifi-ios-client.py e fixtures sotto tools/fixtures/nord2-wifi-ios-*.
- research/nord2/wifi/ios-service-bridge/IO80211-disassembly.txt e altri testi della stessa directory sono estratti dallo stesso IPSW installato. Usa Grep/Read con bounds. Il dictionary scan builder e sentinel4880 sono analizzati in research/nord2/wifi/ios-service-bridge/service-compat-resume/scan-policy-hardware-candidate.
- research/nord2/wifi/ios-service-bridge/service-compat-resume/role-parent/post-compat-hardware/ADDENDUM.md prova GET96fallbackInfrastructure0, nonAWDL6; scansioni ed eventpipe reali già raggiunti. Non reintrodurre l'ipotesi superata di whitelistAppleclass.
- ricerca precedente e trace a199: research/nord2/wifi-ios-compat/ios/backend-full.log, ios-service-parsed.json.
- riferimento utente MakrSas/ios-topaz, snapshot locale in research/nord2/wifi/ios-service-bridge (trovalo con Glob mirato).
- riferimento utente oosexgang: research/reference/oosexgang/hackios-xnu-kernel, commit8e61187098f2c26c5daee8f9f72b6416aa64b9ab, xnu/files/iokit/Kernel/RedwoodWLAN.cpp e xnu/files/bsd/dev/redwood_apple80211.c. I path esatti possono essere nel manifest research/reference/oosexgang/manifest.json. Il backendQualcomm non è MediaTek; riusa contrattiiOS, non registrihardware.
- Android source $HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel (gen4m) commit04d00c2565764c3922c9b4826b5acff1106e533f.

Richiesta:
1. Ricostruisci l'intero profilo di scansione realmente emesso dai servizi iOS: flag0x20, default/altri campi attivi, elenco canali e semantica della durata/caching. Prova esattamente cosa significhi0x20 attraverso producer/consumer/sourceautorevoli; separa dati da ipotesi.
2. Proponi una correzione completa minima per accettare la richiesta supportata e consegnare risultati/eventi reali senza falsificare successi né ignorare filtri richiesti. Controlla anche i rami successivi: owner/enqueue/completion/event10/result11, con particolare attenzione a eventuali rifiuti o timeout immediatamente prevedibili. Non ripetere intere reviewinvariate.
3. Indaga il ritardo di minuti della pagina: distingui la richiesta di scansione rifiutata da altre dipendenze/power/CARD/inventario. Elenca cosa si può dimostrare dal log e dal codice e quali misure minime mancano. Non affermare che il fixRSSI o flags da solo risolva il ritardo.
4. Consegna report con causa/evidenza esatta, patch proposta (diff o funzioni complete), fixture del caller reale con flags32 e test meaningful, e un unico testhardware cumulativo proposto. Nessuna nuova flash authorization è delegata.

Rispondi in italiano, con prose leggibile e riferimenti file/linea/PC. Evita checklist speculative e ulteriori microsonde se la semantica è ricostruibile offline. Non stampare SSID/BSSID/MAC, calibrazione, chiavi o payload privati: i metadati numerici sono sufficienti. Puoi usare l'analisi precedente di Claude RF in research/nord2/wifi/claude-opus-rf-c31/REPORT.md come storico, ma ora il problema è il client iOS.
