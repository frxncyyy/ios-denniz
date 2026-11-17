# Nord2 playback container evidence (2026-10-23)

Downloaded with GitHub blob API from primary device community repository
https://github.com/oneplus-mt6893-devs/android_vendor_oneplus_denniz
branch lineage-18.1, commit f57d728c7ebf02db7a2fb84d4e25cc34eda98018.
Commit message: `denniz: Init vendor`, `From sys_oplus_mssi_64_cn-user 11 RP1A.200720.011 1640787928920 release-keys (A16)`.
This is a known stock Nord2 board container, NOT a dump from this physical phone.

* proprietary/odm/firmware/tfa98xx.cnt, Git blob ef1341ff66b0c2dfee66058fc35b54db2337f70c, 4163 bytes, SHA256 635300fbc96f258ff9a35f82435f2e9d3ff6f1370c156067b40146e011038692.
* proprietary/odm/firmware/tfa98xx_t0.cnt, Git blob f6c48eb62a149a90f66343fe8af250db3eedb5f3, 4171 bytes, SHA256 83961f27f04e4204a5bda7511153fff998dd8e026d0d77c06ead53ae2bfd0cb4.

Normal container matches live DTS I2C8 addresses34/37; t0 uses34/35 and must not be used.
Both PM3_01 headers and entire-file CRC32 verified. Both embedded PA1_00 patches
have independent valid CRC32, 42-byte total length and six-byte revision check
74ffff000000 (family74, no DSP patch). No 9873 equivalence is established.

Parser layout/CRC is sourced from selected kernel audio/tfa98xx/tfa98xx_parameters.h
and tfa_container.c. No-data ext_dsp0 path writes register and bitfield descriptors
only. Normal device list has50 register operations plus patch marker; speaker
profile has5 register operations plus default marker. Generator omits constant
unlock operations0f/a0/a1 because hardware exact-silicon init owns dynamic FBxor5a
handshake. Remaining52 operations peramp preserve source order and masks, delays0.
No OTP/MTP command/data operations are emitted. No gain invented. Profiles speaker,
receiver, MMI and calibration remain in JSON, but only speaker is emitted.

Container does not encode AUDFS; runtime48k must use AUDFS8 from selected Linux
sample-rate mapping. Serial32bit slots are independently fixed by selected Android
mt6885-mt6359.c mt6885_i2s_hw_params_fixup S32_LE. Raw NBCK2 field enum meaning
is not independently established; do not cite guessed2=>64fs as proven. Source
container slot-length/sample-width fields are31=>32bit. Pslot1, Qslot0.

Compile callback only supports source init revisions0a74,0b74,0c74, as selected
kernel tfa9874_specific does. Unknown/9873 silicon must fail closed. Runtimemust
own I2Clease, keep PWDN1/AMPE0, perform exact silicon init/unlock then profile,
set rate and enable serial, then enableamp with bounded clock checks. Hardware
agent integrates this sequencing. No flash, USB, reboot or device write performed.

Verification: parser accepts both real containers; rejects every truncation of
4163-byte normal container, CRC corruption and recomputed-CRC out-of-range pointer.
Generator asserts pinned SHA, addresspair, exactspeaker selection and allowlist.
C syntax checked with host cc -DNORD2 -std=c11 -Wall -Wextra -Werror againsthardwareheaders.

## Canonical-ready runtime decoder

`audio_board.c`, `audio_board.h`, `audio_board_offsets.h` replace compiled firmware
values. Root ADT wrapper must compute SHA256 over the exact immutable property
bytes then pass blob/length/digest to nah_board_load on audio worker before init.
Do not trust a digest supplied alongside data by userspace. Loader compares pinned
SHA and length4163, validates minimal PM3 identity/address pair and decodes104
bounded bitfields using fixed payload offsets. Exact SHA authenticates graph
identity, so no attacker-controlled pointer traversal is needed. Runtime plan is
private static boot state; getterNULL until full successful load; no reload.
Loaded values also drive generic transport register/change validation APIs,
replacing audio_profile_allow.h which otherwise contains proprietary values.
Kernel source therefore contains only digest, public field-layout code, offsets,
and API. Raw4KiB blob is embedded privately into ADT/chosen/nord2-audio-board.

New test_board_loader.py builds generic decoder in a temporary shared library:
104 operations exactly equal Python-decoded expectedmask/value; alltruncations,
baddigest and reload rejected; supportedrevision/addresspairs accepted and
9873/unknownrevision/t0address rejected. StrictC11-Wall-Wextra-Werror syntaxPASS.
Old audio_profile.c and sourced_profile.h remain reference evidence only and
must not be compiled together with runtime audio_board.c (samegetter symbol).
