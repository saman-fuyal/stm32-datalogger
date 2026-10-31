"""Sets the datalogger's clock from this computer's clock.

Usage:  python settime.py COM5
Close PuTTY first: only one program can use the serial port at a time.
Needs:  pip install pyserial
"""
import sys
import time

import serial

port = sys.argv[1] if len(sys.argv) > 1 else "COM5"

with serial.Serial(port, 115200, timeout=1) as s:
    s.reset_input_buffer()
    s.write(b"\r")                      # an empty line makes the board print a fresh prompt
    time.sleep(0.3)
    command = "set time " + time.strftime("%Y-%m-%d %H:%M:%S") + "\r"
    s.write(command.encode("ascii"))
    time.sleep(0.5)
    print(s.read(300).decode("ascii", errors="replace"))
