import os

import unreal

from .. import ToolError, tool
from ..util import create_asset, ensure_absent, load_class


def _registry():
    return unreal.AssetRegistryHelpers.get_asset_registry()


def _data_json(d):
    return {"path": str(d.package_name), "name": str(d.asset_name), "class": str(d.asset_class_path.asset_name)}


@tool("asset_search", "asset", "Search assets by name substring, optionally by class and path prefix.",
      {"query": {"type": "string"}, "class": {"type": "string"}, "pathPrefix": {"type": "string"},
       "limit": {"type": "integer"}})
def asset_search(query="", pathPrefix="/Game", limit=100, **kw):
    cls = kw.get("class")
    flt = unreal.ARFilter(package_paths=[pathPrefix.rstrip("/") or "/"], recursive_paths=True)
    out = []
    for d in _registry().get_assets(flt):
        if query and query.lower() not in str(d.asset_name).lower():
            continue
        if cls and cls.lower() != str(d.asset_class_path.asset_name).lower():
            continue
        out.append(_data_json(d))
        if len(out) >= limit:
            break
    return out


FACTORIES = {
    "Material": ("Material", "MaterialFactoryNew"),
    "MaterialInstanceConstant": ("MaterialInstanceConstant", "MaterialInstanceConstantFactoryNew"),
    "MaterialFunction": ("MaterialFunction", "MaterialFunctionFactoryNew"),
    "Blueprint": ("Blueprint", "BlueprintFactory"),
    "WidgetBlueprint": ("WidgetBlueprint", "WidgetBlueprintFactory"),
    "NiagaraSystem": ("NiagaraSystem", "NiagaraSystemFactoryNew"),
    "SoundCue": ("SoundCue", "SoundCueFactoryNew"),
    "LevelSequence": ("LevelSequence", "LevelSequenceFactoryNew"),
    "InputAction": ("InputAction", None),
    "InputMappingContext": ("InputMappingContext", None),
    "CurveFloat": ("CurveFloat", None),
    "World": ("World", "WorldFactory"),
}


@tool("asset_create", "asset", "Create an asset of a class (Material, MaterialInstanceConstant, Blueprint, "
      "WidgetBlueprint, NiagaraSystem, SoundCue, LevelSequence, InputAction, InputMappingContext, CurveFloat, World, "
      "or any UObject class creatable without a factory).",
      {"path": {"type": "string"}, "class": {"type": "string"}}, ["path", "class"], mutating=True, undoable=False)
def asset_create(path, **kw):
    name = kw["class"]
    cls_name, factory_name = FACTORIES.get(name, (name, None))
    cls = load_class(cls_name)
    factory = None
    if factory_name:
        factory = getattr(unreal, factory_name)()
        if factory_name == "BlueprintFactory":
            factory.set_editor_property("parent_class", unreal.Actor)
    else:
        factory = unreal.DataAssetFactory() if issubclass_safe(cls, "DataAsset") else None
        if factory is not None:
            factory.set_editor_property("data_asset_class", cls)
    asset = create_asset(path, cls, factory)
    return {"path": asset.get_path_name(), "class": asset.get_class().get_name()}


def issubclass_safe(cls, base_name):
    base = unreal.load_class(None, "/Script/Engine." + base_name)
    try:
        return bool(base) and unreal.MathLibrary.class_is_child_of(cls, base)
    except Exception:  # noqa: BLE001
        return False


@tool("asset_duplicate", "asset", "Duplicate an asset to a new path.",
      {"path": {"type": "string"}, "newPath": {"type": "string"}}, ["path", "newPath"], mutating=True, undoable=False)
def asset_duplicate(path, newPath):
    ensure_absent(newPath)
    if unreal.EditorAssetLibrary.duplicate_asset(path, newPath) is None:
        raise ToolError("asset.duplicate_failed", "could not duplicate '%s'" % path, hint="check the source exists")
    return {"path": newPath}


