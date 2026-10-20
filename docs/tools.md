# Tool catalog

185 tools. Generated from tools/catalog.json by scripts/gen-docs.sh.

Call any tool with `unrealengine-cli tool call <name> --args '<json>'`; see its schema with `unrealengine-cli tool describe <name>`.

## ai

| Tool | Changes | Summary |
|---|---|---|
| `behavior_tree_create` | yes | Create a Behavior Tree using a Blackboard; build it with bt_add_node (parent "root" first). |
| `blackboard_add_key` | yes | Add a key to a Blackboard (type: Bool/Int/Float/Vector/Rotator/Object/Class/Name/String/Enum; baseClass for Object/Class). |
| `blackboard_create` | yes | Create a Blackboard with keys [{name, type: Bool/Int/Float/Vector/Rotator/Object/Class/Name/String/Enum, baseClass (Object/Class keys, e.g. Actor)}]. |
| `bt_add_node` | yes | Add to a Behavior Tree: kind composite (Selector, Sequence, SimpleParallel) or task (MoveTo, Wait, RunBehavior, PlaySound, a Blueprint task path...) as the last child of parent ("root" or a node id), or kind decorator (Blackboard, Cooldown, Loop, TimeLimit...) / service (DefaultFocus, RunEQS, a Blueprint service...) attached to parent. properties: {Name: value}; Blackboard key selectors take a key name; value-or-key properties take a value or "key:<KeyName>". |
| `bt_get` |  | A Behavior Tree as a nested tree: node ids, classes, names, attached decorators/services, children in execution order. |
| `nav_bounds_add` | yes | Add a Nav Mesh Bounds Volume (location, extent = half size in cm) to the level; navigation rebuilds in the background (check with nav_path). |
| `nav_build` |  | Start rebuilding navigation for the open level (asynchronous: poll until building=false, or query nav_path). |
| `nav_path` |  | Find a navigation path between two points [x,y,z] in the open level (or the play world when playing): valid, partial, length and points. |

## anim

| Tool | Changes | Summary |
|---|---|---|
| `anim_ikrig_create` | yes | Create an IK Rig for a skeletal mesh. auto=true (default) adds the auto-generated retarget chains and a full-body IK solver. Returns the retarget root and chains (name, start, end). |
| `anim_retarget` | yes | Retarget animation assets (sequences, montages, blend spaces) with an IK Retargeter into new assets for the target mesh: saved in folder (default next to the originals) with suffix (default _Retargeted). |
| `anim_retargeter_create` | yes | Create an IK Retargeter from a source IK Rig to a target IK Rig and map their chains automatically (fuzzy name matching). Returns the chain mapping. |
| `animbp_create` | yes | Create an Animation Blueprint for a skeleton. |
| `blendspace_create` | yes | Create a 2D Blend Space for a skeleton. |
| `controlrig_create` | yes | Create a Control Rig Blueprint for a skeletal mesh with the mesh's bones imported into its hierarchy and the mesh as preview. Add controls and the Forward Solve graph in the Control Rig editor or with bp tools. |
| `montage_create` | yes | Create an Anim Montage from an Anim Sequence. |
| `posesearch_create` | yes | Create Motion Matching assets: <path>_Schema (Pose Search Schema for the skeleton) and the Pose Search Database at path holding the animations. Needs the PoseSearch plugin (posesearch.plugin_disabled otherwise). Tune the schema's channels in its editor. |
| `sequence_player_add` | yes | Play an animation sequence in an existing state (wired to the state result). |
| `state_add` | yes | Add a state to a state machine (first state becomes the entry); optional sequence plays in it. |
| `statemachine_add` | yes | Add a state machine to an Animation Blueprint's AnimGraph and wire it to the output pose. |
| `transition_add` | yes | Add a transition between two states (automatic=true uses the time-remaining rule). |

## asset

