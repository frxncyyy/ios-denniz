Final captured UFS evidence: all3 raw logs match result.json SHA256 and byte counts.

| Capture | Ownership s | Cmd s | Doorbell s | Held s | Map s | Copy s | Wait sum s | Read bytes | Commands |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| ce84_x1 | 47.720902 | 7.309914 | 6.965747 | 7.695039 | 0.986191 | 0.483964 | 24.465547 | 1949347840 | 41941 |
| 2e4f_x1 | 47.672765 | 7.584615 | 7.232668 | 7.987584 | 1.033663 | 0.487089 | 25.354834 | 1979334656 | 43028 |
| 3b2e_live_x2 | 48.425269 | 7.428871 | 7.068633 | 7.832082 | 1.054917 | 0.494366 | 20.237022 | 1993007104 | 45525 |

Retained x2 transaction/read proof succeeded before root publication; zero UFS command errors/timeouts at first-GUI report.

The live-x2 first-GUI ownership record is later than both previous x1 records; no boot wall improvement demonstrated.

Command/held time is slightly lower than2e4f but slightly higher thance84; bytes, request count, histogram and persistent RW filesystem state differ.

Mutex wait is an overlapping aggregate across callers, not removable boot wall time.

Map/copy are aggregate timer costs; do not sum overlapping/partially overlapping phases as wall time.

First-GUI is the kernel ownership record. Human separately confirmed lockscreen and controls functioning; onset time is unknown, so it supplies no boot timing.

Command traffic divided by total command time is mixed-workload telemetry, not sequential link throughput.

No CPU or DRAM execution frequency was measured; CNTFRQ is only the timebase.

Human separately confirmed: Sì, lockscreen e comandi funzionano. Onset time unknown.
