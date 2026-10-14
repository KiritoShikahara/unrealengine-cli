import unreal

from .. import ToolError, tool
from ..util import actor_json, actors, editor_world, find_actor, load_class, rot, vec

V3 = {"type": "array", "items": {"type": "number"}}


def _les():
    return unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)


def _eas():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


@tool("level_new", "level", "Create a new level (empty, or from a template map) and open it.",
      {"path": {"type": "string"}, "template": {"type": "string"}}, ["path"], mutating=True, undoable=False)
def level_new(path, template=None):
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        raise ToolError("asset.exists", "'%s' already exists" % path)
    ok = _les().new_level_from_template(path, template) if template else _les().new_level(path)
    if not ok:
        raise ToolError("level.create_failed", "could not create level '%s'" % path)
    return {"path": path}


@tool("level_open", "level", "Open a level in the editor.", {"path": {"type": "string"}}, ["path"],
      mutating=True, undoable=False)
def level_open(path):
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        raise ToolError("asset.not_found", "no level at '%s'" % path)
    if not _les().load_level(path):
        raise ToolError("level.open_failed", "could not open '%s'" % path)
    return {"path": path}


@tool("level_save", "level", "Save the current level (all=true saves every dirty level).",
      {"all": {"type": "boolean"}}, mutating=True, undoable=False)
def level_save(all=False):  # noqa: A002 - matches the tool argument name
    ok = _les().save_all_dirty_levels() if all else _les().save_current_level()
    if not ok:
        raise ToolError("level.save_failed", "the level could not be saved",
                        hint="an untitled level needs level_new with a path first")
    world = editor_world()
    return {"saved": world.get_path_name() if world else None}


def _class_matcher(class_filter, names):
    """Exact class name (with or without the Blueprint _C suffix) when one exists, else a substring match."""
    if not class_filter:
        return lambda n: True
    f = class_filter.lower()
    exact = {f, f + "_c"}
    if any(n.lower() in exact for n in names):
        return lambda n: n.lower() in exact
    return lambda n: f in n.lower()


@tool("actor_list", "level", "List actors in the open level, filtered by class (an exact class name wins, else substring) and/or label substring.",
      {"classFilter": {"type": "string"}, "nameFilter": {"type": "string"}, "limit": {"type": "integer"}})
def actor_list(classFilter=None, nameFilter=None, limit=500):
    out = []
    all_actors = actors()
    matches = _class_matcher(classFilter, [a.get_class().get_name() for a in all_actors])
    for a in all_actors:
        if not matches(a.get_class().get_name()):
            continue
        if nameFilter and nameFilter.lower() not in a.get_actor_label().lower():
            continue
        out.append(actor_json(a))
        if len(out) >= limit:
            break
    return out


@tool("actor_find", "level", "Get one actor by label, with its components.", {"label": {"type": "string"}}, ["label"])
def actor_find(label):
    a = find_actor(label)
    out = actor_json(a)
    out["components"] = [{"name": c.get_name(), "class": c.get_class().get_name()}
                         for c in a.get_components_by_class(unreal.ActorComponent)]
    out["tags"] = [str(t) for t in a.tags]
    return out


def _null_rhi():
    return "-nullrhi" in unreal.SystemLibrary.get_command_line().lower()


# Native classes known to spawn safely without a renderer.
_NULL_RHI_SAFE = {"Actor", "PointLight", "SpotLight", "DirectionalLight", "RectLight", "SkyLight", "CameraActor",
                  "TriggerBox", "TriggerSphere", "TargetPoint", "PlayerStart", "Note", "ExponentialHeightFog",
                  "PostProcessVolume", "BlockingVolume"}


@tool("actor_spawn", "level", "Spawn an actor from a class name, Blueprint path or asset (mesh, etc.).",
      {"actorClass": {"type": "string"}, "location": V3, "rotation": V3, "label": {"type": "string"}},
      ["actorClass"], mutating=True)
def actor_spawn(actorClass, location=None, rotation=None, label=None):
    short = actorClass[1:] if len(actorClass) > 1 and actorClass[0] == "A" and actorClass[1].isupper() else actorClass
    if _null_rhi() and (actorClass.startswith("/") or short not in _NULL_RHI_SAFE):
        raise ToolError("actor.unsafe_without_renderer",
                        "spawning '%s' in an editor started with -nullrhi crashes the engine" % actorClass,
                        hint="reopen the editor with rendering: unrealengine-cli editor close && unrealengine-cli editor open --headless")
    loc, r = vec(location), rot(rotation)
    actor = None
    if actorClass.startswith("/"):
        asset = unreal.load_asset(actorClass)
        if asset is not None and not isinstance(asset, (unreal.Blueprint, unreal.Class)):
            actor = _eas().spawn_actor_from_object(asset, loc, r)
    if actor is None:
        actor = _eas().spawn_actor_from_class(load_class(actorClass), loc, r)
    if actor is None:
        raise ToolError("actor.spawn_failed", "could not spawn '%s'" % actorClass)
    if label:
        actor.set_actor_label(label)
    return actor_json(actor)


@tool("actor_destroy", "level", "Delete actors by label.", {"labels": {"type": "array", "items": {"type": "string"}}},
      ["labels"], mutating=True)
def actor_destroy(labels):
    removed = []
    for label in labels:
        a = find_actor(label)
        if _eas().destroy_actor(a):
            removed.append(label)
    return {"destroyed": removed}


@tool("actor_transform", "level", "Set any of location / rotation [pitch,yaw,roll] / scale of an actor.",
      {"label": {"type": "string"}, "location": V3, "rotation": V3, "scale": V3}, ["label"], mutating=True)
def actor_transform(label, location=None, rotation=None, scale=None):
    a = find_actor(label)
    a.modify()
    if location is not None:
        a.set_actor_location(vec(location), False, True)
    if rotation is not None:
        a.set_actor_rotation(rot(rotation), True)
    if scale is not None:
        a.set_actor_scale3d(vec(scale))
    return actor_json(a)


@tool("actor_attach", "level", "Attach an actor to a parent actor (keeps world transform).",
      {"label": {"type": "string"}, "parent": {"type": "string"}, "socket": {"type": "string"}},
      ["label", "parent"], mutating=True)
def actor_attach(label, parent, socket=""):
    child, par = find_actor(label), find_actor(parent)
    rule = unreal.AttachmentRule.KEEP_WORLD
    child.attach_to_actor(par, socket or "", rule, rule, rule, False)
    return {"label": label, "parent": par.get_actor_label()}


@tool("actor_duplicate", "level", "Duplicate an actor with an optional offset.",
      {"label": {"type": "string"}, "offset": V3, "newLabel": {"type": "string"}}, ["label"], mutating=True)
def actor_duplicate(label, offset=None, newLabel=None):
    a = find_actor(label)
    dup = _eas().duplicate_actor(a, editor_world(), vec(offset))
    if dup is None:
        raise ToolError("actor.duplicate_failed", "could not duplicate '%s'" % label)
    if newLabel:
        dup.set_actor_label(newLabel)
    return actor_json(dup)


@tool("level_info", "level", "Path of the open level and actor count by class.")
def level_info():
    world = editor_world()
    counts = {}
    for a in actors():
        n = a.get_class().get_name()
        counts[n] = counts.get(n, 0) + 1
    return {"path": world.get_path_name() if world else None, "actors": sum(counts.values()), "byClass": counts}