| Tool | Changes | Summary |
|---|---|---|
| `asset_create` | yes | Create an asset of a class (Material, MaterialInstanceConstant, Blueprint, WidgetBlueprint, NiagaraSystem, SoundCue, LevelSequence, InputAction, InputMappingContext, CurveFloat, World, or any UObject class creatable without a factory). |
| `asset_delete` | yes | Delete an asset. Refused while other assets reference it unless force=true. |
| `asset_dependencies` |  | Packages this asset depends on. |
| `asset_duplicate` | yes | Duplicate an asset to a new path. |
| `asset_import` | yes | Import files (fbx, png, wav, ...) into a content folder. |
| `asset_list` |  | List assets in a content folder. |
| `asset_references` |  | Packages that reference this asset. |
| `asset_rename` | yes | Rename or move an asset (references are fixed up). |
| `asset_save` | yes | Save assets (one path, or every dirty asset under a folder). |
| `asset_search` |  | Search assets by name substring, optionally by class and path prefix. |

## audio

| Tool | Changes | Summary |
|---|---|---|
| `metasound_add_node` | yes | Add a node to a MetaSound by class name Namespace.Name[.Variant], e.g. UE.Sine.Audio, UE.ADSR Envelope.Audio, UE.Multiply.Audio. Returns its node id and pins. |
| `metasound_connect` | yes | Connect an output to an input. from/to are <node id>:<pin name>, or graph:<name> for the MetaSound's own inputs (from) and outputs (to), e.g. from 5F3A...:Audio to graph:UE.OutputFormat.Mono.Audio:0. |
| `metasound_create` | yes | Create a MetaSound Source (format mono or stereo, oneShot default true). Returns its graph inputs (UE.Source.OnPlay) and outputs (audio channels, OnFinished) to connect nodes to with metasound_connect. |
| `metasound_set_input` | yes | Set the default value of a node input: number (float or int), boolean or string. |
| `soundcue_create` | yes | Create a Sound Cue (optionally wrapping a Sound Wave). |

## blueprint

