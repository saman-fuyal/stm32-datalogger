"""Block diagrams and plots for the report, drawn with matplotlib (build_report.py calls them)."""
import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt                       # noqa: E402
from matplotlib.patches import FancyArrowPatch, FancyBboxPatch, Rectangle   # noqa: E402

plt.rcParams.update({
    "font.family": "serif", "font.serif": ["Liberation Serif", "Times New Roman", "DejaVu Serif"],
    "mathtext.fontset": "stix",
    "font.size": 9, "axes.linewidth": 0.8, "axes.titlesize": 9, "axes.labelsize": 9,
    "xtick.labelsize": 8, "ytick.labelsize": 8, "legend.fontsize": 8,
})
INK = "#111111"
GREY = "#eeeeee"
MID = "#666666"
BLUE, ORANGE, GREEN, RED, PURPLE = "#1f77b4", "#ff7f0e", "#2ca02c", "#d62728", "#9467bd"
SCALE = 1.0          # block diagrams are inserted at the size they are drawn, so 7 pt text prints at 7 pt


def _canvas(w, h, xmax, ymax):
    fig, ax = plt.subplots(figsize=(w * SCALE, h * SCALE))
    ax.set_xlim(-0.1, xmax + 0.1)
    ax.set_ylim(-0.1, ymax + 0.1)
    ax.axis("off")
    ax.set_aspect("equal")
    return fig, ax


def box(ax, x, y, w, h, text, fs=8, fill="white", bold=False, ls="-", lw=0.9, ha="center", round_=True):
    if round_:
        ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0,rounding_size=0.08",
                                    fc=fill, ec=INK, lw=lw, ls=ls))
    else:
        ax.add_patch(Rectangle((x, y), w, h, fc=fill, ec=INK, lw=lw, ls=ls))
    tx = x + w / 2 if ha == "center" else x + 0.1
    ax.text(tx, y + h / 2, text, ha=ha, va="center", fontsize=fs, fontweight="bold" if bold else "normal",
            linespacing=1.25)


def arrow(ax, x1, y1, x2, y2, text="", both=False, ls="-", fs=7, off=(0, 0.12), rad=0.0, color=INK):
    ax.add_patch(FancyArrowPatch((x1, y1), (x2, y2), arrowstyle="<|-|>" if both else "-|>",
                                 mutation_scale=8, lw=0.8, color=color, ls=ls,
                                 connectionstyle=f"arc3,rad={rad}", shrinkA=0, shrinkB=0))
    if text:
        ax.text((x1 + x2) / 2 + off[0], (y1 + y2) / 2 + off[1], text, ha="center", va="bottom", fontsize=fs,
                color=MID)


def line(ax, xs, ys, ls="-", arrow_end=False):
    ax.plot(xs, ys, color=INK, lw=0.8, ls=ls, solid_capstyle="butt")
    if arrow_end:
        arrow(ax, xs[-2], ys[-2], xs[-1], ys[-1])


def save(fig, out):
    problems = check_clear(fig)
    fig.savefig(out, dpi=300, bbox_inches="tight", pad_inches=0.04)
    plt.close(fig)
    _trim(out)
    if problems:
        COLLISIONS.extend(f"{os.path.basename(out)}: {p}" for p in problems)
    return out


COLLISIONS = []      # filled by save(); build_report.py stops if anything is in here


def _outline_points(path_disp, step=1.0):
    """Points every `step` pixels along a path in display coordinates (curves flattened)."""
    import numpy as np
    pts = []
    for poly in path_disp.to_polygons(closed_only=False):
        for (x1, y1), (x2, y2) in zip(poly[:-1], poly[1:]):
            n = max(int(np.hypot(x2 - x1, y2 - y1) / step), 1)
            for k in range(n + 1):
                pts.append((x1 + (x2 - x1) * k / n, y1 + (y2 - y1) * k / n))
    return pts


