"""Conversions between JSON values and unreal types, and object lookup helpers."""
import difflib

import unreal

from . import ToolError


def vec(v, default=None):
    if v is None:
        return default if default is not None else unreal.Vector(0, 0, 0)
    if isinstance(v, dict):
        return unreal.Vector(float(v.get("x", 0)), float(v.get("y", 0)), float(v.get("z", 0)))
    if len(v) != 3:
        raise ToolError("args.invalid", "a vector needs 3 numbers [x, y, z]")
    return unreal.Vector(float(v[0]), float(v[1]), float(v[2]))


def rot(v, default=None):
    if v is None:
        return default if default is not None else unreal.Rotator(0, 0, 0)
    if isinstance(v, dict):
        return unreal.Rotator(roll=float(v.get("roll", 0)), pitch=float(v.get("pitch", 0)), yaw=float(v.get("yaw", 0)))
    if len(v) != 3:
        raise ToolError("args.invalid", "a rotation needs 3 numbers [pitch, yaw, roll]")
    return unreal.Rotator(roll=float(v[2]), pitch=float(v[0]), yaw=float(v[1]))


def to_json(value, depth=0):
    """Best-effort conversion of an unreal value to JSON."""
    if depth > 6:
        return str(value)
    if value is None or isinstance(value, (bool, int, float, str)):
        return value
    if isinstance(value, (unreal.Name, unreal.Text)):
        return str(value)
    if isinstance(value, unreal.Vector):
        return {"x": value.x, "y": value.y, "z": value.z}
    if isinstance(value, unreal.Vector2D):
        return {"x": value.x, "y": value.y}
    if isinstance(value, unreal.Rotator):
        return {"pitch": value.pitch, "yaw": value.yaw, "roll": value.roll}
    if isinstance(value, unreal.LinearColor):
        return {"r": value.r, "g": value.g, "b": value.b, "a": value.a}
    if isinstance(value, unreal.Color):
        return {"r": value.r, "g": value.g, "b": value.b, "a": value.a}
    if isinstance(value, unreal.Transform):
        return {"location": to_json(value.translation), "rotation": to_json(value.rotation.rotator()),
                "scale": to_json(value.scale3d)}
    if isinstance(value, unreal.EnumBase):
        return value.name
    if isinstance(value, unreal.Object):
        return value.get_path_name()
    if isinstance(value, (list, tuple, unreal.Array, unreal.Set)):
        return [to_json(v, depth + 1) for v in value]
    if isinstance(value, unreal.Map):
        return {str(k): to_json(v, depth + 1) for k, v in value.items()}
    if isinstance(value, unreal.StructBase):
        out = {}
        for name in public_props(value):
            try:
                out[name] = to_json(value.get_editor_property(name), depth + 1)
            except Exception:  # noqa: BLE001 - skip unreadable members
                pass
        return out
    return str(value)


def public_props(obj):
    """Names of readable editor properties (from the reflected docs of the Python wrapper)."""
    names = []
    for name in dir(obj):
        if name.startswith("_"):
            continue
        try:
            attr = getattr(type(obj), name, None)
        except Exception:  # noqa: BLE001
            continue
        if attr is not None and not callable(attr) and type(attr).__name__ == "getset_descriptor":
            names.append(name)
    return names


def from_json(value, current):
    """Converts a JSON value to the type of `current` (the property's present value)."""
    if isinstance(current, bool):
        if isinstance(value, str):
            return value.lower() in ("true", "1", "yes")
        return bool(value)
    if isinstance(current, int) and not isinstance(current, unreal.EnumBase):
        return int(value)
    if isinstance(current, float):
        return float(value)
    if isinstance(current, unreal.Name):
        return unreal.Name(str(value))
    if isinstance(current, unreal.Text):
        return unreal.Text(str(value))
    if isinstance(current, str):
        return str(value)
    if isinstance(current, unreal.Vector):
        return vec(value)
    if isinstance(current, unreal.Rotator):
        return rot(value)
    if isinstance(current, unreal.LinearColor):
        v = value if isinstance(value, dict) else dict(zip("rgba", value))
        return unreal.LinearColor(float(v.get("r", 0)), float(v.get("g", 0)), float(v.get("b", 0)), float(v.get("a", 1)))
    if isinstance(current, unreal.Color):
        v = value if isinstance(value, dict) else dict(zip("rgba", value))
        return unreal.Color(int(v.get("r", 0)), int(v.get("g", 0)), int(v.get("b", 0)), int(v.get("a", 255)))
    if isinstance(current, unreal.EnumBase):
        enum_type = type(current)
        members = [m for m in dir(enum_type) if m.isupper() and isinstance(getattr(enum_type, m), enum_type)]
        name = str(value).split("::")[-1].upper()
        flat = name.replace("_", "")
        for member in members:
            if member == name or member.endswith("_" + name) or member.replace("_", "") == flat:
                return getattr(enum_type, member)
        raise ToolError("prop.bad_value", "'%s' is not a %s value" % (value, enum_type.__name__),
                        "values: " + ", ".join(members))
    if isinstance(current, unreal.StructBase) and isinstance(value, dict):
        for k, v in value.items():
            current.set_editor_property(k, from_json(v, current.get_editor_property(k)))
        return current
    if isinstance(current, (unreal.Array, list)) and isinstance(value, list):
        sample = current[0] if len(current) else None
        return [array_element(v, sample) for v in value]
    if (current is None or isinstance(current, unreal.Object)) and (value is None or isinstance(value, str)):
        if value in (None, "", "None"):
            return None
        obj = unreal.load_object(None, value) or unreal.load_asset(value)
        if obj is None:
            raise ToolError("prop.bad_value", "no object at '%s'" % value)
        return obj
    return value


