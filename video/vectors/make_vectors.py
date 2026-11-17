#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Generate the deterministic material of gpu/video/vectors and its manifest.

Run through make_vectors.sh.  Only the installed tools named in the task are
used, as black boxes; no codec source is read.  Every path stored in the
manifest is relative to the data directory (`vectors/data` by default), so a
regeneration into a temporary directory produces the same values.

The raw sequences are written by gen_yuv.py's generators (imported, not
reimplemented).  The golden streams are made from `squares` 176x144, 8 frames:

  h264-baseline  x264, Constrained Baseline, Annex B, keyint 8, no B, QP 28
  h264-high      x264, High, CABAC and B frames, keyint 8, QP 28
  hevc           x265, Main, keyint 8, no B frames, QP 30
  jpeg           cjpeg -quality 85 of frame 0's PPM (BT.601 full range,
                 integer conversion in gen_yuv.py)
  heic           heif-enc of a lossless PNG made from that same PPM (this
                 libheif does not read PPM: recorded in made_with)
  av1            aomenc, 2 frames, --cpu-used=8, IVF

spatial sizes: `quick` has 176x144 (all six patterns), 178x146 and 640x480;
`full` adds 1280x720 x10 and 1920x1080 / 1918x1078 x4.  The generated raw
data is deliberately deletable: `make_vectors.sh clean` removes it.
"""

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True
sys.path.insert(0, HERE)
import gen_yuv  # noqa: E402

SQUARES_W, SQUARES_H = 176, 144
SQUARES_FRAMES = 8

# (set, pattern, width, height, frames, format)
RAW_SETS = {
    "quick": [
        ("quick", "gradient", 176, 144, 10, "i420"),
        ("quick", "squares", 176, 144, 10, "i420"),
        ("quick", "pan", 176, 144, 10, "i420"),
        ("quick", "static", 176, 144, 10, "i420"),
        ("quick", "noise", 176, 144, 10, "i420"),
        ("quick", "text", 176, 144, 10, "i420"),
        ("quick", "squares", 178, 146, 10, "i420"),
        ("quick", "squares", 640, 480, 10, "i420"),
        ("quick", "pan", 640, 480, 10, "i420"),
    ],
    "full": [
        ("full", "squares", 1280, 720, 10, "i420"),
        ("full", "pan", 1920, 1080, 4, "i420"),
        ("full", "text", 1918, 1078, 4, "i420"),
    ],
}


def log(msg):
    print("make_vectors: %s" % msg, flush=True)


def run(argv, cwd=None, capture=True):
    proc = subprocess.run(argv, cwd=cwd, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, text=True)
    if proc.returncode != 0:
        sys.stderr.write("command failed (%d): %s\n" % (proc.returncode, " ".join(argv)))
        sys.stderr.write(proc.stdout[-4000:])
        sys.stderr.write(proc.stderr[-4000:])
        raise SystemExit("make_vectors: aborting")
    return proc


def tool_version(argv, needle=None):
    proc = subprocess.run(argv, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True)
    text = (proc.stdout or "").strip()
    lines = text.splitlines()
    if needle is not None:
        for line in lines:
            if needle in line:
                return line.strip()
        return "%s (no version line containing %r; first line: %s)" % (
            argv[0], needle, lines[0].strip() if lines else "")
    return lines[0] if lines else "%s (version output empty)" % argv[0]


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as fp:
        for chunk in iter(lambda: fp.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def md5_file_prefix(path, nbytes):
    h = hashlib.md5()
    left = nbytes
    with open(path, "rb") as fp:
        while left > 0:
            chunk = fp.read(min(left, 1 << 20))
            if not chunk:
                raise SystemExit("make_vectors: short file %s" % path)
            h.update(chunk)
            left -= len(chunk)
    return h.hexdigest()


def decode_frame_md5(path):
    """ffmpeg's own per-frame md5, exactly the command of the task."""
    cmd = ["ffmpeg", "-v", "error", "-i", path, "-f", "framemd5",
           "-pix_fmt", "yuv420p", "-"]
    proc = run(cmd)
    hashes = []
    dims = None
    for line in proc.stdout.splitlines():
        if line.startswith("#dimensions 0:"):
            dims = line.split(":", 1)[1].strip()
        elif line and not line.startswith("#"):
            fields = [f.strip() for f in line.split(",")]
            if len(fields) < 6:
                raise SystemExit("make_vectors: bad framemd5 line %r" % line)
            hashes.append(fields[-1])
    if not hashes:
        raise SystemExit("make_vectors: ffmpeg decoded no frames from %s" % path)
    return hashes, dims


