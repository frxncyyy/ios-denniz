# Fotocamera Nord 2 (MT6893): analisi su Android stock (2026-11-09)

Analisi fatta sul telefono con Android + KernelSU (kernel 4.19.325-cip127-st11 #4, sorgente
github.com/<github-user>/android_kernel_oplus_mt6893 branch KSUN-RUI-susfs-v2.2.0-manual-hook).
Tutto misurato sull'hardware, non dedotto: ogni valore qui sotto ha un log in `logs/` o in
`imx766/`, `imx615/`, `imx355/`.

## Strumento: modulo n2d.ko (tools/)
- Modulo one-shot caricato con insmod, senza modversions (force-load) e senza puntatore exit.
  Il layout di `struct module` di questo kernel ha init a 0x178, size 0x3c0 ed exit NON a 0x360
  (diverso dallo stock 4.19.191).
- Servono le sezioni vuote `.plt`, `.init.plt` e `.text.ftrace_trampoline`.
- Esegue un blob di op patchato nel .ko (`n2d.py script.txt tag`). Gli indirizzi kernel vengono
  da /proc/kallsyms (kptr_restrict=0) a ogni run, quindi il KASLR non è un problema.
- Op disponibili:
  - R/W/X/G: lettura, scrittura, mask e guard su indirizzi fisici, via __ioremap.
  - K/P: lettura di memoria kernel.
  - C/CALLR/SET/ADD/LD: chiamate a funzioni kernel con registri.
  - F/Q: fill e check di un buffer.
  - NZ/Z: abort.
  - WATCH/WATCHR: campiona registri a 13 MHz e logga i cambi.
  - WAITCAM: attende che CAM sia acceso e accessibile.
- Uprobe su `/vendor/lib64/libcamdrv_isp.so`. Offset: IspDrvCamsv::writeReg 0x28d88,
  writeRegs 0x28c08, setDeviceInfo 0x29928, ISP_DRV_CAM::writeReg 0x24bac.
  Catturano OGNI scrittura dell'HAL, anche ai registri write-only
  (`logs/camsv_hal_writes_ordered.txt`).
- Attenzione: leggere camsys senza MDP e CAM accesi e la bus protection rilasciata provoca una
  DEVAPC violation ("MDP_S_S-44, power/clock is not enabled"), quindi panic.

## 1. Alimentazione: serve il dominio MDP
- PWR_STATUS (SPM 0x1000616c) durante lo streaming vale 0x17987fff. Sono accesi:
  - MDP (bit 19), DIS (20), CAM (23) e RAWA/B/C (24-26);
  - ISP/ISP2/IPE (12-14), usati dalla pipeline di Android ma non servono per CAMSV.
- **MDP è indispensabile**:
  - larb13 esce su mdp_smi_common (0x1f002000);
  - larb13 e larb14 passano entrambi dal sysram_smi_common (0x1f022000), che sta nel dominio MDP;
  - perfino i registri camsys (0x1a000000) si raggiungono attraverso il bus MDP (DEVAPC lo prova);
  - il device tree elenca `SCP_SYS_MDP` per primo tra i clock di seninf_top e camsys;
  - il driver ISP (camera_isp.c, Prepare_Enable_ccf_clock) segue questo ordine:
    smi_bus_prepare_enable(larb13/14/16/17/18) → SCP_SYS_MDP → CAM → RAWA/B/C → CG larb →
    SENINF → CAMSV.
- Bus protection in streaming:
  - 0x258 bit22 = 0;
  - MM_EN 0x2d0 = 0x03000000;
  - MM_STA 0x2ec = 0x55000800 / 0x57000800;
  - PROT_2 0x710 = 0x18 e STA 0x724 = 0x18 o 0xf8: il bit1 (CAM step1_0) viene rilasciato
    quando MDP è acceso. Su iOS restava a 1.
- Maschere da clk-mt6885-pg.c (k419): MDP_PROT_STEP1_0 = PROT_1 bit10. Gli altri step sono
  bit sparsi in MM.
- CG di MDP: mdpsys CG1 0x1f000110 = 0xffffeeed. Sono aperti solo APMCU_GALS (bit1), SMI0 (4),
  SMI1 (8) e SMI2 (12).