def check_clear(fig, margin=3.0):
    """List every place where a text touches another text, a box outline, an arrow or a line.

    Works in display units (1 unit = 1/100 inch at the drawing dpi); `margin` is the free space
    required around each text."""
    from matplotlib.text import Text
    fig.canvas.draw()
    r = fig.canvas.get_renderer()
    texts, outlines = [], []
    for ax in fig.axes:
        for t in ax.texts + [c for c in ax.get_children() if isinstance(c, Text) and c in ax.texts]:
            if t.get_text().strip() and t.get_visible():
                bb = t.get_window_extent(r).expanded(1, 1)
                texts.append((t.get_text().replace("\n", " ")[:40], bb.x0 - margin, bb.y0 - margin,
                              bb.x1 + margin, bb.y1 + margin))
        lg = ax.get_legend()
        if lg is not None:
            bb = lg.get_window_extent(r)
            texts.append(("legend", bb.x0, bb.y0, bb.x1, bb.y1))
        for p in ax.patches:
            if isinstance(p, FancyArrowPatch):
                path = p.get_transform().transform_path(p.get_path())
            else:
                path = p.get_transform().transform_path(p.get_path())
            outlines.append((type(p).__name__, _outline_points(path)))
        for ln in ax.lines:
            from matplotlib.path import Path
            xy = ln.get_transform().transform(ln.get_xydata())
            outlines.append(("line", _outline_points(Path(xy))))
    seen = list(dict.fromkeys(texts))
    out = []
    for i, (a, *ba) in enumerate(seen):
        for b, *bb in seen[i + 1:]:
            if ba[0] < bb[2] and bb[0] < ba[2] and ba[1] < bb[3] and bb[1] < ba[3]:
                out.append(f"text '{a}' touches text '{b}'")
        for kind, pts in outlines:
            if any(ba[0] < x < ba[2] and ba[1] < y < ba[3] for x, y in pts):
                out.append(f"text '{a}' touches a {kind}")
                break
    return out


def _trim(png, pad=12):
    """Cut the white border off a saved figure (block diagrams leave empty canvas)."""
    from PIL import Image, ImageChops
    im = Image.open(png).convert("RGB")
    diff = ImageChops.difference(im, Image.new("RGB", im.size, "white"))
    box_ = diff.getbbox()
    if box_:
        l, t, r, b = box_
        im.crop((max(l - pad, 0), max(t - pad, 0), min(r + pad, im.width), min(b + pad, im.height))).save(png)


# ---------------------------------------------------------------- system architecture

def fig_system(out):
    fig, ax = _canvas(7.0, 4.6, 14, 9.2)
    # microcontroller boundary
    ax.add_patch(Rectangle((3.35, 2.15), 7.3, 6.95, fc="none", ec=INK, lw=0.9, ls=(0, (4, 3))))
    ax.text(7.0, 8.7, "STM32F103C8 (Blue Pill): 8 MHz HSI, 64 KB flash, 20 KB RAM", ha="center", fontsize=7.2,
            style="italic")
    box(ax, 5.8, 3.85, 2.4, 3.1, "Cortex-M3\n\nFreeRTOS\nV11.3.1\n\nStart, Sensor,\nLogger, Cli,\nIdle", 7,
        GREY, True)
    left = [(7.0, "I2C1, 100 kHz\nPB6, PB7"), (5.55, "ADC1 + DMA ch1\ntemp, VREFINT"),
            (4.1, "RTC, 1024 Hz\nbackup registers"), (2.65, "EXTI lines 10, 17\nIWDG, 26 s")]
    for y, t in left:
        box(ax, 3.45, y, 2.15, 1.05, t, 6.5)
        arrow(ax, 5.6, y + 0.52, 5.8, min(max(y + 0.52, 3.95), 6.85), both=True)
    right = [(7.0, "SPI1, PA4 to PA7\n250 kHz / 2 MHz"), (5.55, "USART1 + DMA\n115 200 baud"),
             (4.1, "GPIO\nPB12, PC13"), (2.65, "SysTick 1 ms\nDWT cycle counter")]
    for y, t in right:
        box(ax, 8.4, y, 2.15, 1.05, t, 6.5)
        arrow(ax, 8.2, min(max(y + 0.52, 3.95), 6.85), 8.4, y + 0.52, both=True)
    # external parts
    box(ax, 0.0, 7.65, 2.75, 0.95, "BMP280 (0x76)\ntemperature, pressure", 6.5)
    box(ax, 0.0, 6.4, 2.75, 0.95, "MPU6050 GY-521 (0x68)\nacceleration, rotation", 6.5)
    arrow(ax, 2.75, 8.12, 3.45, 7.75, both=True)
    arrow(ax, 2.75, 6.87, 3.45, 7.3, both=True)
    box(ax, 0.0, 4.15, 2.75, 0.95, "32.768 kHz crystal\nCR2032 on VBAT", 6.5)
    arrow(ax, 2.75, 4.62, 3.45, 4.62)
    box(ax, 0.0, 2.7, 2.75, 0.95, "key press on PA10\n(UART start bit)", 6.5)
    arrow(ax, 2.75, 3.17, 3.45, 3.17)
    box(ax, 11.25, 7.65, 2.75, 0.95, "micro SD module\nFAT32, LOGnnnn.CSV", 6.5)
    arrow(ax, 10.55, 7.52, 11.25, 8.12, both=True)
    box(ax, 11.25, 5.6, 2.75, 0.95, "CP2102 to the PC\nPA9 TX, PA10 RX", 6.5)
    arrow(ax, 10.55, 6.07, 11.25, 6.07, both=True)
    box(ax, 11.25, 4.15, 2.75, 0.95, "logic analyzer probe,\non-board LED", 6.5)
    arrow(ax, 10.55, 4.62, 11.25, 4.62)
    box(ax, 11.25, 2.7, 2.75, 0.95, "ST-Link V2\nSWDIO, SWCLK, GND", 6.5)
    arrow(ax, 11.25, 3.17, 10.55, 3.17, both=True)
    # power strip
    box(ax, 0.0, 0.0, 14.0, 1.4,
        "18650 -> TP4056 + DW01 -> SW1 -> MT3608 (5.3 V) -> D2 1N5819 -> +5 V;   "
        "CP2102 USB 5 V -> D1 1N5819 -> +5 V\n"
        "+5 V -> Blue Pill 3.3 V regulator -> STM32, BMP280, MPU6050;   +5 V -> SD module (own 3.3 V regulator)",
        6.5, "#f6f6f6", round_=False)
    arrow(ax, 7.0, 1.4, 7.0, 2.15)
    ax.text(7.15, 1.65, "power", fontsize=6.8, color=MID)
    return save(fig, out)