| Tool | Changes | Summary |
|---|---|---|
| `bp_add_component` | yes | Add a component to the Blueprint's construction script (optionally under a parent component). |
| `bp_add_event_dispatcher` | yes | Add an event dispatcher with optional inputs [{name,type}]; then bp_compile and use CallDispatcher / BindDispatcher nodes. |
| `bp_add_function` | yes | Add a function graph with optional inputs/outputs [{name,type}]. |
| `bp_add_graph` | yes | Add an event graph page (like My Blueprint > Graphs > +), e.g. InterfaceGraph; then place nodes there with bp_add_node graph=<name>. |
| `bp_add_node` | yes | Add a node. type and memberName: Event (overridable event) / CustomEvent (name) / CallFunction (function; memberParent = class) / VariableGet/VariableSet (variable or component; memberParent = class for another object's variable) / Branch / Sequence / Self / Cast (class) / Macro (ForEachLoop, DoOnce, Gate...) / InputAction (Input Action asset) / ComponentEvent (component variable + delegate, e.g. OnComponentBeginOverlap) / SpawnActor (actor class) / CreateWidget (widget Blueprint) / CallDispatcher / BindDispatcher (event dispatcher; memberParent = class for another object's) / MakeStruct / BreakStruct (struct, e.g. Vector, HitResult) / SwitchEnum (enum) / Timeline (name from bp_add_timeline) / Node (any graph node class, e.g. K2Node_SwitchInteger, K2Node_Select, K2Node_MakeArray, K2Node_SwitchString). |
| `bp_add_timeline` | yes | Add a timeline with float tracks [{name, keys:[[time,value],...]}] and place its node in the event graph (outputs: Update, Finished and one pin per track). |
| `bp_add_variable` | yes | Add a member variable. type: bool/int/int64/byte/float/double/string/name/text/vector/rotator/transform/color/<class>/<struct>, optional [], or a container map<K,V> / set<T> (e.g. map<string,int>). |
| `bp_compile` |  | Compile and return status, error/warning counts and messages. |
| `bp_connect` | yes | Connect pins: from/to are '<node>.<pin>' (node = N<k> or guid). |
| `bp_create` | yes | Create a Blueprint asset (parent: class name, /Script path or Blueprint path). kind=interface creates a Blueprint Interface (add its functions with bp_add_function). |
| `bp_disconnect` | yes | Break one link (from+to) or every link of a pin (from only). |
| `bp_get` |  | Variables, graphs, components and interfaces of a Blueprint. |
| `bp_graph_get` |  | Nodes, pins and links of one graph. Node ids N<k> are stable until nodes are removed. |
| `bp_implement_interface` | yes | Add a Blueprint interface (class name or path) to the Blueprint. |
| `bp_remove_node` | yes | Remove a node by N<k> id or guid. |
| `bp_remove_variable` | yes | Remove a member variable and its nodes. |
| `bp_reparent` | yes | Change a Blueprint's parent class and recompile. |
| `bp_save` | yes | Save the Blueprint package. Refused when it does not compile unless force=true. |
| `bp_search_nodes` |  | Find callable functions / overridable events / macros by name for bp_add_node. |
| `bp_set_pin` | yes | Set an input pin's literal default value. |
| `bp_set_replication` | yes | Replicate a member variable as the Details panel does: mode replicated / notify (adds an OnRep_<Var> function graph, called on clients when the value arrives) / none; condition: None, InitialOnly, OwnerOnly, SkipOwner, SimulatedOnly, AutonomousOnly, SimulatedOrPhysics, InitialOrOwner, Custom, ReplayOrOwner, ReplayOnly, SkipReplay, Dynamic, Never. The actor itself must replicate (bReplicates on its class defaults). |
| `bp_set_rpc` | yes | Make a custom event a remote procedure call as the Details panel does: type server (runs on the server; call it from the owning client), client (runs on the owning client), multicast (runs on the server and every client; call it on the server) or none; reliable=true guarantees delivery. |

## code

| Tool | Changes | Summary |
|---|---|---|
| `code_live_compile` | yes | Compile changed .cpp files with Live Coding and patch the running editor (Windows). Fails with code.live_coding_stuck after timeoutSeconds (default 300). |

## data

| Tool | Changes | Summary |
|---|---|---|
| `data_asset_create` | yes | Create a Data Asset of a UDataAsset subclass (class name, /Script or Blueprint path). |
| `datatable_create` | yes | Create a Data Table for a row struct (path of a UserDefinedStruct or /Script struct). |
| `datatable_rows_get` |  | Rows of a Data Table as JSON. |
| `datatable_rows_set` | yes | Replace a Data Table's rows from a JSON array of objects (each with Name). |
| `enum_create` | yes | Create a User Defined Enum with display names. |
| `struct_create` | yes | Create a User Defined Struct with fields [{name, type}] (types as in bp_add_variable). |

## editor

| Tool | Changes | Summary |
|---|---|---|
| `editor_info` |  | Engine version, project, open map, PIE state, dirty package count. |
| `editor_logs` |  | Tail the editor log. Pass since = previous nextSince to get only newer lines. |
| `editor_quit` | yes | Quit the editor. Refused while packages are unsaved unless force=true (unsaved work is lost). |
| `editor_redo` | yes | Redo the last undone editor transaction. |
| `editor_save_all` | yes | Save every dirty map and asset package. |
| `editor_screenshot` | yes | Capture the active viewport to a PNG file. |
| `editor_selection_get` |  | Labels and paths of the selected level actors. |
| `editor_selection_set` | yes | Select level actors by label (replaces the selection). |
| `editor_undo` | yes | Undo the last editor transaction. |
| `editor_viewport_camera` |  | Get the active viewport camera, or move it when location/rotation are given. |

## fab

