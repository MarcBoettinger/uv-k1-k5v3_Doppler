# Doppler 0.2 (overlay app, F4HWN Labs ABI 1 / API 3)

Simulates and tracks the Doppler shift of a moving transmitter (ISS, aircraft)
on a stationary receiver: `f = f0 (1 ± v/c)`. RX only: no transmit service is
used and PTT never does anything but wake the screen.

Set the VFO to the downlink first (145.800 MHz for the ISS voice/APRS channel):
the app starts on it. The receiver is retuned only within ±2 MHz of the VFO (its RF
path and filter stay as the loader set them); farther away the header shows `SIM`.

## Pass mode (automatic tracking)

Take the three numbers from any pass predictor (time until rise, duration,
maximum elevation), e.g. *rise in 5:20:45, 11 min, 35°*:

1. `4` opens PASS SETUP. Type `10500`, `MENU` (digits fill from the right: 1:05:00),
   type `11`, `MENU`, type `35`, `MENU`. The countdown starts.
2. The header counts down (`T-1:05:00`); the dial is already on the rise
   frequency. Beeps at one minute before and at the rise.
3. From the rise the velocity follows the pass: +7 km/s approaching, 0 at closest
   approach, −7 km/s receding. The dial re-centres by itself whenever it is 200 Hz
   off (about 34 retunes in 11 min, each a 40 ms audio gap). The header shows `P mm:ss`.
4. Correct by ear any time: arrows add a frequency correction that the tracking keeps,
   `1`/`3` add RIT on top, `0` clears both, `8` = "this is the closest approach right now"
   (re-aligns the whole pass timeline to what you hear).
5. `EXIT` stops the pass (second `EXIT` leaves the app). A low beep marks the end.

The clock runs only while the app is open and its accuracy over hours is unknown:
start the countdown 10–15 minutes before the pass rather than hours ahead.
The curve is a fit of the exact circular-orbit geometry (420 km): within about 90 m/s
(45 Hz at 145.8 MHz, 136 Hz at 437.8 MHz) of it, Earth's rotation not modelled
(up to ±150 Hz at 145.8 MHz).

## Manual mode

| Screen line | Meaning |
| --- | --- |
| `DOPPLER APPR` + bars | approaching / receding / `LOST` right after a retune / `SIM`; 4 bars = simulated signal quality |
| `F0 145.800000MHz` | carrier |
| `V +7000 m/s` | relative velocity (sign = direction) |
| `dF+3404 E-3404` | Doppler shift (Hz) and your tuning error (Hz, + = you are above the signal) |
| `RX 145.800000` | where the receiver is listening (dial + RIT applied) |
| meter | block = where the signal sits in your passband; centre = zero beat, ±2.9 kHz full scale |
| `S:100Hz R:+0` | dial step and RIT |

| Key | Action |
| --- | --- |
| Arrows | VFO dial ± step; hold accelerates ×10 after 12 repeats, ×100 after 40 |
| `*` | step 1 Hz / 10 Hz / 100 Hz / 1 kHz |
| `1` / `3` | RIT −/+ 100 Hz (±5 kHz); `2` RIT = 0; `0` dial and RIT = 0 |
| `9` | auto zero-beat (sets the dial so the error is 0) |
| `F` | approaching ↔ receding |
| `7` | m/s ↔ km/h (the stored velocity does not change) |
| `MENU` | type F0 (`ddd.ddddd` MHz, digits left to right), `MENU` → velocity (5 digits), `MENU` → done. `EXIT` = backspace, then cancel |
| `4` | pass setup (above) |
| `EXIT` | quit |

Every real retune drops the audio for 40 ms (`LOST`). The BK4819 resolves 10 Hz:
the 1 Hz step is exact on screen, not on the air.

## Build and test

```sh
./compile-app.sh doppler          # Docker route, from the repo root
# or, with arm-none-eabi-gcc and python3 on PATH, no Docker:
cd App/apps/doppler && ./build.sh
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I. test/test_core.c -o /tmp/tc -lm && /tmp/tc
```

The result is `Doppler.app` (in `build/Apps/` when built with compile-app.sh).
Install it with UV Studio, Apps (Labs), local `.app` file; launch with F + 7.
