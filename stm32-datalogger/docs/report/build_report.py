#!/usr/bin/env python3
"""Build the project report (Word and PDF) from the files in results/ and docs/.

    py docs\\report\\build_report.py        -> docs/report/report.docx and report.pdf

The layout follows the IEEE conference template (template/conference-template-a4.docx) in two
columns: numbered headings, "Fig. n." captions under figures, "TABLE n" titles over tables,
numbered equations and IEEE references, numbered in the order they are cited.

Every number comes from a file:
  results/measurements.json      timing, current, build size (values measured so far)
  results/LOG0123.CSV            the example log: line count, CRCs, value ranges, period drift
  firmware/src/config.h          VREF_X10, the accelerometer calibration and the other settings
A section is added only when its results file exists:
  results/logic_analyzer.json    written by tools/la_check.py
  results/battery_current.json   typed in by hand (COMMANDS.md section 6.2)
  results/pcb.json               typed in by hand (COMMANDS.md section 7)
The block diagrams, plots and equations in docs/report/figs/ are drawn again on every run, and
the run stops if any text in a diagram touches a box, a line, an arrow or another text.

Needs: py -m pip install matplotlib pillow
The PDF comes from LibreOffice if it is installed, otherwise from Microsoft Word (Windows).
"""
import json
import os
import re
import shutil
import statistics
import subprocess
import sys
import zipfile
from collections import Counter
from datetime import datetime

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
FIGS = os.path.join(HERE, "figs")
TEMPLATE = os.path.join(HERE, "template", "conference-template-a4.docx")
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import docx_lib as D                                   # noqa: E402
import figures as FG                                   # noqa: E402
from log_check import TIME_FORMAT, parse_line          # noqa: E402

TITLE = "Design and Timing Verification of an RTOS-Based Sensor Data Logger on an STM32F103"
# One sentence of thanks, if you want an Acknowledgment section (left out while empty).
ACKNOWLEDGMENT = ""
COL = 243.0            # one column in pt (A4, two columns)
FULL = 504.6           # both columns in pt
MAX_FIG_H = 330.0      # tallest figure in pt


# ================================================================== data

def path(*p):
    return os.path.join(ROOT, *p)


def load_json(rel, required=False):
    p = path(rel)
    if not os.path.exists(p):
        if required:
            sys.exit(f"missing {rel}")
        return None
    with open(p, encoding="utf-8") as f:
        return json.load(f)


def config_values():
    """#define NAME value lines from firmware/src/config.h."""
    vals = {}
    with open(path("firmware", "src", "config.h"), encoding="utf-8") as f:
        for line in f:
            m = re.match(r"\s*#define\s+(\w+)\s+(0x[0-9A-Fa-f]+|-?\d+)U?\b", line)
            if m:
                vals[m.group(1)] = int(m.group(2), 0)
    return vals


def analyze_log(rel):
    rows, bad, first_line = [], 0, None
    with open(path(rel), encoding="ascii", errors="replace") as f:
        for line in f:
            kind, values = parse_line(line)
            if kind == "ok":
                rows.append(values)
                first_line = first_line or line.rstrip("\r\n")
            elif kind == "bad":
                bad += 1
    stamps = [datetime.strptime(r[0], TIME_FORMAT) for r in rows]
    steps = [(b - a).total_seconds() for a, b in zip(stamps, stamps[1:])]
    span = (stamps[-1] - stamps[0]).total_seconds()
    names = "temp_C press_hPa ax_g ay_g az_g gx_dps gy_dps gz_dps vdd_V chip_C".split()
    stats = {}
    for i, n in enumerate(names, 1):
        v = [float(r[i]) for r in rows if r[i] != "nan"]
        stats[n] = (min(v), max(v), statistics.mean(v), statistics.pstdev(v))
    skips = [i + 1 for i, s in enumerate(steps) if s == 2.0]     # data line after which it jumps
    return {
        "file": os.path.basename(rel), "lines": len(rows), "bad": bad,
        "first": stamps[0], "last": stamps[-1], "span": span, "steps": Counter(steps),
        "skips": skips, "period": span / (len(rows) - 1), "stats": stats, "stamps": stamps,
        "gaps": sum(1 for s in steps if s > 2.5 or s < 0), "sample": first_line,
        "rows": rows,
    }


def code_size():
    src = path("firmware", "src")
    files = [f for f in os.listdir(src) if f.endswith((".c", ".h"))]
    lines = 0
    for f in files:
        with open(os.path.join(src, f), encoding="utf-8", errors="replace") as fh:
            lines += sum(1 for _ in fh)
    return {"files": len(files), "lines": lines,
            "c": sum(f.endswith(".c") for f in files), "h": sum(f.endswith(".h") for f in files)}


def col(L, i):
    """Column i of the log as floats (nan where the sensor gave none)."""
    return [float(r[i]) for r in L["rows"]]


def us(v):
    return f"{v:,}".replace(",", " ")


def photo(src, name, max_px=1800):
    """Smaller copy of a photo for the report (the originals stay in docs/)."""
    from PIL import Image
    out = os.path.join(FIGS, name)
    im = Image.open(src)
    im.thumbnail((max_px, max_px))
    if name.endswith(".jpg"):
        im.convert("RGB").save(out, quality=88)
    else:
        im.save(out)
    return out


# ================================================================== figures

def draw_figures(M, L):
    os.makedirs(FIGS, exist_ok=True)
    t = {r[0]: r for r in M["timing"]["rows_us"]}
    F = {
        "system": FG.fig_system(os.path.join(FIGS, "fig_system.png")),
        "firmware": FG.fig_firmware(os.path.join(FIGS, "fig_firmware.png")),
        "idle": FG.fig_idle(os.path.join(FIGS, "fig_idle.png")),
        "timing": FG.fig_timing(os.path.join(FIGS, "fig_timing.png"), t["sensor read"][2] / 1000,
                                t["line + CRC"][2] / 1000, t["UART (DMA)"][2] / 1000,
                                t["SD write"][2] / 1000, t["sample -> SD"][2] / 1000,
                                len(L["sample"]) + 2, M["timing"]["baud"]),
        "drift": FG.fig_drift(os.path.join(FIGS, "fig_drift.png"), L["stamps"], L["period"], L["file"]),
        "logplot": FG.fig_log(os.path.join(FIGS, "fig_log.png"), L["stamps"], [
            ("temperature (\u00b0C)", [("BMP280", col(L, 1), FG.RED), ("chip", col(L, 10), FG.ORANGE)]),
            ("pressure (hPa)", [("pressure", col(L, 2), FG.BLUE)]),
            ("accel. Z (g)", [("az", col(L, 5), FG.GREEN)]),
            ("rotation Z (\u00b0/s)", [("gz", col(L, 8), FG.PURPLE)])]),
    }
    if FG.COLLISIONS:
        sys.exit("text collides in a figure, fix figures.py first:\n  " + "\n  ".join(FG.COLLISIONS))
    keep = {"fig_system.png", "fig_firmware.png", "fig_idle.png", "fig_timing.png", "fig_drift.png",
            "fig_log.png", "photo_matrix.jpg", "photo_current.jpg"}
    for f in os.listdir(FIGS):                          # left over from earlier layouts
        if not f.startswith("eq_") and f not in keep:
            os.remove(os.path.join(FIGS, f))
    F["matrix"] = photo(path("docs", "photos", "matrix_rev_b_top.jpg"), "photo_matrix.jpg")
    F["meter"] = photo(path("docs", "photos", "current_stop_mode.jpg"), "photo_current.jpg")
    return F


EQUATIONS = [
    ("brr", r"\mathrm{BRR} = \dfrac{f_{\mathrm{PCLK}}}{B} = \dfrac{8\,000\,000}{115\,200} = 69.4"
            r"\;\rightarrow\; 69 = \mathrm{0x45}"),
    ("rtc", r"f_{\mathrm{RTC}} = \dfrac{32\,768\ \mathrm{Hz}}{\mathrm{PRL} + 1} = 1024\ \mathrm{Hz},"
            r"\quad t = t_{\mathrm{base}} + \dfrac{\mathrm{CNT}}{1024}"),
    ("step", r"c \leftarrow c + 1000\,n,\quad \Delta_{\mathrm{ms}} = \left\lfloor \dfrac{c}{1024}"
             r" \right\rfloor,\quad c \leftarrow c - 1024\,\Delta_{\mathrm{ms}}"),
    ("vdd", r"V_{\mathrm{DD}} = V_{\mathrm{REFINT}} \cdot \dfrac{4095 \cdot 32}{\sum D_{\mathrm{REF},k}}"),
    ("vts", r"V_{\mathrm{TS}} = 1.20\ \mathrm{V} \cdot \dfrac{\sum D_{\mathrm{TS},k}}{\sum D_{\mathrm{REF},k}}"),
    ("temp", r"T_{\mathrm{chip}} = 25\,^{\circ}\mathrm{C} + \dfrac{1.43\ \mathrm{V} - V_{\mathrm{TS}}}"
             r"{4.3\ \mathrm{mV}/^{\circ}\mathrm{C}} + T_{\mathrm{off}}"),
    ("acc", r"a_i = \dfrac{D_{a,i} - O_{a,i}}{S_{a,i}}\ \mathrm{g},\quad \omega_i = \dfrac{D_{g,i} - O_{g,i}}"
            r"{131}\ ^{\circ}/\mathrm{s}"),
    ("period", r"\bar{P} = \dfrac{t_N - t_1}{N - 1}"),
    ("life", r"t_{\mathrm{bat}} = \dfrac{C \cdot V_{\mathrm{bat}} \cdot \eta}{V_{\mathrm{in}} \cdot I_{\mathrm{in}}}"),
]


