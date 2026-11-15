# Doppler overlay app

`Doppler` calculates and tracks the Doppler shift of a moving transmitter (the
ISS, other satellites, aircraft) on a stationary receiver, using
`f = f0 (1 ± v/c)`. For a satellite pass you type four numbers from any pass
predictor (time until rise, duration, maximum elevation, altitude); the app
counts down, follows the pass and keeps the receiver on the shifted frequency
by itself. It is receive-only: no transmit service of the API is used and PTT
only wakes the screen.

It needs no computer, no clock calibration and no extra firmware feature, only
overlay-app support (API level 3). For FM satellite passes with transmit, the
firmware's `Sat Track` (orbit data computed on a PC, uplink and downlink
corrected) is more accurate where available; `Doppler` is the lightweight,
stand-alone alternative, and it also works for any signal you can enter a
velocity for.

Tested on a UV-K6 with F4HWN Labs 6.3.0, FM, on the ISS downlink (145.800 MHz).
The modulation auto-detection and the `5` key described below were added
afterwards and have been checked on the host only.

## Quick start: a satellite pass

Set the VFO to the downlink (145.800 MHz for the ISS voice/APRS channel) on the
main screen, then launch the app (`F` then `7`). It starts on the VFO frequency.
Example: the predictor says *rise in 5:20:45, 11 minutes, maximum elevation 35°*,
ISS (about 420 km).

1. Press `4` (PASS SETUP). Digits fill from the right, `MENU` confirms a field:
   - `52045` `MENU`: time until rise, 5:20:45 (hhmmss);
   - `11` `MENU`: duration, minutes;
   - `35` `MENU`: maximum elevation, degrees;
   - altitude in km (200..2000), then `MENU`: for the ISS just press `MENU`,
     420 km is the default.
2. The header counts down (`T-5:20:45`). The receiver is already on the rise
   frequency. Beeps sound one minute before the rise and at the rise.
3. From the rise the velocity follows the pass: about +7 km/s approaching,
   0 at the closest approach, about −7 km/s receding. The receiver follows it
   automatically. The header shows the pass time (`P 03:12 APPR` / `RECD`).
4. Correct by ear at any time: the arrow keys add a frequency correction that
   the tracking keeps, `1`/`3` add RIT on top, `0` clears both. If the signal
   crosses zero-beat at a different moment than the app expects, press `8` at
   that moment (it declares *now* the closest approach and shifts the whole
   timeline).
5. `EXIT` stops the pass (a second `EXIT` leaves the app). A low beep marks the
   end of the pass.

The clock runs only while the app is open and its accuracy over hours is
unknown: start the countdown 10–15 minutes before the pass rather than hours
ahead, and keep the app open.

## Manual mode

Without a pass the app is a Doppler calculator and dial. `MENU` opens F0
(`ddd.ddddd` MHz, digits left to right), `MENU` again opens the velocity (5
digits in the current unit; `7` on the normal screen switches m/s and km/h),
`MENU` confirms. `F` flips approaching/receding, `9` zero-beats the dial.

## Screen

| Line | Meaning |
| --- | --- |
| `DOPPLER APPR` + bars | approaching / receding / `SIM` (see below); four bars = simulated tuning quality |
| `F0 145.800000MHz` | carrier (starts on the VFO frequency) |
| `V +7000 m/s` | relative velocity, sign = direction |
| `dF+3404 E-120` | Doppler shift (Hz) and tuning error (Hz, + = receiver above the signal) |
| `RX 145.803404` | where the receiver listens (dial and RIT applied) |
| meter | block = where the signal sits in the passband; centre = zero-beat, ±2.9 kHz full scale |
| `S100 T500 R+0` | dial step (Hz), tracking tolerance (±Hz), RIT (Hz) |

During a pass line 1 shows the countdown (`T-5:20:45`) or the pass time
(`P 03:12 RECD`). `SIM` means the frequency is more than 2 MHz from the VFO:
the receiver is then not retuned (its RF path stays as the loader set it) and
the app is a pure calculator.