| Tool | Changes | Summary |
|---|---|---|
| `fab_add` | yes | Download a Fab listing from the user's library and add it to the project, as the Fab tab's Add to Project does (asset packs for this engine version, or glTF/GLB/FBX imported under /Game/Fab). Runs as a job; the result lists the imported content path. acquireFree adds a free listing to the user's library first (accepting its license); paid listings are never bought. |
| `fab_library` |  | List the signed-in user's Fab library (owned listings), optionally filtered by query. Pass a uid to fab_add. |
| `fab_page_run` |  | Run JavaScript in the editor's Fab browser page (opens the Fab tab). The script is an async function body; `A` holds the args; its return value comes back as JSON. |
| `fab_search` |  | Search Fab listings: uid, title, type, seller, isFree, formats. Pass a uid to fab_add. |
| `fab_status` |  | Whether the Fab plugin is loaded and its page is signed in to Fab (opens the Fab tab). |

## gas

| Tool | Changes | Summary |
|---|---|---|
| `gas_ability_create` | yes | Create a Gameplay Ability Blueprint. effect: a Gameplay Effect applied to the owner when the ability activates (the graph is ActivateAbility -> CommitAbility -> ApplyGameplayEffectToOwner -> EndAbility); cost / cooldown: Gameplay Effects; tags: ability tags. Compiled and saved. |
| `gas_component_add` | yes | Give a Blueprint actor an AbilitySystemComponent (reused when it already has one) whose DefaultStartingData creates the given AttributeSets (class names) at start, with a DT_<Blueprint>_<Set> table of starting values taken from the set's defaults (edit it to change them). Compiled and saved. |
| `gas_cue_create` | yes | Create a Gameplay Cue notify Blueprint for a GameplayCue.* tag (kind static, default, or actor). Compiled and saved; implement OnExecute / OnActive in its graph. |
| `gas_effect_create` | yes | Create a Gameplay Effect Blueprint: duration instant (default), infinite or seconds; period seconds; modifiers [{attribute: Set.Attribute, op: add/multiply/divide/override, magnitude}]; stacking source/target with stackLimit; grantedTags (must exist, see tags_add). Compiled and saved. |
| `gas_runtime` | yes | Drive an actor's AbilitySystemComponent in the play world: give (ability), activate (ability), apply (effect to self), state. Every action returns the state: attributes (base/current), owned tags, granted abilities, active effects. |

## input

| Tool | Changes | Summary |
|---|---|---|
| `input_action_create` | yes | Create an Enhanced Input Action (valueType: Boolean/Axis1D/Axis2D/Axis3D). |
| `input_default_context_add` | yes | Register an Input Mapping Context in Project Settings > Enhanced Input > Default Mapping Contexts so every player gets it at start (no Blueprint code needed); remove=true takes it out. Saved to DefaultInput.ini. |
| `mapping_add` | yes | Map a key (SpaceBar, W, Gamepad_FaceButton_Bottom, ...) to an action in a mapping context, with optional modifiers (Negate, Swizzle, DeadZone, Scalar) and triggers (Pressed, Released, Down, Hold, Tap). WASD on one Axis2D move action (X = right, Y = forward): D none, A Negate, W Swizzle, S Swizzle+Negate. |
| `mapping_context_create` | yes | Create an Enhanced Input Mapping Context. |

## jobs

| Tool | Changes | Summary |
|---|---|---|
| `jobs_cancel` |  | Cancel a queued or running job. |
| `jobs_get` |  | Get a job's state and result. |
| `jobs_list` |  | List jobs started in this editor session. |
| `jobs_logs` |  | Read a job's log lines from index 'since'. |

## landscape

