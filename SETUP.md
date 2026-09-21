> **Superseded.** This file is an early development snapshot from before
> this fork's full feature set (Manual Finish, Reverse Tube, per-profile
> charge modes, PID/Adaptive controller selection, etc.) existed, and it
> still references the PC simulator, which is no longer built or
> distributed with this fork's releases. For current setup instructions,
> use [QUICKSTART.md](QUICKSTART.md); for the full list of changes, use
> [CHANGELOG.md](CHANGELOG.md). This file is kept only for historical
> reference.

# OpenTrickler_ML — modified build (historical snapshot)

This archive contains the full project source with all the changes made in
this session, plus a prebuilt firmware image. It does **not** contain the
four upstream submodules (pico-sdk, FreeRTOS-Kernel, u8g2,
Trinamic-library), which are ~670 MB and are fetched from their own
repositories — see below.

## Contents

```
src/                 firmware source (all modifications are here)
src/ot_logo_icon.h   generated 64x32 boot-splash bitmap
sim/                 PC simulator (see sim/README.md)
prebuilt/            built firmware, flashable as-is
library/             empty submodule mount points
```

## Just flashing it

`prebuilt/opentrickler_ml.uf2` is built for **Pico 2 W** and contains
everything from this session. Hold BOOTSEL, plug in, drag the .uf2 onto the
drive that appears.

Note: the EEPROM revision was bumped, so charge-mode settings reset to
defaults on first boot. Re-enter your coarse stop threshold and any other
tuned values.

## Building the firmware yourself

Restore the submodules first:

```
git submodule update --init --recursive
```

(If this archive is not a git checkout: `git clone` the original repo, then
copy `src/` and `sim/` from here over the top.)

Then:

```
export PICO_SDK_PATH=$PWD/library/pico-sdk
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DPICO_BOARD=pico2_w -DPICO_SDK_PATH=$PICO_SDK_PATH
cmake --build build -j
```

Output lands in `build/app.uf2`. Last verified build: 20.03% flash,
51.45% RAM, no warnings.

## Building the PC simulator

Independent of the firmware toolchain — no Pico SDK or ARM compiler needed.
See `sim/README.md` for details and limitations.

```
cmake -S sim -B sim/build
cmake --build sim/build --config Debug
```

## Changes made in this session

1. **Per-profile charge modes** — charge-mode settings moved from one global
   set to one per profile, with REST and web-portal support.
2. **Kernel-weight-aware AI tuning** — the model estimates single-granule
   powder weight and uses it as a resolution floor.
3. **Manual kernel weight entry** — a measured value overrides the estimate
   (`/rest/ai_kernel_weight`, plus UI).
4. **Undercharge salvage fix** — the top-up routine no longer gives up on
   shortfalls over a flat 0.30 gn, which was causing "remove cup" while
   still well under target.
5. **Coarse stop authority** — the adaptive controller can no longer
   silently override the configured coarse stop threshold. The clamp is
   applied *after* all the internal floors, which is what the first attempt
   at this fix got wrong.
6. **Accuracy vs speed slider** — per-profile bias, neutral at centre.
7. **UI** — modernised styling, dark mode, logo in the web portal and as a
   boot splash on the 12864 display.
8. **PC simulator** — `sim/`.

## Still outstanding

- The simulator currently exercises the classic PID fallback, not the
  adaptive controller, because simulated flash has no characterized AI
  model. Seeding one is the next step before the fixes above can be
  validated in software.
- The simulator's trickler flow constants are plausible but not measured
  against real hardware; calibrate before trusting absolute timings.
- The accuracy/speed slider's ±30% range is an untested judgement call.
