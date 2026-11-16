#!/usr/bin/env python3
"""Summarize compositor completion windows; these are NOT panel-present FPS.

Use a sustained repeatable gesture for comparisons. Idle time, startup, shader
cache activity and UI content affect the window rate. USB reconnects replay logs;
select a single capture segment before aggregating replayed output.
"""
import argparse
import json
import re
from pathlib import Path


def windows(text, process):
    rows = []
    compact = any(f"|{process}|" in line and "TEGU_PERF_FRAME window_ms=" in line
                  for line in text.splitlines())
    marker = "TEGU_PERF_FRAME" if compact else "TEGU_PERF"
    for line in text.splitlines():
        if f"|{process}|" not in line:
            continue
        for match in re.finditer(rf"{marker} window_ms=(.*?)(?=TEGU_|$)", line):
            fields = dict((k, int(v)) for k, v in re.findall(
                r"\b(\w+)=(\d+)", "window_ms=" + match.group(1)))
            count, elapsed = fields.get("commands", 0), fields.get("window_ms", 0)
            if not count or not elapsed:
                continue
            row = {"window_ms": elapsed, "commands": count,
                   "updates_per_second": round(1000 * count / elapsed, 2)}
            for source, dest in (("exec_ms", "mean_execute_ms"),
                                 ("gpu_ms", "mean_gpu_ms"),
                                 ("encode_ms", "mean_encode_ms")):
                if source in fields:
                    row[dest] = round(fields[source] / count, 3)
            if compact:
                for source, dest in (("mean_exec_us", "mean_execute_ms"),
                                     ("mean_gpu_us", "mean_gpu_ms"),
                                     ("max_exec_us", "max_command_ms")):
                    if source in fields:
                        row[dest] = round(fields[source] / 1000, 3)
                if "kernel_jobs" in fields:
                    row["kernel_jobs_per_command"] = round(fields["kernel_jobs"] / count, 2)
            if "gpu_jobs" in fields and "batched" in fields:
                row["kernel_jobs_per_command"] = round(
                    (fields["gpu_jobs"] - fields["batched"]) / count, 2)
            for name in ("max_command_ms", "compile_ms", "compiles"):
                if name in fields:
                    row[name] = fields[name]
            rows.append(row)
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--process", default="backboardd")
    parser.add_argument("--last", type=int, default=12)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    text = args.log.read_text(errors="replace")
    rows = windows(text, args.process)
    if args.last > 0:
        rows = rows[-args.last:]
    if args.json:
        print(json.dumps({"metric": "compositor command completions, not panel-present FPS",
                          "target_frame_ms": 1000 / 60,
                          "usb_stream_end_markers": text.count("STREAM-END"),
                          "windows": rows}, indent=2))
        return
    print("Compositor completions (not panel-present FPS); 60 fps budget: 16.67 ms")
    print("window_ms commands updates/s mean_exec_ms mean_gpu_ms kernel_jobs/cmd max_exec_ms")
    for r in rows:
        print(" ".join(str(r.get(k, "-")) for k in (
            "window_ms", "commands", "updates_per_second", "mean_execute_ms",
            "mean_gpu_ms", "kernel_jobs_per_command", "max_command_ms")))
    if "STREAM-END" in text:
        print("Note: USB stream restarts may contain replayed windows.")


if __name__ == "__main__":
    main()