| Tool | Changes | Summary |
|---|---|---|
| `landscape_create` | yes | Create a flat landscape: components x components, each 63 quads, at location with scale. |
| `landscape_info` |  | Landscapes in the level with size, component count and material. |
| `landscape_layer_add` | yes | Add a paint layer to a landscape, creating its LayerInfo asset (default /Game/LandscapeLayers/<layer>_LayerInfo) or reusing an existing one. |
| `landscape_paint` | yes | Paint a layer (added with landscape_layer_add) over a circular area; strength 0..1 with smooth falloff. |
| `landscape_sculpt` | yes | Raise (positive height, cm) or lower a circular area of a landscape with smooth falloff. |

## level

| Tool | Changes | Summary |
|---|---|---|
| `actor_attach` | yes | Attach an actor to a parent actor (keeps world transform). |
| `actor_destroy` | yes | Delete actors by label. |
| `actor_duplicate` | yes | Duplicate an actor with an optional offset. |
| `actor_find` |  | Get one actor by label, with its components. |
| `actor_list` |  | List actors in the open level, filtered by class (an exact class name wins, else substring) and/or label substring. |
| `actor_spawn` | yes | Spawn an actor from a class name, Blueprint path or asset (mesh, etc.). |
| `actor_transform` | yes | Set any of location / rotation [pitch,yaw,roll] / scale of an actor. |
| `level_info` |  | Path of the open level and actor count by class. |
| `level_new` | yes | Create a new level (empty, or from a template map) and open it. |
| `level_open` | yes | Open a level in the editor. |
| `level_save` | yes | Save the current level (all=true saves every dirty level). |

## lighting

| Tool | Changes | Summary |
|---|---|---|
| `light_build` | yes | Build static lighting (Lightmass) for the open level as a job: quality preview/medium/high/production (default preview). Fails with lighting.build_in_progress while another build runs; projects without static lighting (r.AllowStaticLighting=0, Lumen-only) have nothing to build. |
| `light_preset` | yes | Light the open level with a preset: outdoor (sun, sky atmosphere, sky light, height fog, clouds, post process), night (moonlight, dark sky, fog) or indoor (no sun: sky light, a ceiling point light, post process with eye adaptation). replace=true (default) first removes the level's existing sun, sky, fog, cloud and post process actors. |
| `render_settings` | yes | Read or change project rendering settings (DefaultEngine.ini). values: {name: value} where name is lumen, lumenReflections, virtualShadowMaps, nanite, rayTracing, staticLighting (true/false), antiAliasing (None, FXAA, TemporalAA, MSAA, TSR) or any URendererSettings property name with its UE text value. Returns each setting's before and after value and whether an editor restart is needed. |

## localization

