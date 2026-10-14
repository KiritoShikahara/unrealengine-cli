# Tool catalog

134 tools. Generated from tools/catalog.json by scripts/gen-docs.sh.

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
| `animbp_create` | yes | Create an Animation Blueprint for a skeleton. |
| `blendspace_create` | yes | Create a 2D Blend Space for a skeleton. |
| `montage_create` | yes | Create an Anim Montage from an Anim Sequence. |
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
| `soundcue_create` | yes | Create a Sound Cue (optionally wrapping a Sound Wave). |

## blueprint

| Tool | Changes | Summary |
|---|---|---|
| `bp_add_component` | yes | Add a component to the Blueprint's construction script (optionally under a parent component). |
| `bp_add_event_dispatcher` | yes | Add an event dispatcher with optional inputs [{name,type}]; then bp_compile and use CallDispatcher / BindDispatcher nodes. |
| `bp_add_function` | yes | Add a function graph with optional inputs/outputs [{name,type}]. |
| `bp_add_node` | yes | Add a node. type and memberName: Event (overridable event) / CustomEvent (name) / CallFunction (function; memberParent = class) / VariableGet/VariableSet (variable or component; memberParent = class for another object's variable) / Branch / Sequence / Self / Cast (class) / Macro (ForEachLoop, DoOnce, Gate...) / InputAction (Input Action asset) / ComponentEvent (component variable + delegate, e.g. OnComponentBeginOverlap) / SpawnActor (actor class) / CreateWidget (widget Blueprint) / CallDispatcher / BindDispatcher (event dispatcher; memberParent = class for another object's) / MakeStruct / BreakStruct (struct, e.g. Vector, HitResult) / SwitchEnum (enum) / Timeline (name from bp_add_timeline) / Node (any graph node class, e.g. K2Node_SwitchInteger, K2Node_Select, K2Node_MakeArray, K2Node_SwitchString). |
| `bp_add_timeline` | yes | Add a timeline with float tracks [{name, keys:[[time,value],...]}] and place its node in the event graph (outputs: Update, Finished and one pin per track). |
| `bp_add_variable` | yes | Add a member variable. type: bool/int/int64/byte/float/double/string/name/text/vector/rotator/transform/color/<class>/<struct>, optional []. |
| `bp_compile` |  | Compile and return status, error/warning counts and messages. |
| `bp_connect` | yes | Connect pins: from/to are '<node>.<pin>' (node = N<k> or guid). |
| `bp_create` | yes | Create a Blueprint asset (parent: class name, /Script path or Blueprint path). |
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

## code

| Tool | Changes | Summary |
|---|---|---|
| `code_live_compile` | yes | Compile changed .cpp files with Live Coding and patch the running editor (Windows). |

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

## input

| Tool | Changes | Summary |
|---|---|---|
| `input_action_create` | yes | Create an Enhanced Input Action (valueType: Boolean/Axis1D/Axis2D/Axis3D). |
| `mapping_add` | yes | Map a key (SpaceBar, W, Gamepad_FaceButton_Bottom, ...) to an action in a mapping context. |
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
| `actor_list` |  | List actors in the open level, filtered by class and/or label substring. |
| `actor_spawn` | yes | Spawn an actor from a class name, Blueprint path or asset (mesh, etc.). |
| `actor_transform` | yes | Set any of location / rotation [pitch,yaw,roll] / scale of an actor. |
| `level_info` |  | Path of the open level and actor count by class. |
| `level_new` | yes | Create a new level (empty, or from a template map) and open it. |
| `level_open` | yes | Open a level in the editor. |
| `level_save` | yes | Save the current level (all=true saves every dirty level). |

## material

| Tool | Changes | Summary |
|---|---|---|
| `material_connect` | yes | Connect an expression output to another expression input, or to a material property (BaseColor, Metallic, Roughness, EmissiveColor, Normal, Opacity, ...). |
| `material_create` | yes | Create an empty Material asset. |
| `material_node_add` | yes | Add a material expression (e.g. Constant3Vector, ScalarParameter, Multiply) with optional properties {name: value}. |
| `material_recompile` |  | Recompile a material and report its expression count. |
| `mi_create` | yes | Create a Material Instance of a parent material. |
| `mi_set_param` | yes | Set a scalar, vector [r,g,b,a] or texture (asset path) parameter on a Material Instance. |