- CG di camsys 0x1a000000 = 0x3c032: sono aperti larb13/14/15, CAM, CAMTG, SENINF, CAMSV0-3
  e CCU0.

## 2. Configurazione SMI (smi_conf.h mt6885, verificata con il readback)
- Catena di larb13: mdp common (unità 22) + mdp-subcom (26, 27) + sysram (23) + cam-subcom (29)
  + cam-subcom2 (31).
- Catena di larb14: disp (21) + disp-subcom (24, 25) + sysram + cam-subcom1 (30) + cam-subcom2.
- Valori dei common:
  - L1LEN 0xb, BUS_SEL 0x4514, M4U_TH 0xe100e10, FIFO_TH1/2 0x506090a, DCM 0x4f1, DUMMY 1;
  - sysram: L1LEN 2, BUS_SEL 0x1111;
  - subcom: L1LEN 0xa, PREULTRA_MASK1 0x2105, DCM 0x4f1.
- larb13/14: CMD_THRT_CON 0x370256, SW_FLAG 1, DBG_CON 1. OSTDL per porta da
  smi_larb13/14/15_init_pair. Readback completo in `imx766/g3_stream.log`.
- Clamp (0x3c0) a 0 su tutti i common.

## 3. IOMMU e porte
- Su Android tutte le porte cam hanno NON_SEC_CON = 0xaa01: MMU on, bank 2, quindi IOVA = 0x2_xxxxxxxx.
- larb13/16/18 stanno su iommu1 (MDP, 0x1f027000); larb14/17 su iommu0 (DISP, 0x1411a000).
- **Mappa CAMSV hw → porta, misurata dai translation fault**:
  - CAMSV 0x1a090000 → `L14_CAM_CAMSV0` (larb14 porta 3, reg 0x1a00238c);
  - CAMSV 0x1a092000 → `L13_CAM_CAMSV4` (larb13 porta 6, reg 0x1a001398).
  - Il nostro driver iOS spegne l'MMU su tutte le 32 porte di larb13-15, quindi è coperto.
- **Bypass funziona**: con MMU_EN=0 sulla porta giusta e un indirizzo fisico in 0x420, CAMSV0 ha
  scritto il frame nel buffer fisico (PA 0xdd240000) senza fault.
- **0x424 NON è l'MSB dell'indirizzo**: 0x420=0x40000000 con 0x424=1 ha prodotto un fault a
  IOVA 0x2_40000000. I bit alti vengono dai bank della larb.
  - Il bypass con PA sopra i 4 GB non è stato provato: su Android è rischioso, perché un alias
    sbagliato scriverebbe nella memoria del kernel.
  - **Per iOS: buffer sotto i 4 GB.**

## 4. Sensori, CSI e SENINF (snapshot completi in imx766/, imx615/, imx355/)

| Sensore | PHY | Analog | SENINF | mux | Dimensione |
|---|---|---|---|---|---|
| IMX615 (front) | CSI0, D-PHY 4 lane | 0x11c80000 | SENINF1 (0x1a004000) | mux1 | 3264×2464 |
| IMX355 (ultrawide) | CSI1, D-PHY | 0x11c84000 | SENINF3 (0x1a006000) | mux1 | 3264×2448 |
| IMX766 (principale) | **CSI2, C-PHY** | 0x11c88000/9000, cphy 0x11c8a000, csi 0x11c8b000 | **SENINF5** (0x1a008000) | mux1 + mux2 | vedi sotto |

- Ogni porta CSI occupa 0x4000: ana A +0, ana B +0x1000, cphy +0x2000, dphy/csi +0x3000.
- SENINF_TOP 0x1a004010: un byte per mux, con il valore = indice del seninf sorgente.
  IMX766: mux1 e mux2 ← seninf5 (valore 4).
- IMX766 sui due mux:
  - mux1: DT 0x2b, 4096×3072, pixel mode 3, verso CAM_A;
  - mux2: DT 0x30/0x31 PDAF, 2560×768, pixel mode 2, verso CAMSV2/3.
- Cammux (base 0x1a004400):
  - CTRL_0..3: sorgente per cammux;
  - EN 0x10; CHK_EN 0x18;
  - OPT 0x20+4i: bit0 VC_EN, bit1 DT_EN, DT a bit 16 (0x002b0003 = RAW10);
  - CHK_CTL_0 0x100+0x10i: pixel mode, 3 per CAM_A e 2 per CAMSV;
  - CHK_CTL_1: dimensione attesa.
  - CAMSV i ↔ cammux i+3.
