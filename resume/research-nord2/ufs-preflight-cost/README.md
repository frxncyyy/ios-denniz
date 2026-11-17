PREPARED ONLY. No image build or device operation performed.

Expected successful CI:39503015652; source870271596eb0a09bd5b2920981681bf8cca200fc. Each verifier requires completed/success run.json with exact headSha and exactly one kernel SHA256SUMS entry matching the downloaded kernel. No hash is guessed or borrowed from an older CI.

After parent downloads/verifies CI artifact, parent may run sequentially:

    sh research/nord2/ufs-preflight-cost/diagnostic/build-image.sh
    sh research/nord2/ufs-preflight-cost/ios-profile/build-image.sh

These are offline build commands only. They create no device runners. Existing image/build log/final manifest evidence is never overwritten implicitly.

Diagnostic args byte-equal previous72ad RO stop-only candidate (13 required exact gate tokens included). Observer args equal0956 normalRW candidate with nord2zoomreset token OMITTED and nord2wifiemi=0. No zoom-reset=0 token is introduced. Firmware verifier, radio2, input,8cores, fastlog, RTCskip, profile and boundedFBdiag retained. Static template test validates these precise differences against stored prior manifests.

Independent unpack verifies original Linux bytes+BSS, appended/header Android DT, tested rescue rawramdisk, Androidv2 headerallowedchanges+SHA1ID, complete608-byte commandline including zero padding, CI/source/hash identity. It also independently strips the pinned CI MachO using same deterministic stripflags, resolves expected fixups and compares every unpacked payload segment address/size/byte plusentry/count. This additional segment algorithm was checked successfully against prior0956 payload and its corresponding CI kernel. mkpayload parser/fixup code is reused for expected transformation; this is not a second independent implementation of chainedfixups.

Final manifest is generated only after actual successful verification. It records requested strategy/preflight counters but no hardwaretest, speedup, lane-success or measuredruntimeclock claim. RO diagnostic still alwayshalts before root; observer may write normal iOS userdata, but contains no lane experiment, EMI query or preference-reset request. Existing GUI/firmware hardware evidence belongs to prior candidate only.

Tests: python research/nord2/ufs-preflight-cost/test-templates.py
