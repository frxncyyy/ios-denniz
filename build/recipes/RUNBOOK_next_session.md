# Runbook: prossima sessione sul telefono (scritto 2026-11-15 00:05)

Stato: telefono fuori dall'USB del PC dalle 19:40 del 14/11. Kernel attuale sul telefono: q10jfin (GNSS+aptico) + payload fin1, sbshift2, api99a, final3, prod9, shader99b, gnssfix22. Tutto il resto sotto e' SOLO host-built/host-tested.

Sequenza (una cattura per passo; nuovo TAG ogni volta; READS<=600; un solo txn alla volta; il telefono va prima in fastboot: `python3 autotest/phone.py reset fastboot`):

0. Diagnosi pre-intervento (opzionale): `python3 autotest/phone.py logs OUT --seconds 0`.
1. SESSIONE 1 (prestazioni: kernel A + clock attivo + userland integrato):
   `cd gpu/build && VARIANT=gfxall5 READS=600 gfx200-20261114/shipall/txn_gfxall.sh $PWD/device-mmu-q10jfinal2f-candidate/ui-sched/nord2-ios-metal-q10jfinal2f-ui654fb60dvfs-cap3500-ram5088-sched.img perf2a`
   Poi riprodurre i passi di prova con l'utente (home, App Library, Mappe, Magnifier, Safari, selettore app, Control Center, uso libero) mentre gira la cattura (autocycle/runs/perf2a).
   Leggere: `python3 perf-20261114/tools/n2perf.py <log>`, `python3 gpusched-20261114/tools/gpuprof.py <log>`, `python3 cpu-20261114/tools/cpuplat_check.py <log>`; marker: NORD2 GPU JSNEXT/PIPE, GDVFS switch 886000, LVTS, CPUPLAT, np pipe v4, np bigpool, WEBXR, GFX100A, GFX200, GFXALL3.
   Se hang/glitch: `ENTER=1 gnss-20261114/sweetlog_tail.sh OUT 400`, poi fallback kernel q10jfinal1c (n2gdv=25) o q10jall3; userland: VARIANT=gfxall5-off / -nov4 / -nopush; UNDO=1.
2. SESSIONE 2 (step B, policy L2): solo kernel `q10jperf3` (flash diretto: `autocycle/to_fastboot.sh 120; autocycle/flash_boot.sh <img>`), n2gdv=9.
3. SESSIONE 3 (step C, tensioni vendor + power-off core): kernel `q10jperf4`.
4. SESSIONE 4: sonda MDP `q10jmdp0` poi `q10jmdp1`; verdict: `python3 mdp-20261114/mdp_verdict.py <log>`.
5. RAM: `q10jram8a` -> `q10jram8b` (pool grafico +585 MiB) oppure fase 2 `q10jram8c0` -> `c1` -> `c2` (RAM normale extra). NON combinare ram8a/b con ram8c. Le immagini ram8* sono su base q10jfinal1: rebase su final2 da fare prima del merge finale.
6. Dopo una sessione d'uso: `gfx200-20261114/shipall/seed_pancache.sh` poi VARIANT=gfxall5-seeded (se gfxall5 gia' installato: UNDO=1 VARIANT=gfxall5 prima).

Altri in sospeso: Wi-Fi ritardo 7 s, Meteo rimandato, gnssfix22 e' residente: se n2mnld riparte serve riavviare il telefono; kernel q10jfinal2* non contiene ancora le patch ram8/mdp.
Utente ha confermato: status bar OK (sbshift2), aptico OK (6a), GNSS: fix valido, Mappe vede la posizione.
