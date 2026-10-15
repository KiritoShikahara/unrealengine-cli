import unreal

from .. import ToolError, tool
from ..util import actors, create_asset, from_json, load_asset_or_fail


def _require():
    if not hasattr(unreal, "PCGGraph"):
        raise ToolError("pcg.plugin_disabled", "the PCG plugin is not enabled",
                        hint="add { \"Name\": \"PCG\", \"Enabled\": true } to the .uproject Plugins and restart the editor")


def _graph(path):
    _require()
    return load_asset_or_fail(path, unreal.PCGGraph)


def _nodes(g):
    return [g.get_input_node(), g.get_output_node()] + list(g.get_editor_property("nodes"))


def _find(g, name):
    for n in _nodes(g):
        if n.get_name() == name:
            return n
    alias = {"input": g.get_input_node(), "output": g.get_output_node()}
    if name.lower() in alias:
        return alias[name.lower()]
    raise ToolError("pcg.no_node", "no node '%s' in the graph" % name,
                    details=[n.get_name() for n in _nodes(g)], hint="use the node names returned by pcg_add_node, or input / output")


def _pins(n):
    def labels(pins):
        return [str(p.get_editor_property("properties").get_editor_property("label")) for p in pins]
    return labels(n.get_editor_property("input_pins")), labels(n.get_editor_property("output_pins"))


def _settings_class(name):
    for cand in (name, "PCG%s" % name, "PCG%sSettings" % name, "%sSettings" % name):
        cls = getattr(unreal, cand, None)
        if cls is not None and isinstance(cls, type) and issubclass(cls, unreal.PCGSettings):
            return cls
    raise ToolError("pcg.no_settings", "no PCG node type '%s'" % name,
                    hint="e.g. CreatePointsGrid, SurfaceSampler, StaticMeshSpawner, DensityFilter, TransformPoints")


def _set_mesh(settings, mesh):
    m = load_asset_or_fail(mesh, unreal.StaticMesh)
    settings.set_editor_property("mesh_selector_type", unreal.PCGMeshSelectorWeighted)
    sel = settings.get_editor_property("mesh_selector_parameters")
    entry = unreal.PCGMeshSelectorWeightedEntry()
    d = entry.get_editor_property("descriptor")
    d.set_editor_property("static_mesh", m)
    entry.set_editor_property("descriptor", d)
    entry.set_editor_property("weight", 1)
    sel.set_editor_property("mesh_entries", [entry])


def _apply(settings, props):
    for k, v in (props or {}).items():
        if k == "mesh" and isinstance(settings, unreal.PCGStaticMeshSpawnerSettings):
            _set_mesh(settings, v)
            continue
        try:
            cur = settings.get_editor_property(k)
        except Exception:
            raise ToolError("prop.not_found", "%s has no property '%s'" % (type(settings).__name__, k))
        settings.set_editor_property(k, from_json(v, cur))


@tool("pcg_create", "pcg", "Create an empty PCG graph (Input and Output nodes only).",
      {"path": {"type": "string"}}, ["path"], mutating=True, undoable=False)
def pcg_create(path):
    _require()
    g = create_asset(path, unreal.PCGGraph, unreal.PCGGraphFactory())
    unreal.EditorAssetLibrary.save_loaded_asset(g, False)
    return {"path": g.get_path_name(), "nodes": [n.get_name() for n in _nodes(g)]}


@tool("pcg_add_node", "pcg",
      "Add a node to a PCG graph by settings type (CreatePointsGrid, SurfaceSampler, StaticMeshSpawner, DensityFilter, "
      "TransformPoints, ... or a full PCG*Settings class) with properties in snake_case (StaticMeshSpawner also takes "
      "mesh: a static mesh path). Returns the node name and its pin labels.",
      {"path": {"type": "string"}, "type": {"type": "string"}, "properties": {"type": "object"}},
      ["path", "type"], mutating=True, undoable=False)
def pcg_add_node(path, type, properties=None):
    g = _graph(path)
    node, settings = g.add_node_of_type(_settings_class(type))
    _apply(settings, properties)
    unreal.EditorAssetLibrary.save_loaded_asset(g, False)
    ins, outs = _pins(node)
    return {"node": node.get_name(), "settings": settings.get_class().get_name(), "inputs": ins, "outputs": outs}


@tool("pcg_connect", "pcg",
      "Connect two PCG nodes: from and to are <node name>:<pin label> (input / output name the graph's own nodes), "
      "e.g. PCGNode_0:Out to PCGNode_1:In.",
      {"path": {"type": "string"}, "from": {"type": "string"}, "to": {"type": "string"}},
      ["path", "from", "to"], mutating=True, undoable=False)
def pcg_connect(path, **kw):
    g = _graph(path)
    ends = []
    for key in ("from", "to"):
        spec = kw[key]
        if ":" not in spec:
            raise ToolError("args.invalid", "'%s' is not <node>:<pin>" % spec)
        name, pin = spec.rsplit(":", 1)
        node = _find(g, name)
        ins, outs = _pins(node)
        pins = outs if key == "from" else ins
        if pin not in pins:
            raise ToolError("pcg.no_pin", "%s has no %s pin '%s'" % (name, "output" if key == "from" else "input", pin),
                            details=pins)
        ends.append((node, pin))
    g.add_edge(ends[0][0], ends[0][1], ends[1][0], ends[1][1])
    unreal.EditorAssetLibrary.save_loaded_asset(g, False)
    return {"from": kw["from"], "to": kw["to"]}


def _volume(label):
    for a in actors():
        if a.get_actor_label() == label:
            return a
    return None


@tool("pcg_generate", "pcg",
      "Run a PCG graph in the open level: places (or reuses) a PCG Volume labelled label (default PCG_<graph>) at location "
      "with extent (half size, default 1000), assigns the graph and generates. Generation finishes over the next frames; "
      "pcg_status reports when it is done and how many instances were spawned.",
      {"graph": {"type": "string"}, "label": {"type": "string"}, "location": {"type": "array", "items": {"type": "number"}},
       "extent": {"type": "number"}},
      ["graph"], mutating=True, undoable=False)
def pcg_generate(graph, label=None, location=None, extent=1000.0):
    g = _graph(graph)
    label = label or "PCG_%s" % g.get_name()
    vol = _volume(label)
    if vol is None:
        loc = unreal.Vector(*(location or [0, 0, 0]))
        vol = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(unreal.PCGVolume, loc)
        vol.set_actor_label(label)
    vol.set_actor_scale3d(unreal.Vector(extent / 100.0, extent / 100.0, extent / 100.0))
    comp = vol.get_component_by_class(unreal.PCGComponent)
    comp.set_graph(g)
    comp.generate_local(True)
    return {"label": label, "graph": g.get_path_name()}


@tool("pcg_status", "pcg",
      "State of a PCG Volume placed by pcg_generate: generated, and the instanced mesh instances it spawned.",
      {"label": {"type": "string"}}, ["label"])
def pcg_status(label):
    vol = _volume(label)
    if vol is None:
        raise ToolError("actor.not_found", "no actor labelled '%s'" % label)
    comp = vol.get_component_by_class(unreal.PCGComponent)
    count = 0
    meshes = set()
    for c in vol.get_components_by_class(unreal.InstancedStaticMeshComponent):
        count += c.get_instance_count()
        if c.static_mesh:
            meshes.add(c.static_mesh.get_path_name())
    generating = comp.is_generating() if hasattr(comp, "is_generating") else False
    return {"label": label, "generated": bool(comp.get_editor_property("generated")), "generating": generating,
            "instances": count, "meshes": sorted(meshes)}