def draw_equations():
    out = {}
    for key, tex in EQUATIONS:
        png = D.render_equation(tex, os.path.join(FIGS, f"eq_{key}.png"), fontsize=9.5)
        from PIL import Image
        if Image.open(png).size[0] / 600 * 72 > COL - 30:
            sys.exit(f"equation {key} is wider than a column")
        out[key] = png
    return out


# ================================================================== references

REFS = {
    "rm0008": "STMicroelectronics, \u201cRM0008 Reference manual: STM32F101xx, STM32F102xx, STM32F103xx, "
              "STM32F105xx and STM32F107xx advanced Arm-based 32-bit MCUs.\u201d",
    "ds5319": "STMicroelectronics, \u201cSTM32F103x8, STM32F103xB medium-density performance line Arm-based "
              "32-bit MCU,\u201d data sheet DS5319.",
    "pm0056": "STMicroelectronics, \u201cPM0056 Programming manual: STM32F10xxx/20xxx/21xxx/L1xxxx Cortex-M3.\u201d",
    "yiu": "J. Yiu, *The Definitive Guide to ARM Cortex-M3 and Cortex-M4 Processors*, 3rd ed. Oxford, U.K.: "
           "Newnes, 2014.",
    "freertos": "FreeRTOS, \u201cFreeRTOS kernel V11.3.1.\u201d [Online]. Available: https://www.freertos.org",
    "barry": "R. Barry, *Mastering the FreeRTOS Real Time Kernel: A Hands-On Tutorial Guide*. Real Time "
             "Engineers Ltd., 2016.",
    "fatfs": "ChaN, \u201cFatFs: Generic FAT filesystem module, R0.16.\u201d [Online]. Available: "
             "http://elm-chan.org/fsw/ff/",
    "sdspec": "SD Association, \u201cSD Specifications Part 1: Physical Layer Simplified Specification.\u201d",
    "bmp280": "Bosch Sensortec, \u201cBMP280 digital pressure sensor,\u201d data sheet BST-BMP280-DS001.",
    "mpu_ps": "InvenSense, \u201cMPU-6000 and MPU-6050 product specification,\u201d PS-MPU-6000A-00.",
    "mpu_rm": "InvenSense, \u201cMPU-6000 and MPU-6050 register map and descriptions,\u201d RM-MPU-6000A-00.",
    "koopman": "P. Koopman, \u201c32-bit cyclic redundancy codes for Internet applications,\u201d in *Proc. Int. "
               "Conf. Dependable Systems and Networks (DSN)*, Washington, DC, USA, 2002, pp. 459\u2013468.",
    "baker": "E. Baker, \u201cOpen source data logger for low-cost environmental monitoring,\u201d *Biodiversity "
             "Data Journal*, vol. 2, Art. no. e1059, 2014, doi: 10.3897/BDJ.2.e1059.",
    "beddows": "P. A. Beddows and E. K. Mallon, \u201cCave Pearl Data Logger: A flexible Arduino-based logging "
               "platform for long-term monitoring in harsh environments,\u201d *Sensors*, vol. 18, no. 2, "
               "Art. no. 530, 2018, doi: 10.3390/s18020530.",
    "wickert": "A. D. Wickert, C. T. Sandell, B. Schulz, and G.-H. C. Ng, \u201cOpen-source Arduino-compatible "
               "data loggers designed for field research,\u201d *Hydrol. Earth Syst. Sci.*, vol. 23, "
               "pp. 2065\u20132076, 2019, doi: 10.5194/hess-23-2065-2019.",
    "hypnos": "B. Nguyen, B. Goto, J. S. Selker, and C. Udell, \u201cHypnos board: A low-cost all-in-one solution "
              "for environment sensor power management, data storage, and task scheduling,\u201d *HardwareX*, "
              "vol. 10, Art. no. e00213, 2021, doi: 10.1016/j.ohx.2021.e00213.",
    "butler": "J. D. Butler, E. C. Hensel, N. Meltzer, and R. J. Robinson, \u201cExtending field deployment of "
              "wireless personal use monitors via STM32L4 low-power configurations,\u201d *Electronics*, vol. 15, "
              "no. 21, Art. no. 4983, 2026, doi: 10.3390/electronics15214983.",
    "tp4056": "NanJing Top Power, \u201cTP4056 1A standalone linear Li-ion battery charger,\u201d data sheet.",
    "mt3608": "Aerosemi, \u201cMT3608 high efficiency 1.2 MHz 2 A step up converter,\u201d data sheet.",
    "cp2102": "Silicon Labs, \u201cCP2102/9 single-chip USB-to-UART bridge,\u201d data sheet.",
}
_order = []


def cite(text):
    """Replace {key1,key2} citation tokens with IEEE numbers in order of first use."""
    def rep(m):
        nums = []
        for k in m.group(1).split(","):
            k = k.strip()
            if k not in REFS:
                raise KeyError(k)
            if k not in _order:
                _order.append(k)
            nums.append(_order.index(k) + 1)
        nums.sort()
        return ", ".join(f"[[REF:{n}]]" for n in nums)
    return re.sub(r"(?<![\^_])\{([a-z0-9_]+(?:,\s*[a-z0-9_]+)*)\}", rep, text)


# ================================================================== document body

class Doc:
    """Collects the body XML. Figure and table numbers come from a first pass (LABELS)."""

    def __init__(self, labels):
        self.B = []
        self.img = D.Images()
        self.labels = labels            # key -> number, from the previous pass
        self.new = {}
        self.nfig = 0
        self.ntab = 0
        self.neq = 0

    def fig(self, key):
        return f"Fig. {self.labels.get('f:' + key, '?')}"

    def tab(self, key):
        n = self.labels.get("t:" + key, 0)
        return "TABLE " + (ROMAN[n - 1] if n else "?")

    def P(self, text):
        self.B.append(D.para("BodyText", D.rich(cite(self.refs(text)))))

    def refs(self, text):
        text = re.sub(r"\[eq:(\w+)\]", lambda m: f"({self.labels.get('e:' + m.group(1), '?')})", text)
        text = re.sub(r"\[fig:(\w+)\]", lambda m: self.fig(m.group(1)), text)
        return re.sub(r"\[tab:(\w+)\]", lambda m: self.tab(m.group(1)), text)

    def H1(self, text):
        self.B.append(D.para("Heading1", D.rich(text)))

    def H2(self, text):
        self.B.append(D.para("Heading2", D.rich(text)))

    def bullets(self, items):
        for it in items:
            self.B.append(D.para("bulletlist", D.rich(cite(self.refs(it)))))

    def FIG(self, key, file, caption, full=False):
        """Figure in one column, or across both columns with full=True. Diagrams drawn by figures.py
        are placed at the size they were drawn (300 dpi), so their text prints at its true size."""
        self.nfig += 1
        self.new["f:" + key] = self.nfig
        from PIL import Image
        iw, ih = Image.open(file).size
        width = FULL if full else COL
        if os.path.basename(file).startswith("fig_"):
            width = min(width, iw / 300 * 72)
        width = min(width, MAX_FIG_H * iw / ih)        # tall images: keep figure and caption together
        if full:
            self.B.append(D.section_break(cols=2))
        self.B.append(D.image_para(self.img, file, width))
        self.B.append(D.para("figurecaption", D.rich(cite(self.refs(caption)))))
        if full:
            self.B.append(D.section_break(cols=1))

    def TABLE(self, key, title, rows, widths, align=None, full=False):
        self.ntab += 1
        self.new["t:" + key] = self.ntab
        room = (FULL if full else COL) - 2
        scale = room / sum(widths) if sum(widths) > room else 1.0
        widths = [round(w * scale, 1) for w in widths]
        if full:
            self.B.append(D.section_break(cols=2))
        self.B.append(D.para("tablehead", D.rich(title), keep_next=True))
        self.B.append(D.table([[cite(c) for c in r] for r in rows], widths, align=align))
        self.B.append(D.para("BodyText", "", extra_ppr='<w:spacing w:after="0pt"/>'))
        if full:
            self.B.append(D.section_break(cols=1))

    def EQ(self, png, key):
        self.neq += 1
        self.new["e:" + key] = self.neq
        # template equation style, but single line spacing: its fixed 10.8 pt line would cut a tall
        # equation image at the top of a column
        self.B.append(f'<w:p><w:pPr><w:pStyle w:val="equation"/><w:keepLines/>'
                      f'<w:spacing w:before="6pt" w:after="6pt" w:line="240" w:lineRule="auto"/></w:pPr>'
                      f'{D.tab()}{D.image_run(self.img, png)}{D.tab()}'
                      f'{D.run(f"({self.neq})", font="Times New Roman")}</w:p>')

    def CODE(self, lines, width=58):
        """Lines of code in the column, 6.5 pt, wrapped after a comma when longer than `width`."""
        out = []
        for ln in lines:
            cur = ""
            for part in re.findall(r"[^,]*,?", ln):
                if cur and len(cur) + len(part) > width:
                    out.append(cur)
                    cur = "  "
                cur += part
            out.append(cur)
        for ln in out:
            self.B.append(D.para("BodyText", D.run(ln, font="Courier New", size=13),
                                 extra_ppr='<w:spacing w:after="0pt" w:line="8.5pt" w:lineRule="exact"/>'
                                           '<w:ind w:firstLine="0pt"/><w:jc w:val="start"/>'))
        self.B.append(D.para("BodyText", "", extra_ppr='<w:spacing w:after="0pt"/>'))


