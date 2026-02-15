# Game-Speed Fast-Forward Plan (2x-8x)

## Verdict

Possible.

Not "a few surgical edits".

Also not "total rearchitecture".

My read: **medium-high complexity**.

Roughly **2-5 focused days** for a robust first version, depending on how strict you want audio behavior to be.

## Why This Is Not Trivial

- Main loop currently advances one full game frame per `RunFrame()` call: `soh/src/code/graph.c:437`, `soh/src/code/graph.c:489`.
- `GameState_Update()` is update + draw together; not cleanly split: `soh/src/code/game.c:258`, `soh/src/code/game.c:270`, `soh/src/code/game.c:342`.
- Render pacing/interpolation is controlled later in `Graph_ProcessGfxCommands()`: `soh/soh/OTRGlobals.cpp:1726`, `soh/soh/OTRGlobals.cpp:1734`, `soh/soh/OTRGlobals.cpp:1753`.
- Audio generation is tied to frame updates via `R_UPDATE_RATE`: `soh/soh/OTRGlobals.cpp:1030`, `soh/soh/OTRGlobals.cpp:1038`.

So yes, there are central choke points.

But simulation, render, and audio are currently coordinated as one pipeline.

## Why This Is Also Not A Total Rearchitecture

- There is a single frame pump (`RunFrame`) and a single present/pacing path (`Graph_ProcessGfxCommands`).
- You can layer fast-forward control there, without rewriting actor logic, cutscene code, or per-system timers.
- Existing speed-modifier hotkey plumbing already exists and can be reused for UX and bindings: `soh/src/overlays/actors/ovl_player_actor/z_player.c:12318`, `soh/soh/Enhancements/controls/SohInputEditorWindow.cpp:1606`.

## Recommendation

Do **global simulation multiplier** at the frame-loop level.

Do **not** repurpose `WalkModifier` math.

Do **not** rely on changing `R_UPDATE_RATE` for this.

Reason: `R_UPDATE_RATE` is compatibility scaling and mode control, not a clean global timescale knob.

## Implementation Plan

## 1) Add New Game-Speed CVars [DONE]

- Add setting CVars for:
  - `gSettings.GameSpeed.Enabled` (bool)
  - `gSettings.GameSpeed.Base` (float, 1.0-8.0)
  - `gSettings.GameSpeed.Mod1` (float, 0.125-8.0 or directly 1.0-8.0)
  - `gSettings.GameSpeed.Mod2` (float, same)
  - `gSettings.GameSpeed.Toggle` (reuse toggle behavior pattern)
  - Optional: `gSettings.GameSpeed.MuteAudioWhenFast` (bool)
- Add migrators only if you rename/replace existing keys later.

## 2) Add UI For Global Game Speed [DONE]

- Add slider in Settings menu (clear wording: affects logic/cutscenes/physics).
- Add controls in modifier section near existing speed modifiers:
  - "Apply modifiers to game speed"
  - Mod1/Mod2 mappings for game speed.
- Candidate files:
  - `soh/soh/SohGui/SohMenuSettings.cpp`
  - `soh/soh/Enhancements/controls/SohInputEditorWindow.cpp`

## 3) Reuse Existing Modifier Hotkey Path [DONE]

- Extend the existing toggle/hold logic near:
  - `soh/src/overlays/actors/ovl_player_actor/z_player.c:12318`
- Compute an **effective game-speed multiplier** each frame:
  - default from `Base`
  - overridden/multiplied by Mod1/Mod2 behavior
- Store the effective multiplier in a global helper accessor used by the frame loop.

## 4) Frame-Loop Fast-Forward Core [DONE]

- Implement accumulator-based variable stepping in frame loop.
- Concept:
  - `accumulator += effectiveGameSpeed`
  - `steps = floor(accumulator)`
  - `accumulator -= steps`
  - clamp `steps` to safe max per outer iteration (e.g. 16) to avoid runaway.
- For each step, run a normal simulation frame path.
- Present at selected render FPS, independently.
- Main touchpoints:
  - `soh/src/code/graph.c`
  - `soh/soh/OTRGlobals.cpp`

## 5) Render Independence [DONE]

- Keep **display fps** and **simulation speed** as separate variables.
- In `soh/soh/OTRGlobals.cpp:1734` onward:
  - keep `target_fps` sourced only from interpolation settings (`GetInterpolationFPS()`).
  - do not multiply `fps` by fast-forward multiplier.
  - keep `wnd->SetTargetFps(fps)` tied only to display fps (`soh/soh/OTRGlobals.cpp:1765`).
- Add `simStepsThisHostFrame` plumbing:
  - update `Graph_ProcessGfxCommands` signature in `soh/soh/OTRGlobals.h:97`.
  - pass value from `RunFrame()` in `soh/src/code/graph.c:496`.
- Execute multiple simulation steps, but submit/present once:
  - add `submitTask` flag to `Graph_Update(...)` in `soh/src/code/graph.c:275`.
  - guard task submit block (`Graph_TaskSet00`, pool/fb increments) at `soh/src/code/graph.c:386-390`.
  - run N-1 steps with `submitTask=false`, final step with `submitTask=true`.
- Interpolation behavior when fast-forwarding:
  - if `simStepsThisHostFrame > 1`, prefer one final matrix set (no extra interpolation frames) in `Graph_ProcessGfxCommands`.
  - avoids generating fake in-between frames for skipped logic states.
- Input stability while stepping:
  - poll pad once per host frame (`GameState_ReqPadData` currently at `soh/src/code/graph.c:296`).
  - for extra internal sim steps, clear `press` edge bits so taps do not replay N times.

## 6) Audio Strategy (Important Tradeoff)

- Fast-forward audio at 2x-8x is the hardest part to make pleasant.
- Practical options:
  - MVP: mute audio while `gameSpeed > 1.0` (most reliable).
  - Next: allow chipmunk audio by pushing more audio per wall-time.
  - Avoid promising time-stretched high-quality audio in first pass.
- Relevant code:
  - `soh/soh/OTRGlobals.cpp:1011`
  - `soh/soh/OTRGlobals.cpp:1726`

## 7) Safety Gates

- Disable/limit extreme stepping in known sensitive states if needed:
  - pause transitions
  - save/load transitions
  - debug/fault states
- Keep max step clamp.
- Add fallback to 1x on unstable state detection.

## 8) Testing Matrix

- Overworld movement + camera + combat at 1x/2x/3x/8x.
- Swimming, climbing, recoil, knockback.
- Cutscenes (intro, short scripted scenes, scene transitions).
- Pause menu open/close, text boxes, shop interactions.
- Scene loads, room transitions, death/respawn.
- Save states while fast-forward active.
- Render settings:
  - Interpolation FPS = 20, 60, 120+
  - Vsync on/off
- Verify: selected render FPS unchanged while game logic speed changes.

## Expected Difficulty

Closest label: **"moderate subsystem work"**.

Not a full engine rewrite.

But definitely beyond a small patch, mostly because update/render/audio are coupled in the current loop.
