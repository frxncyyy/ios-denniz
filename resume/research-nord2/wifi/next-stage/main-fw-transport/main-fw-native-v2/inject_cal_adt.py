#!/usr/bin/env python3
"""Private opt-in ADT packaging of the saved, pinned board WIFI data.

The calibration bytes are supplied at build time; this source contains none.
Never run for the existing ROM or DMA images. Native XNU must still check the
same 6146-byte hash and current original FDT before constructing FORCE_CAL.
"""
import argparse
import hashlib
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[6]
sys.path.insert(0, str(ROOT / "tools"))
import adt  # noqa: E402

SHA = "0000000000000000000000000000000000000000000000000000000000000000"
PROPERTY = "nord2-wifi-calibration"


def add(root, wifi):
    if len(wifi) != 6146 or hashlib.sha256(wifi).hexdigest() != SHA:
        raise ValueError("board WIFI provenance mismatch")
    chosen = [n for n in root.children if any(
        k == "name" and v == b"chosen\0" for k, _, v in n.props)]
    if len(chosen) != 1 or any(k == PROPERTY for k, _, _ in chosen[0].props):
        raise ValueError("missing/duplicate chosen or calibration property")
    chosen[0].props.append((PROPERTY, 0, wifi))
    return root


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--enable-nord2wifimain", action="store_true",
                    help="explicitly package board calibration for a future nord2wifimain=1 image")
    ap.add_argument("input_adt", type=Path)
    ap.add_argument("saved_wifi", type=Path)
    ap.add_argument("output_adt", type=Path)
    args = ap.parse_args()
    if not args.enable_nord2wifimain:
        raise SystemExit("calibration packaging requires --enable-nord2wifimain")
    if args.input_adt.resolve() == args.output_adt.resolve():
        raise SystemExit("output must be distinct from input")
    blob = args.input_adt.read_bytes()
    root, consumed = adt.parse(blob)
    if consumed != len(blob):
        raise SystemExit("ADT trailing bytes")
    wifi = args.saved_wifi.read_bytes()
    result = adt.serialize(add(root, wifi))
    root2, consumed2 = adt.parse(result)
    if consumed2 != len(result):
        raise SystemExit("ADT round trip")
    chosen = [n for n in root2.children if any(
        k == "name" and v == b"chosen\0" for k, _, v in n.props)]
    if len(chosen) != 1 or [v for k, _, v in chosen[0].props if k == PROPERTY] != [wifi]:
        raise SystemExit("ADT calibration round trip")
    fd = os.open(args.output_adt, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "wb") as out:
        out.write(result)
    print(f"{args.output_adt}: {len(result)} bytes; pinned WIFI property verified")


if __name__ == "__main__":
    main()