ROMAN = "I II III IV V VI VII VIII IX X XI XII XIII XIV XV".split()


def build(labels, M, L, C, LA, BAT, PCB, F, E):
    _order.clear()
    d = Doc(labels)
    P, H1, H2 = d.P, d.H1, d.H2
    T = M["timing"]
    t = {r[0]: r for r in T["rows_us"]}
    cur = M["current"]
    cm = {r[0]: r[2] for r in cur["rows_ma"]}
    eff, vb, vs = cur["boost_efficiency"], cur["battery_v"], cur["supply_v"]

    def battery_days(ma_at_5v):
        return cur["battery_mah"] * vb * eff / (vs * ma_at_5v) / 24

    stop_cut = 100 * (1 - cm["power stop"] / cm["power run"])
    vref = C.get("VREF_X10", 12000) / 10000
    ts_off = C.get("TS_OFFSET_C100", 0)
    st = L["stats"]
    drift_ms = (L["period"] - 1) * 1000
    drift_pct = (L["period"] - 1) * 100
    mb = M["matrix_board"]
    bi = M["build"]
    code = code_size()
    read_ms = t["sensor read"][2] / 1000
    to_card_ms = t["sample -> SD"][2] / 1000
    line_bytes = len(L["sample"]) + 2
    bat_stop = None
    if BAT:
        bat_stop = dict((r[0], r[1]) for r in BAT["rows_ma"]).get("power stop")

    # ------------------------------------------------------------ title block
    d.B.append(D.para("papertitle", D.rich(TITLE)))
    d.B.append(D.para("Author", D.run("Saman Fuyal") + D.brk()
                      + D.run("Department of Electrical and Electronics Engineering", i=True) + D.brk()
                      + D.run("Kathmandu University", i=True) + D.brk() + D.run("Dhulikhel, Nepal")
                      + D.brk() + D.run("smnfyl@gmail.com"),
                      extra_ppr='<w:spacing w:before="6pt" w:after="12pt"/>'))
    d.B.append(D.section_break(cols=1, first=True, top="54pt"))      # title across the page

    # ------------------------------------------------------------ abstract
    la_txt = ""
    if LA:
        la_txt = (f" A logic analyzer on the test pin measured a mean period of {LA['period_mean_s']:.6f} s "
                  f"with its own clock.")
    d.B.append(D.para("Abstract", D.run("Abstract", i=True, b=True) + D.run("\u2014", b=True) + D.rich(
        "A data logger has to take samples at a steady rate, keep the correct time through a power cut, "
        "store data that can be checked later, and use little current between samples. This report "
        "describes such a logger built on an STM32F103C8 microcontroller. It reads a BMP280 pressure and "
        "temperature sensor and an MPU6050 accelerometer and gyroscope over I2C, adds the chip's own "
        "temperature and supply voltage, and writes one CSV line per sample to a micro SD card with a "
        "CRC-32 at the end of every line. The firmware is written on the registers without a vendor "
        "library and runs four FreeRTOS tasks. Between samples the chip sleeps in STOP mode, and the "
        "real-time clock, running at 1024 Hz from a 32.768 kHz crystal, wakes it 1 ms before the next "
        f"sample. In a {T['minutes']}-minute run at one sample per second the period measured by the "
        f"firmware stayed within \u00b12 \u00b5s, a sample reached the card {to_card_ms:.1f} ms after it was "
        f"taken, and {us(L['lines'])} lines had {L['bad']} CRC errors. STOP mode lowered the board "
        f"current from {cm['power run']:g} mA to {cm['power stop']:g} mA, a {stop_cut:.0f} % reduction; the rest is drawn by the "
        f"off-the-shelf modules, which stay powered. "
        f"Comparing the time stamps with the line count showed that the real sample period is "
        f"{L['period']:.4f} s, an error of {drift_pct:.2f} % that the firmware's own timing cannot see "
        f"because it uses the same clock that schedules the samples.{la_txt} The final circuit runs from "
        "an 18650 cell on a matrix board with a charger, a boost converter and a coin cell for the clock.",
        base_b=True)))
    d.B.append(D.para("Keywords", D.run("Keywords\u2014", b=True, i=True) + D.rich(
        "data logger, STM32F103, FreeRTOS, tickless idle, STOP mode, real-time clock, SD card, FatFs, "
        "CRC-32, power management", base_b=True)))

    # ------------------------------------------------------------ I introduction
    H1("Introduction")
    P("A data logger records measurements over a long time without a computer attached. For "
      "environmental and motion data this usually means a small board that runs from a battery, keeps "
      "the correct time through a power cut, and stores its data in a form that opens on any computer. "
      "Many such loggers have been published, most of them built on Arduino boards with an external "
      "real-time clock chip and an SD card {baker, beddows, wickert}.")
    P("This project builds a logger from cheap, easy-to-get modules around an STM32F103C8 "
      "microcontroller (the \u201cBlue Pill\u201d board) {ds5319}. The aim was to understand every layer of it: "
      "the peripheral registers {rm0008}, a real-time operating system {freertos}, the FAT file system on "
      "the card {fatfs} and the battery supply. The logger had to:")
    d.bullets([
        "read temperature, pressure, acceleration and rotation at a fixed rate, 1 sample/s by default and "
        f"settable from {C.get('RATE_MS_MIN', 250)} ms to {C.get('RATE_MS_MAX', 60000) // 1000} s;",
        "stamp every sample with a clock that keeps running when the power is off;",
        "store each sample on a FAT-formatted micro SD card and lose as little as possible when the power "
        "fails or the card is pulled out;",
        "make every stored line checkable, so a damaged line can be found afterwards;",
        "use as little current as possible between samples and run from a rechargeable cell;",
        "start logging by itself at power-on, and offer a command line over USB for setting the clock, "
        "reading the sensors and measuring its own timing.",
    ])
    P("The contributions of this work are:")
    d.bullets([
        "a register-level FreeRTOS firmware in which a sensor task, a logger task and a command-line task "
        "share the buses through ordered mutexes, so slow SD card writes never delay a sample;",
        "tickless idle with STOP mode woken by a 1024 Hz real-time clock through EXTI events, which "
        f"lowered the board current by {stop_cut:.0f} %;",
        "a CSV format with a CRC-32 on every line, checked by PC tools while logging and afterwards;",
        "timing measured on the target for every step of a sample, and a check of the sample period "
        f"against the RTC time stamps, which found a {drift_pct:.2f} % period error that the on-target "
        "measurement could not see.",
    ])
    P("Section II reviews related loggers. Sections III and IV describe the architecture and the design, "
      "Section V how the logger was developed and tested, Section VI the results, and Section VII "
      "concludes.")

    # ------------------------------------------------------------ II related work
    H1("Related Work and Research Gap")
    P("Most published open-source loggers are built on Arduino boards. Baker built one for environmental "
      "monitoring on an Arduino Mega, with a micro SD card, a DS1307 clock backed by a CR2032 and a sleep "
      "library {baker}. Beddows and Mallon developed the Cave Pearl logger on an ATmega328P with a DS3231 "
      "clock {beddows}; it buffers readings in an EEPROM, writes them to the SD card in groups and sleeps "
      "at about 0.2 mA, which gives more than a year on three AA cells. Wickert et al. described loggers on "
      "the ATmega328p and 644p with a DS3231, an SD card and a watchdog, which sleep at about 12 \u00b5A and "
      "run for about two years on three AA cells {wickert}.")
    P("Two works use 32-bit Arm microcontrollers. Nguyen et al. built Hypnos, a board for an Adafruit "
      "Feather M0 (a Cortex-M0+) that adds a micro SD slot, a DS3231 with a coin cell, and switched supply "
      "rails so the sensors and storage can be powered down between samples {hypnos}. In a field test on "
      "a 6600 mAh cell it took about 25 times more samples than the same system without it; the software "
      "is Arduino code without an operating system. Butler et al. built personal-use monitors on an "
      "STM32L433 and compared its low-power modes {butler}. The monitors log to external flash and wake "
      "from STOP every 60 s with the RTC alarm. For the microcontroller alone, the vendor's estimate in "
      "STOP1 mode is 4.34 \u00b5A, and the whole device ran a mean of 30.15 days on a 350 mAh cell. Their "
      "timing check is the continuity of the time stamps.")
    P("[tab:related] sets these works beside this one. The columns show what each paper reports, and "
      "the last column says what each current figure covers, because the numbers are not measured in the "
      "same way.")
    d.TABLE("related", "Comparison with Published Loggers", [
        ["Work", "Micro-controller", "Software", "Storage", "Time stamps", "Check per line",
         "Timing checked against", "Current reported (what it covers)"],
        ["{baker}", "ATmega2560", "Arduino loop", "micro SD", "DS1307 + CR2032", "not described",
         "not described", "not reported"],
        ["{beddows}", "ATmega328P", "Arduino loop", "micro SD, EEPROM buffer", "DS3231", "not described",
         "not described", "about 0.2 mA asleep (logger)"],
        ["{wickert}", "ATmega328p / 644p", "Arduino loop", "SD card", "DS3231", "not described",
         "not described", "about 12 \u00b5A asleep (logger)"],
        ["{hypnos}", "SAMD21 (Cortex-M0+)", "Arduino loop", "micro SD", "DS3231 + coin cell",
         "not described", "not described", "not reported; 25\u00d7 more samples per charge"],
        ["{butler}", "STM32L433 (Cortex-M4)", "not stated", "external flash", "internal RTC",
         "not described", "time stamp continuity", "4.34 \u00b5A in STOP1 (MCU, estimate); 30 days on 350 mAh"],
        ["This work", "STM32F103C8 (Cortex-M3)", "FreeRTOS, registers", "micro SD (FatFs)",
         "internal RTC at 1024 Hz + CR2032", "CRC-32", "RTC time stamps (tick vs. crystal)",
         f"{cm['power stop']:g} mA in STOP (whole breadboard with modules, at 5 V)"],
    ], [42, 62, 54, 58, 66, 48, 66, 108], align="llllllll", full=True)
    P("The purpose-built loggers draw two to three orders of magnitude less current than this board. "
      "Their boards are designed for it, and Hypnos switches off the sensors and storage between samples. "
      "This logger uses ready-made modules: the SD module's regulator, the power LEDs and the sensors stay "
      "powered, so its current is set by the modules, not by the microcontroller. Its contribution is on "
      "the firmware side. As far as the papers describe, none of the other loggers uses an operating "
      "system to keep sampling independent of slow storage, stores a checksum with each line, or checks "
      "its sample period against a second clock. Butler et al. come closest, checking that the time "
      "stamps are continuous, which shows missing records but not a period that is slightly too long.")

    # ------------------------------------------------------------ III architecture
    H1("System Architecture")
    P("[fig:system] shows the logger. The STM32F103C8 is an Arm Cortex-M3 with 64 KB of flash and "
      "20 KB of RAM {ds5319, yiu}. It runs from its internal 8 MHz RC oscillator (HSI). A faster clock is "
      "not needed, because the work per sample is small, and the HSI is running again immediately after "
      "STOP mode, which matters when the chip wakes up every second. The 32.768 kHz crystal drives the "
      "real-time clock (RTC), and a CR2032 on the VBAT pin keeps the RTC and its backup registers running "
      "when the main supply is off.")
    d.FIG("system", F["system"], "System architecture. The dashed line is the microcontroller; the boxes "
          "inside it are the peripherals the firmware uses.", full=True)
    P("Both sensors share the I2C bus at 100 kHz. The BMP280 measures temperature and pressure "
      "{bmp280}; the MPU6050 on a GY-521 module measures acceleration and rotation {mpu_ps, mpu_rm}. "
      "The SD card is used in SPI mode {sdspec}. A CP2102 USB-to-UART bridge connects the board to a PC "
      "for commands and live data {cp2102}. [tab:parts] lists the parts and [tab:pins] the pins.")
    d.TABLE("parts", "Main Parts", [
        ["Part", "Function", "Interface"],
        ["STM32F103C8T6 (Blue Pill)", "Cortex-M3, 64 KB flash, 20 KB RAM", "SWD for programming"],
        ["BMP280 module (4 pins)", "temperature and pressure", "I2C, address 0x76"],
        ["GY-521 (MPU6050)", "3-axis accelerometer and gyroscope", "I2C, address 0x68"],
        ["micro SD card module", "storage, own 3.3 V regulator and level shifter", "SPI, 5 V supply"],
        ["CP2102 module", "USB to serial for commands and live data", "UART, 115 200 baud"],
        ["18650 cell, TP4056 + DW01", "battery, charger with protection", "USB-C charging"],
        ["MT3608 module", "boost converter, set to 5.3 V", "SW1 in its input"],
        ["2 \u00d7 1N5819", "Schottky diodes joining battery and USB supply", "D1 USB, D2 boost"],
        ["CR2032 in a holder", "keeps the RTC and backup registers alive", "VBAT pin"],
        ["2 \u00d7 4.7 k\u03a9", "I2C pull-ups", "SCL, SDA to 3.3 V"],
    ], [70, 100, 73])
    d.TABLE("pins", "Microcontroller Pins", [
        ["Signal", "Pin", "Connected to"],
        ["UART TX / RX", "PA9 / PA10", "CP2102 RXD / TXD; a falling edge on PA10 also wakes the chip from STOP"],
        ["I2C SCL / SDA", "PB6 / PB7", "BMP280 and MPU6050, 4.7 k\u03a9 to 3.3 V"],
        ["SPI CS / SCK / MISO / MOSI", "PA4 / PA5 / PA6 / PA7", "SD card module"],
        ["LED", "PC13", "on-board LED, active low"],
        ["Test point", "PB12", "high while the sensors are read"],
        ["Clock battery", "VBAT", "CR2032 positive terminal"],
        ["Debug", "SWDIO, SWCLK", "ST-Link V2 (ground only, never its 3.3 V)"],
    ], [62, 62, 119])
    P("The power path is drawn at the bottom of [fig:system]. The 18650 cell connects only to the TP4056 module, "
      "which charges it from USB-C and disconnects it when it is over-discharged or shorted, through its "
      f"DW01 protection chip {{tp4056}}. From the module's output the current goes through the switch SW1 "
      f"to the MT3608 boost converter {{mt3608}}, set to {mb['boost_setting_v']:.2f} V. The Schottky diode "
      "D2 after the converter leaves about 5.0 V on the board's 5 V line. The CP2102's USB 5 V reaches "
      "the same line through D1, so the board runs from either source and neither can push current into "
      "the other. With both connected, the battery side is higher (about 5.0 V against 4.7 V) and supplies "
      "the board. The Blue Pill's 3.3 V regulator supplies the microcontroller and both sensors; the SD "
      "module takes 5 V because it has its own regulator. The board ground is the TP4056's output "
      "ground, after the protection switch, not the cell's negative terminal. The full schematic, drawn "
      "in EasyEDA, is in the hardware folder of the repository.")

    # ------------------------------------------------------------ IV design
    H1("Design Methodology")
    H2("Firmware Structure")
    P(f"The firmware is {us(code['lines'])} lines of C in {code['files']} files ({code['c']} .c and "
      f"{code['h']} .h), plus FatFs R0.16 {{fatfs}} and the FreeRTOS kernel V11.3.1 {{freertos}}. Every "
      "peripheral is set up through its registers {rm0008}; the addresses and bit names are collected in "
      "board.h. Drivers return error codes and never print; only the logger and the command line do. "
      f"Built with {bi['tool']}, the program uses {bi['flash_kb']} KB of the {bi['flash_total_kb']} KB "
      f"of flash and {bi['ram_kb']} KB of the {bi['ram_total_kb']} KB of RAM, 10 KB of which is the "
      "FreeRTOS heap. [fig:firmware] shows the layers and [tab:tasks] the tasks.")
    d.FIG("firmware", F["firmware"], "Firmware layers. The sensor task and the logger task are joined by "
          "a queue; the three mutexes protect the UART, the I2C bus and the SD card.", full=True)
    d.TABLE("tasks", "FreeRTOS Tasks", [
        ["Task", "Priority", "Stack", "Job"],
        ["Start", "4", "2 KB", "checks every part at power-on, creates the other tasks, deletes itself"],
        ["Sensor", "3", "1 KB", "reads the sensors every \u2018rate\u2019 ms and puts a record in the queue"],
        ["Logger", "2", "1.5 KB", "turns a record into a CSV line with its CRC, prints it, writes it to the card"],
        ["Cli", "1", "2 KB", "the command line, woken by every received byte"],
        ["Idle", "0", "kernel", "FreeRTOS's own task; it puts the chip to sleep"],
    ], [38, 36, 34, 135], align="lccl")
    P("The sensor task has the highest priority so that samples are taken on time even while the SD "
      "card is slow; a write that makes FatFs sync the file can take tens of milliseconds. The queue "
      "between the sensor task and the logger task holds eight records, eight seconds at the default "
      "rate. The next sample time is planned from the previous planned time, not from when the task woke "
      "up, so small delays do not add up {barry}.")
    P("Three resources are shared between tasks, and each has a mutex: the UART output, the I2C bus "
      "with both sensor drivers, and the SPI bus with the SD card and FatFs. They are always taken in the "
      "order UART, I2C, SD, which rules out a deadlock. FreeRTOS mutexes have priority inheritance "
      "{barry}: if the command line holds the I2C bus during the one-second gyro zeroing and the sensor "
      "task waits for it, the command line runs at the sensor task's priority until it lets go. The "
      "sensor and logger tasks take the UART only with a timeout. When the command line is printing, a "
      "log line still goes to the card and only its copy on the UART is skipped.")
    P("The UART sends through DMA channel 4 from two 128-byte buffers: one is sent while the other "
      "fills, so printing a log line does not hold up the task. Received bytes go from the interrupt into "
      "a FreeRTOS stream buffer, where the command-line task waits for them. Interrupts that call "
      "FreeRTOS functions must have a priority between 5 and 15 on this port; after reset every "
      "interrupt has priority 0, so the UART and DMA interrupts are set to 6 {pm0056}. With the 8 MHz "
      "clock the baud rate register is set by [eq:brr]. The real rate is 8 000 000/69 = 115 942 baud, "
      "0.64 % above the nominal rate and well inside what a UART receiver accepts.")
    d.EQ(E["brr"], "brr")

    H2("Timekeeping")
    P("The RTC normally counts seconds. Here its prescaler is set to 31, so the counter advances 1024 "
      "times per second, as in [eq:rtc]. The time in seconds is a base value kept in two backup registers "
      "plus the counter divided by 1024. Every 24 days the whole seconds are moved from the counter into "
      "the base so the counter cannot overflow. A magic value in a third backup register tells the "
      "firmware at power-on whether the time survived on the coin cell.")
    d.EQ(E["rtc"], "rtc")

    H2("Power Management")
    P("FreeRTOS knows when the next task is due. When no task is ready, the idle task calls "
      "power_idle() with the time until then, called tickless idle {freertos, barry}. [fig:idle] shows "
      "what that function does. Three modes can be chosen with the power command. In run mode the CPU "
      "spins. In sleep mode it executes WFI: the CPU clock stops, the peripherals and the 1 ms tick keep "
      "running. In STOP mode, the default, every clock stops except the 32 kHz one {rm0008}, and the RTC "
      "alarm wakes the chip 1 ms before the next task is due.")
    d.FIG("idle", F["idle"], "Decisions in power_idle(), called by the idle task when no task is ready.")
    P("After waking, the number of RTC counts slept, n, is turned into milliseconds and added to the "
      "FreeRTOS tick with vTaskStepTick(). Because one RTC count is 1000/1024 ms, the remainder c is "
      "carried from one wake-up to the next, as in [eq:step], so no fractions of a millisecond are lost in this "
      "step.")
    d.EQ(E["step"], "step")
    P("A falling edge on PA10, the start bit of a received byte, also wakes the chip, so a key press "
      "brings the console back. The first byte is lost because the UART had no clock, and after that the "
      f"chip stays out of STOP for {C.get('CONSOLE_AWAKE_MS', 30000) // 1000} s. Both wake-up sources are "
      "EXTI events used with WFE, not interrupts, so no interrupt handler is involved at all (Section VI "
      "explains why). STOP is used only when it is safe; otherwise the chip falls back to sleep mode. The "
      "RTC crystal must be running, no debugger may be attached, the UART must have finished sending, at "
      f"least {C.get('STOP_HOLDOFF_MS', 10000) // 1000} s must have passed since reset so the ST-Link can "
      f"still connect for flashing, and the idle time must be at least {C.get('STOP_MIN_MS', 10)} ms. "
      "The independent watchdog, which has its own 40 kHz clock, is fed every time the idle task runs. "
      "If the chip ever stays stuck for about 26 s, the watchdog restarts it, the start-up message "
      "reports it, and the board uses sleep mode instead of STOP until the next power-on.")
    P("[fig:timing] puts the pieces together for one sample at one sample per second. The sensor task "
      "raises the test pin PB12, starts a BMP280 conversion and waits; the chip can enter STOP during "
      "that wait. The logger task then builds the line, hands it to the UART DMA and writes it to the "
      "card. While the UART is still sending, power_idle() does not enter STOP, so the chip sleeps with "
      "WFI until the line has left, and then stays in STOP until 1 ms before the next sample.")
    d.FIG("timing", F["timing"], "One sample in time, drawn from the smallest step times measured by "
          "the firmware ([tab:timing]). The CPU row follows the rules of power_idle().", full=True)

    H2("Sensors and Calibration")
    P("The BMP280 runs in forced mode: the firmware starts one measurement, waits "
      f"{C.get('BMP_WAIT_MS', 15)} ms while the sensor converts, and reads the result. Temperature and "
      "pressure are calculated with the integer compensation formulas from the datasheet and the 24 "
      "bytes of factory constants stored in each chip {bmp280}. The selftest command runs the formulas on "
      "the datasheet's example and prints the expected 25.08 \u00b0C and 1006.53 hPa. Values outside the "
      "sensor's range are rejected and the read is tried once more.")
    P("The MPU6050 is set to \u00b12 g and \u00b1250 \u00b0/s with its 44 Hz low-pass filter {mpu_rm}. "
      "Acceleration and rotation are scaled by [eq:acc], where D is the raw reading, O the offset and S the "
      "counts per g. The accelerometer offsets and scale factors come from a six-position calibration "
      "(each axis pointing up and then down) and are stored in config.h: "
      f"S = {C.get('ACC_SCALE_X')}, {C.get('ACC_SCALE_Y')} and {C.get('ACC_SCALE_Z')} counts per g for X, "
      "Y and Z, against the nominal 16 384. The gyro offsets are measured at every power-on while the "
      "board is still for one second; 131 counts per \u00b0/s is the datasheet value {mpu_ps}. If the "
      "MPU6050 loses power for a moment, it wakes up asleep with its settings lost and returns zeros; the "
      "driver notices fourteen zero bytes, sets the chip up again and reports it.")
    d.EQ(E["acc"], "acc")
    P("The ADC measures the internal temperature sensor and the internal reference VREFINT 32 times "
      "each, copied to memory by DMA. Because VREFINT is fixed, its reading gives the supply voltage by "
      "[eq:vdd]. The datasheet gives VREFINT as 1.20 V typical, but it differs from chip to chip {ds5319}; on "
      f"this board it was measured as {vref:.4f} V with the vddcal command against a multimeter reading of "
      f"{M['vdd_check']['meter_v']:.2f} V on the 3.3 V pin. The chip temperature is computed by [eq:vts] and [eq:temp] with "
      "the datasheet's 1.43 V at 25 \u00b0C and 4.3 mV/\u00b0C, where the sensor voltage uses the nominal "
      f"1.20 V for VREFINT and the offset T_{{off}} is {ts_off / 100:g} \u00b0C.")
    d.EQ(E["vdd"], "vdd")
    d.EQ(E["vts"], "vts")
    d.EQ(E["temp"], "temp")

    H2("Data Format and Storage")
    P("Each log file starts with a header line, followed by one line per sample (a long line is "
      "wrapped here after a comma):")
    d.CODE(["time,temp_C,press_hPa,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps,vdd_V,chip_C", L["sample"]])
    P("The eight hexadecimal digits after the asterisk are the CRC-32 of the text before it, the same "
      "CRC that Python's zlib.crc32 computes, so the PC tools can check every line. A 32-bit CRC detects "
      "every error burst up to 32 bits long and almost all longer ones {koopman}. All values are computed "
      "in integers (hundredths of a degree, pascals, thousandths of a g) and printed with a fixed number "
      "of decimals; the firmware uses no floating point. Each logging session gets a new file, "
      "LOG0001.CSV, LOG0002.CSV and so on. The file is synced every 10 lines or every 10 s, so a power cut "
      "loses at most the last few lines.")
    P("Logging starts by itself at power-on. If a write or a sync fails, for example because the card "
      "was pulled out, the logger carries on over the UART, blinks the LED and tries to mount the card "
      f"again every {C.get('SD_RETRY_MS', 5000) // 1000} s. The card starts at 250 kHz, as the SD "
      "specification requires for initialization, and runs at 2 MHz afterwards {sdspec}. When the card "
      "mounts again, logging to the card continues.")

    H2("Fault Handling and Tools")
    P("A logger that restarts and keeps logging is more useful than one that hangs. A stack overflow, an "
      "exhausted heap, a failed assertion and a hard fault all print the reason with interrupts off and "
      "restart the chip. For a hard fault the address of the failing instruction, the link register and "
      "the fault status register are saved in backup registers and printed at the next start-up, with the "
      "name of the task that was running. Every I2C wait has a 25 ms timeout; if a sensor holds the data "
      "line low, the driver clocks SCL by hand up to nine times and sends a STOP. A debugging session can "
      "leave hardware breakpoints armed after a reset, which causes a hard fault once the debugger is "
      "gone, so the start-up code clears them when no debugger is attached.")
    P("Every command is one row in a table in cli.c with its name, its function, the locks it needs and "
      "its help text, so the help can never get out of date; the Appendix lists them. Four Python scripts "
      "run on the PC: settime.py sets the clock from the computer, live_plot.py plots the data while the "
      "board logs and saves every good line, getfile.py copies a log file from the card over the serial "
      "port, and log_check.py checks the CRC of every line, reports gaps in time and plots each column.")

    # ------------------------------------------------------------ V verification
    H1("Verification Methodology")
    P("Each peripheral was first brought up alone in a small test program before it went into the "
      "logger. The programs are kept in the bringup folder in the order they were written "
      "([tab:bringup]). The logger then grew in four milestones, and each was checked before the next "
      "began.")
    d.TABLE("bringup", "Bring-Up Programs", [
        ["No.", "Program", "What it established"],
        ["01", "blink", "clock and GPIO registers, the PC13 LED"],
        ["02", "uart_echo", "USART1 by polling"],
        ["03", "uart_rx_irq_ringbuffer", "receive interrupt with a 64-byte ring buffer"],
        ["04", "uart_cli", "a small command line, SysTick 1 ms timing"],
        ["05", "i2c_sensor_id", "I2C1, reading the BMP280 chip ID (0x58)"],
        ["06", "bmp280", "temperature and pressure with the compensation formulas"],
        ["07", "mpu6050", "acceleration and rotation, scaling, gyro zero"],
        ["08", "mpu6050_calibration", "six-position accelerometer calibration"],
        ["09", "sd_card_read", "SPI1, SD card start-up in SPI mode, reading sectors"],
    ], [22, 88, 133], align="cll")
    P("M0 split the bring-up code into modules with a Makefile. M1 added FatFs, the CSV format with its "
      "CRC, and recovery from a removed card. M2 moved everything onto FreeRTOS with tasks, a queue, "
      "mutexes and DMA for the UART. M3 added tickless idle, STOP mode, the RTC alarm and the current "
      "measurements. All four were tested on a breadboard powered from the CP2102. At every power-on "
      "the start task checks each part and prints the result with the reset cause.")
    P("Timing was measured on the target. The firmware keeps a microsecond clock built from the "
      "FreeRTOS tick count and the SysTick counter, which counts down from 7999 to 0 in each millisecond. "
      "For each step of a sample it records the last, the smallest and the largest time, and the timing "
      "command prints them. The tasks command prints the share of CPU time per task, counted by the DWT "
      "cycle counter. Because the microsecond clock is driven by the same tick that schedules the samples, "
      "it cannot show an error in the tick itself. The time stamps in the log come from the RTC crystal "
      "instead, so the period between them, [eq:period], is a second and independent measure, where t_{1} and t_{N} "
      "are the first and last of N stamps.")
    d.EQ(E["period"], "period")
    if LA:
        P("A third measure comes from outside the board: a logic analyzer on PB12, which is high while the "
          "sensors are read, records each sample with the analyzer's own clock. The script la_check.py "
          "finds the edges and computes the periods and the high times.")
    P("Current was measured with a multimeter in series with the 5 V input of the breadboard. Before "
      "each reading the ST-Link was unplugged, because the chip never enters STOP with a debugger "
      "attached, and nobody typed for 30 s ([fig:meter]). The battery life follows from [eq:life], where C is "
      "the cell capacity, V_{bat} its voltage, \u03b7 the boost converter efficiency and I_{in} the "
      "current at the input voltage V_{in}.")
    d.EQ(E["life"], "life")
    d.FIG("meter", F["meter"], "Current measurement at the 5 V input of the breadboard in STOP mode.")
    P(f"Before the circuit went onto the matrix board, each power part was tested on its own: "
      f"the 18650 cell, charging through the TP4056 with its protection, the switch, "
      f"the MT3608 adjusted to {mb['boost_setting_v']:.2f} V before anything was connected to it, both "
      "diodes, the change-over between USB and battery, charging while the board ran, and the coin cell. "
      "The resistance from VBAT to 3.3 V was also measured and found open; on some Blue Pill copies these "
      "two pins are joined, and the 3.3 V supply would then charge the CR2032. The finished board was "
      "checked in stages: first the power path without any module, then the Blue Pill alone, then one "
      "module at a time.")

    # ------------------------------------------------------------ VI results
    H1("Results and Discussion")
    H2("Timing")
    P(f"[tab:timing] shows the step times after {T['minutes']} minutes of logging at one sample per "
      f"second with STOP mode on ({us(T['samples_timed'])} samples).")
    rows = [["Step", "Last (\u00b5s)", "Min (\u00b5s)", "Max (\u00b5s)"]]
    for r in T["rows_us"]:
        rows.append([r[0], us(r[1]), us(r[2]), us(r[3])])
    d.TABLE("timing", "Step Times from the Timing Command", rows, [76, 56, 56, 55], align="lrrr")
    P(T["typing_note"] + f" When nobody types, a line reaches the card {to_card_ms:.1f} ms after the "
      "sample started.")
    P(f"The sensor read takes {read_ms:.1f} ms and varies by only 1 \u00b5s. About 15 ms of it is the "
      "BMP280 conversion and about 6 ms the ADC, which is reset, calibrated, used and switched off again "
      f"for every sample. An SD write takes {t['SD write'][2] / 1000:.2f} ms when FatFs only copies the line "
      f"into its buffer and up to {t['SD write'][3] / 1000:.0f} ms when it writes a sector to the card. Of "
      f"the time the CPU was awake, the idle task had {T['cpu_share_awake']['IDLE']} %, the logger "
      f"{T['cpu_share_awake']['Logger']} %, the sensor task {T['cpu_share_awake']['Sensor']} % and the "
      f"command line {T['cpu_share_awake']['Cli']} %. The lowest free heap was {us(T['heap_lowest'])} bytes "
      f"of {us(T['heap_total'])}, and {T['records_dropped']} records were dropped from the queue. An "
      f"earlier run at 250 ms per sample ({T['rate_250_ms']['samples']} samples) gave periods between "
      f"{us(T['rate_250_ms']['min_us'])} and {us(T['rate_250_ms']['max_us'])} \u00b5s.")

    H2("Data Integrity and Sensor Readings")
    P(f"The log {L['file']} runs from {L['first'].strftime('%H:%M:%S')} to "
      f"{L['last'].strftime('%H:%M:%S')}. It has {us(L['lines'])} "
      f"data lines, {L['bad']} with a wrong CRC, and no gap longer than 2 s. The values change very little "
      "over the run, so they mainly show the noise and offsets of the sensors ([tab:stats] and [fig:logplot]).")
    rows = [["Column", "Min", "Max", "Mean", "Std. dev."]]
    labels = {"temp_C": "BMP280 temperature (\u00b0C)", "press_hPa": "pressure (hPa)",
              "ax_g": "acceleration X (g)", "ay_g": "acceleration Y (g)", "az_g": "acceleration Z (g)",
              "gx_dps": "rotation X (\u00b0/s)", "gy_dps": "rotation Y (\u00b0/s)",
              "gz_dps": "rotation Z (\u00b0/s)", "vdd_V": "supply VDD (V)", "chip_C": "chip temperature (\u00b0C)"}
    for k, lab in labels.items():
        lo, hi, mean, sdev = st[k]
        nd = 3 if k.startswith("a") else 2
        rows.append([lab, f"{lo:.{nd}f}", f"{hi:.{nd}f}", f"{mean:.{nd + 1}f}", f"{sdev:.{nd + 1}f}"])
    d.TABLE("stats", f"Values in {L['file']} ({us(L['lines'])} Lines)", rows,
            [88, 39, 39, 39, 38], align="lrrrr")
    P(f"The Z acceleration averages {st['az_g'][2]:.3f} g with a spread of {st['az_g'][3]:.4f} g, which "
      f"shows that the six-position calibration holds. The gyro reads close to zero; the largest value, "
      f"{st['gz_dps'][1]:.2f} \u00b0/s on Z, is a single sample. The chip temperature reads "
      f"{st['chip_C'][2] - st['temp_C'][2]:.1f} \u00b0C above the BMP280 on average. The supply voltage "
      f"stayed between {st['vdd_V'][0]:.2f} and {st['vdd_V'][1]:.2f} V for the whole run, matching the "
      "multimeter.")
    d.FIG("logplot", F["logplot"], f"Temperature, pressure, Z acceleration and Z rotation in {L['file']}. "
          "All ten columns are plotted by log_check.py.")

    H2("Sample Period Against the Real-Time Clock")
    steps_txt = ", ".join(str(s) for s in L["skips"])
    P(f"The log spans {L['span']:.0f} s but has {us(L['lines'])} lines, and {len(L['skips'])} times the "
      f"time stamp jumps by 2 s instead of 1 s, after data lines {steps_txt}. The jumps come at regular "
      "intervals, so this is a steady drift and not lost samples. By [eq:period] the real sample period is "
      f"{L['period']:.4f} s, {drift_ms:.1f} ms or {drift_pct:.2f} % longer than intended ([fig:drift]). "
      "The timing command reported every period within 2 \u00b5s of 1 s, because it measures with the same "
      "FreeRTOS tick that schedules the samples.")
    d.FIG("drift", F["drift"], f"Time stamp minus line number in {L['file']}. Each step is a second "
          "without a sample; the dashed line is the mean period.")
    P("The time stamps come from the RTC crystal; it is the scheduler's time that runs slow. During STOP "
      "the FreeRTOS tick does not count, and power_idle() adds the time slept, measured with the RTC, when "
      "the chip wakes up. Three effects there can make the tick differ from the RTC. SysTick is stopped "
      "part-way through a millisecond and restarted from zero, so the part it had already counted is "
      "dropped at every STOP, which makes the tick slow. The RTC reading taken before STOP falls at a "
      "random point inside a 1/1024 s count, while the wake-up comes exactly on a count, so the time "
      "slept is overestimated by up to one count, which makes the tick fast. And the time awake is counted "
      "by the 8 MHz RC oscillator, which is accurate to about 1 % {ds5319}. The first two depend on where "
      "in the millisecond the chip goes to sleep, so they do not cancel in general; the log shows the net "
      "effect.")

    if LA:
        H2("Logic Analyzer Measurement")
        width_txt = ""
        if LA.get("width_mean_ms") is not None:
            width_txt = (f" PB12 was high for {LA['width_mean_ms']:.3f} ms on average, between "
                         f"{LA['width_min_ms']:.3f} and {LA['width_max_ms']:.3f} ms, against "
                         f"{LA['firmware_read_ms']:.3f} ms from the firmware's timing command.")
        rate = f" at {LA['sample_rate']}" if LA.get("sample_rate") else ""
        P(f"The PB12 test point was recorded with a {LA['analyzer']}{rate} for "
          f"{LA['capture_s']:.0f} s ({LA['periods']} sample periods). The mean period was "
          f"{LA['period_mean_s']:.6f} s ({LA['period_error_ppm']:+.0f} ppm from 1 s), the shortest "
          f"{LA['period_min_s']:.6f} s and the longest {LA['period_max_s']:.6f} s, with a standard "
          f"deviation of {LA['period_std_us']:.1f} \u00b5s.{width_txt} [tab:la] sets the three measures side "
          "by side.")
        d.TABLE("la", "Three Measures of the Sample Timing", [
            ["Quantity", "Logic analyzer", "Firmware tick", "RTC time stamps"],
            ["Mean period", f"{LA['period_mean_s']:.6f} s", "1.000000 s", f"{L['period']:.4f} s"],
            ["Period range", f"{LA['period_min_s']:.6f} to {LA['period_max_s']:.6f} s",
             f"{us(t['sample period'][2])} to {us(t['sample period'][3])} \u00b5s", "1 or 2 s steps"],
            ["PB12 high time", f"{LA['width_mean_ms']:.3f} ms" if LA.get("width_mean_ms") is not None
             else "not measured", f"{LA['firmware_read_ms']:.3f} ms", "not applicable"],
        ], [110, 130, 130, 130], align="llll", full=True)
        d.FIG("la", path("docs", "captures", "logic_analyzer.png"),
              "Sample period and PB12 high time from the logic analyzer (la_check.py).", full=True)
        if os.path.exists(path("docs", "captures", "pulseview_pulse.png")):
            d.FIG("pulse", path("docs", "captures", "pulseview_pulse.png"), "One PB12 pulse in PulseView.", full=True)

    H2("Current Consumption")
    P(f"[tab:current] gives the current of the whole breadboard (Blue Pill, both "
      f"sensors and the SD module) at the 5 V input.")
    rows = [["Command", "State", "Current (mA)"]]
    for r in cur["rows_ma"]:
        rows.append([r[0], r[1], f"{r[2]:g}"])
    d.TABLE("current", "Board Current at 5 V, Logging at 1 Sample/s", rows, [78, 120, 45], align="llr")
    P(f"STOP mode cuts the board current by {stop_cut:.0f} %. The power command reported the chip in "
      f"STOP {cur['stop_share_quiet_pct']} % of the time when nobody types. The microcontroller itself "
      f"draws only tens of microamps in STOP {{ds5319}}, so most of the remaining {cm['power stop']:g} mA "
      "is the modules: the SD module's regulator, the power LEDs, the regulators and the MPU6050, whose "
      f"share shows in the difference between the last two STOP rows, "
      f"{cm['stop + power stop'] - cm['as above + mpusleep']:.1f} mA. The PC13 LED takes about "
      f"{cur['led_ma']:g} mA, which is why in STOP mode it only flashes during a sample. Without a card the "
      "current is higher, because the LED blinks and the logger tries to mount the card every 5 s.")
    P(f"For a {cur['battery_mah']} mAh 18650 at {vb:g} V through a boost converter of about "
      f"{eff * 100:.0f} % efficiency, [eq:life] gives about {battery_days(cm['power stop']):.1f} days of logging "
      f"in STOP mode and {battery_days(cm['power run']):.1f} days in run mode.")
    if BAT:
        H2("Current from the Battery")
        P(f"The current drawn from the 18650 was measured on the matrix board, with the "
          f"multimeter in place of the open switch SW1, the CP2102 and the ST-Link unplugged, and the cell "
          f"at {BAT['battery_v']:.2f} V ([tab:bat]). This includes the TP4056, the boost converter and "
          "both diodes.")
        rows = [["Mode", "Battery current (mA)"]] + [[r[0], f"{r[1]:g}"] for r in BAT["rows_ma"]]
        d.TABLE("bat", "Current from the 18650, Logging at 1 Sample/s", rows, [140, 103], align="lr")
        if bat_stop:
            P(f"At {bat_stop:g} mA in STOP mode a {cur['battery_mah']} mAh cell lasts about "
              f"{cur['battery_mah'] / bat_stop / 24:.1f} days, against the "
              f"{battery_days(cm['power stop']):.1f} days estimated from the breadboard with [eq:life].")

    H2("Matrix Board")
    P(f"The circuit was rebuilt on a matrix board with every module on female headers ([fig:matrix]). "
      f"It runs from the 18650 alone or from the CP2102, charges through the "
      "TP4056's USB-C connector while it logs, and writes to the SD card in the same format as the "
      "breadboard. With the CR2032 fitted, the clock kept the correct time through a "
      f"{mb['coin_cell_off_minutes']}-minute power-off, so the time needs to be set only once.")
    d.FIG("matrix", F["matrix"], "Matrix board (rev B) running from the 18650 cell.")
    if PCB:
        H2("PCB Layout")
        size = PCB.get("size_mm", [])
        txt = (f"The schematic was taken into a printed circuit board layout in {PCB.get('tool', 'EasyEDA')} "
               "([tab:pcb]).")
        if PCB.get("notes"):
            txt += " " + PCB["notes"]
        P(txt)
        rows = [["Item", "Value"]]
        if len(size) == 2:
            rows.append(["Board size", f"{size[0]} \u00d7 {size[1]} mm"])
        if "layers" in PCB:
            rows.append(["Copper layers", str(PCB["layers"])])
        if "drc_errors" in PCB:
            rows.append(["Design rule check", f"{PCB['drc_errors']} errors"])
        d.TABLE("pcb", "PCB Layout", rows, [120, 123])
        for i, im in enumerate(PCB.get("images", [])):
            p = path(*im.split("/"))
            if os.path.exists(p):
                d.FIG(f"pcb{i}", p, f"PCB layout: {os.path.splitext(os.path.basename(im))[0].replace('_', ' ')}.")

    H2("Problems Found and Fixed")
    P("STOP mode never woke up. The first version used the RTC alarm interrupt to wake the chip. It "
      "worked with the Makefile build but never woke up when built in STM32CubeIDE, and the watchdog reset "
      "the board every 26 s. The vector table in the compiled program showed the reason: CubeIDE's "
      "startup file names vector 41 RTCAlarm_IRQHandler, while the code defined RTC_Alarm_IRQHandler, so "
      "the alarm ran the default handler, an endless loop. The wake-up was changed to EXTI events with "
      "WFE. An event wakes the core without running any handler, so the code no longer depends on the "
      "handler names of a particular startup file.")
    P("The chip temperature and VDD froze. In an 18-minute log both stopped changing about two minutes "
      "after power-on. The ADC was converting continuously in the background, and after the first STOP "
      "it did not continue; restarting only the conversions gave nan values. Each reading now resets the "
      "ADC through the RCC, calibrates it, takes 32 pairs by DMA and switches it off again. This takes "
      f"about 6 ms per sample and also lowers the current in STOP. The {T['minutes']}-minute log above was "
      "recorded after the fix.")
    P(f"VDD read 0.01 V low. The logged supply was 3.21 V while the multimeter showed 3.22 V. The "
      f"internal reference was {vref:.4f} V instead of 1.20 V, and the value was cut off instead of "
      "rounded. The vddcal command now measures the reference against a meter reading, and VDD is "
      "rounded.")
    P("Flashing failed once the board slept. STM32CubeIDE reported \u201cError finishing flash operation.\u201d "
      "The debug configuration was set to connect under reset, which needs the reset line of the ST-Link, "
      "and the three-wire cable does not have it. A software system reset fixed it; setting BOOT0 to 1 "
      "remains the fallback.")

    H2("Discussion and Limitations")
    lim = []
    lim.append(f"The cause of the {drift_pct:.2f} % period error is not confirmed. The three effects "
               "described above are candidates, and the log shows only their sum.")
    if LA:
        lim.append(f"The logic analyzer measured {LA['period_mean_s']:.6f} s with its own crystal, "
                   "which is independent of both clocks on the board.")
    lim.append(f"The chip temperature uses the nominal 1.20 V for VREFINT, while VDD uses the measured "
               f"{vref:.4f} V, and its offset is {ts_off / 100:g} \u00b0C; the datasheet allows 1.34 to "
               "1.52 V at 25 \u00b0C {ds5319}, so the value shows changes in temperature better than the "
               "temperature itself.")
    if BAT:
        lim.append("The battery life is computed from currents measured at one battery voltage.")
    else:
        lim.append(f"The battery life of {battery_days(cm['power stop']):.1f} days is computed from the "
                   "breadboard current at 5 V and an assumed 85 % boost efficiency.")
    lim.append(f"The board current in STOP, {cm['power stop']:g} mA, is two to three orders of magnitude "
               "above the purpose-built loggers in [tab:related]. It is set by the modules, not by the "
               "microcontroller: the SD module's regulator, the power LEDs and the sensors stay powered, and "
               f"putting only the MPU6050 to sleep already saves "
               f"{cm['stop + power stop'] - cm['as above + mpusleep']:.1f} mA. The power result of this work "
               "is the firmware's share, not a low-current board.")
    P(" ".join(lim))

    # ------------------------------------------------------------ VII conclusion
    H1("Conclusion")
    P(f"A sensor data logger was built on an STM32F103C8 with register-level drivers and FreeRTOS. It "
      "samples two sensors and the chip's own temperature and supply at a steady rate, stamps every line "
      "with a clock that survives a power cut, stores the data as CSV with a CRC-32 on every line, and "
      "recovers by itself from a removed card, a sensor that lost power and firmware faults. In a "
      f"{T['minutes']}-minute test it logged {us(L['lines'])} lines without a CRC error, a sample reached "
      f"the card {to_card_ms:.1f} ms after it was taken, and STOP mode reduced the board current by "
      f"{stop_cut:.0f} % to {cm['power stop']:g} mA. Checking the scheduler against the RTC time stamps "
      f"showed a sample period {drift_ms:.1f} ms too long, an error that the firmware's own measurement "
      "could not show. The circuit now runs from an 18650 cell on a matrix board.")

    # ------------------------------------------------------------ acknowledgment, references, appendix
    if ACKNOWLEDGMENT:
        d.B.append(D.para("Heading5", D.rich("Acknowledgment")))
        P(ACKNOWLEDGMENT)
    d.B.append(D.para("Heading5", D.rich("References")))
    for n, k in enumerate(_order, 1):
        d.B.append(D.para("references", D.bookmark(f"_RefCite{n}", 900 + n, D.rich(REFS[k]))))
    d.B.append(D.para("Heading5", D.rich("Appendix: Firmware Commands")))
    d.TABLE("cmds", "Commands of the Firmware", [
        ["Command", "Function"],
        ["start, stop", "start or stop logging (it starts by itself at power-on)"],
        ["set rate N", f"N = {C.get('RATE_MS_MIN', 250)} to {us(C.get('RATE_MS_MAX', 60000))} ms between samples"],
        ["set time YYYY-MM-DD HH:MM:SS, time", "set or show the clock"],
        ["read, chip", "one readable line from all sensors; supply and chip temperature"],
        ["zero", "measure the gyro offsets again (board still for 1 s)"],
        ["status", "logger state and error counters"],
        ["ls, cat FILE, sd mount, sd init", "the SD card: list, print a file, mount, start"],
        ["tasks", "FreeRTOS tasks: priority, state, free stack, CPU share"],
        ["timing, timing reset", "step times in microseconds"],
        ["power, power run | sleep | stop", "low-power mode and the time spent in STOP"],
        ["power test, power reset", "one 1 s STOP woken by the RTC alarm; clear the counters"],
        ["scan, bmpread, mpuread, i2ctest N", "I2C checks (debug build)"],
        ["tscal, vddcal V", "chip temperature offset; VREFINT against a meter (debug build)"],
        ["selftest, crctest, mpusleep", "BMP280 formulas, CRC-32, MPU6050 recovery (debug build)"],
        ["sd read N, sd trace on | off", "show a sector; trace the card start-up (debug build)"],
    ], [105, 138])

    unused = [k for k in REFS if k not in _order]
    return d, unused


