# COLD ENTRY — Kessler Annex vertical slice (UE 5.8, C++)

Tactical 5v5 attack/defend FPS. Tarkov-style grounded presentation, Siege-style round flow.
Single-player with bots in this build (see **Multiplayer plan**).

## Latest update: forest and facility art

The November 13 forest pass adds scanned conifers, grass, ferns and rocks around
the facility, damp ground with reflective puddles, weathered concrete, factory
windows, rooftop equipment and a warm entrance canopy. Vegetation follows the
terrain; stacked props and suspended fixtures have corrected supports.

See [the update folder](Updates/2026-11-13-Forest-Polish/README.md) for actual
gameplay-renderer screenshots, the change list, verification notes, and the
ColdEntry logo, cover and menu artwork. The reference artwork is a visual target;
the current environment is an iterative art pass, not an exact recreation.

## Getting started from GitHub

This repository contains the C++ project, configuration, imported Unreal assets in `Content/`,
and prepared Poly Haven source assets in `SourceArt/`. Raw forest downloads are reproducible
with `Tools/fetch_forest.py` and excluded from Git. Generated binaries, build files,
caches, logs, and local settings are excluded. This is an Unreal project, not a packaged game executable.

Requirements: Windows, **Unreal Engine 5.8**, and Visual Studio with the **Game development with C++**
workload and a Windows SDK. The batch scripts expect Unreal at `C:\Program Files\Epic Games\UE_5.8`;
adjust that path in the scripts if your installation is elsewhere.

Clone the repository (or download and extract its ZIP), then follow **Run it** below. Imported
characters, weapons, and photoreal materials are already included. `Import_Characters.bat`
restores the Epic template assets from your Unreal installation, and `Photoreal_Setup.bat`
recreates the Poly Haven imports if needed; neither is required for a normal first build.

Build status (November 13, 2026): `Build_ColdEntry.bat` succeeds on UE 5.8 / MSVC. The two errors
that broke the uploaded snapshot (a local named `Mesh` hiding a class member in `CECharacterRig.cpp`,
and an uninitialised `BestLoc` in `CECharacter.cpp`) are fixed. A bot-only round has been run and
photographed with `Capture_Shots.bat`; nobody has hand-played this build yet (see **Verified vs. assumed**).

Asset sources: Epic's Unreal template mannequin, animations and weapons, plus Poly Haven's CC0
scanned textures, props and sky. Every Poly Haven asset ID is listed in `Tools/photoreal_manifest.json`.

## Run it

1. **Build**: double-click `Build_ColdEntry.bat` (writes `Saved\build.log`, ends with `BUILD SUCCEEDED`).
   The editor must be closed while building (or use Live Coding with Ctrl+Alt+F11 for .cpp-only changes).
2. **Open**: double-click `Open_Editor.bat` (or open `ColdEntry.uproject`). The project boots into the
   engine's empty `Entry` map; the game mode builds the whole facility at runtime.
3. **Play**: press **Play** (Alt+P). Click or press **Enter** on the menu to deploy.
   `Shift+F1` frees the mouse; `Esc` stops Play-in-Editor.
4. **Standalone**: `Play_Standalone.bat` (add `-ceauto` for a bot-only soak test that loops matches).
5. **Review the visuals**: `Capture_Shots.bat` runs a bot-only round, saves a fixed set of views to
   `Saved\Shots\*.png` (characters, first person, yards, each main room) and quits. Add `-celineup`
   to also photograph one of every scanned prop.

No additional asset import is needed for the included snapshot. The facility is assembled in code
and dressed with the imported scans; audio is synthesized.

### Changing the scanned assets

`Tools/photoreal_manifest.json` is the single list: which Poly Haven scan each game surface uses
(with tiling, tint, grime, anti-tiling), which sky, and which props. Edit it, close the editor and run
`Photoreal_Setup.bat`: it downloads what is missing into `SourceArt/` and rebuilds everything under
`/Game/Photoreal` (surface instances, the sky dome material, one PBR material per prop). Assets the
manifest no longer lists are removed.

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
| `CEMap` | Kessler Annex: walls with openings, stairs, lights, sky dome/haze/exposure, callout zones, nav graph, spawns, holds. `Dress()` swaps a graybox block for scanned meshes while keeping the block as hidden collision; `Prop`/`PropFacing`/`WallProp` place set dressing |
| `CEMapEnvironment` | Forest terrain, instanced trees/understory, grounding, perimeter fencing, facade detail and roof fixtures |
| `CECharacter` | Shared pawn for player & bots: movement, crouch/lean/ADS, weapons, recoil patterns, hitscan + penetration, hitboxes, actions, box-built fallback body and viewmodels |
| `CECharacterRig` | Fallback character model: template mannequin driven by the template rifle/pistol animation blueprints, crouch/lean correction, first-person arm IK, team tints, ragdoll |
| `CECharacterSwat` | The SWAT operator: imported clips sampled and blended in code (no animation blueprint), crouch/lean/aim pitch, first-person arm IK, team tints, death clip |
| `CEPlayerController` | Raw mouse look, input, feeds (drone/cameras), spectating, settings |
| `CEBotController` | Bot brain: vision cone + LOS + smoke + reaction time, hearing, team intel, combat, nav A*, breaching, setup tasks |
| `CEGameMode` | Round state machine, spawning, side swap, win rules, intel, gadget effects, plant/defuse, the `-ceshots` photo tour |
| `CEEntities` | Doors, breach walls (reinforceable), glass, barricades, the interceptor, FX (dust/sparks/tracers/decals/debris) |
| `CEGadgets` | Flash/smoke grenades, smoke volume, breach charge, security camera, proximity alarm, drone |
| `CESound` | Procedural synthesized audio, occlusion + distance LPF attenuation, per-room reverb |
| `CEAssets` | Surface library: scanned material instances from `/Game/Photoreal` with procedural fallbacks (also used on moving parts), weapon finish, emissives, decals |
| `CEHUD` | Canvas HUD: timer/score/alive pips, vitals, ammo/gadgets, crosshair, hit/damage markers, kill feed, death card, round summary |

