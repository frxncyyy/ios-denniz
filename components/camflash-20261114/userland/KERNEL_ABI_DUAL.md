# ABI kernel per la camera doppia (kernel q10hj, patch 0292; aggiornata per q10hq, patch 0294)

Scritto dall'agente kernel. Vale per il kernel del candidate `device-mmu-q10hj-candidate`.
Sorgente di riferimento: `gpu/build/camdual-20261110/b/bsd/dev/nord2_camdev.inc`.

## Nodi

| Nodo | Istanza | Sensore di default | Pipe hardware |
|---|---|---|---|
| `/dev/redwood-camera`  | 1 | 1 (IMX766, wide)       | SENINF mux1 -> cam mux 3 -> CAMSV0 (come prima) |
| `/dev/redwood-camera2` | 2 | 3 (IMX355, ultra-wide) | SENINF mux2 -> cam mux 5 -> CAMSV2 |

- Le due istanze sono indipendenti: ciascuna ha la propria sessione, la propria coda di buffer, la propria
  esposizione, il proprio contatore `sequence` (riparte da dove era, per istanza).
- Stessa identica ABI sui due nodi: stessa `struct redwood_camera_frame` (120 byte), stessi comandi, formato 4.
- I due nodi possono trasmettere sensori diversi nello stesso momento. Qualunque sensore (1, 2, 3) può essere chiesto
  a qualunque nodo, purché non sia in uso dall'altro.
- Con un solo nodo aperto il comportamento è quello del kernel q10hi.
- Un solo lettore per nodo alla volta (una seconda `read` contemporanea sullo stesso nodo dà `EBUSY`, come prima).

## Sessione (invariato, ma per nodo)

- La sessione di un nodo parte alla prima `read` su quel nodo e finisce quando nessuno legge quel nodo per 4 s, quando
  il nodo viene chiuso da tutti, o quando gli viene chiesto un altro sensore.
- Per tenere vivo un sensore che non è a schermo basta leggerne l'header (vedi sotto) più spesso di una volta ogni 4 s;
  per non perdere l'andamento dell'esposizione conviene farlo a ogni frame o quasi.

## Comandi (`write` di `struct redwood_camera_command`, 24 byte)

| Comando | Valore | Argomenti | Note |
|---|---|---|---|
| `REDWOOD_CAMERA_EXPOSURE` | 1 | a righe, b guadagno (256 = 1x), c righe di frame (0 = minimo) | per nodo |
| `REDWOOD_CAMERA_KEEP`     | 2 | - | sempre `ENOSPC` (non implementato) |
| `REDWOOD_CAMERA_SENSOR`   | 3 | a = sensore (1, 2, 3) | per nodo; vedi errori |
| `REDWOOD_CAMERA_FOCUS`    | 4 | a = posizione 0..focus_max | ha effetto solo sul nodo che sta trasmettendo il sensore 1 |

`REDWOOD_CAMERA_SENSOR`:
- `0`: accettato. I frame del nodo si fermano, il sensore vecchio si spegne, la `read` successiva avvia il nuovo.
- `EBUSY`: il sensore chiesto è in uso dall'altro nodo, cioè l'altro nodo è aperto e lo ha come sensore richiesto,
  oppure lo sta trasmettendo. Nessun cambiamento.
- `EINVAL`: numero di sensore sconosciuto.

Si può scrivere `SENSOR` prima della prima `read`: così il nodo non accende il suo sensore di default per poi cambiarlo.

## Lettura

