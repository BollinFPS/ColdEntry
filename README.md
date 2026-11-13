# COLD ENTRY — Kessler Annex vertical slice (UE 5.8, C++)

Tactical 5v5 attack/defend FPS. Tarkov-style grounded presentation, Siege-style round flow.
Single-player with bots in this build (see **Multiplayer plan**).

## Getting started from GitHub

This repository contains the C++ project, configuration, imported Unreal assets in `Content/`,
and the downloaded Poly Haven source assets in `SourceArt/`. Generated binaries, build files,
caches, logs, and local settings are excluded. This is an Unreal project, not a packaged game executable.

Requirements: Windows, **Unreal Engine 5.8**, and Visual Studio with the **Game development with C++**
workload and a Windows SDK. The batch scripts expect Unreal at `C:\Program Files\Epic Games\UE_5.8`;
adjust that path in the scripts if your installation is elsewhere.

Clone the repository (or download and extract its ZIP), then follow **Run it** below. Imported
characters, weapons, and photoreal materials are already included. `Import_Characters.bat`
restores the Epic template assets from your Unreal installation, and `Photoreal_Setup.bat`
recreates the Poly Haven imports if needed; neither is required for a normal first build.

The latest available local build log (November 13, 2026) reports a failed build:
`CECharacterRig.cpp:100` declares `Mesh`, which hides a class member (C4458).
It also reports a potentially uninitialized `BestLoc` in `CECharacter.cpp:798` (C4701).
These gameplay files are preserved as supplied. The original development and playtest notes
below describe earlier work; this uploaded snapshot has not been rebuilt or playtested.

Asset sources: the import scripts identify Epic's Unreal template characters, animations,
and weapons, plus Poly Haven's CC0 textures, models, and HDRI. The Poly Haven asset IDs
are recorded in `Tools/photoreal_manifest.json`.

## Run it

1. **Build**: double-click `Build_ColdEntry.bat` (writes `Saved\build.log`, ends with `BUILD SUCCEEDED`).
   The editor must be closed while building (or use Live Coding with Ctrl+Alt+F11 for .cpp-only changes).
2. **Open**: double-click `Open_Editor.bat` (or open `ColdEntry.uproject`). The project boots into the
   engine's empty `Entry` map; the game mode builds the whole facility at runtime.
3. **Play**: press **Play** (Alt+P). Click or press **Enter** on the menu to deploy.
   `Shift+F1` frees the mouse; `Esc` stops Play-in-Editor.
4. **Standalone**: `Play_Standalone.bat` (add `-ceauto` for a bot-only soak test that loops matches).

No additional asset import is needed for the included snapshot. The facility is assembled in code,
with imported assets included for characters, weapons, and photoreal surfaces; audio is synthesized.

## Controls

| Action | Key |
|---|---|
| Move / sprint | WASD / Left Shift |
| Crouch | C (toggle) or Left Ctrl (hold) |
| Lean | Q / E (toggle) |
| Fire / aim | LMB / RMB |
| Reload / weapons | R / 1 primary, 2 pistol, wheel swap |
| Gadgets | 3 / 4 / 5, then LMB (breach charge = hold LMB on the target) |
| Detonate charges | G |
| Interact | F (doors), hold F (reinforce, barricade, plant, disable) |
| Medkit | hold H |
| Drone / cameras | V (arrow keys switch feeds, LMB marks an enemy, V exits) |
| Prep phase | B change attack spawn, N swap primary (R4 / SMG-9) |
| Scores / settings / nav debug | Tab / P / F9 |

## Rules (communicated in-game)

- 5v5, one life, 30 s prep + 150 s action, first to 4 rounds, sides swap every round.
- Attackers plant the interceptor anywhere in **Comms** (hold F, 5 s). It runs for 40 s; defenders hold F 6 s to disable.
- **Headshots kill.** Plate carrier: torso takes 80 %. Limbs take 75 %.
- Penetration: drywall, wood, glass, barricades and crates can be shot through with damage loss; concrete,
  block and reinforced steel stop everything. Kill feed marks `HS` and `WALL`; the death card lists every hit
  (zone, damage, weapon, range, what it went through).
