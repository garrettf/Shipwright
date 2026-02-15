# Fast-Forward Audio Plan (Pitch-Preserving)

## Decision

Implement an **in-tree WSOLA time-stretcher**, inspired by Chromium's `AudioRendererAlgorithm` structure.

Why:
- best fit for this repo’s constraints
- no external licensing baggage
- deterministic performance and full control over tradeoffs
- proven architecture reference available from Chromium

## Decision Notes

Rejected for first pass:
- SoundTouch: mature, but LGPL dependency cost not worth it here
- Rubber Band: quality is strong, licensing/integration cost too high for this branch
- Signalsmith Stretch: excellent library, but recommended operating range does not match `2x-8x`
- Sonic: practical for voice, weaker fit for mixed BGM + SFX

Important realism check:
- pitch-preserving `8x` will never sound pristine
- plan includes quality guardrails and explicit fallback policy

## Repo Constraints We Must Respect

- audio device: `32,000 Hz`, stereo output path in SoH thread
- current audio chunking: ~`528/560` samples per internal frame, typically `R_UPDATE_RATE` grouped
- game-speed can change every frame
- render FPS must remain independent

## Implementation Plan

## 1) Audio Speed Controls + Debug Telemetry

- add CVars:
  - `gSettings.GameSpeed.AudioMode` (`Mute`, `Chipmunk`, `PitchPreserve`)
  - `gSettings.GameSpeed.AudioMaxPitchPreserve` (default `4.0`)
  - `gSettings.GameSpeed.AudioDebug` (off by default)
- keep current mute behavior as fallback path
- add tiny debug counters (source samples in, stretched samples out, underflows)

Commit goal:
- no behavior change when `AudioMode != PitchPreserve`

## 2) Add Time-Stretch Module (Scaffold)

- create `soh/soh/audio/GameSpeedTimeStretch.h/.cpp`
- class responsibilities:
  - hold input FIFO (interleaved `s16` stereo)
  - generate fixed-length output blocks
  - reset cleanly on mode changes / seek / load-state
- add unit-ish self-check helper (compiled in dev builds) for invariants

Commit goal:
- compile-time integration only, no runtime routing yet

## 3) Route Audio Through Module in OTRAudio_Thread

- in `PitchPreserve` mode:
  - accumulate *source* audio proportional to game speed
  - request exactly target output length for device queue
- preserve old direct path for `Mute`/`Chipmunk`
- keep locking behavior unchanged

Commit goal:
- end-to-end data path active, temporary naive copier inside module

## 4) Implement WSOLA Core (Chromium-Inspired)

- implement overlap/add with seek:
  - OLA window ~`20 ms`
  - search interval ~`30 ms`
  - cross-correlation seek for best overlap
- optimize near-`1.0x` with direct copy shortcut
- support dynamic speed updates without hard discontinuities

Commit goal:
- audible pitch-preserving speed-up at `2x-4x`

## 5) High-Speed Policy + Guardrails

- for speed `> AudioMaxPitchPreserve`:
  - default: soft fallback to `Mute` (or optional chipmunk, CVar-controlled)
- add underrun protection:
  - if stretcher lacks source, output short zero-crossfade-safe silence
- clear buffers on save/load and hard state changes

Commit goal:
- stable behavior through aggressive speed toggles

## 6) UI + Settings Wiring

- add settings rows in `SohMenuSettings.cpp`
- wording explicit about quality limits at high multipliers
- keep defaults safe (`Mute` or `PitchPreserve<=4x`, per preference chosen during testing)

Commit goal:
- user-facing control complete

## 7) Validation Matrix

- verify at `1x/2x/3x/4x/8x`
- scenes: overworld, combat, menu, file select, transitions
- check:
  - no render-FPS coupling regressions
  - no audio queue runaway/underrun spam
  - acceptable artifact profile through `4x`

Commit goal:
- document final known limits in this file

## Sources

- WSOLA reference: [Verhelst/ROELANDS](https://www.isca-archive.org/eurospeech_1993/verhelst93_eurospeech.html)
- Chromium implementation references:
  - [audio_renderer_algorithm.h](https://chromium.googlesource.com/chromium/src/+/HEAD/media/filters/audio_renderer_algorithm.h)
  - [audio_renderer_algorithm.cc](https://chromium.googlesource.com/chromium/src/+/HEAD/media/filters/audio_renderer_algorithm.cc)