# ---------------------------------------------------------------- firmware layers

def fig_firmware(out):
    fig, ax = _canvas(7.0, 4.9, 14, 9.8)
    ax.text(0.1, 9.45, "Tasks (priority)", fontsize=8, style="italic")
    tasks = [(0.1, "Start (4)\nchecks every part,\nthen deletes itself", False),
             (2.75, "Sensor (3)\nevery 'rate' ms,\nPB12 high", True),
             (6.65, "Logger (2)\nCSV + CRC-32\nto UART and SD", True),
             (9.3, "Cli (1)\ncommand table,\nwoken per byte", False),
             (11.95, "Idle (0)\npower_idle():\nSTOP or SLEEP", False)]
    for x, t, g in tasks:
        box(ax, x, 7.3, 1.95, 1.9, t, 6.5, GREY if g else "white")
    box(ax, 5.0, 7.8, 1.35, 0.9, "queue\n8 records", 7, "white", round_=False)
    arrow(ax, 4.7, 8.25, 5.0, 8.25)
    arrow(ax, 6.35, 8.25, 6.65, 8.25)
    ax.text(0.1, 6.75, "Services", fontsize=8, style="italic")
    svc = ["logger.c\nsample, line", "sdlog.c\nfiles, sync", "cli.c\ncommands",
           "power.c\nSTOP, watchdog", "rtc.c\ntime, alarm", "timing.c, fmt.c\nµs clock, CRC-32"]
    w, gap = 2.15, 0.18
    for i, t in enumerate(svc):
        box(ax, 0.1 + i * (w + gap), 5.35, w, 1.15, t, 6.5)
    ax.text(0.1, 4.8, "Drivers", fontsize=8, style="italic")
    drv = ["bmp280.c", "mpu6050.c", "adc.c", "i2c.c", "spi.c\nsd.c", "diskio.c\nFatFs R0.16",
           "uart.c\nDMA, ISR", "board.c/h\nregisters"]
    w, gap = 1.6, 0.143
    for i, t in enumerate(drv):
        box(ax, 0.1 + i * (w + gap), 3.45, w, 1.1, t, 6.5)
    box(ax, 0.1, 1.9, 13.8, 0.95,
        "STM32F103 peripherals: I2C1, SPI1, USART1, ADC1, DMA1, RTC, EXTI, IWDG, SysTick, DWT, GPIO",
        7.2, "#f6f6f6", round_=False)
    box(ax, 0.1, 0.25, 13.8, 1.0,
        "Mutexes, always taken in this order:  LOCK_UART -> LOCK_I2C -> LOCK_SD\n"
        "(recursive, with priority inheritance)", 7.2, "white", ls=(0, (4, 3)), round_=False)
    return save(fig, out)