## Verified vs. assumed

Verified before the forest update on this PC (UE 5.8, standalone `-game`, November 13, 2026):
- Project compiles. The facility builds at runtime (32 entities, 135 nav nodes, 46 lights) with 15 scanned
  surfaces, the scanned sky and 27 scanned props loading; the template mannequin, animation blueprints
  and weapons load and animate on the characters.
- A bot-only round starts and holds in prep while `Capture_Shots.bat` photographs it; the game log shows no errors.

Verified in an earlier session (Play-in-Editor), not re-tested since the art pass:
- Full loop: menu → prep → action → plant → round end → side swap → next round; kill feed and logs.
- Bots: defenders reinforce/barricade/place alarms in prep and hold; attackers path in, fight, and plant.

Not yet verified (needs a hands-on playtest): the feel of everything with the new art in place,
first-person arm IK across all weapons and gadgets, ragdolls, breach-charge flow end to end, smoke
occlusion of bots, flashbang blinding, drone driving, alarm triggers, audio balance, mouse sensitivity.
Dressed scan props retain hidden collision blocks; the forest update adds solid shipping containers.
Small set dressing
(extinguishers, tyres, chairs, bins) has no collision at all, so you can walk through it.

## Art status (honest)

**Environment: forest art pass, not final.** The facility now has scanned weathered concrete,
industrial window mullions, runoff decals, downpipes, roof rails/HVAC, a supported entrance
canopy and detailed corrugated containers. Scanned conifers and understory surround the yard;
the ground blends soil, moss, mud and asphalt with wet reflections. Lighting uses an overcast
HDRI, Lumen and a warm entry light. Furniture and utility equipment use scanned meshes.
The building still has a procedural box-based shell, slab doors and simple glass. Some machines
remain placeholders. Bespoke architecture, richer terrain transitions and further lighting work
are needed to reach the reference image. See the latest update folder for current screenshots.

**Characters: real model, minimal animation.** Both teams use a SWAT operator (Mixamo skeleton, its
own uniform, vest and helmet), tinted olive for attackers and navy for defenders. It was imported with
`Import_Swat.bat` from `SourceArt/Characters/Swat` and is animated with Mixamo's Basic Shooter Pack
(`shooter_pack/`), blended in code by `CECharacterSwat.cpp`: aiming idle, walk and run loops in four
directions sharing one gait phase, and upper-body overlays for reload, recoil, hit reaction and grenade
throw. Still missing: crouch clips (crouching is pelvis drop + leg IK over the standing clips), a pistol
or gadget stance (the rifle pose is used for everything), turn-in-place, and a ragdoll (the mesh has no
physics asset, so deaths always play the one dying clip). Set **Models > Swat Characters** off in the
tuning to get the template mannequin back.

**Weapons: placeholder.** Epic's template rifle (re-skinned dark) and pistol. They are generic
sci-fi shapes, not the grounded firearms the brief calls for, and the SMG is the rifle scaled down.

**Audio and VFX: placeholder.** Synthesized sounds; muzzle flash, impacts, smoke and debris are
simple shapes.

## What I need from you to reach the visual target

None of these can be downloaded by a script; they need your Epic/Fab account or a purchase:
- **Character animation**: the remaining gaps for the SWAT skeleton (Mixamo downloads work): crouch
  idle and crouch walk with a rifle, pistol idle/walk, a few more death clips. Drop the FBX files into
  `SourceArt/Characters/Swat/shooter_pack` and they can be wired into the blend. A second outfit would
  let the teams differ by more than colour.
- **Weapons**: a rigged carbine, a compact SMG and a 9 mm pistol with first-person animation sets
  (idle, fire, reload, reload-empty, equip). Fab has several; I can wire them into `CECharacterRig`.
- **Environment kit**: Quixel Megascans or similar Fab packs (check current licensing) for a shipping
  container, forklift, industrial machinery, pipes/ducts, doors and decals. Keep the metric grid
  (walls 0.2-0.3 m, doors 1.1 x 2.15 m) and they drop into the same `Dress()` calls.
- **Audio**: recorded weapon sets (close/far/indoor tails), Foley footsteps per surface, door, glass
  and breach recordings to replace the `CESound` synthesis.
- **VFX**: Niagara muzzle flash, per-surface impacts, smoke and breach systems.

## Multiplayer plan (not implemented)

All authority currently lives in `ACEGameMode`. To network it: move round state to a replicated
`AGameState`, per-player loadout/health/intel to `APlayerState` and replicated pawn properties,
make `FCEInput` the client-to-server move/fire payload (server-authoritative hitscan with lag
compensation via rewound hitbox history), replicate entity state (door open/destroyed, wall
reinforced/breached) with `ReplicatedUsing`, and drive HUD from replicated state rather than
the game mode. Bots stay server-side.
