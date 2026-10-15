import unreal

from .. import ToolError, tool
from ..util import ensure_absent, load_asset_or_fail, split_path

_OK = unreal.MetaSoundBuilderResult.SUCCEEDED


def _check(result, what):
    if result != _OK:
        raise ToolError("metasound.failed", what)


def _editor():
    return unreal.get_editor_subsystem(unreal.MetaSoundEditorSubsystem)


def _builder(path):
    asset = load_asset_or_fail(path)
    if not isinstance(asset, (unreal.MetaSoundSource, unreal.MetaSoundPatch)):
        raise ToolError("asset.wrong_type", "'%s' is not a MetaSound" % path)
    b, r = _editor().find_or_begin_building(asset)
    _check(r, "cannot edit '%s'" % path)
    return asset, b


def _node(b, node_id):
    h = unreal.MetaSoundNodeHandle()
    h.import_text("(NodeID=%s)" % node_id)
    if not b.contains_node(h):
        raise ToolError("metasound.no_node", "no node %s in this MetaSound" % node_id,
                        hint="node ids come from metasound_add_node / metasound_info")
    return h


def _node_id(h):
    return h.export_text().split("=", 1)[1].rstrip(")")


def _pins(b, h):
    ins, _ = b.find_node_inputs(h)
    outs, _ = b.find_node_outputs(h)
    return ([{"name": str(b.get_node_input_data(i)[0]), "type": str(b.get_node_input_data(i)[1])} for i in ins],
            [{"name": str(b.get_node_output_data(o)[0]), "type": str(b.get_node_output_data(o)[1])} for o in outs])


def _graph(b):
    return {"inputs": [str(n) for n in b.get_graph_input_names()[0]],
            "outputs": [str(n) for n in b.get_graph_output_names()[0]]}


def _save(asset):
    unreal.EditorAssetLibrary.save_loaded_asset(asset, False)


@tool("metasound_create", "audio",
      "Create a MetaSound Source (format mono or stereo, oneShot default true). Returns its graph inputs (UE.Source.OnPlay) "
      "and outputs (audio channels, OnFinished) to connect nodes to with metasound_connect.",
      {"path": {"type": "string"}, "format": {"type": "string", "enum": ["mono", "stereo"]}, "oneShot": {"type": "boolean"}},
      ["path"], mutating=True, undoable=False)
def metasound_create(path, format="mono", oneShot=True):
    ensure_absent(path)
    folder, name = split_path(path)
    fmt = unreal.MetaSoundOutputAudioFormat.STEREO if format == "stereo" else unreal.MetaSoundOutputAudioFormat.MONO
    sub = unreal.get_engine_subsystem(unreal.MetaSoundBuilderSubsystem)
    b, _on_play, _on_finished, _outs, r = sub.create_source_builder(name, fmt, oneShot)
    _check(r, "cannot start a MetaSound builder")
    asset, r = _editor().build_to_asset(b, "unrealengine-cli", name, folder)
    _check(r, "cannot create the MetaSound asset at '%s'" % path)
    _save(asset)
    out = {"path": asset.get_path_name()}
    out.update(_graph(b))
    return out


@tool("metasound_add_node", "audio",
      "Add a node to a MetaSound by class name Namespace.Name[.Variant], e.g. UE.Sine.Audio, UE.ADSR Envelope.Audio, "
      "UE.Multiply.Audio. Returns its node id and pins.",
      {"path": {"type": "string"}, "node": {"type": "string"}, "version": {"type": "integer"}},
      ["path", "node"], mutating=True, undoable=False)