# ---------------------------------------------------------------- one sample in time

def fig_timing(out, t_read, t_line, t_uart, t_sd, t_total, line_bytes, baud=115200):
    """One sample in time, from the smallest measured step times (ms)."""
    tx_ms = line_bytes * 10 / baud * 1000
    fig, ax = plt.subplots(figsize=(7.0, 3.0))
    rows = ["PB12 (test point)", "Sensor task", "Logger task", "UART TX (DMA)", "CPU"]
    ys = {r: len(rows) - i for i, r in enumerate(rows)}
    h = 0.3

    def seg(row, a, b, label="", fill="white"):
        y = ys[row]
        ax.add_patch(Rectangle((a, y - h), b - a, 2 * h, fc=fill, ec=INK, lw=0.7))
        if label:
            ax.text((a + b) / 2, y, label, ha="center", va="center", fontsize=6.8)

    end = 48
    y = ys["PB12 (test point)"]
    ax.plot([-2, 0, 0, t_read, t_read, end], [y - h, y - h, y + h, y + h, y - h, y - h], color=BLUE, lw=1.2)
    ax.text(t_read / 2, y + h + 0.12, f"sensor read {t_read:.1f} ms", ha="center", fontsize=6.8)
    bmp0, bmp = 0.6, 15.0
    adc = 6.0
    seg("Sensor task", 0, bmp0)
    seg("Sensor task", bmp0, bmp0 + bmp, "BMP280 converts, task waits", "#fde0bf")
    seg("Sensor task", bmp0 + bmp, t_read - adc, "I2C", "#d4ecd4")
    seg("Sensor task", t_read - adc, t_read, "ADC", "#e5dcf0")
    a = t_read
    seg("Logger task", a, a + t_line)
    seg("Logger task", a + t_line, a + t_line + t_uart)
    seg("Logger task", a + t_line + t_uart, a + t_line + t_uart + t_sd + 0.2)
    ax.text(a + t_line + t_uart + t_sd + 0.8, ys["Logger task"], "line + CRC, hand to DMA, SD write",
            va="center", fontsize=6.8)
    u0 = a + t_line + t_uart
    seg("UART TX (DMA)", u0, u0 + tx_ms, f"{line_bytes} bytes, {tx_ms:.1f} ms", "#f9d5d5")
    seg("CPU", 0, bmp0 + 0.4)
    seg("CPU", bmp0 + 0.4, bmp0 + bmp - 1.0, "STOP", "#cfe2f3")
    seg("CPU", bmp0 + bmp - 1.0, u0 + tx_ms, "run or SLEEP")
    seg("CPU", u0 + tx_ms, end, "STOP", "#cfe2f3")
    ax.axvline(t_total, color=MID, lw=0.7, ls="--")
    ax.text(t_total + 0.4, 0.42, f"line on the card: {t_total:.1f} ms", fontsize=6.8, color=MID)
    ax.set_yticks([ys[r] for r in rows])
    ax.set_yticklabels(rows)
    ax.set_ylim(0.25, len(rows) + 0.7)
    ax.set_xlim(-2, end)
    ax.set_xlabel("time from the start of a sample (ms); the next sample starts at 1000 ms")
    for sp in ("top", "right", "left"):
        ax.spines[sp].set_visible(False)
    ax.tick_params(axis="y", length=0)
    return save(fig, out)


# ---------------------------------------------------------------- power_idle flow (one column)