# ================================================================== docx + pdf

def write_docx(body, img, out_path):
    with zipfile.ZipFile(TEMPLATE) as z:
        files = {n: z.read(n) for n in z.namelist()}
    doc = files["word/document.xml"].decode("utf-8")
    head = doc[:doc.index("<w:body>") + len("<w:body>")]
    if 'xmlns:a="' not in head[:head.index(">", head.index("<w:document"))]:
        head = head.replace("<w:document ", '<w:document xmlns:a="http://purl.oclc.org/ooxml/drawingml/main" ', 1)
    final_sect = D.sect(cols=2)
    files["word/document.xml"] = (head + "".join(body) + final_sect + "</w:body></w:document>").encode("utf-8")
    rels = files["word/_rels/document.xml.rels"].decode("utf-8")
    rels = re.sub(r'<Relationship Id="rId(9|10)"[^>]*/>', "", rels)
    add = "".join(f'<Relationship Id="{rid}" Type="{D.REL_IMAGE}" Target="{tgt}"/>' for rid, tgt, _ in img.items)
    ftype = re.search(r'Type="([^"]+/footer)"', rels).group(1)
    add += f'<Relationship Id="rIdPgNum" Type="{ftype}" Target="footer2.xml"/>'
    files["word/_rels/document.xml.rels"] = rels.replace("</Relationships>", add + "</Relationships>").encode()
    for rid, tgt, p in img.items:
        with open(p, "rb") as f:
            files["word/" + tgt] = f.read()
    ct = files["[Content_Types].xml"].decode()
    for ext, mime in (("png", "image/png"), ("jpeg", "image/jpeg")):
        if f'Extension="{ext}"' not in ct:
            ct = ct.replace('<Default Extension="xml"', f'<Default Extension="{ext}" ContentType="{mime}"/>'
                            '<Default Extension="xml"')
    if "/word/footer2.xml" not in ct:
        ct = ct.replace("</Types>", '<Override PartName="/word/footer2.xml" ContentType="application/'
                        'vnd.openxmlformats-officedocument.wordprocessingml.footer+xml"/></Types>')
    files["[Content_Types].xml"] = ct.encode()
    ftr = files["word/footer1.xml"].decode("utf-8")
    root = ftr[:ftr.index("<w:p ")]
    files["word/footer1.xml"] = (root + D.page_number_paragraph() + "</w:ftr>").encode("utf-8")
    files["word/footer2.xml"] = (root + D.page_number_paragraph() + "</w:ftr>").encode("utf-8")
    core = files["docProps/core.xml"].decode()
    core = re.sub(r"<dc:title>.*?</dc:title>", "<dc:title>" + TITLE + "</dc:title>", core)
    core = re.sub(r"<dc:creator>.*?</dc:creator>", "<dc:creator>Saman Fuyal</dc:creator>", core)
    files["docProps/core.xml"] = core.encode()
    st = files.get("word/settings.xml")
    if st is not None:
        st = st.decode("utf-8")
        if "doNotAutoCompressPictures" not in st:
            m = re.search(r"<w:(forceUpgrade|captions|readModeInkLockDown|smartTagType|schemaLibrary|shapeDefaults|"
                          r"doNotEmbedSmartTags|decimalSymbol|listSeparator)\b|<w1[45]:|</w:settings>", st)
            st = st[:m.start()] + "<w:doNotAutoCompressPictures/>" + st[m.start():]
        files["word/settings.xml"] = st.encode("utf-8")
    with zipfile.ZipFile(out_path, "w", zipfile.ZIP_DEFLATED) as z:
        for n in ["[Content_Types].xml"] + [n for n in files if n != "[Content_Types].xml"]:
            z.writestr(n, files[n])