def metasound_add_node(path, node, version=1):
    asset, b = _builder(path)
    parts = node.split(".")
    if len(parts) < 2:
        raise ToolError("args.invalid", "'%s' is not Namespace.Name[.Variant]" % node, hint="e.g. UE.Sine.Audio")
    cn = unreal.MetasoundFrontendClassName()
    cn.set_editor_property("namespace", parts[0])
    cn.set_editor_property("name", parts[1])
    cn.set_editor_property("variant", ".".join(parts[2:]) if len(parts) > 2 else "")
    h, r = b.add_node_by_class_name(cn, version)
    if r != _OK:
        raise ToolError("metasound.no_class", "no MetaSound node class '%s' (version %d)" % (node, version),
                        hint="class names are Namespace.Name.Variant as shown in the MetaSound editor, e.g. UE.Sine.Audio")
    _save(asset)
    ins, outs = _pins(b, h)
    return {"node": _node_id(h), "class": node, "inputs": ins, "outputs": outs}


def _endpoint(spec):
    if ":" not in spec:
        raise ToolError("args.invalid", "'%s' is not <node id>:<pin> or graph:<name>" % spec)
    return spec.split(":", 1)


@tool("metasound_connect", "audio",
      "Connect an output to an input. from/to are <node id>:<pin name>, or graph:<name> for the MetaSound's own inputs "
      "(from) and outputs (to), e.g. from 5F3A...:Audio to graph:UE.OutputFormat.Mono.Audio:0.",
      {"path": {"type": "string"}, "from": {"type": "string"}, "to": {"type": "string"}},
      ["path", "from", "to"], mutating=True, undoable=False)
def metasound_connect(path, **kw):
    asset, b = _builder(path)
    src_node, src_pin = _endpoint(kw["from"])
    dst_node, dst_pin = _endpoint(kw["to"])
    if src_node == "graph":
        if src_pin not in _graph(b)["inputs"]:
            raise ToolError("metasound.no_pin", "no graph input '%s'" % src_pin, details=_graph(b))
        dst = _node(b, dst_node)
        i, r = b.find_node_input_by_name(dst, dst_pin)
        _check(r, "node %s has no input '%s'" % (dst_node, dst_pin))
        _check(b.connect_node_input_to_graph_input(src_pin, i), "cannot connect %s to %s" % (kw["from"], kw["to"]))
    else:
        src = _node(b, src_node)
        o, r = b.find_node_output_by_name(src, src_pin)
        _check(r, "node %s has no output '%s'" % (src_node, src_pin))
        if dst_node == "graph":
            if dst_pin not in _graph(b)["outputs"]:
                raise ToolError("metasound.no_pin", "no graph output '%s'" % dst_pin, details=_graph(b))
            _gnode, _dtype, i, r = b.find_graph_output_node(dst_pin)
            _check(r, "no graph output '%s'" % dst_pin)
        else:
            dst = _node(b, dst_node)
            i, r = b.find_node_input_by_name(dst, dst_pin)
            _check(r, "node %s has no input '%s'" % (dst_node, dst_pin))
        _check(b.connect_nodes(o, i), "cannot connect %s to %s (types differ?)" % (kw["from"], kw["to"]))
    _save(asset)
    return {"from": kw["from"], "to": kw["to"], "connected": True}


@tool("metasound_set_input", "audio",
      "Set the default value of a node input: number (float or int), boolean or string.",
      {"path": {"type": "string"}, "node": {"type": "string"}, "input": {"type": "string"}, "value": {}},
      ["path", "node", "input", "value"], mutating=True, undoable=False)
def metasound_set_input(path, node, input, value):
    asset, b = _builder(path)
    h = _node(b, node)
    i, r = b.find_node_input_by_name(h, input)
    _check(r, "node %s has no input '%s'" % (node, input))
    kind = str(b.get_node_input_data(i)[1])
    L = unreal.MetasoundFrontendLiteralBlueprintAccess
    if kind == "Bool" or isinstance(value, bool):
        lit = L.create_bool_meta_sound_literal(bool(value))
    elif kind == "Int32":
        lit = L.create_int_meta_sound_literal(int(value))
    elif kind in ("Float", "Time") or isinstance(value, (int, float)):
        lit = L.create_float_meta_sound_literal(float(value))
    else:
        lit = L.create_string_meta_sound_literal(str(value))
    _check(b.set_node_input_default(i, lit), "cannot set %s on %s" % (input, node))
    _save(asset)
    return {"node": node, "input": input, "type": kind, "value": value}
