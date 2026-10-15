# Skills

Bundled skills live in `skills/<name>/SKILL.md` and are embedded in the binary.

```bash
unrealengine-cli skills list
unrealengine-cli skills install claude-code --local   # .claude/skills
unrealengine-cli skills install codex --local         # .agents/skills
unrealengine-cli skills install cursor --global       # ~/.cursor/skills
unrealengine-cli skills refresh --local               # after updating the CLI
unrealengine-cli skills uninstall cursor --local
unrealengine-cli skills lint --severity error
```

## Bundled skills

| Skill | Use when |
|---|---|
| `unreal-advanced-animation` | Sharing animations between skeletons or rigging characters: IK Rigs, IK Retargeters and batch retargeting, Control Rigs, and Motion Matching (Pose Search) databases |
| `unreal-ai` | Giving characters AI: Blackboards, Behavior Trees, AI controllers and navigation meshes |
| `unreal-animation` | Creating Animation Blueprints, state machines, states, transitions, montages or blend spaces |
| `unreal-asset-management` | Searching, creating, moving, importing or deleting assets and checking references |
| `unreal-audio-physics-pcg` | Building procedural audio with MetaSounds, destructibles with Chaos Geometry Collections, or procedural placement with PCG graphs |
| `unreal-blueprint-edit` | Creating or changing Blueprint graphs: events, function calls, variables, components, compile and save |
| `unreal-cpp-edit` | Changing C++ code in the project and applying it to the running editor |
| `unreal-data-assets` | Defining data: structs, enums, data tables and data assets |
| `unreal-dedicated-server` | Running or testing a game on a dedicated server: starting a server process, joining it from Play In Editor, reading its log, or building and packaging a Server target |
| `unreal-development-loop` | Iterating on a feature: edit, compile, run in PIE, read logs, repeat until it works |
| `unreal-engine-reference` | You need to check an engine C++ or Python API signature before using it |
| `unreal-enhanced-input` | Adding input actions, mapping contexts and key bindings, and wiring them to player Blueprints |
| `unreal-fab-assets` | The project needs ready-made content from Fab (meshes, materials, VFX, sounds, asset packs): find a listing, then download and add it to the project |
| `unreal-game-from-scratch` | Building a playable game or prototype end to end: game mode, player pawn and input, gameplay actors, HUD, level, then play-testing it |
| `unreal-gas` | Building with the Gameplay Ability System: attributes (health, mana), gameplay effects (damage, buffs, damage over time), abilities, gameplay cues and gameplay tags, and checking them in play |
| `unreal-landscape` | Creating terrain, sculpting or painting a landscape |
| `unreal-level-build` | Building or changing a level: creating maps, spawning, moving, attaching or deleting actors |
| `unreal-lighting` | Lighting a level or changing how the project renders: lighting presets (day, night, indoor), static lighting and HLOD builds, and rendering settings such as Lumen, virtual shadow maps, Nanite and anti-aliasing |
| `unreal-localization` | Translating a game or keeping save games working across versions: localization targets, gathering text, PO files, compiling translations, previewing a language, and SaveGame compatibility |
| `unreal-material` | Creating materials, wiring material expressions or setting material instance parameters |
| `unreal-multiplayer` | Making a game multiplayer or checking multiplayer behaviour: replicated actors and variables, RPCs, and play-testing a listen or dedicated server with clients in the editor |
| `unreal-niagara` | Creating Niagara systems, adding emitters and modules, and setting module inputs |
| `unreal-online-sessions` | Adding online play: configuring the online subsystem (Null for local tests, EOS or Steam), checking the settings, and creating, finding and joining sessions |
| `unreal-packaging` | Packaging or cooking the project |
| `unreal-pie-testing` | Verifying behaviour by playing the game in the editor (input, screenshots, actor state) or running Automation tests |
| `unreal-project-bootstrap` | A project has no UnrealCliBridge plugin yet, or the editor must be built or started for the first time |
| `unreal-property-edit` | Reading or changing reflected properties of actors, components, assets or Blueprint defaults |
| `unreal-python-scripting` | No dedicated tool exists and the editor's Python API can do the job |
| `unreal-sequencer-audio` | Building Level Sequences (tracks, keys) or Sound Cues |
| `unreal-umg` | Building UI with Widget Blueprints: widgets, layout, properties, animations, button events and showing the UI in game |
| `unreal-visual-perf-testing` | Checking that a change did not break how the game looks or how fast it runs: screenshot baselines and comparisons, frame time measurement, and Unreal Insights traces |
| `unrealengine-cli-usage` | An agent needs to drive Unreal Editor through unrealengine-cli: connecting, finding tools, calling them, reading errors |

`skills install claude-code --global` writes to `$CLAUDE_CONFIG_DIR/skills` when that variable is set, else
`~/.claude/skills`.

## Skill Contract v1

* Frontmatter: `name` (= directory), `description` (starts with "Use when", at most 300 characters), `version`,
  optional `tools: [...]` (each must exist in the tool catalog).
* Sections: `## When to use`, `## Steps`, `## Verify`, `## On failure` (none empty).
* `unrealengine-cli <command>` references must be real commands; relative links must exist; no machine-specific
  absolute paths; at most 500 lines.
