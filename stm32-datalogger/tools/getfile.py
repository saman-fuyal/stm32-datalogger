#!/usr/bin/env python3
"""Copy a log file from the board's SD card to the PC over the serial port (no card reader).

Usage:
  python getfile.py COM5                    list the files on the card
  python getfile.py COM5 LOG0003.CSV        save it as LOG0003.CSV here
  python getfile.py COM5 LOG0003.CSV -o run3.csv

It sends 'cat FILE' and keeps exactly what the board prints between the command and the
next prompt, so the saved file is the same as the one on the card. The size is compared
with 'ls' and every CRC is checked. Then plot it with:
  python log_check.py LOG0003.CSV --plot log3.png

Close PuTTY first. Needs: pip install pyserial
"""
import argparse
import os
import re
import sys
import time

import serial

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from log_check import parse_line

PROMPT = b"> "
BAUD_BYTES = 11520          # 115200 baud = about 11520 bytes per second


def wake(port):
    """The first byte only wakes the board from STOP, so send two Enters, then clear."""
    for _ in range(2):
        port.write(b"\r")
        time.sleep(0.3)
    port.reset_input_buffer()


def command(port, cmd, timeout):
    """Sends cmd, returns what the board printed for it (between the echo and the prompt)."""
    echo = cmd.encode("ascii") + b"\r\n"
    port.write(cmd.encode("ascii") + b"\r")
    buf = b""
    start = last = time.monotonic()
    while True:
        data = port.read(4096)
        now = time.monotonic()
        if data:
            buf += data
            last = now
        # A log line may come just before the echo, and log lines follow right after the
        # prompt (the board keeps logging). The answer is what lies between the two.
        at = buf.find(echo)
        if at >= 0:
            end = buf.find(PROMPT, at + len(echo))
            if end >= 0:
                return buf[at + len(echo):end]
        if now - start > timeout or (buf and now - last > 3.0):
            raise TimeoutError(f"no complete answer to '{cmd}' (got {len(buf)} bytes)")


def list_files(port):
    text = command(port, "ls", 10).decode("ascii", "replace")
    files = {}
    for line in text.splitlines():
        m = re.match(r"(\S+)\s+(\d+) bytes", line.strip())
        if m:
            files[m.group(1).upper()] = int(m.group(2))
    return text, files


def main():
    ap = argparse.ArgumentParser(description="Copy a file from the logger's SD card")
    ap.add_argument("port", help="serial port, e.g. COM5")
    ap.add_argument("file", nargs="?", help="file on the card, e.g. LOG0003.CSV")
    ap.add_argument("-o", "--out", help="name on the PC (default: same name)")
    args = ap.parse_args()

    with serial.Serial(args.port, 115200, timeout=0.1) as port:
        wake(port)
        text, files = list_files(port)
        if not args.file:
            print(text.rstrip())
            return 0
        name = args.file.upper()
        if name not in files:
            print(text.rstrip())
            print(f"{name} is not on the card")
            return 1
        size = files[name]
        print(f"Reading {name} ({size} bytes, about {size / BAUD_BYTES:.0f} s)...")
        data = command(port, "cat " + name, timeout=size / BAUD_BYTES * 2 + 10)

    if not data.startswith(b"time,"):
        print("The board answered:", data.decode("ascii", "replace").strip())
        return 1
    out = args.out or name
    with open(out, "wb") as f:
        f.write(data)                  # same bytes as on the card (lines end with \r\n)

    ok = bad = 0
    for line in data.decode("ascii", "replace").splitlines():
        kind, _ = parse_line(line)
        ok += kind == "ok"
        bad += kind == "bad"
    print(f"Saved {out}: {len(data)} bytes, {ok} lines, CRC bad: {bad}")
    if len(data) < size:
        print(f"WARNING: {size - len(data)} bytes missing, try again")
        return 1
    if len(data) > size:
        print("(the file grew while copying: it is the file being logged right now)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
