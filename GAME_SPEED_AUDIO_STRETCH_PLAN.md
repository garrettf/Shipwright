# Fast-Forward Audio Options (Pitch-Preserving)

## Context

Current pipeline:
- audio device initialized at `32,000 Hz`, sample length `1024`, desired buffered `1680` (`soh/soh/OTRGlobals.cpp`)
- game audio mixed in `OTRAudio_Thread()` via `AudioMgr_CreateNextAudioBuffer(...)`
- output pushed through `AudioPlayer_Play(...)`
- current fast-forward behavior: optional mute when `gameSpeed > 1.0`

Target:
- game speed `2x-8x`
- audio follows tempo (faster timeline)
- original pitch retained
- real-time safe on desktop + lower-end targets

## Options Compared

## 1) Keep Current Mute/Chipmunk Path

Pros:
- trivial
- no risk

Cons:
- does not meet requirement
- poor UX

Verdict:
- reject

## 2) In-Tree WSOLA Implementation (Custom)

Pros:
- no third-party licensing friction
- algorithm directly aligned with requirement (time-scale modification with pitch retention)
- controllable CPU/latency tradeoffs

Cons:
- moderate implementation complexity
- tuning required (window/seek overlap) for music + SFX mix
- quality drops at extreme factors (especially near `8x`)

Risk:
- medium

## 3) SoundTouch (WSOLA-family, external)

Pros:
- mature, practical real-time time-stretch/pitch API
- known tempo/pitch/rate separation
- explicitly optimized for high speedups (quick mode)

Cons:
- additional dependency + integration surface
- LGPL licensing obligations
- quality/latency tuning still needed

Risk:
- medium

## 4) Rubber Band Library (phase-vocoder/hybrid, external)

Pros:
- high quality for music
- robust real-time mode + extensive tuning

Cons:
- heavier integration + more CPU than minimal WSOLA paths
- licensing model not ideal for this repo workflow (GPL/commercial)

Risk:
- medium-high (integration + licensing)

## 5) Signalsmith Stretch (header-only, MIT)

Pros:
- permissive license
- small integration surface

Cons:
- recommended factor range is near `0.75x-1.5x`; far outside target for `2x-8x`
- algorithmic fit/risk for extreme speedups uncertain

Risk:
- medium-high

## 6) Sonic Library (time-domain, speech-optimized)

Pros:
- permissive license
- real-time friendly

Cons:
- tuned more for voice/speech than full game music+SFX mix
- likely noticeable artifacts for BGM at high factors

Risk:
- medium

## 7) Chromium-Inspired In-Tree WSOLA (Reference Architecture)

Pros:
- proven production design
- BSD-licensed reference implementation and tunings
- directly relevant to real-time media playback

Cons:
- Chromium implementation is tightly coupled to its media abstractions
- requires adaptation to SoH’s simpler audio thread and integer PCM path

Notable Chromium design points to reuse:
- `20 ms` OLA window + `30 ms` search interval
- queue-based buffering with explicit capacity growth/underflow handling
- quality guardrail: mute at extreme playback rates
- avoids expensive path near `1.0x` by shortcutting copies

Risk:
- medium

## Practical Read

Most realistic paths:
- custom in-tree WSOLA
- SoundTouch

Best quality path ignoring licensing complexity:
- Rubber Band

Best fit for this codebase constraints (control + no new legal baggage):
- custom in-tree WSOLA

## Sources

- WSOLA reference (Verhelst/ROELANDS): [ISCA entry](https://www.isca-archive.org/eurospeech_1993/verhelst93_eurospeech.html)
- Chromium WSOLA implementation reference: [audio_renderer_algorithm.h](https://chromium.googlesource.com/chromium/src/+/HEAD/media/filters/audio_renderer_algorithm.h), [audio_renderer_algorithm.cc](https://chromium.googlesource.com/chromium/src/+/HEAD/media/filters/audio_renderer_algorithm.cc)
- SoundTouch docs/FAQ: [README](https://codeberg.org/soundtouch/soundtouch/src/branch/master/README.md), [FAQ](https://www.surina.net/soundtouch/faq.html)
- Rubber Band integration/licensing: [integration notes](https://breakfastquay.com/rubberband/integration.html), [license](https://breakfastquay.com/rubberband/licensing.html)
- Signalsmith Stretch: [README](https://github.com/Signalsmith-Audio/signalsmith-stretch/blob/main/README.md)
- Sonic: [README](https://github.com/waywardgeek/sonic)