- Defenders have 6 shared reinforcements, 2 barricades, 2 alarms, 1 camera each, plus 4 pre-installed cameras.
- Attackers have 2 breach charges, 2 flashbangs, 1 smoke, 1 drone each.

## Code map (`Source/ColdEntry`)

| File | What it owns |
|---|---|
| `CETuning` | **Every tuning value** — Project Settings ▸ Game ▸ Cold Entry Tuning (saved to `Config/DefaultGame.ini`) |
| `CEMap` | Kessler Annex: walls with openings, stairs, props, lights, sky/exposure, callout zones, nav graph, spawns, holds |
| `CECharacter` | Shared pawn for player & bots: movement, crouch/lean/ADS, weapons, recoil patterns, hitscan + penetration, hitboxes, actions, viewmodels, body |
| `CEPlayerController` | Raw mouse look, input, feeds (drone/cameras), spectating, settings |
| `CEBotController` | Bot brain: vision cone + LOS + smoke + reaction time, hearing, team intel, combat, nav A*, breaching, setup tasks |
| `CEGameMode` | Round state machine, spawning, side swap, win rules, intel, gadget effects, plant/defuse |
| `CEEntities` | Doors, breach walls (reinforceable), glass, barricades, the interceptor, FX (dust/sparks/tracers/decals/debris) |
| `CEGadgets` | Flash/smoke grenades, smoke volume, breach charge, security camera, proximity alarm, drone |
| `CESound` | Procedural synthesized audio, occlusion + distance LPF attenuation, per-room reverb |
| `CEAssets` | Runtime procedural materials (world-space noise mottling), emissives, decals |
| `CEHUD` | Canvas HUD: timer/score/alive pips, vitals, ammo/gadgets, crosshair, hit/damage markers, kill feed, death card, round summary |

## Verified vs. assumed

Verified on this PC (UE 5.8, MSVC 14.50, Play-in-Editor):
- Project compiles; facility builds at runtime (32 entities, 135 nav nodes, 46 lights).
- Full loop: menu → prep → action → plant → round end → side swap → next round; kill feed and logs.
- Bots: defenders reinforce/barricade/place alarms in prep and hold; attackers path in, fight, and plant.
- Lumen lighting with shadows from practical lights; security camera feeds render.

Not yet verified (implemented, needs hands-on playtest): breach-charge flow end to end, smoke
occlusion of bots, flashbang blinding, drone driving, alarm triggers, every audio cue's balance,
mouse sensitivity calibration on your hardware.

## Placeholder status (honest)

Everything visual is **graybox placeholder**: engine cubes + procedural noise materials, box-built
characters and weapons, synthesized audio. It reads as physical surfaces under real lighting, but it
is **not** the Tarkov-grade visual target.

## Path to the visual target (stage 5) — assets you'll need

- **Environment**: modular industrial kit (concrete/block walls, doors, frames, windows, stairs,
  catwalks, pipes, HVAC, lockers, desks) with scanned PBR materials. Candidates: Quixel Megascans
  (free with UE), Fab industrial kits. Keep the same metric grid (walls 0.2–0.3 m, doors 1.1 × 2.15 m)
  so `CEMap` coordinates can drive the placement of real meshes.
- **Weapons**: three rigged first-person weapon models (carbine, compact SMG, 9 mm pistol) with
  first-person arm rigs and animation sets (idle/sway/fire/reload/reload-empty/equip/sprint).
- **Characters**: two team outfits (contractor attackers, facility security defenders) on a shared
  skeleton, plus locomotion/lean/crouch blendspaces.
- **Audio**: recorded weapon sets (close/mid/far/indoor tails), Foley footsteps per surface, door,
  glass and breach recordings; replace `CESound` buffers with SoundCues/MetaSounds, keep the routing.
- **VFX**: Niagara muzzle flash, impacts per surface, smoke and breach systems.

## Multiplayer plan (not implemented)

All authority currently lives in `ACEGameMode`. To network it: move round state to a replicated
`AGameState`, per-player loadout/health/intel to `APlayerState` and replicated pawn properties,
make `FCEInput` the client-to-server move/fire payload (server-authoritative hitscan with lag
compensation via rewound hitbox history), replicate entity state (door open/destroyed, wall
reinforced/breached) with `ReplicatedUsing`, and drive HUD from replicated state rather than
the game mode. Bots stay server-side.