## niagara

| Tool | Changes | Summary |
|---|---|---|
| `emitter_add` | yes | Add an emitter asset (e.g. a template under /Niagara/DefaultAssets/Templates/Emitters) to a Niagara System. |
| `niagara_emitters_list` |  | Emitters of a Niagara System. |
| `niagara_input_set` | yes | Set a module input: a local value (float, int, bool, or [x,y(,z(,w))] for vectors/colors), a link to a parameter (e.g. Particles.NormalizedAge, User.Speed) or a dynamic input script. module is the name niagara_module_add returned; a dynamic input's returned node name works as module for its own inputs. |
| `niagara_module_add` | yes | Add a module script (e.g. /Niagara/Modules/Update/Forces/Drag) to an emitter stage: EmitterSpawn / EmitterUpdate / ParticleSpawn / ParticleUpdate. |
| `niagara_system_create` | yes | Create an empty Niagara System asset. |

## packaging

| Tool | Changes | Summary |
|---|---|---|
| `cook_content` | yes | Start a content cook for a platform; returns the process id and log file. |
| `package_project` | yes | Start BuildCookRun to package the project; returns the process id and log file (poll with package_status). |
| `package_status` |  | State of a package/cook run from its log file (running, succeeded, failed). |

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
| `prop_get` |  | Read reflected properties of an asset, object, Blueprint defaults or actor ('Label' or 'Label.Component'). Without property, lists readable properties. |
| `prop_set` | yes | Set one property; property may be dotted (Settings.Speed) with array indices (Tags[0]). value is JSON: numbers, strings, [x,y,z], {r,g,b,a}, object paths, enum names. |

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
| `pie_call` |  | Call a function or custom event on a play-world actor with space-separated arguments, e.g. actor=player command="SetActorHiddenInGame true" or command="TakeHit 25". |
| `pie_exec` |  | Run a console command in the play world (e.g. "slomo 0.5", "show collision", "ce MyEvent"). |
| `pie_get` |  | Read properties of a play-world actor (or one of its components): actor is a label, name or "player"; without properties returns its transform and velocity. |
| `pie_input` |  | Send input to the running play session: a key (W, SpaceBar, LeftMouseButton, Gamepad_LeftX, ...) or an Enhanced Input action asset with a value (bool, number, [x,y] or [x,y,z]). Held for seconds (0 = one press/injection); replies after release. |
| `pie_run` |  | Play In Editor for durationSeconds (or until a log line contains stopOnLog), then stop and report errors. |
| `pie_screenshot` |  | Capture the play session's game viewport to a PNG (default under Saved/UnrealCli/Screenshots) and return its path; blank=true means a single flat colour (unlit level or camera sees nothing). View the image to check what the player sees. |
| `pie_start` |  | Start Play In Editor and return once the play world exists; drive it with pie_input / pie_wait / pie_screenshot, end it with pie_stop. |
| `pie_status` |  | Whether a play session is running and its world. |
| `pie_stop` |  | Stop the running play session. |
| `pie_wait` |  | Let the play session run for seconds, or until a log line contains untilLog (whichever comes first). |

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

## umg

| Tool | Changes | Summary |
|---|---|---|
| `widget_add` | yes | Add a widget (TextBlock, Button, Image, VerticalBox, ...) under a panel (default: root). |
| `widget_bind` | yes | Create a bound event node for a widget delegate (e.g. Button OnClicked) in the event graph. |
| `widget_create` | yes | Create a Widget Blueprint (UserWidget) with an optional root panel class (default CanvasPanel). |
| `widget_set_prop` | yes | Set a widget property by reflection (UE text form); prefix 'Slot.' targets the slot (e.g. Slot.Padding). |
| `widget_tree_get` |  | Widgets of a Widget Blueprint with parent, slot and variable flags. |
