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
    out = unreal.IKRetargetBatchOperation.duplicate_and_retarget(
        data, src_mesh, tgt_mesh, rtg, "", "", "", suffix or "", folder or "", not folder, False, False)
    created = [str(d.package_name) for d in out]
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