def make_pdf(docx_path):
    pdf = os.path.splitext(docx_path)[0] + ".pdf"
    soffice = shutil.which("soffice") or shutil.which("libreoffice")
    if not soffice and os.name == "nt":
        for p in (r"C:\Program Files\LibreOffice\program\soffice.exe",
                  r"C:\Program Files (x86)\LibreOffice\program\soffice.exe"):
            if os.path.exists(p):
                soffice = p
    if soffice:
        subprocess.run([soffice, "--headless", "--convert-to", "pdf", "--outdir",
                        os.path.dirname(docx_path), docx_path],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False, timeout=300)
    elif os.name == "nt":
        ps = ("$w = New-Object -ComObject Word.Application; $w.Visible = $false; $w.DisplayAlerts = 0; "
              f"$d = $w.Documents.Open('{docx_path}', $false, $true); "
              f"$d.SaveAs([ref] '{pdf}', [ref] 17); $d.Close([ref] 0); $w.Quit()")
        subprocess.run(["powershell", "-NoProfile", "-Command", ps], check=False, timeout=300)
    if os.path.exists(pdf) and os.path.getmtime(pdf) >= os.path.getmtime(docx_path):
        print("written", pdf)
    else:
        print("report.pdf was not updated (no LibreOffice or Word found): open report.docx in Word "
              "and use File > Save As > PDF")


