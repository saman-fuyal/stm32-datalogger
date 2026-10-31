#!/usr/bin/env python3
"""Live plot of the data logger while it is logging.

Reads the CSV lines the board prints on the serial port, checks each CRC,
and draws temperature, pressure, acceleration and gyro as they arrive.
Every good line is also saved to a file on the laptop.

Usage:
  python live_plot.py COM5                     (the board is already logging)
  python live_plot.py COM5 --start             (sends 'start' first, 'stop' when you close)
  python live_plot.py COM5 --window 600 --save mylog.csv

Click a name in the accel or gyro legend (for example gz) to hide that line,
click it again to show it.

While it runs you can type board commands in this terminal (set rate 500, read, power ...)
and press Enter: they are sent to the board, its answers are printed here.

Close PuTTY first: only one program can use the serial port at a time.
Needs:  pip install pyserial matplotlib
"""
import argparse
import math
import os
import queue
import sys
import threading
import time
import zlib
from collections import deque
from datetime import datetime, timedelta

import serial

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from log_check import DEFAULT_HEADER, TIME_FORMAT, parse_line, to_float   # same line format and CRC check

HEADER = DEFAULT_HEADER.split(",")
COL = {name: i for i, name in enumerate(HEADER)}


def send(port, text):
    """One command. The first byte only wakes the board from STOP, so a lone CR goes first."""
    port.write(b"\r")
    time.sleep(0.05)
    port.write(text.encode("ascii", "replace") + b"\r")


def reader(port, lines, stop):
    buf = b""
    while not stop.is_set():
        try:
            data = port.read(256)
        except serial.SerialException as e:
            lines.put(("error", str(e)))
            return
        if not data:
            continue
        buf += data
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            lines.put(("line", raw.decode("ascii", "replace").rstrip("\r")))


def keyboard(port, stop):
    """Lines typed in this terminal go to the board."""
    for typed in sys.stdin:
        if stop.is_set():
            return
        send(port, typed.strip())


