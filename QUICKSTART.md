# Quick Start Guide — TP Custom Rifle Parts OpenTrickler Firmware

This guide gets a Pico 2 W / RP2350 OpenTrickler controller running on this
fork, from flashing to your first charges. It assumes the hardware
(trickler, motors, scale interface, 128x64 display) is already built and
wired per the original OpenTrickler hardware documentation.

> This firmware controls reloading equipment. Verify every charge with a
> calibrated scale, supervise all operation, and keep a known-good `.uf2`
> rollback image available.

## 1. Flash the firmware

1. Disconnect motor power before flashing.
2. Hold the **BOOTSEL** button on the Pico 2 W, then plug it into USB while
   still holding it. Release once it appears as a `RPI-RP2` USB drive.
3. Drag `opentrickler_ml_latest.uf2` onto the `RPI-RP2` drive.
4. The board reboots automatically once the copy finishes.

If you're updating an existing controller rather than flashing for the
first time: the EEPROM layout in this build changed to support Reverse
Tube, so **your charge-mode settings (stop thresholds, tolerances, Manual
Finish, Reverse Tube, LED colours) will reset to defaults on first boot.**
Your saved AI characterization models and base profile calibration are not
affected. Re-enter your tuned values (or re-run characterization) after
updating — see [CHANGELOG.md](CHANGELOG.md) for the full list of what
changed.

## 2. Connect to Wi-Fi and open the web portal

1. On first boot the controller starts its own setup access point — connect
   to it from your phone or laptop and give it your 2.4 GHz Wi-Fi
   credentials.
2. Once it joins your network, find its address (your router's client list,
   or the controller's own display) and open it in a browser.
3. You should see the OpenTrickler web portal with the TP Custom Rifle
   Parts branding.

## 3. Set your appearance (optional)

Settings → Appearance lets you set the button/accent colour and the
background colour independently, with presets for each, or reset both to
defaults with one button. Purely cosmetic — safe to skip.

## 4. Create a profile

Keep **one profile per powder / tube size / scale combination**. Mixing
setups on one profile is the single biggest cause of "the AI tuning is
inaccurate" reports — a model characterized on one trickler tube size does
not carry over to another.

1. Go to Settings → Profiles and pick an empty slot.
2. Name it for the setup, e.g. `Varget 36.5 - medium tube` or
   `H4350 - large tube`.
3. Save before running characterization.

If you use the small/medium/large trickler tube options, treat each tube
size on a given powder as its own profile (or at minimum, re-run
characterization every time you change tube size — see step 6). The
controller has no way to detect which physical tube is installed, so it
relies entirely on the profile you selected matching the tube that's
actually on the machine.

## 5. Choose your controller

Settings → Charge Mode → Controller:

- **PID - Original OpenTrickler Firmware** (default, recommended). The PID
  gains, speed limits, and stop thresholds all live in the profile where
  you can see and adjust them directly. AI characterization is used to
  *suggest* values for those fields (a "Suggested PID Baseline") rather
  than to drive the charge itself.
- **Adaptive - Based on [Opentrickler_ML](https://github.com/WhoKilledBambiLabs/Opentrickler_ML)
  Firmware**. The legacy adaptive controller, which computes its own
  margins at runtime and overrides several profile settings. Kept for
  compatibility; its internal margins are not directly user-editable.

If you're not sure which to use, start with PID — it's the more
transparent and directly-tunable option, and it's what AI characterization
is designed to feed into.

## 6. Run AI characterization

Characterization measures how powder actually moves through your specific
machine and profile — do this once per profile, and again any time the
tube size, powder, or scale changes.

1. Settings → AI Tuning, select the profile you just created.
2. Enter your normal target charge weight for that setup.
3. Confirm the scale is stable and zeroed.
4. Start characterization and follow the pan remove/return prompts — it
   runs a series of controlled coarse and fine pulses on its own.
5. Wait for it to reach `ready_to_save`.
6. Review the **Suggested PID Baseline** it produces. This is a suggestion,
   not an automatic change — nothing is applied until you tell it to.
7. If it looks reasonable, apply it to the profile's PID settings.

Don't trust a characterization run if the scale was unstable, powder
bridged in the tube, the pan workflow was interrupted, or you had the
wrong profile selected.

**If you change trickler tube size, re-run characterization for that
profile (or switch to the matching profile) before trusting the numbers.**
This build fixes a bug where the large tube's characterization and
suggested PID values could badly overthrow (previously up to ~20 gn) — see
[CHANGELOG.md](CHANGELOG.md) — but characterization is still
tube-specific: a model built on the small tube is not expected to be
correct on the large tube.

## 7. Optional: Manual Finish

For large-kernel powders (extruded stick powders where a single kernel is
close to or larger than your Accepted Charge Tolerance), enable Manual
Finish in Charge Mode settings and enter your cut-kernel weight (e.g. 0.04
gn for a kernel cut in half). Instead of guessing at a fractional dose the
controller can't deliver, it stops, tells you how many hand-cut fragments
to add, and verifies the result.

## 8. Optional: Reverse Tube

If you're seeing consistent post-stop dribble (a little extra powder lands
after the motor has already stopped), enable Reverse Tube for the coarse
and/or fine motor in Charge Mode settings and set a small number of
reverse revolutions (typical starting points: 0.05–0.3 rev coarse,
0.02–0.15 rev fine). It briefly reverses that tube's motor right after its
own genuine final stop to pull back residual powder sitting in the
tube/gate. Start small and increase only if dribble persists — too much
reverse travel can starve the next charge's initial feed.

## 9. Everyday use

1. Select the profile matching your current powder/tube/scale setup.
2. Enter the target charge weight.
3. Place the pan, wait for zero.
4. Let the controller charge and settle; follow Manual Finish prompts if
   enabled and triggered.
5. Remove the pan only when prompted; verify the charge on your own
   reference scale periodically.
6. If you start seeing repeatable under/over behavior, re-run
   characterization rather than nudging individual settings blind.

## 10. Backing up your setup

Settings → Export Profile (single profile) or Export Config (all 8 slots)
downloads a JSON file with the profile's base configuration, its
charge-mode settings, and its kernel weight — enough to fully restore that
profile via Import on another controller or after a reset. Worth doing
once you have a profile you trust.

## Getting help

- Full list of what this fork changes vs. upstream: [CHANGELOG.md](CHANGELOG.md)
- Upstream AI tuning algorithm details and REST reference:
  [AI tuning reference](https://whokilledbambilabs.github.io/Opentrickler_ML/ai-tuning-reference.html)
- Building from source: see the "Build From Source" section in
  [README.md](README.md)