## Keys

| Key | Action |
| --- | --- |
| Arrows | dial ± step (manual) / frequency correction (pass); hold accelerates ×10 after 12 repeats, ×100 after 40 |
| `*` | dial step 10 / 100 / 500 / 1000 Hz |
| `5` | tracking tolerance FM ±500 Hz / AM ±250 Hz / SSB ±30 Hz |
| `1` / `3` | RIT −/+ 100 Hz (±5 kHz) |
| `2` | RIT = 0 |
| `0` | dial and RIT = 0 (pass: also the correction) |
| `9` | auto zero-beat (manual mode) |
| `F` | approaching ↔ receding (manual mode) |
| `7` | m/s ↔ km/h (the stored velocity does not change) |
| `MENU` | type F0, then velocity (manual mode) |
| `4` | pass setup |
| `8` | pass: *now* is the closest approach |
| `EXIT` | backspace while typing, then cancel; stops a pass; leaves the app |

## Tracking tolerance

Every retune relocks the PLL and drops the audio for a few milliseconds, so a
pass retunes only as often as the mode allows, as the firmware's Sat Track does:
the dial is moved only when it is more than the tolerance off the model, and it
lands one tolerance *below* the model (the shift only falls during a pass), so
the error sweeps from +tolerance to −tolerance before the next retune.

The tolerance depends on the mode, not on the frequency: FM ±500 Hz, AM
±250 Hz, SSB/CW ±30 Hz. The carrier frequency sets how fast the shift moves and
therefore how often a retune is needed. Retunes in an 11-minute, 35° ISS pass
(simulated, model checked every 200 ms):

| Mode | 145.8 MHz | 437.8 MHz |
| --- | --- | --- |
| FM ±500 Hz | 7 | 21 |
| AM ±250 Hz | 14 | 41 |
| SSB ±30 Hz | 107 | 297 |

On start the tolerance is taken from the selected VFO's modulation when the
firmware reports it (builds with the BEAM feature); otherwise it is FM. Press `5`
to change it. For SSB at UHF every retune is audible: expect an interruption
every second or two near the closest approach.

## How it works and its limits

- The shift is `f0·v/c` in 32-bit integer arithmetic (no floating point, no
  64-bit divide), within about 1 Hz of an exact calculation for 18 MHz to 1.3 GHz
  and velocities up to 99 999 m/s. The BK4819 resolves 10 Hz.
- The pass velocity comes from a fit of the exact circular-orbit geometry, scaled
  to the duration you enter: `v = V·u/√(u² + c²)`, `u` = time from the closest
  approach, `c` from the maximum elevation. It is within about 90 m/s of the exact
  model at 420 km (45 Hz at 145.8 MHz) and within about 250 m/s for 350–1200 km
  (122 Hz at 145.8 MHz); ignoring altitude would be off by up to 1.9 km/s.
- Earth's rotation is not modelled (up to about ±150 Hz at 145.8 MHz) and the
  orbit is assumed circular. The arrow keys and `8` correct what remains by ear.
- The tracking is not orbit data: expect passes tracked by ear to be as good as
  your timing and the predictor's numbers.

## Build and test

```sh
./compile-app.sh doppler                   # from the repository root
# or, with arm-none-eabi-gcc and python3 on PATH:
cd App/apps/doppler && ./build.sh          # prints the size, fails above 4096 bytes

gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I. \
    test/test_core.c -o /tmp/tc -lm && /tmp/tc
```

The result is `Doppler.app`; install it with UV Studio and launch it with `F` + `7`.

`doppler_core.h` holds the arithmetic, the key logic and the pass model with no
firmware calls; `test/test_core.c` runs it on the host (98 checks, including a
comparison with an independent exact orbit model and a full simulated pass in
every tolerance mode). `doppler_app.c` is the thin radio and screen layer.