def main():
    ap = argparse.ArgumentParser(description="Live plot of the data logger")
    ap.add_argument("port", help="serial port, e.g. COM5 or /dev/ttyUSB0")
    ap.add_argument("--start", action="store_true",
                    help="send 'start' at the beginning and 'stop' when the window is closed")
    ap.add_argument("--window", type=int, default=300,
                    help="how many samples are shown (default 300 = 5 min at 1 s)")
    ap.add_argument("--save", default=None,
                    help="file for the received lines (default live_YYYYmmdd_HHMMSS.csv)")
    args = ap.parse_args()

    try:
        import matplotlib.dates as mdates
        import matplotlib.pyplot as plt
        from matplotlib.animation import FuncAnimation
    except ImportError:
        print("matplotlib not installed: pip install matplotlib")
        return 1

    save_name = args.save or time.strftime("live_%Y%m%d_%H%M%S.csv")
    port = serial.Serial(args.port, 115200, timeout=0.1)
    port.reset_input_buffer()
    lines = queue.Queue()
    stop = threading.Event()
    threading.Thread(target=reader, args=(port, lines, stop), daemon=True).start()
    threading.Thread(target=keyboard, args=(port, stop), daemon=True).start()
    if args.start:
        send(port, "start")

    out = open(save_name, "w", encoding="ascii", newline="")
    out.write(DEFAULT_HEADER + "\n")
    print(f"Saving good lines to {save_name}. Type board commands here, close the plot to quit.")

    n = args.window
    t = deque(maxlen=n)                       # board time of each sample
    same = {"stamp": None, "first": 0.0}     # for rates faster than 1 line per second
    data = {k: deque(maxlen=n) for k in
            ("temp_C", "press_hPa", "ax_g", "ay_g", "az_g", "amag", "gx_dps", "gy_dps", "gz_dps")}
    count = {"ok": 0, "bad": 0}
    last_stamp = [""]

    fig, axes = plt.subplots(4, 1, sharex=True, figsize=(10, 8))
    fig.canvas.manager.set_window_title("Data logger - live")
    panels = [
        (axes[0], [("temp_C", "temp")], "Temp (C)"),
        (axes[1], [("press_hPa", "pressure")], "Pressure (hPa)"),
        (axes[2], [("ax_g", "ax"), ("ay_g", "ay"), ("az_g", "az"), ("amag", "|a|")], "Accel (g)"),
        (axes[3], [("gx_dps", "gx"), ("gy_dps", "gy"), ("gz_dps", "gz")], "Gyro (dps)"),
    ]
    plot_lines = []
    legend_map = {}                           # legend line or name -> (plot line, legend line, name)
    for ax, series, label in panels:
        for key, name in series:
            (ln,) = ax.plot([], [], linewidth=1, label=name)
            plot_lines.append((ln, key))
        ax.set_ylabel(label)
        ax.grid(True, alpha=0.3)
        if len(series) > 1:
            leg = ax.legend(loc="upper left", fontsize=8, ncol=len(series))
            # click a legend entry (line or name) to hide / show that line
            for leg_line, leg_text, (ln, _key) in zip(leg.get_lines(), leg.get_texts(),
                                                     plot_lines[-len(series):]):
                leg_line.set_picker(6)            # 6 points around the line count as a hit
                leg_text.set_picker(True)
                legend_map[leg_line] = (ln, leg_line, leg_text)
                legend_map[leg_text] = (ln, leg_line, leg_text)
    axes[-1].set_xlabel("board time")
    axes[-1].xaxis.set_major_formatter(mdates.DateFormatter("%H:%M:%S"))
    fig.tight_layout(rect=(0, 0, 1, 0.95))   # room for the title line

    def update(_frame):
        new = False
        while True:
            try:
                kind, text = lines.get_nowait()
            except queue.Empty:
                break
            if kind == "error":
                print("Serial error:", text)
                stop.set()
                continue
            result, values = parse_line(text)
            if result == "ok" and len(values) >= len(HEADER):
                count["ok"] += 1
                line = ",".join(values)            # saved with its CRC: log_check.py can check it
                out.write(f"{line}*{zlib.crc32(line.encode('ascii')):08X}\n")
                try:
                    stamp = datetime.strptime(values[0], TIME_FORMAT)
                except ValueError:
                    continue
                if t and stamp < t[-1] - timedelta(seconds=1):
                    # the clock went back ('set time', or a new run): start the plot again
                    t.clear()
                    for d in data.values():
                        d.clear()
                    print("[time went backwards: plot restarted]")
                # the stamp has whole seconds only; lines inside the same second are spread
                # by their arrival time on the laptop (set rate 500 = two points per second)
                now = time.monotonic()
                if values[0] == same["stamp"]:
                    stamp += timedelta(seconds=min(now - same["first"], 0.99))
                else:
                    same["stamp"], same["first"] = values[0], now
                last_stamp[0] = values[0]
                t.append(stamp)
                for key in data:
                    if key == "amag":
                        a = [to_float(values[COL[k]]) for k in ("ax_g", "ay_g", "az_g")]
                        data[key].append(math.sqrt(sum(v * v for v in a)))
                    else:
                        data[key].append(to_float(values[COL[key]]))
                new = True
            elif result == "bad":
                count["bad"] += 1
            elif text.strip():
                print(text)                     # messages and command answers
        if new:
            out.flush()
            x = list(t)
            for ln, key in plot_lines:
                ln.set_data(x, list(data[key]))
            for ax, _s, _l in panels:
                ax.relim(visible_only=True)       # hidden lines do not stretch the scale
                ax.autoscale_view()
            axes[-1].set_xlim(x[0], max(x[-1], x[0] + timedelta(seconds=10)))
            temp = data["temp_C"][-1]
            press = data["press_hPa"][-1]
            fig.suptitle(f"{last_stamp[0]}   {temp:.2f} C   {press:.2f} hPa   "
                         f"lines ok {count['ok']}, CRC bad {count['bad']}", fontsize=10)
        return []

    def on_pick(event):
        if event.artist not in legend_map:
            return
        ln, leg_line, leg_text = legend_map[event.artist]
        show = not ln.get_visible()
        ln.set_visible(show)
        leg_line.set_alpha(1.0 if show else 0.2)  # hidden: the legend entry turns pale
        leg_text.set_alpha(1.0 if show else 0.3)
        ax = ln.axes
        ax.relim(visible_only=True)
        ax.autoscale_view()
        fig.canvas.draw_idle()

    fig.canvas.mpl_connect("pick_event", on_pick)
    anim = FuncAnimation(fig, update, interval=200, cache_frame_data=False)
    try:
        plt.show()
    finally:
        if args.start:
            send(port, "stop")
            time.sleep(0.5)
            while not lines.empty():
                kind, text = lines.get()
                if kind == "line" and text.strip():
                    print(text)
        stop.set()
        out.close()
        port.close()
        print(f"Saved {count['ok']} lines to {save_name} (CRC bad: {count['bad']}).")
    del anim
    return 0


if __name__ == "__main__":
    sys.exit(main())
