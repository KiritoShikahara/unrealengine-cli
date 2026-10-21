import unreal

from .. import ToolError, tool
from ..util import create_asset, ensure_absent, load_asset_or_fail, split_path


def _save(*assets):
    for a in assets:
        unreal.EditorAssetLibrary.save_loaded_asset(a, False)


@tool("anim_ikrig_create", "anim",
      "Create an IK Rig for a skeletal mesh. auto=true (default) adds the auto-generated retarget chains and a full-body IK "
      "solver. Returns the retarget root and chains (name, start, end).",
      {"path": {"type": "string"}, "mesh": {"type": "string"}, "auto": {"type": "boolean"}},
      ["path", "mesh"], mutating=True, undoable=False)
def anim_ikrig_create(path, mesh, auto=True):
    sk = load_asset_or_fail(mesh, unreal.SkeletalMesh)
    rig = create_asset(path, unreal.IKRigDefinition, unreal.IKRigDefinitionFactory())
    c = unreal.IKRigController.get_controller(rig)
    if not c.set_skeletal_mesh(sk):
        raise ToolError("anim.ikrig_failed", "the IK Rig does not accept %s" % mesh)
    if auto:
        c.apply_auto_generated_retarget_definition()
        c.apply_auto_fbik()
    _save(rig)
    chains = [{"name": str(ch.chain_name), "start": str(c.get_retarget_chain_start_bone(ch.chain_name)),
               "end": str(c.get_retarget_chain_end_bone(ch.chain_name))} for ch in c.get_retarget_chains()]
    return {"path": rig.get_path_name(), "mesh": sk.get_path_name(), "retargetRoot": str(c.get_retarget_root()),
            "chains": chains, "solvers": c.get_num_solvers()}


@tool("anim_retargeter_create", "anim",
      "Create an IK Retargeter from a source IK Rig to a target IK Rig and map their chains automatically "
      "(fuzzy name matching). Returns the chain mapping.",
      {"path": {"type": "string"}, "source": {"type": "string"}, "target": {"type": "string"}},
      ["path", "source", "target"], mutating=True, undoable=False)
def anim_retargeter_create(path, source, target):
    src = load_asset_or_fail(source, unreal.IKRigDefinition)
    tgt = load_asset_or_fail(target, unreal.IKRigDefinition)
    rtg = create_asset(path, unreal.IKRetargeter, unreal.IKRetargetFactory())
    c = unreal.IKRetargeterController.get_controller(rtg)
    c.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, src)
    c.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, tgt)
    if hasattr(c, "add_default_ops") and c.get_num_retarget_ops() == 0:
        c.add_default_ops()
    if hasattr(c, "assign_ik_rig_to_all_ops"):
        c.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.SOURCE, src)
        c.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.TARGET, tgt)
    c.auto_map_chains(unreal.AutoMapChainType.FUZZY, True)
    mapping = []
    for ch in unreal.IKRigController.get_controller(tgt).get_retarget_chains():
        name = ch.chain_name
        mapping.append({"target": str(name), "source": str(c.get_source_chain(name))})
    _save(rtg)
    return {"path": rtg.get_path_name(), "source": src.get_path_name(), "target": tgt.get_path_name(),
            "mapping": mapping}


@tool("anim_retarget", "anim",
      "Retarget animation assets (sequences, montages, blend spaces) with an IK Retargeter into new assets for the target "
      "mesh: saved in folder (default next to the originals) with suffix (default _Retargeted).",
      {"retargeter": {"type": "string"}, "assets": {"type": "array", "items": {"type": "string"}},
       "folder": {"type": "string"}, "suffix": {"type": "string"}},
      ["retargeter", "assets"], mutating=True, undoable=False)