def decode_luma_md5(path, width):
    cmd = ["ffmpeg", "-v", "error", "-i", path, "-frames:v", "1",
           "-f", "rawvideo", "-pix_fmt", "yuv420p", "-"]
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr.decode("utf-8", "replace")[-4000:])
        raise SystemExit("make_vectors: ffmpeg failed on %s" % path)
    frame = proc.stdout
    if len(frame) < width:  # at least one luma row
        raise SystemExit("make_vectors: no frame from %s" % path)
    if len(frame) % width != 0:
        raise SystemExit("make_vectors: unexpected raw frame size from %s" % path)
    height = len(frame) * 2 // (3 * width)
    return hashlib.md5(frame[:width * height]).hexdigest(), height


def raw_name(pattern, w, h, frames, fmt):
    return "%s-%dx%d-%d-%s.yuv" % (pattern, w, h, frames, fmt)


def generate_raw(out_dir, entries):
    raws = []
    for group, pattern, w, h, frames, fmt in entries:
        name = raw_name(pattern, w, h, frames, fmt)
        rel = "%s/%s" % (group, name)
        path = os.path.join(out_dir, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        log("raw %s (%dx%d, %d frames, %s)" % (name, w, h, frames, fmt))
        gen_yuv.generate(pattern, w, h, frames, fmt, path)
        raws.append({
            "name": name,
            "pattern": pattern,
            "width": w,
            "height": h,
            "frames": frames,
            "format": fmt,
            "path": rel,
            "sha256": sha256_file(path),
        })
    return raws


def make_streams(out_dir):
    streams_dir = os.path.join(out_dir, "streams")
    os.makedirs(streams_dir, exist_ok=True)
    source_rel = "streams/source-squares-%dx%d-%d-i420.yuv" % (
        SQUARES_W, SQUARES_H, SQUARES_FRAMES)
    source = os.path.join(out_dir, source_rel)
    log("raw %s (source of the golden streams)" % os.path.basename(source))
    gen_yuv.generate("squares", SQUARES_W, SQUARES_H, SQUARES_FRAMES, "i420", source)

    ppm = os.path.join(streams_dir, "squares-%dx%d-frame0.ppm" % (SQUARES_W, SQUARES_H))
    png = os.path.join(streams_dir, "squares-%dx%d-frame0.png" % (SQUARES_W, SQUARES_H))
    gen_yuv.yuv_to_ppm(source, SQUARES_W, SQUARES_H, 0, "i420", ppm)

    tools = {
        "ffmpeg": tool_version(["ffmpeg", "-version"]),
        "x264": tool_version(["x264", "--version"]),
        "x265": tool_version(["x265", "--version"]),
        "cjpeg": tool_version(["cjpeg", "-version"]),
        "heif-enc": tool_version(["heif-enc", "--version"]),
        # aomenc has no --version; its --help lists the encoder version.
        "aomenc": tool_version(["aomenc", "--help"], "AOMedia Project AV1 Encoder"),
    }

    specs = []

    # ---- H.264 Constrained Baseline, Annex B ----
    p = "streams/squares-%dx%d-%d-baseline.264" % (SQUARES_W, SQUARES_H, SQUARES_FRAMES)
    specs.append({
        "name": "h264-baseline",
        "codec": "h264",
        "profile": "constrained baseline",
        "frames": SQUARES_FRAMES,
        "path": p,
        "normative": True,
        "commands": [[
            "x264", "--profile", "baseline", "--keyint", "8", "--bframes", "0",
            "--qp", "28", "--ref", "1", "--threads", "1",
            "--input-res", "%dx%d" % (SQUARES_W, SQUARES_H), "--fps", "30",
            "--frames", str(SQUARES_FRAMES), "-o", p, source_rel,
        ]],
    })

    # ---- H.264 High, CABAC and B frames ----
    p = "streams/squares-%dx%d-%d-high.264" % (SQUARES_W, SQUARES_H, SQUARES_FRAMES)
    specs.append({
        "name": "h264-high",
        "codec": "h264",
        "profile": "high (CABAC, B frames)",
        "frames": SQUARES_FRAMES,
        "path": p,
        "normative": True,
        "commands": [[
            "x264", "--profile", "high", "--keyint", "8", "--bframes", "3",
            "--qp", "28", "--threads", "1",
            "--input-res", "%dx%d" % (SQUARES_W, SQUARES_H), "--fps", "30",
            "--frames", str(SQUARES_FRAMES), "-o", p, source_rel,
        ]],
    })

    # ---- HEVC Main, no B frames ----
    p = "streams/squares-%dx%d-%d-main.265" % (SQUARES_W, SQUARES_H, SQUARES_FRAMES)
    specs.append({
        "name": "hevc",
        "codec": "hevc",
        "profile": "main",
        "frames": SQUARES_FRAMES,
        "path": p,
        "normative": True,
        "commands": [[
            "x265", "--preset", "fast", "--qp", "30", "--keyint", "8",
            "--bframes", "0", "--input-res", "%dx%d" % (SQUARES_W, SQUARES_H),
            "--fps", "30", "--frames", str(SQUARES_FRAMES),
            "--pools", "1", "--frame-threads", "1", "-o", p, source_rel,
        ]],
    })

    # ---- JPEG, quality 85, from the PPM ----
    p = "streams/squares-%dx%d-frame0-q85.jpg" % (SQUARES_W, SQUARES_H)
    specs.append({
        "name": "jpeg-q85",
        "codec": "jpeg",
        "profile": "baseline sequential",
        "frames": 1,
        "path": p,
        "normative": False,
        "commands": [[
            "cjpeg", "-quality", "85", "-outfile", p,
            "streams/" + os.path.basename(ppm),
        ]],
    })

    # ---- HEIC, libheif 1.23.7 does not read PPM: go through a lossless PNG ----
    p = "streams/squares-%dx%d-frame0.heic" % (SQUARES_W, SQUARES_H)
    specs.append({
        "name": "heic",
        "codec": "hevc",
        "container": "heic",
        "profile": "main still picture",
        "frames": 1,
        "path": p,
        "normative": False,
        "commands": [
            ["ffmpeg", "-v", "error", "-y", "-i",
             "streams/" + os.path.basename(ppm), "-frames:v", "1",
             "streams/" + os.path.basename(png)],
            ["heif-enc", "-o", p, "streams/" + os.path.basename(png)],
        ],
    })

    # ---- AV1, two frames, IVF ----
    p = "streams/squares-%dx%d-av1-2.ivf" % (SQUARES_W, SQUARES_H)
    specs.append({
        "name": "av1-2",
        "codec": "av1",
        "profile": "main",
        "frames": 2,
        "path": p,
        "normative": True,
        "commands": [[
            "aomenc", "--limit=2", "--cpu-used=8", "--passes=1",
            "--end-usage=q", "--cq-level=30",
            "--width=%d" % SQUARES_W, "--height=%d" % SQUARES_H,
            "--fps=30/1", "--i420", "--threads=1", "-o", p, source_rel,
        ]],
    })

    streams = []
    for spec in specs:
        log("stream %s" % spec["name"])
        for argv in spec["commands"]:
            run(argv, cwd=out_dir)
        path = os.path.join(out_dir, spec["path"])
        frame_md5, dims = decode_frame_md5(path)
        luma_md5, decoded_h = decode_luma_md5(path, SQUARES_W)
        if len(frame_md5) != spec["frames"]:
            raise SystemExit("make_vectors: %s decoded %d frames, expected %d"
                             % (spec["name"], len(frame_md5), spec["frames"]))
        expect_dims = "%dx%d" % (SQUARES_W, SQUARES_H)
        if dims is not None and dims != expect_dims:
            raise SystemExit("make_vectors: %s decoded at %s, expected %s"
                             % (spec["name"], dims, expect_dims))
        entry = {
            "name": spec["name"],
            "codec": spec["codec"],
            "profile": spec["profile"],
            "width": SQUARES_W,
            "height": SQUARES_H,
            "frames": spec["frames"],
            "path": spec["path"],
            "sha256": sha256_file(path),
            "made_with": {
                "commands": [" ".join(argv) for argv in spec["commands"]],
                "tools": tools,
            },
            "frame_md5": frame_md5,
            "luma_md5_first_frame": luma_md5,
            "frame_md5_normative": spec["normative"],
        }
        if "container" in spec:
            entry["container"] = spec["container"]
        streams.append(entry)

    # The PPM/PNG are intermediates; the manifest keeps the commands that
    # recreate them.  The PPM is tiny and useful next to the other streams.
    try:
        os.unlink(png)
    except OSError:
        pass

    return streams, source_rel, tools


def build_manifest(out_dir, mode, raw_entries):
    raws = generate_raw(out_dir, raw_entries)
    streams, source_rel, tools = make_streams(out_dir)
    source_path = os.path.join(out_dir, source_rel)
    return {
        "version": 1,
        "generator": "gpu/video/vectors/make_vectors.sh",
        "mode": mode,
        "paths_relative_to": "vectors/data",
        "notes": [
            "Raw pictures and golden streams are reproducible and may be deleted;",
            "run make_vectors.sh quick (or full) to recreate them.",
            "The 'pan' and 'static' patterns define luma only in the work package;",
            "their chroma is neutral 128 (gen_yuv.py documents this).",
            "heif-enc 1.23.7 cannot read PPM, so the HEIC is made from a lossless PNG",
            "converted from the same PPM (see streams[].made_with.commands).",
        ],
        "sizes": {
            "quick_raw_bytes": sum(os.path.getsize(os.path.join(out_dir, r["path"])) for r in raws
                                   if r["path"].startswith("quick/")),
            "full_raw_bytes": sum(os.path.getsize(os.path.join(out_dir, r["path"])) for r in raws
                                  if r["path"].startswith("full/")),
        },
        "tools": tools,
        "raw": raws,
        "streams": streams,
        "stream_sources": [{
            "name": os.path.basename(source_rel),
            "path": source_rel,
            "width": SQUARES_W,
            "height": SQUARES_H,
            "frames": SQUARES_FRAMES,
            "format": "i420",
            "sha256": sha256_file(source_path),
        }],
    }


def safe_data_dir(path):
    resolved = os.path.realpath(path)
    here = os.path.realpath(HERE)
    if resolved == os.sep or resolved == here or here.startswith(resolved + os.sep):
        raise SystemExit("make_vectors: refusing to clear %s" % resolved)
    return resolved


def main(argv=None):
    ap = argparse.ArgumentParser(description="generate the WP09 test vectors")
    ap.add_argument("mode", choices=("quick", "full", "clean"))
    ap.add_argument("--out", default=os.path.join(HERE, "data"),
                    help="data directory (default gpu/video/vectors/data)")
    ap.add_argument("--manifest", default=os.path.join(HERE, "manifest.json"))
    args = ap.parse_args(argv)

    out_dir = safe_data_dir(args.out)
    if args.mode == "clean":
        if os.path.isdir(out_dir):
            shutil.rmtree(out_dir)
            log("removed %s" % out_dir)
        else:
            log("nothing to remove at %s" % out_dir)
        return 0

    entries = list(RAW_SETS["quick"])
    if args.mode == "full":
        entries += RAW_SETS["full"]

    if os.path.isdir(out_dir):
        shutil.rmtree(out_dir)
    os.makedirs(out_dir)

    manifest = build_manifest(out_dir, args.mode, entries)
    tmp = args.manifest + ".tmp.%d" % os.getpid()
    with open(tmp, "w", encoding="utf-8") as fp:
        json.dump(manifest, fp, indent=1, sort_keys=False)
        fp.write("\n")
    os.replace(tmp, args.manifest)
    total = sum(os.path.getsize(os.path.join(out_dir, r["path"])) for r in manifest["raw"])
    total += sum(os.path.getsize(os.path.join(out_dir, s["path"])) for s in manifest["streams"])
    log("wrote %s: %d raw sequences (%.2f MiB), %d streams (%.2f KiB)"
        % (args.manifest, len(manifest["raw"]), manifest["sizes"]["quick_raw_bytes"] / 1048576.0,
           len(manifest["streams"]),
           sum(os.path.getsize(os.path.join(out_dir, s["path"]))
               for s in manifest["streams"]) / 1024.0))
    if args.mode == "full":
        log("NOTE: the full set is ~50 MiB; the 40 MiB repository budget is met"
            " only after 'make_vectors.sh clean'")
    return 0


if __name__ == "__main__":
    sys.exit(main())