- Pixel mode: mux in modo 2 con TG CAMSV a 1 pixel (SEN_MODE senza bit1) misura la larghezza
  giusta. Mux in modo 3 richiede il TG a 2 pixel.

## 5. Sequenza CAMSV (catturata con le uprobe e replicata)
Il punto che mancava è il **fronte su MODULE_EN bit 30 dopo TOP_EN**. Senza quel fronte il TG
riceve i frame ma la DMA non si arma mai: 0x400 bit0 resta a 1, 0x48 resta a 0 e FBC si ferma
a "pending".

```
TG:   0x130=0x10000; 0x194=0x19; 0x1b0=1; 0x1a4=0; 0x134=0; 0x130=0x12000;
      0x138=(w<<16); 0x13c=(h<<16)     (righe di grab = righe misurate - 1)
SW:   0x20=0,1,(poll &3==3),4,0
TOP:  0x10=0x50000001→0x40000001; 0x5c=0; 0x58=0; 0x14=FMT; 0x18=0x31871; 0x10=0x40000005
      0x4c=PAK (0x48 per 8 bit/PDAF, 0x49 per RAW10); 0x2d0=0xe0010; 0x800=0; 0x7b0=0;
      0x7c4=~0; 0x418=0x61000000
FBC:  0xa00=0, 0x800
DMA:  0x430=xsize-1; 0x434=ysize-1; 0x438=stride|0x08070000; 0x448=0; 0x43c=0x80000080;
      0x440=0x400030; 0x444=0x200010
      0x130|=1 (CMOS_EN); 0xa00=0x800,0x8800,0x8c00; 0x10=0x40000015; 0x800=1
ENQ:  0x420=buf; 0x804=header (64 B); 0x2c=0x11    (0x420 viene letto al momento del RCNT_INC)
EN:   0x30=1,5,0x8005; 0x10=0x15; 0x10=0x40000015 (FRONTE); 0x134=1 (VF, su Android lo fa il kernel)
```

- FBC CTL2 (0xa04): [7:0] enqueue, [15:8] done, [23:16] pending.
- 0x48: bit1 = DMA armata, bit0 = burst in corso.
- INT_STATUS: bit 0/1/2/3/6 = VS/TG/EXP/SOF; 4, 5, 16, 17 = errori.
- **Prove riuscite**:
  - CAMSV0 da fresco, IOVA mappato, PDAF → 2560×64 + header;
  - CAMSV0 in bypass, PA 0xdd240000, PDAF → OK;
  - CAMSV0 in bypass con **RAW10 immagine dal sensore frontale** (mux2 configurato da noi,
    pixel mode 2, TG a 1 pixel, FMT=1, PAK=0x49) → 40 righe × 4080 byte, dati densi.
    Con PAK=0x29, il valore attuale di iOS, i dati escono intervallati da zeri: è sbagliato.

## 6. Cosa cambiare in iOS (nord2_seninf.inc / nord2_camsv.inc)
1. **Accendere MDP**:
   - SPM MDP_PWR_CON e rilascio della MDP bus protection, prima di CAM;
   - topck mdp_sel;
   - CG di mdpsys (CG1 bit 1, 4, 8, 12);
   - scrivere le conf pair SMI (mdp common, sysram, subcom, larb).
   - Poi accendere CAM e rilasciare la sua protezione: lo step1_0 (PROT_2 bit1) a quel punto
     dovrebbe andare a 0.
2. **Sequenza CAMSV identica al paragrafo 5**, con il fronte su bit 30 e l'enqueue in ordine
   0x420 → 0x804 → 0x2c.
3. **PAK 0x49 per RAW10.** Per il path immagine: mux in pixel mode 2 e TG a 1 pixel
   (oppure modo 3 con TG a 2 pixel).
4. **Buffer sotto i 4 GB.** Non scrivere 0x424 come MSB.
5. IMX766: CSI2 C-PHY → SENINF5 → mux, con i valori di `imx766/ana2.log` e `imx766/sen_8.log`
   come riferimento per il ricevitore.
6. Mappa delle porte: lasciare l'MMU spenta su tutte le porte di larb13, 14 e 15.