@tool("asset_rename", "asset", "Rename or move an asset (references are fixed up).",
      {"path": {"type": "string"}, "newPath": {"type": "string"}}, ["path", "newPath"], mutating=True, undoable=False)
def asset_rename(path, newPath):
    ensure_absent(newPath)
    if not unreal.EditorAssetLibrary.rename_asset(path, newPath):
        raise ToolError("asset.rename_failed", "could not move '%s' to '%s'" % (path, newPath))
    return {"path": newPath}


@tool("asset_delete", "asset", "Delete an asset. Refused while other assets reference it unless force=true.",
      {"path": {"type": "string"}, "force": {"type": "boolean"}}, ["path"], mutating=True, undoable=False)
def asset_delete(path, force=False):
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        raise ToolError("asset.not_found", "no asset at '%s'" % path)
    refs = [str(r) for r in unreal.EditorAssetLibrary.find_package_referencers_for_asset(path, False)]
    if refs and not force:
        raise ToolError("asset.referenced", "%d asset(s) reference '%s'" % (len(refs), path),
                        hint="pass force=true to delete anyway", details=refs)
    if not unreal.EditorAssetLibrary.delete_asset(path):
        raise ToolError("asset.delete_failed", "could not delete '%s'" % path)
    return {"deleted": path, "referencers": refs}


@tool("asset_import", "asset", "Import files (fbx, png, wav, ...) into a content folder.",
      {"files": {"type": "array", "items": {"type": "string"}}, "destination": {"type": "string"},
       "replace": {"type": "boolean"}}, ["files", "destination"], mutating=True, undoable=False)
def asset_import(files, destination, replace=False):
    tasks = []
    for f in files:
        if not os.path.isfile(f):
            raise ToolError("args.invalid", "file not found: %s" % f)
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", f)
        t.set_editor_property("destination_path", destination)
        t.set_editor_property("automated", True)
        t.set_editor_property("save", True)
        t.set_editor_property("replace_existing", replace)
        tasks.append(t)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    imported = [str(p) for t in tasks for p in t.get_editor_property("imported_object_paths")]
    if not imported:
        raise ToolError("asset.import_failed", "nothing was imported", hint="check the log: unreal-cli logs --severity warning")
    return {"imported": imported}


@tool("asset_save", "asset", "Save assets (one path, or every dirty asset under a folder).",
      {"path": {"type": "string"}, "folder": {"type": "string"}}, mutating=True, undoable=False)
def asset_save(path=None, folder=None):
    if path:
        if not unreal.EditorAssetLibrary.save_asset(path, False):
            raise ToolError("asset.save_failed", "could not save '%s'" % path)
        return {"saved": [path]}
    if folder:
        ok = unreal.EditorAssetLibrary.save_directory(folder, True, True)
        return {"saved": folder, "ok": bool(ok)}
    raise ToolError("args.invalid", "pass path or folder")


def _deps(package, referencers):
    opts = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True,
                                                 include_hard_package_references=True)
    reg = _registry()
    res = reg.get_referencers(package, opts) if referencers else reg.get_dependencies(package, opts)
    return sorted({str(p) for p in (res or [])})


@tool("asset_references", "asset", "Packages that reference this asset.", {"path": {"type": "string"}}, ["path"])
def asset_references(path):
    return {"path": path, "referencers": _deps(path.split(".")[0], True)}


@tool("asset_dependencies", "asset", "Packages this asset depends on.", {"path": {"type": "string"}}, ["path"])
def asset_dependencies(path):
    return {"path": path, "dependencies": _deps(path.split(".")[0], False)}


@tool("asset_list", "asset", "List assets in a content folder.",
      {"folder": {"type": "string"}, "recursive": {"type": "boolean"}}, ["folder"])
def asset_list(folder, recursive=True):
    return [str(p).split(".")[0] for p in unreal.EditorAssetLibrary.list_assets(folder, recursive, False)]