def anim_retarget(retargeter, assets, folder=None, suffix="_Retargeted"):
    rtg = load_asset_or_fail(retargeter, unreal.IKRetargeter)
    c = unreal.IKRetargeterController.get_controller(rtg)
    src = c.get_ik_rig(unreal.RetargetSourceOrTarget.SOURCE)
    tgt = c.get_ik_rig(unreal.RetargetSourceOrTarget.TARGET)
    if not src or not tgt:
        raise ToolError("anim.retarget_failed", "the retargeter needs a source and a target IK Rig")
    src_mesh = unreal.IKRigController.get_controller(src).get_skeletal_mesh()
    tgt_mesh = unreal.IKRigController.get_controller(tgt).get_skeletal_mesh()
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    data = []
    for a in assets:
        load_asset_or_fail(a, unreal.AnimationAsset)
        data.append(reg.get_asset_by_object_path(a if "." in a.rsplit("/", 1)[-1] else "%s.%s" % (a, a.rsplit("/", 1)[-1])))
    op = unreal.IKRetargetBatchOperation
    copies = []
    try:
        out = op.duplicate_and_retarget(data, src_mesh, tgt_mesh, rtg, "", "", "", suffix or "", folder or "", not folder,
                                        False, False)
    except TypeError:
        # Engines without a target folder argument write next to the inputs: retarget copies in the folder instead.
        if folder:
            for d in data:
                dest = "%s/%s" % (folder, d.asset_name)
                copies.append(unreal.EditorAssetLibrary.duplicate_asset(str(d.package_name), dest))
            data = [reg.get_asset_by_object_path(c.get_path_name()) for c in copies]
        out = op.duplicate_and_retarget(data, src_mesh, tgt_mesh, rtg, "", "", "", suffix or "", False, False)
    created = [str(d.package_name) for d in out]
    for c in copies:
        unreal.EditorAssetLibrary.delete_loaded_asset(c)
    if not created:
        raise ToolError("anim.retarget_failed", "nothing was retargeted",
                        hint="are the assets for the source IK Rig's skeleton? does a result with this suffix already exist?")
    return {"retargeter": rtg.get_path_name(), "created": created, "targetMesh": tgt_mesh.get_path_name()}


@tool("controlrig_create", "anim",
      "Create a Control Rig Blueprint for a skeletal mesh with the mesh's bones imported into its hierarchy and the mesh as "
      "preview. Add controls and the Forward Solve graph in the Control Rig editor or with bp tools.",
      {"path": {"type": "string"}, "mesh": {"type": "string"}, "modular": {"type": "boolean"}},
      ["path", "mesh"], mutating=True, undoable=False)
def controlrig_create(path, mesh, modular=False):
    sk = load_asset_or_fail(mesh, unreal.SkeletalMesh)
    ensure_absent(path)
    split_path(path)
    rig = unreal.ControlRigBlueprintFactory.create_new_control_rig_asset(path, modular)
    if rig is None:
        raise ToolError("asset.create_failed", "could not create a Control Rig at '%s'" % path)
    rig.set_preview_mesh(sk)
    bones = rig.get_hierarchy_controller().import_bones_from_asset(sk.get_path_name(), "None", True, True, False, False)
    _save(rig)
    return {"path": rig.get_path_name(), "mesh": sk.get_path_name(), "bones": len(bones)}



_RIG_TYPES = {"bone": "BONE", "null": "NULL", "control": "CONTROL", "curve": "CURVE"}


def _rig(path):
    rig = load_asset_or_fail(path, unreal.ControlRigBlueprint)
    return rig, rig.get_editor_property("hierarchy"), rig.get_hierarchy_controller()


def _rig_key(hierarchy, name):
    if not name:
        return unreal.RigElementKey()
    for k in hierarchy.get_all_keys():
        if str(k.name) == name and k.type != unreal.RigElementType.CURVE:
            return k
    raise ToolError("controlrig.element_not_found", "no element '%s' in the hierarchy" % name,
                    "names: controlrig_get")


def _rig_element(hierarchy, key):
    parent = hierarchy.get_first_parent(key)
    t = hierarchy.get_global_transform(key) if key.type != unreal.RigElementType.CURVE else None
    out = {"name": str(key.name), "type": str(key.type.name).lower(),
           "parent": str(parent.name) if parent and str(parent.name) != "None" else ""}
    if t is not None:
        out["location"] = [t.translation.x, t.translation.y, t.translation.z]
    return out


def _transform(location):
    t = unreal.Transform()
    if location:
        t.translation = unreal.Vector(*location)
    return t


