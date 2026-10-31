#!/usr/bin/env python3
"""Check a data logger file (LOGnnnn.CSV from the SD card, or a saved terminal log).

Every data line looks like   <values>*<CRC32 in hex>
The CRC is zlib.crc32 of the text before the '*'. This script:
  - checks the CRC of every line (ok / bad),
  - counts other lines (header, messages),
  - reports time gaps bigger than --gap seconds,
  - with --plot out.png, draws every column against time (needs matplotlib).

Usage:
  python3 log_check.py LOG0001.CSV
  python3 log_check.py LOG0001.CSV --plot log1.png --gap 3
  python3 log_check.py LOG0001.CSV --plot log1.png --x sample   (x axis: time / sample)
Exit code is 1 if any line had a bad CRC.
"""
import argparse
import os
import re
import sys
import zlib
from datetime import datetime

DEFAULT_HEADER = ("time,temp_C,press_hPa,ax_g,ay_g,az_g,"
                  "gx_dps,gy_dps,gz_dps,vdd_V,chip_C")
TIME_FORMAT = "%Y-%m-%d %H:%M:%S"
# a data line starts with a time stamp; in a terminal log, typed letters can come before it
STAMP = re.compile(r"\d{4}-\d\d-\d\d \d\d:\d\d:\d\d,")


def parse_line(line):
    """Returns ('ok', values) / ('bad', None) / ('other', None)."""
    line = line.strip()
    m = STAMP.search(line)               # skip "> " or echoed keys in front of the stamp
    if m:
        line = line[m.start():]
    if "*" not in line:
        return "other", None
    text, _, crc_hex = line.rpartition("*")
    try:
        crc = int(crc_hex, 16)
    except ValueError:
        return "other", None
    if len(crc_hex) != 8:
        return "other", None
    if zlib.crc32(text.encode("ascii", "replace")) != crc:
        return "bad", None
    return "ok", text.split(",")


def to_float(s):
    try:
        return float(s)                  # float("nan") works too
    except ValueError:
        return float("nan")


def main():
    ap = argparse.ArgumentParser(description="Check CRCs in a data logger file")
    ap.add_argument("file")
    ap.add_argument("--plot", metavar="OUT.png", help="save a plot of all columns")
    ap.add_argument("--gap", type=float, default=0.0,
                    help="report time gaps bigger than this many seconds "
                         "(default: 2.5 x the most common step)")
    ap.add_argument("--x", choices=("auto", "time", "sample"), default="auto",
                    help="x axis of the plot: time, sample number, or auto (default: time "
                         "if it never goes backwards, else sample number)")
    args = ap.parse_args()

    header = DEFAULT_HEADER.split(",")
    ok = bad = other = 0
    bad_lines = []
    rows = []

    with open(args.file, "r", encoding="ascii", errors="replace") as f:
        for number, line in enumerate(f, 1):
            if line.strip().startswith("time,"):
                header = line.strip().split(",")
                other += 1
                continue
            kind, values = parse_line(line)
            if kind == "ok":
                ok += 1
                rows.append(values)
            elif kind == "bad":
                bad += 1
                bad_lines.append(number)
            elif line.strip():
                other += 1

    print(f"File: {args.file}")
    print(f"CRC ok: {ok}   CRC bad: {bad}   other lines: {other}")
    if bad_lines:
        shown = ", ".join(str(n) for n in bad_lines[:20])
        print(f"Bad CRC on line(s): {shown}{' ...' if len(bad_lines) > 20 else ''}")

    # ---- time gaps ----
    times = []
    for r in rows:
        try:
            times.append(datetime.strptime(r[0], TIME_FORMAT))
        except (ValueError, IndexError):
            times.append(None)
    steps = [(b - a).total_seconds() for a, b in zip(times, times[1:])
             if a is not None and b is not None]
    if steps:
        common = max(set(steps), key=steps.count)
        limit = args.gap if args.gap > 0 else max(2.5 * common, 2.0)
        gaps = [(times[i], s) for i, s in enumerate(steps) if s > limit or s < 0]
        print(f"Most common step: {common:g} s, gaps over {limit:g} s: {len(gaps)}")
        for t, s in gaps[:20]:
            print(f"  after {t}: {s:g} s")
        print(f"First: {times[0]}   last: {times[-1]}")
        good = [t for t in times if t is not None]
        if len(good) > 1 and all(b >= a for a, b in zip(good, good[1:])):
            span = (good[-1] - good[0]).total_seconds()
            skips = sum(1 for s in steps if s == 2 * common)
            print(f"Period from the time stamps: {span / (len(good) - 1):.4f} s "
                  f"({skips} step(s) of {2 * common:g} s)")

    # ---- plot ----
    if args.plot and rows:
        try:
            import matplotlib
            matplotlib.use("Agg")
            import matplotlib.pyplot as plt
        except ImportError:
            print("matplotlib not installed: pip install matplotlib")
            return 1 if bad else 0
        names = header[1:]
        x = list(range(len(rows)))
        # time on the x axis, unless time goes backwards (a terminal log with 'cat' output,
        # several runs, or 'set time' in the middle): the lines would then zig-zag back
        all_parsed = all(t is not None for t in times)
        back = [i for i, (a, b) in enumerate(zip(times, times[1:]), 1)
                if all_parsed and b < a]
        if args.x == "auto":
            use_time = all_parsed and not back
            if all_parsed and back:
                print(f"Time goes backwards {len(back)} time(s), first at data line {back[0] + 1} "
                      f"({times[back[0] - 1]} -> {times[back[0]]}): plotting by sample number "
                      f"(use --x time to force time)")
        else:
            use_time = args.x == "time" and all_parsed
        style = "." if (use_time and back) else "-"      # dots: no zig-zag lines
        fig, axes = plt.subplots(len(names), 1, sharex=True,
                                 figsize=(10, 1.6 * len(names)))
        for i, (ax, name) in enumerate(zip(axes, names), 1):
            y = [to_float(r[i]) if i < len(r) else float("nan") for r in rows]
            ax.plot(times if use_time else x, y, style, linewidth=0.8, markersize=2)
            ax.set_ylabel(name, rotation=0, ha="right", fontsize=8)
            ax.grid(True, alpha=0.3)
        axes[-1].set_xlabel("time" if use_time else "sample number")
        fig.suptitle(os.path.basename(args.file))
        fig.tight_layout()
        fig.savefig(args.plot, dpi=110)
        print(f"Plot saved to {args.plot}")

    return 1 if bad else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except BrokenPipeError:              # output piped into 'head' and the like
        sys.exit(0)