- `read` di `header_bytes + stride * height` byte: header + frame, formato 4 (buffer della DMA così com'è).
- `read` di soli `sizeof(struct redwood_camera_frame)` byte (120): **solo header** del frame più recente non ancora
  consegnato. Non copia l'immagine e non invalida la cache del frame: costa pochi microsecondi. Il frame viene
  considerato consegnato (la `read` successiva aspetta il frame dopo).
- Errori:
  - `EAGAIN`: nessun frame entro 3 s a sessione avviata, o entro 20 s all'avvio (come prima).
  - `EBUSY`: (nuovo) il sensore di questo nodo è in trasmissione sull'altro nodo, quindi la sessione non può partire
    (succede solo se si apre il nodo 2 col suo default mentre il nodo 1 ha già preso il sensore 3, o viceversa);
    oppure un'altra `read` è in corso sullo stesso nodo.
  - `EIO` non viene più restituito (dal kernel q10hc non c'è più la disattivazione permanente).

## Header: campi nuovi (ex `reserved[0..3]`)

```c
struct redwood_camera_frame {
	char     magic[8];              /* "RWFRAME1" */
	uint32_t header_bytes, width, height, stride, format, bayer, black;
	uint64_t sequence, time;
	uint32_t exposure_lines, line_ns, gain, gain_unity, gain_max;
	uint32_t frame_lines, frame_lines_min, frame_lines_max, margin, sensor;
	uint32_t focus, focus_max;
	uint32_t stat_samples;          /* NUOVO: numero di celle 2x2 campionate (64); 0 = statistiche assenti */
	uint32_t stat_green;            /* NUOVO: media del verde sulle celle, in passi a 10 bit SOPRA il nero (0..1023-black) */
	uint32_t stat_red;              /* NUOVO: idem per il rosso */
	uint32_t stat_blue;             /* NUOVO: idem per il blu */
};
```

- Le statistiche sono calcolate dal kernel a fine frame su una griglia 8x8 di celle Bayer 2x2 distribuite
  uniformemente su tutto il frame (centri a (2i+1)/16 della larghezza e dell'altezza). Il verde è la media dei due
  verdi della cella. Il nero (`black`) è già sottratto, con minimo 0. Nessuna correzione di guadagno digitale.
- Sono presenti in ogni header, anche nelle letture complete. Servono per regolare l'esposizione dello stream non
  visualizzato leggendo solo l'header.
- `focus_max` è 1023 solo sul nodo che trasmette il sensore 1 con l'attuatore attivo; altrimenti 0.
- Non restano campi riservati: la struct è sempre di 120 byte.

## Timbro del frame (`time`) - cambiato dal kernel q10hq (patch 0294)

- `time` è l'istante di **inizio del frame** secondo l'hardware, in unità di `mach_absolute_time()`: il contatore a 13 MHz
  che CAMSV scrive nell'header DMA di ogni frame (lo stesso contatore di sistema su cui gira `mach_absolute_time()`),
  riportato a 64 bit. È lo stesso orologio per i due nodi: i `time` dei due stream sono direttamente confrontabili.
- Prima (fino a q10hn) `time` era l'istante in cui il kernel si accorgeva della fine del frame (polling a 3 ms): cadeva
  circa un tempo di lettura del sensore (circa 32 ms) più 0-3 ms DOPO il valore di adesso. Chi calcola "età del frame"
  vedrà quindi circa 32-33 ms in più.
- Risoluzione 1/13 MHz; nessuna dipendenza dal carico del kernel.
- Se il contatore non è utilizzabile (non atteso), `time` torna a essere l'istante in cui il frame è stato visto finito,
  come prima; il kernel lo dice nelle righe dei primi tre frame di sessione ("N not stamped by it").

## Esposizione: quando viene applicata (q10hq)

- Il kernel scrive solo i registri che cambiano e uno per giro del suo ciclo (circa 6 ms a registro): un comando
  `EXPOSURE` completo arriva al sensore in 25-50 ms. I campi `exposure_lines`, `gain`, `frame_lines` dell'header passano
  al valore nuovo solo quando la scrittura è finita. Un comando arrivato mentre il precedente è in scrittura aspetta;
  se ne arrivano più d'uno, vale l'ultimo.

## Limiti noti

- Lo stesso sensore non può andare su due nodi.
- La seconda pipe (mux2 -> cam mux 5 -> CAMSV2) funziona sull'hardware (run q10hp: i due sensori posteriori insieme).
- Uno stream che nessuno legge resta acceso per 4 s dall'ultima `read` (anche solo header), poi la sessione si chiude;
  i frame non letti vengono sovrascritti.
