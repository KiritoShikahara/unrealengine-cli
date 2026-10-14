import difflib
import re

import unreal

from .. import ToolError, tool
from ..util import find_actor, from_json, load_class, public_props, to_json

_SEG = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)(?:\[(\d+)\])?$")


def _target(path):
    """Object for an asset path (Blueprints give class defaults), an object path, or 'Label[.Component]'."""
    if path.startswith("/"):
        obj = unreal.load_asset(path) or unreal.load_object(None, path)
        if obj is None and "." in path.rsplit("/", 1)[-1]:
            base, comp = path.rsplit(".", 1)
            bp = unreal.load_asset(base)
            if isinstance(bp, unreal.Blueprint):
                return _bp_component(bp, comp)
        if obj is None:
            raise ToolError("asset.not_found", "no asset or object at '%s'" % path)
        if isinstance(obj, unreal.Blueprint):
            return unreal.get_default_object(obj.generated_class())
        return obj
    label, _, comp = path.partition(".")
    actor = find_actor(label)
    if not comp:
        return actor
    for c in actor.get_components_by_class(unreal.ActorComponent):
        if c.get_name() == comp:
            return c
    names = [c.get_name() for c in actor.get_components_by_class(unreal.ActorComponent)]
    raise ToolError("component.not_found", "actor '%s' has no component '%s'" % (label, comp),
                    hint="components: " + ", ".join(names))


def _snake(name):
    return re.sub(r"(?<!^)(?=[A-Z])", "_", name).lower() if any(ch.isupper() for ch in name) else name


def _get(obj, name):
    for candidate in (name, _snake(name)):
        try:
            return candidate, obj.get_editor_property(candidate)
        except Exception:  # noqa: BLE001 - try the next spelling
            continue
    close = difflib.get_close_matches(_snake(name), public_props(obj), n=5)
    raise ToolError("prop.not_found", "%s has no property '%s'" % (type(obj).__name__, name),
                    hint=("did you mean: " + ", ".join(close)) if close else "list them: prop_get without property")


def _walk(obj, dotted):
    """Returns [(container, name, index, value)] for each segment of a dotted path."""
    chain = []
    current = obj
    for seg in dotted.split("."):
        m = _SEG.match(seg)
        if not m:
            raise ToolError("args.invalid", "bad property path segment '%s'" % seg)
        name, index = m.group(1), m.group(2)
        real, value = _get(current, name)
        if index is not None:
            i = int(index)
            if i >= len(value):
                raise ToolError("prop.bad_index", "%s has %d element(s)" % (name, len(value)))
            chain.append((current, real, i, value))
            value = value[i]
        else:
            chain.append((current, real, None, value))
        current = value
    return chain, current


@tool("prop_get", "property", "Read reflected properties of an asset, object, Blueprint defaults ('/Game/BP'), a Blueprint "
      "component ('/Game/BP.Component'), an actor ('Label' or 'Label.Component') or 'WorldSettings'. Without property, "
      "lists readable properties.",
      {"path": {"type": "string"}, "property": {"type": "string"}, "filter": {"type": "string"}}, ["path"])
def prop_get(path, property=None, filter=None):  # noqa: A002 - tool argument names
    obj = _target(path)
    if property:
        _, value = _walk(obj, property)
        return {"path": path, "property": property, "value": to_json(value)}
    out = {}
    for name in public_props(obj):
        if filter and filter.lower() not in name.lower():
            continue
        try:
            out[name] = to_json(obj.get_editor_property(name))
        except Exception:  # noqa: BLE001 - not every attribute is an editor property
            continue
    return {"path": path, "class": obj.get_class().get_name(), "properties": out}


@tool("prop_set", "property", "Set one property; property may be dotted (Settings.Speed) with array indices "
      "(Tags[0]). value is JSON: numbers, strings, [x,y,z], {r,g,b,a}, object paths, enum names.",
      {"path": {"type": "string"}, "property": {"type": "string"}, "value": {}}, ["path", "property"],
      mutating=True)
def prop_set(path, property, value=None):  # noqa: A002
    obj = _target(path)
    obj.modify()
    chain, current = _walk(obj, property)
    new = from_json(value, current)
    # Write back from the leaf to the root: struct and array values are copies.
    for container, name, index, held in reversed(chain):
        if index is not None:
            held[index] = new
            new = held
        container.set_editor_property(name, new)
        new = container
    _, after = _walk(obj, property)
    return {"path": path, "property": property, "value": to_json(after)}