def fig_idle(out):
    fig, ax = _canvas(3.4, 5.3, 6.8, 10.6)

    def dia(x, y, w, h, t):
        ax.add_patch(plt.Polygon([(x, y + h / 2), (x + w / 2, y + h), (x + w, y + h / 2), (x + w / 2, y)],
                                 fc="white", ec=INK, lw=0.9))
        ax.text(x + w / 2, y + h / 2, t, ha="center", va="center", fontsize=6.5)

    def down(y1, y2, label=""):
        arrow(ax, 2.15, y1, 2.15, y2)
        if label:
            ax.text(2.3, (y1 + y2) / 2, label, va="center", fontsize=6.5, color=MID)

    box(ax, 0.0, 9.8, 4.3, 0.75, "idle task: no task due for T ms\n-> power_idle(T)", 6.5, GREY)
    dia(0.75, 8.2, 2.8, 1.1, "mode = run?")
    box(ax, 4.9, 8.37, 1.9, 0.75, "return:\nCPU spins", 6.5)
    dia(0.45, 6.25, 3.4, 1.4, "mode = stop and\nSTOP allowed?")
    box(ax, 4.9, 6.57, 1.9, 0.75, "WFI: SLEEP,\n1 ms tick", 6.5)
    box(ax, 0.0, 4.35, 4.3, 1.3, "interrupts off; return if a task\nis ready or the UART is busy;\n"
        "stop SysTick; alarm at T - 1 ms", 6.5)
    box(ax, 0.0, 2.95, 4.3, 0.85, "WFE: STOP until the RTC alarm\n(EXTI 17) or a key (EXTI 10)", 6.5, "#d9d9d9")
    box(ax, 0.0, 0.0, 4.3, 2.35, "resync the RTC;\nslept counts -> ms, keep\nthe fraction for next time;\n"
        "vTaskStepTick();\nrestart SysTick; feed watchdog", 6.5)
    box(ax, 4.9, 0.6, 1.9, 1.15, "woken by a key:\nconsole awake\n30 s", 6.5)
    down(9.8, 9.3)
    arrow(ax, 3.55, 8.75, 4.9, 8.75, "yes", fs=6.5, off=(0, 0.12))
    down(8.2, 7.65, "no")
    arrow(ax, 3.85, 6.95, 4.9, 6.95, "no", fs=6.5, off=(0, 0.12))
    down(6.25, 5.65, "yes")
    down(4.35, 3.8)
    down(2.95, 2.35)
    arrow(ax, 4.3, 1.17, 4.9, 1.17)
    return save(fig, out)


# ---------------------------------------------------------------- data plots (one column)

def fig_drift(out, stamps, period, name):
    first = stamps[0]
    offs = [(s - first).total_seconds() - i for i, s in enumerate(stamps)]
    fig, ax = plt.subplots(figsize=(3.4, 2.0))
    ax.step(range(1, len(offs) + 1), offs, where="post", color=BLUE, lw=1.0, label="time stamp - line number")
    ax.plot([1, len(offs)], [0, (len(offs) - 1) * (period - 1)], "--", color=ORANGE, lw=1.0,
            label=f"mean period {period:.4f} s")
    ax.set_xlabel(f"data line in {name}", fontsize=7)
    ax.set_ylabel("seconds", fontsize=7)
    ax.tick_params(labelsize=6.5)
    ax.grid(True, alpha=0.3, lw=0.5)
    ax.legend(loc="upper left", frameon=False, fontsize=6.5)
    ax.set_ylim(-0.3, max(offs) + 1.6)
    return save(fig, out)


def fig_log(out, times, cols):
    """Panels of the example log, one column wide. cols: list of (label, [(name, values, colour)])."""
    fig, axes = plt.subplots(len(cols), 1, figsize=(3.4, 0.85 * len(cols) + 0.35), sharex=True)
    for ax, (label, series) in zip(axes, cols):
        for name, vals, color in series:
            ax.plot(times, vals, "-", color=color, lw=0.5, label=name)
        ax.set_ylabel(label, fontsize=6.5)
        ax.tick_params(labelsize=6)
        ax.grid(True, alpha=0.3, lw=0.4)
        if len(series) > 1:
            ax.legend(loc="lower left", bbox_to_anchor=(0, 1.0), frameon=False, fontsize=6, ncol=len(series),
                      borderaxespad=0.1)
    import matplotlib.dates as mdates
    axes[-1].xaxis.set_major_formatter(mdates.DateFormatter("%H:%M"))
    axes[-1].set_xlabel("time of day", fontsize=6.5)
    fig.align_ylabels(axes)
    fig.tight_layout(h_pad=0.3)
    return save(fig, out)
