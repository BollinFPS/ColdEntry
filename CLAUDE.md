Act as a senior FPS game designer, Unreal Engine developer, technical artist, and combat systems engineer. Help me build a playable, original tactical FPS with this clear creative direction:

VISUALS AND ATMOSPHERE: Escape from Tarkov.
CORE GAMEPLAY AND MATCH FLOW: Rainbow Six Siege.

I want Tarkov’s believable environments, detailed weapons, grounded equipment, and intense atmosphere combined with Siege’s responsive controls, close-quarters tactics, information gathering, environmental destruction, and objective-based rounds.

Create an original game with its own identity. Do not copy either game’s maps, characters, branding, UI, or proprietary assets.

CORE EXPERIENCE

The player should feel like a trained professional clearing a dangerous, believable location. Combat should be tense and lethal, but controls should remain responsive and readable.

The central gameplay loop is:
Gather information → coordinate an approach → breach or defend → engage in short, decisive firefights → secure the objective.

Prioritize tactical depth over excessive simulation. Realism should strengthen immersion without making basic actions frustrating.

VISUAL DIRECTION

Aim for photorealism with:
- Weathered industrial and urban interiors.
- Physically believable materials: chipped paint, concrete, oxidized metal, dirty glass, worn fabric, and scuffed weapon finishes.
- Detailed firearms with convincing proportions, attachments, animations, and mechanical operation.
- Grounded tactical clothing and equipment.
- Natural lighting, restrained colors, convincing shadows, and atmospheric effects used sparingly.
- Environmental storytelling through believable room layouts, maintenance equipment, abandoned belongings, and signs of occupation.

Use Tarkov’s material detail and atmosphere as the visual reference, but preserve competitive visibility. Avoid crushed blacks, excessive fog, aggressive motion blur, or visual noise that makes opponents difficult to identify.

Do not substitute a muddy filter for realism.

MOVEMENT AND WEAPON HANDLING

Make the controls feel closer to Siege:
- Responsive mouse input and predictable acceleration.
- Walking, sprinting, crouching, aiming, and left/right leaning.
- Controlled movement while aiming.
- Clear transitions between sprinting and firing.
- Limited camera shake and restrained head bob.
- No slide cancelling, double jumping, or exaggerated movement.

Weapons should feel physical and powerful through animation, recoil, sound, muzzle effects, and impacts—not through sluggish input.

Use distinct, learnable recoil patterns with some natural variation. Avoid excessive random recoil and unnecessary aim disruption.

COMBAT AND DAMAGE

Build lethal, understandable combat:
- Short time to kill.
- Strong rewards for accurate shots, good positioning, and coordinated pushes.
- Meaningful headshots.
- Simple armor and penetration rules that are clearly communicated.
- Surface penetration based on material and thickness.
- Limited healing with a meaningful time cost.

Borrow the tension of Tarkov, but avoid complicated ammunition spreadsheets, long medical routines, gear-based power gaps, and bullet-sponge enemies.

Use hitscan or simulated projectiles according to the needs of the engagement distances. Do not add expensive simulation unless it visibly improves the experience.

MATCH STRUCTURE

Design around 5v5 attackers versus defenders with:
- A short preparation phase.
- A timed action phase.
- One life per round.
- An objective such as securing sensitive equipment or planting/disabling a device.
- Alternating attack and defense.
- Useful spectator tools after death.

For the initial playable build, use competent bots to fill the teams if multiplayer is not already available. Clearly distinguish implemented networking from future multiplayer plans.

Do not make extraction, inventory management, or collecting loot the main loop. The Tarkov influence should come primarily from presentation, weapon authenticity, sound, and tension.

TACTICAL SYSTEMS

Include a focused selection of grounded tools:
- Reconnaissance drones or inspection cameras.
- Breaching charges.
- Flashbangs and smoke grenades.
- Barricades and limited reinforcement.
- Simple surveillance or detection equipment.

Create distinct equipment roles without supernatural abilities.

Make information gathering useful but imperfect. Sound, camera placement, sightlines, and gadget counters should create tactical choices.

DESTRUCTION

Implement selective, purposeful destruction:
- Breachable doors and designated wall sections.
- Destructible barricades and breakable glass.
- Limited bullet penetration through appropriate surfaces.
- Breaches that create new entry routes or sightlines.

Use a controlled, reliable destruction system. Fully simulated destruction is unnecessary for the first build.

AUDIO

Treat audio as a major gameplay system:
- Distinct footsteps for different surfaces.
- Understandable directional cues.
- Room-dependent gunshot character and reverberation.
- Audible reloads, equipment use, and breaches.
- Basic sound obstruction between rooms.

Balance authenticity with useful information. Players should be able to learn and interpret the soundscape.

FIRST PLAYABLE VERTICAL SLICE

Build one small, polished map: a weathered industrial security facility with two floors, two staircases, a loading area, offices, service corridors, and one objective room.

Provide:
- Multiple viable entrances and approaches.
- Clear navigational landmarks.
- Deliberate cover and defensible positions.
- A few meaningful breach locations.
- Balanced sightlines without excessive clutter.

Initial content:
- One assault rifle.
- One compact SMG.
- One pistol.
- One attacker equipment kit.
- One defender equipment kit.
- Functional bots.
- A complete preparation, combat, win/loss, and restart loop.
- A restrained HUD showing only useful combat and objective information.

IMPLEMENTATION APPROACH

First inspect the existing project, available tools, and runtime.

If starting from scratch in a suitable desktop development environment, prefer Unreal Engine 5. If the environment cannot support that, explain the limitation and select a feasible implementation path without pretending a browser prototype can deliver finished AAA visuals.

Build in this order:
1. Responsive movement, aiming, and firing.
2. A graybox map and complete objective round.
3. Bots and tactical equipment.
4. Selective destruction and penetration.
5. Final environment art, lighting, animation, audio, and optimization.

Keep the game playable after each stage.

Use available, appropriately licensed assets. Identify any external assets I need to provide. Use clearly labelled placeholders where necessary, and do not claim placeholder art is the finished visual target.

Keep code modular, expose important tuning values, and provide practical launch instructions. Verify changes with the tools actually available, and distinguish tested functionality from assumptions.

QUALITY BAR

Judge the result by these questions:
- Does moving and shooting feel immediately responsive?
- Does the environment look physically believable?
- Do positioning, information, and teamwork determine outcomes?
- Can players understand why they won or lost a fight?
- Does destruction meaningfully change tactical options?
- Is the complete round loop enjoyable before more content is added?

Favor one polished, playable encounter over a large collection of unfinished systems.

Start with a concise concept and implementation plan, then begin building the first playable milestone. Do not stop at a design document. If you cannot directly edit or run the project, state that clearly and provide concrete project files, code, and setup steps instead.