@tool("prop_array_edit", "property", "Edit an array property: op = append | insert | remove | clear.",
      {"path": {"type": "string"}, "property": {"type": "string"},
       "op": {"type": "string", "enum": ["append", "insert", "remove", "clear"]},
       "index": {"type": "integer"}, "value": {}}, ["path", "property", "op"], mutating=True)
def prop_array_edit(path, property, op, index=None, value=None):  # noqa: A002
    obj = _target(path)
    obj.modify()
    real, arr = _get(obj, property)
    items = list(arr)
    sample = items[0] if items else None
    if op == "append":
        items.append(from_json(value, sample) if sample is not None else value)
    elif op == "insert":
        items.insert(index or 0, from_json(value, sample) if sample is not None else value)
    elif op == "remove":
        if index is None or index >= len(items):
            raise ToolError("prop.bad_index", "index out of range (%d element(s))" % len(items))
        items.pop(index)
    else:
        items = []
    obj.set_editor_property(real, items)
    return {"path": path, "property": property, "value": to_json(obj.get_editor_property(real))}


def _sds():
    return unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)


@tool("component_list", "property", "Components of a level actor.", {"label": {"type": "string"}}, ["label"])
def component_list(label):
    actor = find_actor(label)
    return [{"name": c.get_name(), "class": c.get_class().get_name(),
             "attachParent": c.get_attach_parent().get_name() if isinstance(c, unreal.SceneComponent) and c.get_attach_parent() else None}
            for c in actor.get_components_by_class(unreal.ActorComponent)]


@tool("component_add", "property", "Add a component to a level actor instance.",
      {"label": {"type": "string"}, "class": {"type": "string"}, "name": {"type": "string"}}, ["label", "class"],
      mutating=True)
def component_add(label, name=None, **kw):
    actor = find_actor(label)
    cls = load_class(kw["class"])
    handles = _sds().k2_gather_subobject_data_for_instance(actor)
    params = unreal.AddNewSubobjectParams(parent_handle=handles[0], new_class=cls)
    handle, reason = _sds().add_new_subobject(params)
    if not unreal.SubobjectDataBlueprintFunctionLibrary.is_handle_valid(handle):
        raise ToolError("component.add_failed", str(reason) or "could not add the component")
    if name:
        _sds().rename_subobject(handle, unreal.Text(name))
    data = unreal.SubobjectDataBlueprintFunctionLibrary.get_data(handle)
    comp = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(data)
    return {"label": label, "name": comp.get_name() if comp else name, "class": cls.get_name()}


@tool("component_remove", "property", "Remove a component from a level actor instance.",
      {"label": {"type": "string"}, "name": {"type": "string"}}, ["label", "name"], mutating=True)
def component_remove(label, name):
    actor = find_actor(label)
    handles = _sds().k2_gather_subobject_data_for_instance(actor)
    lib = unreal.SubobjectDataBlueprintFunctionLibrary
    for h in handles[1:]:
        obj = lib.get_object(lib.get_data(h))
        if obj and obj.get_name() == name:
            count = _sds().delete_subobjects(handles[0], [h], None)
            if not count:
                raise ToolError("component.remove_failed", "'%s' cannot be removed (inherited?)" % name)
            return {"label": label, "removed": name}
    raise ToolError("component.not_found", "actor '%s' has no component '%s'" % (label, name))


def _bp_component(bp, name):
    """Component template of a Blueprint: one added in the Blueprint (SCS) or inherited from its parent class."""
    lib = unreal.SubobjectDataBlueprintFunctionLibrary
    names = []
    for h in _sds().k2_gather_subobject_data_for_blueprint(bp):
        data = lib.get_data(h)
        obj = lib.get_object(data)
        var = str(lib.get_variable_name(data))
        if obj is None or var in ("", "None"):
            continue
        if var == name or obj.get_name() in (name, name + "_GEN_VARIABLE"):
            return obj
        names.append(var)
    cdo = unreal.get_default_object(bp.generated_class())
    if isinstance(cdo, unreal.Actor):
        for c in cdo.get_components_by_class(unreal.ActorComponent):
            if c.get_name() == name:
                return c
            names.append(c.get_name())
    raise ToolError("component.not_found", "Blueprint '%s' has no component '%s'" % (bp.get_name(), name),
                    hint="components: " + ", ".join(sorted(set(names))) + " (compile after bp_add_component)")
