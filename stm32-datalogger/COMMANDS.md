# Commands

Every command used in this project, in the order you would run them. Windows commands go in
Command Prompt, opened in the project folder:

```bat
cd /d C:\Users\ACER\Desktop\stm32-datalogger
```

The CP2102 is COM5 here. Check Device Manager, under Ports, if yours differs. Only one program
can use the port at a time, so close PuTTY before running a Python tool and the other way round.

## 1. One-time setup on Windows

```bat
py -m pip install pyserial matplotlib pillow
```
pyserial for the tools that talk to the board, matplotlib for the plots and the report's
figures, pillow for the photos in the report.

Also needed: STM32CubeIDE (it brings the ST-Link driver), PuTTY, and Git for Windows. For the
Makefile route instead of CubeIDE: the Arm GNU toolchain (arm-none-eabi-gcc), make and st-flash.
For the logic analyzer: PulseView (sigrok), and Zadig once to give the analyzer the WinUSB driver.
For the report PDF: Microsoft Word or LibreOffice.

```bat
git config --global user.name "Saman Fuyal"
git config --global user.email "your-email@example.com"
```
Once per computer, so the commits carry your name.

## 2. Build and flash

STM32CubeIDE, once:

1. New STM32 project, STM32F103C8Tx, project type Empty.
2. Delete Core/Src/main.c.
3. Copy `firmware/src/*.c`, `firmware/fatfs/ff.c`, `firmware/freertos/*.c` to Core/Src, and
   `firmware/src/*.h`, `firmware/fatfs/ff.h`, `diskio.h`, `ffconf.h`, `firmware/freertos/include/*.h`
   to Core/Inc. Not `firmware/startup/` (CubeIDE has its own).
4. Project > Properties > C/C++ Build > Settings > MCU GCC Compiler > Optimization: `-Os`.
   At `-O0` the program does not fit in 64 KB.
5. Run > Debug Configurations > Debugger > Reset behaviour: **Software system reset**.
   "Connect under reset" needs the NRST wire, which the 3-wire ST-Link cable does not have.

Then Build (hammer) and Run. The ST-Link connects to SWDIO, SWCLK and GND only, never 3V3.

If flashing fails while the board is in STOP: press the reset button and flash within 10 s, or
type `power sleep` first. Last resort: BOOT0 jumper to 1, reset, flash, BOOT0 back to 0, reset.
If CubeIDE says "could not connect (error 138)", end `ST-LINK_gdbserver.exe` in Task Manager.

