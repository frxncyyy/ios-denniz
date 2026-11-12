#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Scripted vcodecd for the n2vcd_cli test (host only).

Speaks the frozen datagrams of include/n2vcd_proto.h with literal replies.  Buffers are the P03 socket transport's
files: <socket>.buf/<decimal handle>.  For every ENC_FRAME it reads the input buffer and compares it with the NV12
frames of the .yuv file, then writes a canned Annex B access unit into the output buffer.

Modes:
  enc   HELLO, OPEN, ENC_FRAME xN, ENC_SET, FLUSH, CLOSE
  info  HELLO, then wait for the client to disconnect

Exit 0 only if every check passed; writes a JSON result to --result.
"""
import argparse
import json
import os
import socket
import struct
import sys

MAGIC = 0x4443564E
VERSION = 1
# enum n2vcd_type, include/n2vcd_proto.h: 9-12 are DEC_HEADER(_OK) and DEC_FRAME(_OK)
HELLO, HELLO_OK, OPEN, OPEN_OK, ENC_FRAME, ENC_FRAME_OK, ENC_SET, ENC_SET_OK = 1, 2, 3, 4, 5, 6, 7, 8
FLUSH, FLUSH_OK, CLOSE, CLOSE_OK, ERROR = 13, 14, 15, 16, 100
CODEC_H264 = 0x31637661
SESSION = 42


class Fail(Exception):
    pass


def hdr(t, seq, session):
    return struct.pack("<IIII", MAGIC, t, seq, session)


def align16(v):
    return (v + 15) & ~15


class Server:
    def __init__(self, args):
        self.a = args
        self.frames = 0
        self.last_seq = 0
        self.bufdir = args.socket + ".buf"

    def recv(self, c):
        data = c.recv(4096)
        if not data:
            raise Fail("client closed the connection")
        if len(data) < 16:
            raise Fail("short datagram %d" % len(data))
        magic, typ, seq, session = struct.unpack_from("<IIII", data, 0)
        if magic != MAGIC:
            raise Fail("bad magic 0x%08x" % magic)
        if seq != self.last_seq + 1:
            raise Fail("seq %u, expected %u" % (seq, self.last_seq + 1))
        self.last_seq = seq
        return typ, seq, session, data

    def send(self, c, data):
        c.sendall(data)

    def hello(self, c):
        typ, seq, session, data = self.recv(c)
        if typ != HELLO or session != 0 or len(data) != 24:
            raise Fail("HELLO: type=%u session=%u len=%u" % (typ, session, len(data)))
        version, reserved = struct.unpack_from("<II", data, 16)
        if version != VERSION or reserved != 0:
            raise Fail("HELLO version=%u reserved=%u" % (version, reserved))
        ok = hdr(HELLO_OK, seq, 0) + struct.pack("<IIIIII", VERSION, 3, 0, 4096, 2160, 0)
        self.send(c, ok)

    def open(self, c):
        typ, seq, session, data = self.recv(c)
        if typ != OPEN or session != 0 or len(data) != 80:
            raise Fail("OPEN: type=%u session=%u len=%u" % (typ, session, len(data)))
        (codec, encoder, width, height, fps_num, fps_den, bitrate, gop, profile, level, flags) = \
            struct.unpack_from("<11I", data, 16)
        reserved = struct.unpack_from("<5I", data, 60)
        if codec != CODEC_H264 or encoder != 1:
            raise Fail("OPEN codec=0x%08x encoder=%u" % (codec, encoder))
        if (width, height) != (self.a.width, self.a.height):
            raise Fail("OPEN size %ux%u, expected %ux%u" % (width, height, self.a.width, self.a.height))
        if fps_num != 30 or fps_den != 1 or bitrate != 1000000:
            raise Fail("OPEN fps=%u/%u bitrate=%u" % (fps_num, fps_den, bitrate))
        if gop != 0 or profile != 0 or level != 0 or flags != 0 or any(reserved):
            raise Fail("OPEN gop=%u profile=%u level=%u flags=%u reserved=%s" %
                       (gop, profile, level, flags, reserved))
        self.stride = align16(width)
        self.rows = align16(height)
        self.in_bytes = self.stride * self.rows * 3 // 2
        self.out_bytes = 65536
        ok = hdr(OPEN_OK, seq, SESSION) + struct.pack("<IIII", 0, self.in_bytes, self.out_bytes, 0)
        self.send(c, ok)

    def buffer_path(self, handle):
        return os.path.join(self.bufdir, str(handle))

    def frame(self, c):
        typ, seq, session, data = self.recv(c)
        if typ != ENC_FRAME or session != SESSION or len(data) != 48:
            raise Fail("ENC_FRAME: type=%u session=%u len=%u" % (typ, session, len(data)))
        in_handle, stride, out_handle, cap = struct.unpack_from("<IIII", data, 16)
        pts, force_key, reserved = struct.unpack_from("<QII", data, 32)
        if stride != self.stride or cap != self.out_bytes:
            raise Fail("ENC_FRAME stride=%u cap=%u (want %u/%u)" % (stride, cap, self.stride, self.out_bytes))
        if reserved != 0:
            raise Fail("ENC_FRAME reserved=%u" % reserved)

        # The .yuv file is the source of truth; frame N is compared byte for byte (width 64 -> stride 64).
        with open(self.a.yuv, "rb") as f:
            f.seek(self.frames * self.in_bytes)
            want = f.read(self.in_bytes)
        if len(want) != self.in_bytes:
            raise Fail("yuv has no frame %u" % self.frames)
        with open(self.buffer_path(in_handle), "rb") as f:
            got = f.read(self.in_bytes)
        if got != want:
            raise Fail("input buffer %u does not hold frame %u" % (in_handle, self.frames))

        au = b"\x00\x00\x00\x01\x09\x10" + bytes([0x30 + self.frames])
        with open(self.buffer_path(out_handle), "r+b") as f:
            f.write(au)
        key = 1 if self.frames == 0 or force_key else 0
        reply = hdr(ENC_FRAME_OK, seq, SESSION) + struct.pack(
            "<IIIIQII", 0, len(au), key, 1000 + self.frames, pts, 0, 0)
        self.send(c, reply)
        self.frames += 1

    def enc_set(self, c):
        typ, seq, session, data = self.recv(c)
        if typ != ENC_SET or session != SESSION or len(data) != 32:
            raise Fail("ENC_SET: type=%u session=%u len=%u" % (typ, session, len(data)))
        bitrate, fps_num, fps_den, force_key = struct.unpack_from("<4I", data, 16)
        if (bitrate, fps_num, fps_den, force_key) != (500000, 15, 1, 1):
            raise Fail("ENC_SET fields %u %u %u %u" % (bitrate, fps_num, fps_den, force_key))
        self.send(c, hdr(ENC_SET_OK, seq, SESSION) + struct.pack("<II", 0, 0))

    def flush(self, c):
        typ, seq, session, data = self.recv(c)
        if typ != FLUSH or session != SESSION or len(data) != 24:
            raise Fail("FLUSH: type=%u session=%u len=%u" % (typ, session, len(data)))
        self.send(c, hdr(FLUSH_OK, seq, SESSION) + struct.pack("<II", 0, 0))

    def close(self, c):
        typ, seq, session, data = self.recv(c)
        if typ != CLOSE or session != SESSION or len(data) != 24:
            raise Fail("CLOSE: type=%u session=%u len=%u" % (typ, session, len(data)))
        self.send(c, hdr(CLOSE_OK, seq, SESSION) + struct.pack("<II", 0, 0))

    def run(self):
        lfd = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        try:
            os.unlink(self.a.socket)
        except FileNotFoundError:
            pass
        lfd.bind(self.a.socket)
        lfd.listen(1)
        with open(self.a.ready, "w") as f:
            f.write("ready\n")
        lfd.settimeout(10)
        c, _ = lfd.accept()
        lfd.close()
        c.settimeout(10)
        try:
            self.hello(c)
            if self.a.mode == "info":
                try:
                    data = c.recv(4096)
                    if data:
                        raise Fail("info client sent %d bytes after HELLO" % len(data))
                except socket.timeout:
                    raise Fail("info client did not disconnect")
            else:
                self.open(c)
                # The CLI has no run-time option that reaches ENC_SET/FLUSH; a second frame is the test.
                self.frame(c)
                self.frame(c)
                self.close(c)
        finally:
            c.close()
            try:
                os.unlink(self.a.socket)
            except FileNotFoundError:
                pass

    def result(self, ok, error=""):
        with open(self.a.result, "w") as f:
            json.dump({"ok": ok, "error": error, "frames": self.frames, "last_seq": self.last_seq}, f)
            f.write("\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--socket", required=True)
    ap.add_argument("--mode", choices=("enc", "info"), default="enc")
    ap.add_argument("--width", type=int, default=64)
    ap.add_argument("--height", type=int, default=48)
    ap.add_argument("--yuv", default="")
    ap.add_argument("--result", required=True)
    ap.add_argument("--ready", required=True)
    args = ap.parse_args()
    srv = Server(args)
    try:
        srv.run()
    except Exception as exc:                      # noqa: BLE001 - the reason goes into the JSON result
        srv.result(False, "%s: %s" % (type(exc).__name__, exc))
        print("cli_fake_server: %s" % exc, file=sys.stderr)
        return 1
    srv.result(True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