def array_element(value, sample=None):
    """One array element from JSON: '/Game/...' strings become assets (e.g. materials), others follow the sample."""
    if isinstance(value, str) and value.startswith("/"):
        obj = unreal.load_asset(value) or unreal.load_object(None, value)
        if obj is None:
            raise ToolError("prop.bad_value", "no asset at '%s'" % value)
        return obj
    return from_json(value, sample) if sample is not None else value


def editor_world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()


def actors():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()


def find_actor(label):
    if label == "WorldSettings":
        # The level's World Settings (game mode override, gravity, kill Z...) is not a listed actor.
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        return world.get_world_settings()
    for a in actors():
        if a.get_actor_label() == label or a.get_name() == label:
            return a
    labels = [a.get_actor_label() for a in actors()]
    close = difflib.get_close_matches(label, labels, n=5)
    raise ToolError("actor.not_found", "no actor labelled '%s'" % label,
                    hint=("did you mean: " + ", ".join(close)) if close else "list actors: actor_list")


def load_class(name):
    """Class from a short name (Actor, StaticMeshActor), a /Script path or a Blueprint asset path."""
    if not name:
        raise ToolError("args.invalid", "a class name is required")
    if name.startswith("/"):
        asset = unreal.load_asset(name)
        if isinstance(asset, unreal.Blueprint):
            return asset.generated_class()
        if isinstance(asset, unreal.Class):
            return asset
        cls = unreal.load_class(None, name)
        if cls:
            return cls
        raise ToolError("class.not_found", "no class or Blueprint at '%s'" % name)
    for candidate in (name, name[1:] if len(name) > 1 and name[0] in "AU" and name[1].isupper() else None):
        if not candidate:
            continue
        py = getattr(unreal, candidate, None)
        if isinstance(py, type) and issubclass(py, unreal.Object):
            return py.static_class()
        for module in ("Engine", "CoreUObject", "UMG", "Niagara", "EnhancedInput", "Landscape"):
            cls = unreal.load_class(None, "/Script/%s.%s" % (module, candidate))
            if cls:
                return cls
    raise ToolError("class.not_found", "unknown class '%s'" % name, hint="use a /Script/Module.Class or Blueprint path")


def load_asset_or_fail(path, kind=None):
    asset = unreal.load_asset(path) if path else None
    if asset is None:
        raise ToolError("asset.not_found", "no asset at '%s'" % path, hint="search: asset_search")
    if kind is not None and not isinstance(asset, kind):
        raise ToolError("asset.wrong_type", "'%s' is a %s, not a %s" % (path, type(asset).__name__, kind.__name__))
    return asset


def split_path(path):
    if "/" not in path.strip("/"):
        raise ToolError("args.invalid", "'%s' must be a package path like /Game/Folder/Name" % path)
    folder, name = path.rsplit("/", 1)
    return folder, name.split(".")[0]


def ensure_absent(path):
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        raise ToolError("asset.exists", "'%s' already exists" % path)


def create_asset(path, asset_class, factory):
    ensure_absent(path)
    folder, name = split_path(path)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, asset_class, factory)
    if asset is None:
        raise ToolError("asset.create_failed", "could not create %s at '%s'" % (asset_class.get_name(), path))
    return asset


def actor_json(a):
    return {
        "label": a.get_actor_label(),
        "name": a.get_name(),
        "class": a.get_class().get_name(),
        "location": to_json(a.get_actor_location()),
        "rotation": to_json(a.get_actor_rotation()),
        "scale": to_json(a.get_actor_scale3d()),
        "path": a.get_path_name(),
    }