| Tool | Changes | Summary |
|---|---|---|
| `loc_preview` |  | Preview the game's compiled translations in the editor: culture (e.g. ja) switches the game text to it, empty or off goes back to the native text. With asset and property, returns how that text property (on the asset, or a Blueprint's defaults) reads now. |
| `loc_target` | yes | Create or update a game localization target (default Game, which the game loads): native culture (default en), cultures to translate into (e.g. ja), and the content paths to gather text from (default Content/*). Writes the target settings and its Gather/Export/Import/Compile configs under Config/Localization. |

## material

| Tool | Changes | Summary |
|---|---|---|
| `material_connect` | yes | Connect an expression output to another expression input, or to a material property (BaseColor, Metallic, Roughness, EmissiveColor, Normal, Opacity, ...). |
| `material_create` | yes | Create an empty Material asset. |
| `material_node_add` | yes | Add a material expression (e.g. Constant3Vector, ScalarParameter, Multiply) with optional properties {name: value}. |
| `material_recompile` |  | Recompile a material and report its expression count. |
| `mi_create` | yes | Create a Material Instance of a parent material. |
| `mi_set_param` | yes | Set a scalar, vector [r,g,b,a] or texture (asset path) parameter on a Material Instance. |

## net

| Tool | Changes | Summary |
|---|---|---|
| `net_assert` |  | Wait until an actor's property has the same value in every world of the play session (or the expect value in all of them); fails with net.mismatch and each world's last value after timeoutSeconds (default 5). actor: label or name (level actors have the same one in every world) or a class name; property: Name or Component.Name. |
| `net_emulate` |  | Simulate network conditions in the running play session: latencyMs (outgoing lag), jitterMs, lossPercent on the server, the clients or all worlds; off=true removes them. Ends with the session. |

## niagara

| Tool | Changes | Summary |
|---|---|---|
| `emitter_add` | yes | Add an emitter asset (e.g. a template under /Niagara/DefaultAssets/Templates/Emitters) to a Niagara System. |
| `niagara_emitters_list` |  | Emitters of a Niagara System. |
| `niagara_input_set` | yes | Set a module input: a local value (float, int, bool, or [x,y(,z(,w))] for vectors/colors), a link to a parameter (e.g. Particles.NormalizedAge, User.Speed) or a dynamic input script. module is the name niagara_module_add returned; a dynamic input's returned node name works as module for its own inputs. |
| `niagara_module_add` | yes | Add a module script (e.g. /Niagara/Modules/Update/Forces/Drag) to an emitter stage: EmitterSpawn / EmitterUpdate / ParticleSpawn / ParticleUpdate. |
| `niagara_module_enable` | yes | Enable or disable a module in an emitter stage, as the checkbox in the Niagara stack does. |
| `niagara_modules_list` |  | Modules of an emitter per stage in stack order, each with enabled and its inputs (name, type, kind input/staticSwitch, current local value, enum options), plus the emitter's renderers. Use the names with niagara_input_set, niagara_module_enable and niagara_renderer_set. |
| `niagara_renderer_set` | yes | Set a property of an emitter's renderer (index or class, e.g. SpriteRenderer; see niagara_modules_list), e.g. property=Material value=/Game/M_Glow, or bIsEnabled, SubImageSize, Alignment. |
| `niagara_system_create` | yes | Create an empty Niagara System asset. |

## online

| Tool | Changes | Summary |
|---|---|---|
| `online_session` | yes | Session in a play world through the online subsystem (Null in PIE): create (sessionName, maxPlayers, lan), find (lists results with their index), join (index of the last find in that world; travel=true then moves the player to it), destroy. Waits up to timeoutSeconds (default 10) and fails with online.session_failed. |

## packaging

| Tool | Changes | Summary |
|---|---|---|
| `cook_content` | yes | Start a content cook for a platform; returns the process id and log file. |
| `package_project` | yes | Start BuildCookRun to package the project into outputDir (relative paths are under the project, e.g. Build/Windows; never an engine install or Content); returns the process id and log file (poll with package_status). check=true only validates outputDir. |
| `package_status` |  | State of a package/cook run from its log file (running, succeeded, failed). |

## pcg

| Tool | Changes | Summary |
|---|---|---|
| `pcg_add_node` | yes | Add a node to a PCG graph by settings type (CreatePointsGrid, SurfaceSampler, StaticMeshSpawner, DensityFilter, TransformPoints, ... or a full PCG*Settings class) with properties in snake_case (StaticMeshSpawner also takes mesh: a static mesh path). Returns the node name and its pin labels. |
| `pcg_connect` | yes | Connect two PCG nodes: from and to are <node name>:<pin label> (input / output name the graph's own nodes), e.g. PCGNode_0:Out to PCGNode_1:In. |
| `pcg_create` | yes | Create an empty PCG graph (Input and Output nodes only). |
| `pcg_generate` | yes | Run a PCG graph in the open level: places (or reuses) a PCG Volume labelled label (default PCG_<graph>) at location with extent (half size, default 1000), assigns the graph and generates. Generation finishes over the next frames; pcg_status reports when it is done and how many instances were spawned. |
| `pcg_status` |  | State of a PCG Volume placed by pcg_generate: generated, and the instanced mesh instances it spawned. |

## perf

| Tool | Changes | Summary |
|---|---|---|
| `perf_measure` |  | Measure the running play session for seconds (default 10): frameMs, gameMs, renderMs and gpuMs as {avg, p95, max}, hitches (frames over hitchMs, default 50), drawCalls {avg, max}, memoryMB {used, peak}, fps. partial=true when the session ended early. |
| `perf_trace` |  | Unreal Insights trace of the editor process: start (to file, default Saved/UnrealCli/Traces/<time>.utrace; channels default cpu,gpu,frame,bookmark) or stop. Summarise the file with: unrealengine-cli perf trace summary <file>. |

## physics

| Tool | Changes | Summary |
|---|---|---|
| `geometry_collection_create` | yes | Create a Chaos Geometry Collection (destructible) asset from one or more static meshes, each piece a bone. Fracture it further in the Fracture editor mode (Fracture plugin); place it in a level with actor_spawn (GeometryCollectionActor) and set its RestCollection. |

## project

| Tool | Changes | Summary |
|---|---|---|
| `plugin_enable` | yes | Enable or disable a plugin in the .uproject (takes effect after restart). |
| `plugin_list` |  | List plugins (enabled only unless all=true). |
| `project_info` |  | Project name, paths and modules. |
| `project_settings_get` |  | Read a config value (file: Engine/Game/Input/Editor). |
| `project_settings_set` | yes | Write a config value and flush it to the project's Default*.ini. |

## property

| Tool | Changes | Summary |
|---|---|---|
| `component_add` | yes | Add a component to a level actor instance. |
| `component_list` |  | Components of a level actor. |
| `component_remove` | yes | Remove a component from a level actor instance. |
| `prop_array_edit` | yes | Edit an array property: op = append / insert / remove / clear. |
| `prop_get` |  | Read reflected properties of an asset, object, Blueprint defaults ('/Game/BP'), a Blueprint component ('/Game/BP.Component'), an actor ('Label' or 'Label.Component') or 'WorldSettings'. Without property, lists readable properties. |
| `prop_set` | yes | Set one property; property may be dotted (Settings.Speed) with array indices (Tags[0]). value is JSON: numbers, strings, [x,y,z], {r,g,b,a}, object paths, enum names. Components also take CollisionProfileName, CollisionEnabled, GenerateOverlapEvents, SimulatePhysics, NotifyRigidBodyCollision, EnableGravity. |

## python

| Tool | Changes | Summary |
|---|---|---|
| `python_exec` | yes | Run Python inside the editor (import unreal). Returns printed output and log. |

## runtime

| Tool | Changes | Summary |
|---|---|---|
| `automation_list` |  | List Automation tests whose full path contains filter. |
| `automation_run` |  | Run Automation tests matching filter as a job; the result lists each test with its errors. |
| `pie_actors` |  | Actors of the play world (filter by class or label/name substring) with location, rotation and velocity; the player's pawn is flagged. |
| `pie_call` |  | Call a function or custom event on a play-world actor with space-separated arguments in parameter order, e.g. actor=player command="SetActorHiddenInGame true" or command="TakeHit 25". Structs use Unreal text form without spaces: K2_SetActorLocation (X=-140,Y=300,Z=100) false () false passes NewLocation, bSweep, SweepHitResult (out, any value), bTeleport. A bad argument fails with the parameter list. |
| `pie_click` |  | Click a button of the on-screen UI in the play session by widget name (fires its OnClicked; see pie_widgets). |
| `pie_exec` |  | Run a console command in the play world (e.g. "slomo 0.5", "show collision", "ce MyEvent"). |
| `pie_get` |  | Read properties of a play-world actor (or one of its components): actor is a label, name, "player" (the pawn) or "controller" (its player controller); without properties returns its transform and velocity. |
| `pie_input` |  | Send input to the running play session: a key (W, SpaceBar, LeftMouseButton, Gamepad_LeftX, ...) or an Enhanced Input action asset with a value (bool, number, [x,y] or [x,y,z]). Held for seconds (0 = one press/injection); replies after release. |
| `pie_run` |  | Play In Editor for durationSeconds (or until a log line contains stopOnLog), then stop and report errors. |
| `pie_screenshot` |  | Capture the play session's game viewport to a PNG (default under Saved/UnrealCli/Screenshots) and return its path; blank=true means a single flat colour (unlit level or camera sees nothing). View the image to check what the player sees. |
| `pie_start` |  | Start Play In Editor and return once every player exists; drive it with pie_input / pie_wait / pie_screenshot, end it with pie_stop. players (1-4) and netMode (standalone; listen = listen server + clients; client = dedicated server + clients) start a multiplayer session in the editor process; pick a world with the world argument of the other pie tools (see pie_worlds). |
| `pie_status` |  | Whether a play session is running and its world. |
| `pie_stop` |  | Stop the running play session. |
| `pie_wait` |  | Let the play session run for seconds, or until a log line contains untilLog (whichever comes first). |
| `pie_widgets` |  | UI on screen in the play session: user widgets in the viewport with their child widgets (name, class, visible, text for text blocks). |
| `pie_worlds` |  | Worlds of the running play session: id (standalone, server, client:1, ...; pass it as world to the other pie tools), netMode and the number of player controllers. |

## sequencer

| Tool | Changes | Summary |
|---|---|---|
| `sequence_create` | yes | Create a Level Sequence with a frame range. |
| `sequence_key_add` | yes | Add a key on a bound actor's track channel (e.g. channel 'Location.X'). |
| `sequence_track_add` | yes | Bind a level actor (by label) and add a track: Transform / Visibility / Audio / Event / Fade (Fade/Audio/Event need no actor). |

## system

| Tool | Changes | Summary |
|---|---|---|
| `system_ping` |  | Check the bridge answers; returns editor identity. |
| `tools_list` |  | List registered tools with their input schemas. |

## tags

| Tool | Changes | Summary |
|---|---|---|
| `tags_add` | yes | Add a Gameplay Tag to Config/DefaultGameplayTags.ini (parents are implied). No-op when it exists. |
| `tags_list` |  | Registered Gameplay Tags (optionally only those containing filter). |
| `tags_remove` | yes | Remove a Gameplay Tag from the project ini. Refused while assets still reference it. |
| `tags_rename` | yes | Rename a Gameplay Tag (and its children); writes a redirect so existing references keep working. |

## umg

| Tool | Changes | Summary |
|---|---|---|
| `widget_add` | yes | Add a widget (TextBlock, Button, Image, VerticalBox, ...) under a panel (default: root). |
| `widget_animation_add` | yes | Add (or extend) a widget animation: track opacity / translation / scale / angle on a widget with keys [[seconds, value]] (translation/scale values are [x,y]). After bp_compile the animation is a variable; play it with PlayAnimation. |
| `widget_bind` | yes | Create a bound event node for a widget delegate (e.g. Button OnClicked) in the event graph. |
| `widget_create` | yes | Create a Widget Blueprint (UserWidget) with an optional root panel class (default CanvasPanel). |
| `widget_layout` | yes | Lay out a widget in its parent. Canvas children: anchor (TopLeft, TopCenter, TopRight, CenterLeft, Center, CenterRight, BottomLeft, BottomCenter, BottomRight, TopFill, BottomFill, LeftFill, RightFill, Fill or [minX,minY,maxX,maxY]), position [x,y], size [w,h], alignment [x,y] (0..1 pivot), autoSize, zOrder. Box/overlay children: padding (number or [l,t,r,b]), hAlign (Left/Center/Right/Fill), vAlign (Top/Center/Bottom/Fill), fill (share of the remaining space). |
| `widget_set_prop` | yes | Set a widget property by reflection (UE text form); prefix 'Slot.' targets the slot (e.g. Slot.Padding). |
| `widget_tree_get` |  | Widgets of a Widget Blueprint with parent, slot and variable flags. |
