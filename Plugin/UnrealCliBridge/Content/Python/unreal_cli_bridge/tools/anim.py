import unreal

from .. import tool
from ..util import create_asset, load_asset_or_fail


@tool("animbp_create", "anim", "Create an Animation Blueprint for a skeleton.",
      {"path": {"type": "string"}, "skeleton": {"type": "string"}}, ["path", "skeleton"], mutating=True, undoable=False)
def animbp_create(path, skeleton):
    f = unreal.AnimBlueprintFactory()
    f.set_editor_property("target_skeleton", load_asset_or_fail(skeleton, unreal.Skeleton))
    f.set_editor_property("parent_class", unreal.AnimInstance)
    bp = create_asset(path, unreal.AnimBlueprint, f)
    return {"path": bp.get_path_name()}


@tool("montage_create", "anim", "Create an Anim Montage from an Anim Sequence.",
      {"path": {"type": "string"}, "sequence": {"type": "string"}}, ["path", "sequence"], mutating=True, undoable=False)
def montage_create(path, sequence):
    seq = load_asset_or_fail(sequence, unreal.AnimSequence)
    f = unreal.AnimMontageFactory()
    f.set_editor_property("source_animation", seq)
    f.set_editor_property("target_skeleton", seq.get_editor_property("skeleton"))
    m = create_asset(path, unreal.AnimMontage, f)
    return {"path": m.get_path_name(), "length": m.get_play_length()}


@tool("blendspace_create", "anim", "Create a 2D Blend Space for a skeleton.",
      {"path": {"type": "string"}, "skeleton": {"type": "string"}}, ["path", "skeleton"], mutating=True, undoable=False)
def blendspace_create(path, skeleton):
    f = unreal.BlendSpaceFactoryNew()
    f.set_editor_property("target_skeleton", load_asset_or_fail(skeleton, unreal.Skeleton))
    bs = create_asset(path, unreal.BlendSpace, f)
    return {"path": bs.get_path_name()}