def _added(rig, hierarchy, key):
    if key is None or str(key.name) == "None":
        raise ToolError("controlrig.add_failed", "the hierarchy controller refused the element (name taken or bad parent)",
                        "use a new name; list elements with controlrig_get")
    _save(rig)
    return _rig_element(hierarchy, key)


@tool("controlrig_get", "anim",
      "A Control Rig's hierarchy: elements (name, type bone|null|control|curve, parent, global location). "
      "type filters to one element type.",
      {"path": {"type": "string"}, "type": {"type": "string", "enum": list(_RIG_TYPES)}},
      ["path"])
def controlrig_get(path, type=None):
    rig, hierarchy, _ = _rig(path)
    keys = hierarchy.get_all_keys()
    if type:
        keys = [k for k in keys if k.type == getattr(unreal.RigElementType, _RIG_TYPES[type])]
    elements = [_rig_element(hierarchy, k) for k in keys]
    counts = {}
    for e in elements:
        counts[e["type"]] = counts.get(e["type"], 0) + 1
    return {"path": rig.get_path_name(), "counts": counts, "elements": elements}


@tool("controlrig_add_bone", "anim",
      "Add a bone to a Control Rig's hierarchy under parent (an element name; empty = top level) at location "
      "(global unless local=true).",
      {"path": {"type": "string"}, "name": {"type": "string"}, "parent": {"type": "string"},
       "location": {"type": "array"}, "local": {"type": "boolean"}},
      ["path", "name"], mutating=True, undoable=False)
def controlrig_add_bone(path, name, parent="", location=None, local=False):
    rig, hierarchy, ctrl = _rig(path)
    key = ctrl.add_bone(name, _rig_key(hierarchy, parent), _transform(location), not local,
                        unreal.RigBoneType.USER, True, False)
    return _added(rig, hierarchy, key)


@tool("controlrig_add_null", "anim",
      "Add a null (a space / group transform) to a Control Rig's hierarchy under parent (element name; empty = top level).",
      {"path": {"type": "string"}, "name": {"type": "string"}, "parent": {"type": "string"},
       "location": {"type": "array"}, "local": {"type": "boolean"}},
      ["path", "name"], mutating=True, undoable=False)
def controlrig_add_null(path, name, parent="", location=None, local=False):
    rig, hierarchy, ctrl = _rig(path)
    key = ctrl.add_null(name, _rig_key(hierarchy, parent), _transform(location), not local, True, False)
    return _added(rig, hierarchy, key)


_CONTROL_TYPES = {"transform": "EULER_TRANSFORM", "float": "FLOAT", "bool": "BOOL", "position": "POSITION",
                  "rotator": "ROTATOR", "scale": "SCALE", "vector2d": "VECTOR2D", "integer": "INTEGER"}


@tool("controlrig_add_control", "anim",
      "Add an animator control to a Control Rig's hierarchy under parent (element name; empty = top level). "
      "controlType: transform (default), float, bool, position, rotator, scale, vector2d, integer; shape: a shape name "
      "from the rig's shape library (e.g. Circle_Thick, Box_Thin); color [r,g,b] 0-1; location is the offset.",
      {"path": {"type": "string"}, "name": {"type": "string"}, "parent": {"type": "string"},
       "controlType": {"type": "string", "enum": list(_CONTROL_TYPES)}, "shape": {"type": "string"},
       "color": {"type": "array"}, "location": {"type": "array"}},
      ["path", "name"], mutating=True, undoable=False)
def controlrig_add_control(path, name, parent="", controlType="transform", shape=None, color=None, location=None):
    rig, hierarchy, ctrl = _rig(path)
    settings = unreal.RigControlSettings()
    settings.control_type = getattr(unreal.RigControlType, _CONTROL_TYPES[controlType])
    if shape:
        settings.shape_name = shape
    if color:
        settings.shape_color = unreal.LinearColor(color[0], color[1], color[2], 1.0)
    key = ctrl.add_control(name, _rig_key(hierarchy, parent), settings, unreal.RigControlValue(), True, False)
    if location and key is not None and str(key.name) != "None":
        hierarchy.set_control_offset_transform(key, _transform(location), True)
    return _added(rig, hierarchy, key)
