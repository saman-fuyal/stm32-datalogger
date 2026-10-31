#!/usr/bin/env python3
"""Measure the sample period and the sensor-read time from a logic analyzer capture of PB12.

PB12 (the TP test point) is high while the sensors are read, once per sample. So:
  period = time from one rising edge to the next   (firmware: 1 s at the default rate)
  width  = time from a rising edge to its falling edge   (firmware 'sensor read': about 24.2 ms)

Export the capture as CSV first:
  Saleae Logic 2:  File > Export Data > CSV, digital, only the PB12 channel
                   (one row per change: "Time [s],Channel 0")
  PulseView:       File > Export > Comma-separated values, tick "time column"
                   (one row per sample; give the sample rate if there is no time column)

Usage:
  python la_check.py capture.csv
  python la_check.py capture.csv --col 1 --rate 1000000 --analyzer "24 MHz 8-channel, PulseView"

Writes results/logic_analyzer.json and docs/captures/logic_analyzer.png (the report picks both up).
Needs: pip install matplotlib
"""
import argparse
import json
import os
import statistics
import sys
from datetime import date

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
FIRMWARE_READ_MS = 24.198        # 'sensor read' from results/timing.md


def read_capture(path, col, rate):
    """Returns a list of (time_s, level) for every row that holds numbers."""
    rows = []
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line or line[0] in ";#":
                continue                         # PulseView puts comments at the top
            parts = [p.strip() for p in line.replace(";", ",").split(",")]
            try:
                nums = [float(p) for p in parts if p != ""]
            except ValueError:
                continue                         # the header line
            if not nums:
                continue
            if rate:                             # no time column: the row number is the time
                level = nums[col] if col < len(nums) else nums[-1]
                rows.append((len(rows) / rate, int(level > 0.5)))
            else:
                if len(nums) < 2:
                    sys.exit("only one column: give the sample rate with --rate")
                level = nums[col] if col < len(nums) else nums[-1]
                rows.append((nums[0], int(level > 0.5)))
    return rows


def edges(rows):
    rising, falling = [], []
    prev = None
    for t, level in rows:
        if prev is not None and level != prev:
            (rising if level else falling).append(t)
        prev = level
    return rising, falling


def main():
    ap = argparse.ArgumentParser(description="Sample period and pulse width from a PB12 capture")
    ap.add_argument("csv", help="exported capture")
    ap.add_argument("--col", type=int, default=1,
                    help="column of PB12, counted from 0 (default 1: the column after the time)")
    ap.add_argument("--rate", type=float, default=0.0,
                    help="sample rate in Hz, only if the file has no time column")
    ap.add_argument("--analyzer", default="logic analyzer", help="name, for the report")
    ap.add_argument("--sample-rate-text", default="", help="for the report, e.g. '4 MHz'")
    ap.add_argument("--out", default=os.path.join(ROOT, "results", "logic_analyzer.json"))
    ap.add_argument("--plot", default=os.path.join(ROOT, "docs", "captures", "logic_analyzer.png"))
    args = ap.parse_args()

    # without a time column the channels start at column 0
    rows = read_capture(args.csv, args.col if not args.rate else max(args.col - 1, 0), args.rate)
    if len(rows) < 3:
        sys.exit("no data found in the file")
    rising, falling = edges(rows)
    if len(rising) < 3:
        sys.exit(f"only {len(rising)} rising edges: is this the PB12 channel, and was it logging?")

    periods = [b - a for a, b in zip(rising, rising[1:])]
    widths = []
    for r in rising:
        f = next((x for x in falling if x > r), None)
        if f is not None and (not periods or f - r < min(periods)):
            widths.append(f - r)

    mean_p = statistics.mean(periods)
    result = {
        "date": date.today().strftime("%d-%m-%Y"),
        "analyzer": args.analyzer,
        "sample_rate": args.sample_rate_text,
        "file": os.path.basename(args.csv),
        "capture_s": round(rows[-1][0] - rows[0][0], 3),
        "pulses": len(widths),
        "periods": len(periods),
        "period_mean_s": round(mean_p, 7),
        "period_min_s": round(min(periods), 7),
        "period_max_s": round(max(periods), 7),
        "period_std_us": round(statistics.pstdev(periods) * 1e6, 2),
        "period_error_ppm": round((mean_p - 1.0) / 1.0 * 1e6, 1),
        "width_mean_ms": round(statistics.mean(widths) * 1e3, 4) if widths else None,
        "width_min_ms": round(min(widths) * 1e3, 4) if widths else None,
        "width_max_ms": round(max(widths) * 1e3, 4) if widths else None,
        "firmware_read_ms": FIRMWARE_READ_MS,
    }
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w") as f:
        json.dump(result, f, indent=2)

    print(f"{len(rising)} pulses over {result['capture_s']} s")
    print(f"period: mean {mean_p:.6f} s, min {min(periods):.6f} s, max {max(periods):.6f} s, "
          f"spread (std) {result['period_std_us']} us, {result['period_error_ppm']:+} ppm from 1 s")
    if widths:
        print(f"high time (sensor read): mean {result['width_mean_ms']} ms, "
              f"min {result['width_min_ms']} ms, max {result['width_max_ms']} ms "
              f"(firmware says {FIRMWARE_READ_MS} ms)")
    print(f"saved {args.out}")

    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib not installed: no plot (pip install matplotlib)")
        return 0
    fig, (a1, a2) = plt.subplots(2, 1, figsize=(9, 5.5))
    a1.plot(range(1, len(periods) + 1), [(p - 1.0) * 1e3 for p in periods], ".", markersize=3)
    a1.set_xlabel("sample")
    a1.set_ylabel("period - 1 s (ms)")
    a1.grid(True, alpha=0.3)
    if widths:
        a2.hist([w * 1e3 for w in widths], bins=40)
        a2.axvline(FIRMWARE_READ_MS, color="k", linestyle="--", linewidth=1)
        a2.set_xlabel("PB12 high time (ms); dashed line = firmware 'sensor read'")
        a2.set_ylabel("pulses")
        a2.grid(True, alpha=0.3)
    fig.suptitle(f"PB12 on {args.analyzer}: {len(periods)} periods")
    fig.tight_layout()
    os.makedirs(os.path.dirname(args.plot), exist_ok=True)
    fig.savefig(args.plot, dpi=120)
    print(f"saved {args.plot}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
