# iOS Wi-Fi scan-service image stage

Current phone: healthy VCN33 6a6a image, no collector. Candidate source52bb4e7,
CI39690505727 in progress. This stage has never accessed/flashed the device.
All exact-image approval fields remain false until direct human approval.

The candidate preserves existing iOS,8cores,UFSx2,display,input,USB and calibration.
Explicit nord2wifiios=1 adds software wlan ADT, real carrier-down enN and an
entitled IOUserClient. A completed startup scan hands the sole radio owner to
a serialized queue for fresh passive scans requested by iOS. Association,
security,key installation,data TX/RX and IP networking are not implemented.
Settings compatibility is not inferred from source or synthetic host tests.

After verified CI and image preparation, ask exact boot-only authorization with
healthy6a6a and RAM-rescue fallback. Only run this stage's one-use runner.
Do not reuse any historical runner or boot stock Android over iOS userdata.
The collector will allow300seconds for boot and real Settings interaction.

parse-backend.py checks the initial26commands/VCN33/RXRF and the exact new
IOS OWNER terminal (old parked-CAP receipt is rejected). parse-ios-service.py
summarizes actual numeric registry/client/enqueue/completion/event/result
observations separately. Missing live requests are not successful scans;
malformed or duplicate completion evidence cannot count as success.
The final human observation must separately establish lockscreen/input health
and whether Settings displays Wi-Fi scan results. No association claim.
