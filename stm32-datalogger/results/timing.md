# Timing

## Procedure

1. Logging on at 1 s, `set time`, then `timing reset`.
2. Leave it logging without typing (51 min here).
3. Type `timing` (values in us, measured with the DWT cycle counter) and `tasks`.

## Results

20-10-2026, 115200 baud, 128-byte UART DMA buffers, STOP mode on, 3056 samples (51 min).
Log: LOG0123.CSV, 3154 lines, CRC bad 0, no gaps.

| Step | last | min | max |
|---|---|---|---|
| sample period | 1 000 000 | 999 998 | 1 000 002 |
| sensor read | 24 198 | 24 197 | 24 198 |
| line + CRC | 1 343 | 1 318 | 1 408 |
| UART (DMA) | 99 368 * | 2 524 | 99 368 * |
| SD write | 5 261 | 261 | 41 815 |
| sample -> SD | 130 356 * | 28 487 | 130 356 * |

\* while I typed `timing`: the CLI holds the UART, the logger waits up to 100 ms and then skips the
UART copy of that line (the line still goes to the SD card). 8 lines skipped in 51 min.

`tasks`, CPU share of the time awake: IDLE 81.0 %, Logger 10.6 %, Sensor 8.0 %, Cli 0.1 %.
Heap: 3680 B free, lowest 1520 B of 10240. Records dropped: 0.

Earlier run at `set rate 250` (19-10-2026, 822 samples): period 249 998 - 250 002 us.

## Interpretation

- The sample period stays within +-2 us over 3056 samples. The next sample is planned from the
  last planned time, so the error does not add up.
- That is the period by the firmware's own clock (FreeRTOS tick + SysTick). Against the RTC
  crystal it is about 1.0016 s: the log spans 3158 s with 3154 lines, and the stamp jumps by 2 s
  five times, every 651 to 687 lines. The cause is not confirmed; the main README lists what in
  power_idle() can make the tick differ from the RTC, and how to tell them apart.
- Sensor read is 24.2 ms and varies by only 1 us. Most of it is the BMP280 conversion (15 ms);
  about 6 ms is the ADC, which is now switched on, calibrated and off again for every sample.
  The task sleeps during both.
- UART: 2.5 ms per line when nobody types (66.5 ms at 9600 baud with 64-byte buffers earlier).
- SD write: 0.26 ms when FatFs only buffers the line, up to 42 ms when it syncs (every 10 lines).
- A sample reaches the SD card 28 ms after it was taken.
- The CPU numbers count only the time awake; the chip is in STOP about 96 % of the time
  (see current_measurements.md).