Makefile route (in `firmware\`):

```bat
make
```
Debug build with every command: `build\datalogger.elf` and `.bin`, and prints the size.

```bat
make RELEASE=1
```
Same without the debug commands (scan, i2ctest, tscal, vddcal, selftest, mpusleep, crctest,
sd read, sd trace).

```bat
make flash
```
Writes `build\datalogger.bin` with st-flash.

## 3. Terminal and board commands

PuTTY: Connection type Serial, COM5, 115200. In STOP mode the first key only wakes the board:
press Enter once, then type. After a key the board stays awake for 30 s.

| Command | What it does |
|---|---|
| `help` | the list below, from the command table in cli.c |
| `start` / `stop` | start or stop logging (it starts by itself at power-on) |
| `set rate N` | N = 250 to 60000 ms between samples |
| `set time YYYY-MM-DD HH:MM:SS` / `time` | set or show the clock |
| `read` | one readable line from all sensors |
| `chip` | supply voltage and chip temperature |
| `zero` | re-zero the gyro (keep the board still for 1 s) |
| `status` | logger state and error counters |
| `ls` / `cat FILE` | list the card, print a file |
| `sd mount` / `sd init` | mount the card / start it and print its type and size |
| `tasks` | FreeRTOS tasks: priority, state, free stack, CPU share |
| `timing` / `timing reset` | how long each step takes, in microseconds |
| `power` | power mode and time spent in STOP |
| `power run` / `sleep` / `stop` | choose the low-power mode |
| `power test` / `power reset` | one 1 s STOP woken by the RTC alarm / clear the STOP counters |
| `clc` | clear the screen |
| `scan`, `bmpread`, `mpuread`, `i2ctest N` | (debug build) I2C checks |
| `tscal`, `vddcal V` | (debug build) chip temperature offset, VDD reference against a meter |
| `selftest`, `crctest`, `mpusleep` | (debug build) BMP280 formulas, CRC-32, MPU recovery |
| `sd read N`, `sd trace on` / `off` | (debug build) show a sector, trace the card start-up |

## 4. PC tools

```bat
py tools\settime.py COM5
```
Sets the board's clock from the laptop's clock. With the CR2032 fitted this is needed once.

```bat
py tools\live_plot.py COM5
```
Live plot of temperature, pressure, acceleration and gyro while the board logs. Click a legend
name to hide that line. Board commands typed in this window go to the board. Good lines are
also saved to `live_YYYYmmdd_HHMMSS.csv`.

```bat
py tools\getfile.py COM5
py tools\getfile.py COM5 LOG0123.CSV
```
Lists the files on the card, or copies one to the laptop over USB (no card reader). Checks the
size and every CRC.

```bat
py tools\log_check.py LOG0123.CSV --plot log_plot.png
```
Checks every CRC, reports gaps in time, and plots every column. `--x sample` plots against the
sample number instead of the time.

## 5. Measurements already done (to repeat them)

Timing (`results/timing.md`): logging at 1 s, then `set time`, `timing reset`, leave it for an
hour without typing, then `timing` and `tasks`.

Current at the 5 V input (`results/current_measurements.md`): multimeter on DC mA in series with
the 5V wire, ST-Link unplugged, board power-cycled, then for each mode `power run`, `power sleep`,
`power stop`, wait 30 s without typing and read the meter.

VDD calibration: measure the 3V3 pin with the meter, type `vddcal 3.22` (your reading), and put
the printed `#define VREF_X10` line into `firmware/src/config.h`.

## 6. Tests that are not done yet

### 6.1 Logic analyzer on PB12

PB12 (the TP pin) is high while the sensors are read. The analyzer measures the sample period and
the sensor-read time with its own clock, independent of the firmware.

1. Unplug the ST-Link. Analyzer channel D0 to the TP pin, analyzer GND to the board GND.
2. Power the board, wait 1 minute (STOP starts 10 s after reset and 30 s after the last key).
   With the CP2102 connected, type `timing reset`, then do not touch the keyboard.
3. PulseView: device "fx2lafw", sample rate 1 MHz, 100 M samples (100 s). Run.
4. Afterwards type `timing` and save what it prints.
5. File > Export > Comma-separated values. Tick the time column. If it offers "don't output
   duplicate rows", tick it too (the file becomes much smaller). Save as `pb12.csv`.
6. Measure:

```bat
py tools\la_check.py pb12.csv --analyzer "24 MHz 8-channel, PulseView" --sample-rate-text "1 MHz"
```
Prints the mean, smallest and largest period and the high time, and writes
`results\logic_analyzer.json` and `docs\captures\logic_analyzer.png`. If the file has no time
column, add `--rate 1000000`.

What to expect: a high time close to the firmware's 24.2 ms, and a period of about 1.0016 s if
the drift seen in LOG0123.CSV is real (see the main README). A cheap analyzer's crystal is good
to roughly 100 ppm; the expected difference is 1600 ppm.

7. Take a screenshot of one pulse in PulseView and save it as `docs\captures\pulseview_pulse.png`.

### 6.2 Battery current on the matrix board

No wire needs cutting: with the switch off, the meter in its place carries the whole current.

1. With the CP2102 connected, choose the mode first (`power run`, `power sleep` or `power stop`).
2. Unplug the CP2102 (the mode stays until the next reset) and the ST-Link.
3. Switch SW1 off. Meter on DC mA, probes on the two SW1 pins the board uses (middle pin and
   the ON pin). The board now runs through the meter.
4. Wait 30 s, read the meter. Measure the battery voltage too (DC V across the holder).
5. For the next mode, leave the meter where it is, plug the CP2102 back in, type the command
   (press Enter once first to wake the console), unplug the CP2102, wait 30 s and read again.

Write the values into `results\battery_current.json`:

```json
{
  "date": "15-11-2026",
  "battery_v": 3.95,
  "meter": "your meter's name",
  "rows_ma": [
    ["power run", 0.0],
    ["power sleep", 0.0],
    ["power stop", 0.0]
  ]
}
```
(replace the zeros and the date with your readings; the report computes the battery life from
the STOP value and the 2500 mAh in `results\measurements.json`).

### 6.3 Sample period without STOP mode

Tells whether STOP mode causes the 1.0016 s period (see the main README).

1. Clock set, logging at 1 s. Type `power sleep`, then `stop` and `start` (a new log file).
2. Leave it for an hour without typing.
3. `stop`, then copy the file and check it:

```bat
py tools\getfile.py COM5 LOG0124.CSV
py tools\log_check.py LOG0124.CSV
```
`log_check.py` prints "Period from the time stamps". If it says 1.0000 s with no 2 s steps, the
STOP hand-over in `power_idle()` is the cause. Type `power stop` afterwards to go back to normal.

## 7. PCB design in EasyEDA (software only)

1. In the schematic: Design > Convert Schematic to PCB.
2. Draw the board outline (for example 100 x 80 mm) and four M3 holes.
3. Check every footprint: pad numbers must equal the symbol's pin numbers. The Blue Pill is a
   DIP-40 with 600 mil row spacing; the modules are 2.54 mm pin headers.
4. Place the GY-521 near the centre, the power parts in one corner away from it, the Blue Pill
   with its SWD end at an edge, the SD slot and the USB connectors at the edges.
5. Route: 1.0 mm for the battery path, 0.6 mm for 5V and 3V3, 0.3 mm for signals, a GND pour on
   the bottom layer.
6. Design > Design Rule Check until it reports no errors. View > 3D view.
7. Export images (top, bottom, 3D) to `docs\pcb\` and, if wanted, the Gerber files to
   `hardware\pcb\`.

Write `results\pcb.json`:

```json
{
  "date": "20-11-2026",
  "tool": "EasyEDA Standard",
  "size_mm": [100, 80],
  "layers": 2,
  "drc_errors": 0,
  "notes": "one or two sentences about the layout",
  "images": ["docs/pcb/pcb_top.png", "docs/pcb/pcb_3d.png"]
}
```

## 8. The report

```bat
py docs\report\build_report.py
```
Builds `docs\report\report.docx` and `report.pdf` in the IEEE conference layout (two columns)
from the files in `results\` and the photos in `docs\`. The block diagrams, plots and
equations in `docs\report\figs\` are drawn again on every run. If any text in a diagram
touches a box, a line, an arrow or another text, the script stops and names the figure.

A section for the logic analyzer, the battery current or the PCB appears only when
`results\logic_analyzer.json`, `results\battery_current.json` or `results\pcb.json` exists.
The abstract, the discussion and the tables pick up the new numbers by themselves.

The PDF comes from LibreOffice if it is installed, otherwise from Microsoft Word. Close
report.docx in Word before running the script, or Windows will not let it write the file.

To add an Acknowledgment section, put one sentence in `ACKNOWLEDGMENT = ""` near the top of
`build_report.py`.

## 9. GitHub

### First upload

1. On github.com, sign in, click **+** (top right) > **New repository**.
2. Name: `stm32-datalogger`. Description, for example: "Low-power STM32F103 data logger on
   FreeRTOS, registers only". Public. Leave "Add a README", ".gitignore" and "license" all
   off (the project has its own README, .gitignore and LICENSE; a file made by GitHub would
   block the first push).
   Click **Create repository** and copy the address it shows,
   `https://github.com/YOUR-USER-NAME/stm32-datalogger.git`.
3. Unzip the project to `C:\Users\ACER\Desktop\stm32-datalogger`, so that README.md is
   directly inside that folder (not inside a second `stm32-datalogger` folder).
4. Command Prompt in that folder:

```bat
cd /d C:\Users\ACER\Desktop\stm32-datalogger
git init
git add .
git status
```
Warnings like "CRLF will be replaced by LF" are normal: `.gitattributes` stores every text
file with LF line ends. `git status` lists the files that will go up. Check that there is no `build\`, `Debug\` or
`__pycache__\` folder in the list (`.gitignore` keeps them out).

```bat
git commit -m "STM32 data logger: firmware, tools, hardware, results, report"
git branch -M main
git remote add origin https://github.com/YOUR-USER-NAME/stm32-datalogger.git
git push -u origin main
```
The first push opens a browser window to sign in to GitHub. Afterwards, refresh the
repository page: the README with the board photo shows on the front page.

5. On the repository page, click the gear next to **About** and add topics: `stm32`,
   `freertos`, `embedded`, `data-logger`, `bare-metal`, `low-power`.

If the push says "rejected ... fetch first", the repository on GitHub was created with a file
in it. Run `git pull origin main --allow-unrelated-histories`, close the editor that opens
(it asks for a merge message), then `git push -u origin main`.

If it says "remote origin already exists", run
`git remote set-url origin https://github.com/YOUR-USER-NAME/stm32-datalogger.git` and push
again.

### After a test (for example the logic analyzer)

```bat
py tools\la_check.py pb12.csv --analyzer "24 MHz 8-channel, PulseView" --sample-rate-text "1 MHz"
py docs\report\build_report.py
git status
git add -A
git commit -m "Logic analyzer test on PB12"
git push
```
`git status` shows what changed: the new results file, the new capture and the rebuilt
report. Only `git push` sends it to GitHub; until then everything stays on the laptop.

To undo a change to a file that is not committed yet: `git restore FILE`.
