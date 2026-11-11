#!/usr/bin/env python3
"""0330: edit the CI audio members of a COPY of the startup archive (called by build_p1_candidate.py).

The 24 Oct CI objects audio_i2c8.o / audio_hw.o (built from shim/nord2-xnu, never recompiled by the pipeline) refuse
the amplifiers while the CAM MTCMOS (SPM PWR_STATUS/_2ND bit 23) is on:
  audio_i2c8.o  na8_transfer16 stage 5   tbnz w11,#23 -> REFUSE(16)          (phone: i2c=5/16 i2cpa=10006170)
  audio_hw.o    amps_prepare (inlined bus_clock_prepare)  tbnz w8,#23 -> NAH_E_AMPS -> -3   (phone: i2c=16/0)
  audio_hw.o    bus_clock_restore        tbnz w8,#23 -> NAH_E_IO -> stop error -> uncertain=1 (sticky)
The camera powers CAM at its first session and keeps it on, so the speaker died for the rest of the boot. Each of the
three tbnz becomes a nop (fall through = the CAM-off path); nothing else changes. Same technique as 0296's sweet_usb.o
edit. The source equivalent is shim-0330.diff (and 0284's hunk in the stage audio_i2c8.c, 0330's in audio_hw.c).

    patch_audio_objs_0330.py AR ARCHIVE WORKDIR    -> prints a JSON receipt
"""
import hashlib, json, struct, subprocess, sys
from pathlib import Path

NOP = 0xd503201f
# member: (sha256 of the CI member, [(text offset, file offset, expected word, 3 preceding words)])
EDITS = {
    'audio_i2c8.o': ('c269b799dd9fcaa4cd5aa6ab9556b6ff7a30c9a57258757e5fadb24f7166db09',
                     [(0x8e8, 0xe40, 0x37b80a8b, (0xb9000109, 0xf94037e8, 0xb900010a))]),
    'audio_hw.o': ('552e4f63b9e5e4024692ed9190c6bf90f1ab3b9c6067955dbcdffcd633578885',
                   [(0x568, 0xac0, 0x37b80608, (0x35000660,0x294123e9, 0x2a080128)),
                    (0x23a0, 0x28f8, 0x37b80168, (0x350001c0, 0x294123e9, 0x2a080128))]),
}


def main():
    ar, archive, work = sys.argv[1], Path(sys.argv[2]).resolve(), Path(sys.argv[3]).resolve()
    work.mkdir(parents=True, exist_ok=False)
    members = subprocess.check_output([ar, 't', str(archive)], text=True).splitlines()
    rec = {}
    for name, (want, edits) in EDITS.items():
        assert members.count(name) == 1, name
        subprocess.run([ar, 'x', str(archive), name], cwd=work, check=True)
        data = bytearray((work/name).read_bytes())
        assert hashlib.sha256(data).hexdigest() == want, (name, 'not the 24 Oct CI member')
        for text_off, off, word, before in edits:
            assert struct.unpack_from('<I', data, off)[0] == word, (name, hex(text_off))
            assert struct.unpack_from('<3I', data, off - 12) == before, (name, hex(text_off), 'context')
            struct.pack_into('<I', data, off, NOP)
        (work/name).write_bytes(bytes(data))
        rec[name] = {'ci_sha256': want, 'sha256': hashlib.sha256(data).hexdigest(),
                     'nops_at_text': [hex(e[0]) for e in edits]}
    subprocess.run([ar, 'r', str(archive)] + [str(work/n) for n in EDITS], cwd=work, check=True)
    print(json.dumps(rec))


if __name__ == '__main__':
    main()
