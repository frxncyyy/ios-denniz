Sto ricostruendo la sequenza di accensione conninfra/A-die del port rispetto al callgraph Linux selezionato. È il punto più probabile per un RF "digitalmente sano ma sordo".

Sto verificando chi accende il clock top dell'A-die per il Wi-Fi (`adie_top_ck_en_on(FW_WIFI)`), perché Linux lo spegne solo in `wf_pwr_off`. Se il port non lo accende mai, è un candidato forte.

Trovato un candidato forte: `consys_plt_pmic_wifi_power_ctrl` accende VCN33_1/VCN33_2 (3,3 V per l'RF Wi-Fi). Ora controllo se il port lo fa.

Confermato nel sorgente: dopo il POS, Linux chiama `opfunc_vcn_control_internal(WIFI)` → VCN33_1+VCN33_2. Ora verifico il helper PMIC nella patch 0082.

Ora cerco la tabella `nw_addr`/`nw_mask` per vedere esattamente quali registri PMIC programma il port.

The snapshot already reads 0x1be2..0x1c1a: I'm checking whether these are the VCN33_1/VCN33_2 registers of the MT6359P.

The port only programs VCN13/VCN18. I'm checking whether the c31 receipt contains the VCN33 snapshot (0x1be2..0x1c1a).

Strong evidence: in the snapshot VCN33_1/2 have MON=0 (rail off) and OP_EN=0x8000. I'm checking what bit 15 of OP_EN means and whether anything turns them on later.

VOSEL a 0x1f98/0x1f9c = 0xf00 → indice 0xf = 3,5 V (tabella `vcn33_*_voltages`), mentre Linux imposta 3,3 V. Verifico il DT live per i supply di conninfra e la modalità RC.

Sto verificando la mappatura dei canali SRCLKEN-RC (quale canale PMRC alimenta Wi-Fi), per capire se la sola abilitazione HW di VCN33 sarebbe sufficiente.

Confermato: canale RC 6 = WIFI, 5 = BT, proprio quelli che Linux abilita per VCN33. Verifico che il driver WLAN chiami davvero `conninfra_pwr_on(WIFI)`.

Ora verifico se audit precedenti hanno già scartato VCN33 e con quale motivazione.

Il candidato VCN33 regge. Controllo velocemente l'altro prerequisito pre-POS di Linux, `clock_buffer_ctrl(1)` (buffer XO del DCXO per connsys), per escluderlo o includerlo.

# Analisi: ricezione Wi-Fi a zero sul Nord2 (c31)

**In sintesi:** il port non accende mai le due alimentazioni a 3,3 V del Wi-Fi, VCN33_1 e VCN33_2. Linux le abilita a ogni accensione del Wi-Fi. Che manchino è provato, e nessun audit precedente le aveva considerate. Che questa sia la causa della ricezione a zero è un'ipotesi forte ma non ancora verificata: la conferma richiede un solo build hardware, descritto sotto. Ho fatto solo analisi in sola lettura: nessun file, git, dispositivo o CI toccato.

## (1) Candidato principale: VCN33_1 e VCN33_2 mai abilitate

### Cosa fa Linux (sorgente 04d00c25)
- `wlan/.../chips/common/cmm_asic_connac2x.c:2559-2575`: `conninfra_pwr_on(CONNDRV_TYPE_WIFI)` viene chiamato prima di `wmmcupwron` (accensione MCU Wi-Fi). Quindi tutto ciò che segue avviene prima della ROM, del FORCE_CAL e dello scan.
- `conninfra/core/conninfra_core.c:298-322`: subito dopo la sequenza di accensione (`consys_hw_pwr_on`) c'è `opfunc_vcn_control_internal(drv_type, true)`. Alla riga 258-259, per il Wi-Fi, questo chiama `consys_hw_wifi_power_ctl(on)`.
- Il percorso prosegue in `consys_hw.c:410-413` → `pmic_mng.c:250-256` → `mt6893_pmic.c:88,472-483`, dove `consys_plt_pmic_wifi_power_ctrl` accende VCN33_1 e VCN33_2.
- In modalità RC (lo stato del Nord2):
  - VCN33_1 (`mt6893_pmic.c:235-259`): abilitazione hardware sui canali 6 e 5, SW_LP=0, tensione 3,3 V, nessun SW_EN.
  - VCN33_2 (`mt6893_pmic.c:297-319`): abilitazione hardware sul canale 6, SW_LP=0, tensione 3,3 V.
- I canali RC sono definiti in `srclken_rc/src/mtk-srclken-rc-hw.h:179-187`: 5 = BT, 6 = **WIFI**. Il canale 6 è quindi la richiesta Wi-Fi.
- Il DT di consys non ha proprietà `-supply`, ma `regulator_get("vcn33_1_wifi")` trova comunque il regolatore per nome (`drivers/regulator/core.c:1628`; `live.dts:3628-3634,3708-3714`). Su Linux queste scritture avvengono davvero.

### Cosa fa il port
- La tabella PMIC contiene solo VCN13 e VCN18: `0082-nord2-early-probe.patch:5275-5277` (`nw_addr = 1c34,1c3a,1c2e,1c22,1c28,1c1c`), usata alle righe 5495-5500.
- Nessuna scrittura VCN33 esiste in shim o patch (verificato via grep).
- `WIFI-PERSISTENT.md:16-18` e `wifi_persistent.c:115-117` dicono che la callback di tensione Wi-Fi è vuota. È vero, ma riguarda solo `_consys_hw_raise_voltage` (`mt6893_pmic.c:634-637`), una funzione diversa.
- Perché gli audit l'hanno mancato: la chiamata VCN33 sta nel core conninfra (`conninfra_core.c:322`), che non è tra i file elencati in `WIFI-PERSISTENT.md:61-63`.

### Cosa mostrano le ricevute
Ricevuta attuale `backend-full.log`, uguale alla 1c5 (`wifi-async-scan/.../backend-full.log:671-690`):

| Riga | Valore letto | Significato |
|---|---|---|
| 702 | `rc=0x401425` | bit0=1: modalità RC attiva |
| 707-716 | VCN33_1 `CON0 0x1be2=0`, `MON 0x1be6=0`, `OP_EN 0x1be8=0x8000` | rail spento; il bit15 è SW_OP_EN (`registers.h:20196-20199`), nessun canale HW abilitato |
| 707-716 | VCN33_2 `0x1c08=0`, `0x1c0c=0`, `0x1c0e=0x8000` | rail spento, stessa situazione |
| 725-726 | VOSEL `0x1f98=0xf00`, `0x1f9c=0xf00` | indice 0xF = **3,5 V** (`mt6359p-regulator.c:393-410,504-521`); Linux imposta 0xD = 3,3 V |

- Il port non scrive più nulla su questi registri dopo lo snapshot. Con SW_EN=0 e nessun canale HW abilitato, VCN33_1 e VCN33_2 restano **certamente spente** per tutto ROM, FORCE_CAL e scan.

### Perché è coerente con il sintomo (ipotesi)
- Il digitale funziona perché è alimentato da VCN13/VCN18: firmware, comandi, eventi e SCAN_DONE.
- La parte analogica a 3,3 V del front-end è spenta. Questo spiega MDRDY=0 e zero beacon su entrambe le bande e su tutti i canali, anche dopo l'aggiunta di CID70 e del filtro RX.
- Il FORCE_CAL con status0 è stato eseguito senza questa alimentazione, quindi i suoi risultati sono sospetti. È coerente con `rf-calibration-recheck/REPORT.md`: status0 e lunghezza del backup non provano che l'RF funzioni.
- Non ho uno schematico che mostri quali blocchi dell'MT6635 alimenta VCN33. Il legame causale resta quindi un'ipotesi, finché il build sotto non lo conferma.

### Altre ipotesi controllate e scartate
- **Buffer XO:** `mt6893.c:256-268` dice che è controllato in hardware e che la chiamata software non fa nulla.
- **Clock top dell'A-die per il Wi-Fi:** l'host lo spegne soltanto (`soc3_0.c:2536`), lo accende il firmware.
- **Tensione per il Wi-Fi:** `raise_voltage` non fa nulla per il Wi-Fi (`mt6893_pmic.c:634-637`).
- **POS dell'A-die / AFE / PLL:** `wifi_persistent.c` corrisponde riga per riga a `mt6893_pos.c:1113-1456`.

## (2) Un solo build: correzione mirata più diagnostica in sola lettura

**Dove:** sul thread di input che già possiede il PMIF, dopo `npa_persistent_window` (fase 10) e **prima** di wake/WFSYS/ROM/FORCE_CAL. È lo stesso ordine di Linux: sequenza di accensione → VCN33 → `wmmcupwron`.

**Controlli preliminari (sola lettura):**
- `0x10006504` bit0=1 (modalità RC).
- `0x1be8==0x8000`, `0x1c0e==0x8000`.
- `0x1be2&3==0`, `0x1c08&3==0`.
- `0x1bee` bit5/6 = 0, `0x1c14` bit6 = 0.
- VOSEL (bit 11:8) di `0x1f98`/`0x1f9c` uguale a 0xF.
- Se uno qualsiasi fallisce: rifiuto, senza nessuna scrittura.

**Scritture (PMIF, read-modify-write con rilettura della parola intera, come `nw_pmic_write`):**
1. `0x1f98` bit 11:8 → 0xD, preservando gli altri bit.
2. `0x1f9c` bit 11:8 → 0xD.
3. `0x1be8 |= 0x40`, poi `|= 0x20` (canali 6 e 5, nell'ordine della sorgente).
4. `0x1c0e |= 0x40`.

**Dettagli e limiti:**
- La tensione va impostata *prima* dei bit di abilitazione. È un riordino deliberato rispetto a `mt6893_pmic.c:238-254`, così il rail non sale mai a 3,5 V.
- La scrittura a OP_CFG_SET con valore 0 non ha effetto: va solo verificata, non scritta.
- SW_LP è già 0: da verificare, non scrivere.
- **Nessun SW_EN** (`0x1be2` bit0, `0x1bf4` bit15, `0x1c08` bit0, `0x1c1a` bit15): nel ramo RC Linux non lo imposta.
- Gli indirizzi 0x1be8, 0x1c0e, 0x1f98 e 0x1f9c vanno aggiunti alla whitelist di scrittura; sono già nella whitelist di lettura (`patch:5174-5178`).
- Se una rilettura non corrisponde: stop prima dell'accensione WFSYS, stato mantenuto, nessun rollback (come la politica attuale dopo la fase 10).

**Diagnostica in sola lettura (registri PMIC già ammessi):**
- Registri: `0x1be6` e `0x1c0c` (bit0 B_EN, bit1 B_STB; `registers.h:20108-20115`), più `0x1c20`/`0x1c32` come riferimento VCN13/VCN18.
- Momenti di lettura: (a) subito dopo le scritture, (b) dopo FW_READY, (c) subito prima del FORCE_CAL, (d) inizio scan, (e) SCAN_DONE.
- Opzionale, senza MMIO aggiuntivo: due flag aggregati sul backup di calibrazione già in memoria (tutto-zero / tutto-FF), senza stampare i byte.

**Come leggere i risultati:**

| Esito | Interpretazione |
|---|---|
| MON=3 durante FORCE_CAL e scan, MDRDY/beacon > 0 | causa confermata |
| MON=3 ma RF ancora a zero | VCN33 necessaria ma non sufficiente; passo successivo: query statistiche EID 0x03 (`HANDOFF.md` punto 1) |
| MON=0 anche con le abilitazioni HW scritte | la richiesta RC Wi-Fi sul canale 6 non viene asserita; problema nella configurazione SRCLKEN-RC. **Non** forzare SW_EN come scorciatoia |

## (3) Evidenze mancanti
- **Stato di VCN33 dopo l'accensione:** non esiste in nessuna ricevuta. Si ottiene con la diagnostica (a)-(e) sopra.
- **Stato delle richieste PMRC del controller RC (opzionale):** gli offset dei registri di stato non sono nel DT live (`srclken@10006500` ha solo `reg`). Vanno presi da una sorgente o un DT mt6893 completo, oppure da un log Android con `srclken` (`mtk-srclken-rc-hw.c:245,693-711`). Non vanno indovinati.
- **Schematico Nord2:** servirebbe per sapere quali blocchi alimenta VCN33 sul board. Non serve per il build, ma servirebbe a confermare il legame causale.

Nessuna rete né alcun funzionamento Wi-Fi viene dichiarato finché il nuovo build non è stato provato sul dispositivo.
