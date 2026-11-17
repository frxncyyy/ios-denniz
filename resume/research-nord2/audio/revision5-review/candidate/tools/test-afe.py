#!/usr/bin/env python3
"""Host-only source-plan test; all callbacks are in-memory register models."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = root / "shim/nord2-xnu"
with tempfile.TemporaryDirectory(prefix="nord2-audio-afe-") as directory:
    binary = Path(directory) / "test-audio-afe"
    subprocess.run([
        os.environ.get("CC", "clang"), "-std=c11", "-O1", "-g", "-Wall",
        "-Wextra", "-Werror", "-DNORD2", "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer", "-I" + str(src),
        str(root / "tools/test-audio-afe.c"), str(src / "audio_afe.c"),
        str(src / "audio_id.c"), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