def main():
    M = load_json(os.path.join("results", "measurements.json"), required=True)
    L = analyze_log(os.path.join("results", M["timing"]["log_file"]))
    C = config_values()
    LA = load_json(os.path.join("results", "logic_analyzer.json"))
    BAT = load_json(os.path.join("results", "battery_current.json"))
    PCB = load_json(os.path.join("results", "pcb.json"))
    F = draw_figures(M, L)
    E = draw_equations()
    first, _ = build({}, M, L, C, LA, BAT, PCB, F, E)          # pass 1: figure and table numbers
    d, unused = build(first.new, M, L, C, LA, BAT, PCB, F, E)  # pass 2: the text uses them
    text = "".join(d.B)
    if "Fig. ?" in text or "TABLE ?" in text or "(?)" in text:
        sys.exit("a [fig:], [tab:] or [eq:] reference has no figure, table or equation")
    if unused:
        print("note: references not cited, left out:", ", ".join(unused))
    out = os.path.join(HERE, "report.docx")
    write_docx(d.B, d.img, out)
    print("written", out, f"({d.nfig} figures, {d.ntab} tables)")
    print("sections added:", ", ".join(n for n, v in (("logic analyzer", LA), ("battery current", BAT),
                                                       ("PCB", PCB)) if v) or "none (no extra results files)")
    make_pdf(out)


if __name__ == "__main__":
    main()
