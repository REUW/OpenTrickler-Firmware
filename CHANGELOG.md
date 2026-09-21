# Changelog — TP Custom Rifle Parts Fork

This file documents the changes made in **this fork** (TP Custom Rifle Parts)
on top of [WhoKilledBambiLabs/Opentrickler_ML](https://github.com/WhoKilledBambiLabs/Opentrickler_ML),
which is itself a fork of the original [Eamars/OpenTrickler](https://github.com/eamars/OpenTrickler)
firmware. Upstream's own AI-algorithm-tuning history (coarse handoff logic,
recovery bounds, etc.) is tracked separately in
[`firmware_release_history/CHANGELOG.md`](firmware_release_history/CHANGELOG.md)
and is not duplicated here.

Base: `Opentrickler_ML` `2026.07.12-beta.14`.

## [Unreleased]

### Added

- **Reverse Tube (anti-dribble)** — after a tube reaches its own genuine
  final stop for a charge (not on an abort), it can be briefly reversed to
  pull back the last bit of powder sitting in the tube/gate, reducing
  post-stop dribble. Configurable independently for the coarse and fine
  tubes (enable + number of motor revolutions, 0–10, typical 0.05–0.3 rev
  coarse / 0.02–0.15 rev fine), each reusing that tube's own configured
  minimum flow speed rather than needing a separate reverse-speed setting.
  New settings page section under Reverse Tube, next to Fine Trickler Stop
  Threshold.
- **Background colour theming** — Appearance settings previously only let
  you recolour buttons/accents. Background colour is now independently
  customizable with its own colour picker and six presets (Warm Cream, Cool
  Grey, Slate Blue, Sage, Pure White, Stone), plus a "Reset appearance to
  defaults" button. Both button accent and background persist across
  sessions and are derived consistently in light and dark mode.
- **Controller labeling and documentation link** — the Controller dropdown
  now reads "PID - Original OpenTrickler Firmware" / "Adaptive - Based on
  Opentrickler_ML Firmware", with a link to the
  [Opentrickler_ML repository](https://github.com/WhoKilledBambiLabs/Opentrickler_ML)
  so it's clear which controller you're choosing and where the adaptive
  logic comes from.
- **Manual Finish** — for large-kernel powders (e.g. N565/N570-class
  extruded stick powder, ~0.08 gn/kernel) where your Accepted Charge
  Tolerance is tighter than one whole kernel, the trickler stops short of
  guessing, blinks the under-charge LED, and tells you how many hand-cut
  partial kernels to add. Configurable cut-kernel weight; verifies and
  reports pass/fail exactly like an automatic finish.
- **Accepted Charge Tolerance** — the ± grains band that decides a "good"
  charge is now a per-profile setting (previously a hardcoded 0.0205 gn).
  Statistics can optionally include out-of-tolerance rejects for
  diagnosing the trickler itself rather than judging load consistency.
- **Per-profile charge mode settings** — charge-mode configuration (stop
  thresholds, stabilization, pulse mode, Accepted Charge Tolerance, Manual
  Finish, Reverse Tube, LED colours, etc.) moved from one global config to
  one set per profile, with matching REST and web-portal support. Switch
  profiles and your tuning switches with it.
- **Kernel-weight-aware AI tuning** — the model estimates single-granule
  powder weight and uses it as a resolution floor when finishing a charge,
  with a manual override (measured kernel weight) available via
  `/rest/ai_kernel_weight` and the UI, for when you've weighed a known
  count of kernels yourself.
- **Accuracy vs Speed bias** — per-profile slider (0.0 = prioritise speed,
  1.0 = prioritise accuracy, 0.5 = balanced) that scales how much finishing
  margin and settle patience the fine phase gets.
- **Branding** — TP Custom Rifle Parts logo in the web portal and as the
  boot-splash bitmap on the 128x64 display, plus a modernized UI with light
  and dark mode.

### Changed

- **LCD status line tidied up** — the on-device status line previously
  showed both the target weight and the remaining amount, duplicating the
  title bar's own "Target: X.XXX" line. It now shows a short phase label
  (e.g. coarse/fine/settling/recovering) alongside the remaining amount
  instead, so the same information isn't shown twice and the phase is
  visible at a glance.
- **LCD decimal places default to 2** — the main screen weight readout's
  default display precision. `Decimal Places` remains a per-profile
  setting (Settings → Profile), adjustable from 0–3 independent of this
  default at any time.
- **Export Profile / Export Config now capture everything needed to
  restore a profile** — previously only the base profile configuration was
  exported. Export now also includes each profile's charge-mode settings
  (stop thresholds, Manual Finish, Reverse Tube, tolerances, etc.) and its
  kernel weight setting, for both single-profile export and full
  8-profile config export, so an exported file round-trips back into a
  complete, working profile rather than a partial one.
- **Coarse stop authority** — when the adaptive controller is in use, it
  can no longer silently override your configured coarse stop threshold;
  the clamp to that threshold is applied after all of the controller's
  internal floors, not before.
- **Undercharge salvage fix** — the top-up routine no longer gives up on
  shortfalls over a flat 0.30 gn, which previously could prompt "remove
  cup" while the charge was still well under target.

### Fixed

- **AI-tuning overthrow on large trickler tubes, and on transferring AI
  suggestions to PID** — root-caused to two related issues:
  1. Settle detection used a shorter timeout window during AI
     characterization (1400 ms) than during normal charges (3000 ms), so
     characterization could record a sample before the scale had actually
     settled, especially with the higher-capacity large tube. Both paths
     now use a consistent ~3000 ms settle window, and stage-sample
     selection now prefers samples that were confirmed settled over ones
     that were cut off by the timeout.
  2. `/rest/ai_suggestions` (the "Suggested PID Baseline" AI tuning
     produces for you to review and apply) now checks each tube's
     currently-configured minimum flow speed and widens the suggested stop
     thresholds and Kp cap accordingly, instead of assuming
     characterization's own sample speed is always representative. This
     is what caused suggested PID values to overthrow by a large margin
     (reported up to ~20 gn) specifically on the large tube size after
     applying them.

  Together these should make characterization results, and the PID values
  suggested from them, track the actual tube size in use rather than
  assuming one flow profile for all three tube sizes. As before,
  characterization should still be re-run for each tube size — a model
  learned on one tube size is not assumed to transfer to another.

- **Unreadable "neutral" buttons (dark text on a dark button) with the new
  Background Colour picker** — the Appearance page's Background Colour
  feature was overriding `--nc` (the text colour used on every
  `.btn-neutral` button, including the Settings "Apply" button and the
  "Apply" / "Save to EEPROM" confirmation dialog) to a value derived from
  the picked page background. `.btn-neutral`'s own background (`--n`) is a
  fixed dark tone that never changes with that picker, so picking a light
  page background (including the default) paired dark button text with
  that still-dark button, making it unreadable. `--nc` is no longer tied
  to the Background Colour picker and keeps its correct, fixed contrast
  against `--n` in both light and dark mode.

- **Renaming a profile silently didn't save, even though a "Settings
  Applied" success message appeared.** The Profile settings page reads as
  one continuous screen, but it's actually two separate forms stacked in
  the same section: the Profile Name and PID gains (`/rest/profile_config`)
  are one form with its own "Apply" button partway down the page, and
  everything below that (LED colours, stop thresholds, Controller, Manual
  Finish, Reverse Tube, pulse/stabilization settings) is a second form
  (`/rest/charge_mode_config`) with its *own* "Apply" button at the very
  bottom. Editing the name, scrolling straight past the first form's Apply
  button, and clicking the second form's Apply button at the bottom
  submitted only the charge-mode fields — the name change was never sent,
  even though the dialog reported success. Clicking Apply or Save to
  EEPROM from *either* button on this page now saves both forms together,
  so it no longer matters which one you reach first.

### Notes for anyone updating from an earlier build of this fork

- The EEPROM charge-mode data revision has been bumped to accommodate the
  Reverse Tube fields. **Charge-mode settings (stop thresholds,
  tolerances, Manual Finish, Reverse Tube, LED colours, etc.) will reset
  to defaults on first boot after flashing.** Base motor/profile
  calibration and AI characterization history are not affected. Re-enter
  your tuned values (or re-run characterization) after updating.
- The PC simulator (`sim/`) from an earlier development pass is not
  actively maintained or distributed with this fork's releases; only the
  Pico 2 W / RP2350 firmware (`.uf2`) is built and shipped going forward.
