import unreal

from .. import ToolError, tool
from ..util import create_asset, from_json, load_asset_or_fail, to_json

MEL = unreal.MaterialEditingLibrary


def _expr_class(name):
    short = name if name.startswith("MaterialExpression") else "MaterialExpression" + name
    cls = getattr(unreal, short, None)
    if cls is None:
        raise ToolError("material.bad_expression", "unknown material expression '%s'" % name,
                        hint="examples: Constant, Constant3Vector, ScalarParameter, VectorParameter, TextureSample, Multiply, Add, Lerp")
    return cls


def _expr(material, name):
    e = unreal.find_object(material, name)
    if e is None:
        raise ToolError("material.expression_not_found", "no expression '%s' in %s" % (name, material.get_name()),
                        hint="names are returned by material_node_add")
    return e


@tool("material_create", "material", "Create an empty Material asset.", {"path": {"type": "string"}}, ["path"],
      mutating=True, undoable=False)
def material_create(path):
    m = create_asset(path, unreal.Material, unreal.MaterialFactoryNew())
    return {"path": m.get_path_name()}


@tool("material_node_add", "material", "Add a material expression (e.g. Constant3Vector, ScalarParameter, Multiply) "
      "with optional properties {name: value}.",
      {"path": {"type": "string"}, "type": {"type": "string"}, "position": {"type": "array"},
       "properties": {"type": "object"}}, ["path", "type"], mutating=True)
def material_node_add(path, type, position=None, properties=None):  # noqa: A002
    m = load_asset_or_fail(path, unreal.Material)
    x, y = (position or [-400, 0])[:2]
    e = MEL.create_material_expression(m, _expr_class(type), int(x), int(y))
    for k, v in (properties or {}).items():
        e.set_editor_property(k, from_json(v, e.get_editor_property(k)))
    return {"name": e.get_name(), "class": e.get_class().get_name()}


@tool("material_connect", "material", "Connect an expression output to another expression input, or to a material "
      "property (BaseColor, Metallic, Roughness, EmissiveColor, Normal, Opacity, ...).",
      {"path": {"type": "string"}, "from": {"type": "string"}, "fromOutput": {"type": "string"},
       "to": {"type": "string"}, "toInput": {"type": "string"}, "property": {"type": "string"}},
      ["path", "from"], mutating=True)
def material_connect(path, fromOutput="", to=None, toInput="", property=None, **kw):  # noqa: A002
    m = load_asset_or_fail(path, unreal.Material)
    src = _expr(m, kw["from"])
    if property:
        enum_name = "MP_" + "".join("_" + c if c.isupper() else c for c in property).lstrip("_").upper()
        prop = getattr(unreal.MaterialProperty, enum_name, None)
        if prop is None:
            raise ToolError("material.bad_property", "unknown material property '%s'" % property)
        ok = MEL.connect_material_property(src, fromOutput, prop)
    elif to:
        ok = MEL.connect_material_expressions(src, fromOutput, _expr(m, to), toInput)
    else:
        raise ToolError("args.invalid", "pass 'to' (expression) or 'property' (material input)")
    if not ok:
        raise ToolError("material.connect_failed", "connection refused (check output/input names)")
    MEL.recompile_material(m)
    return {"connected": True}


@tool("material_recompile", "material", "Recompile a material and report its expression count.",
      {"path": {"type": "string"}}, ["path"])
def material_recompile(path):
    m = load_asset_or_fail(path, unreal.Material)
    MEL.recompile_material(m)
    return {"path": path, "expressions": MEL.get_num_material_expressions(m)}


@tool("mi_create", "material", "Create a Material Instance of a parent material.",
      {"path": {"type": "string"}, "parent": {"type": "string"}}, ["path", "parent"], mutating=True, undoable=False)
def mi_create(path, parent):
    p = load_asset_or_fail(parent, unreal.MaterialInterface)
    mi = create_asset(path, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, p)
    return {"path": mi.get_path_name(), "parent": p.get_path_name()}


@tool("mi_set_param", "material", "Set a scalar, vector [r,g,b,a] or texture (asset path) parameter on a "
      "Material Instance.",
      {"path": {"type": "string"}, "name": {"type": "string"}, "value": {},
       "kind": {"type": "string", "enum": ["scalar", "vector", "texture"]}}, ["path", "name", "value"],
      mutating=True)
def mi_set_param(path, name, value, kind=None):
    mi = load_asset_or_fail(path, unreal.MaterialInstanceConstant)
    MEL.update_material_instance(mi)
    if kind is None:
        kind = "scalar" if isinstance(value, (int, float)) else ("texture" if isinstance(value, str) else "vector")
    names = {"scalar": MEL.get_scalar_parameter_names, "vector": MEL.get_vector_parameter_names,
             "texture": MEL.get_texture_parameter_names}[kind](mi)
    if name not in [str(n) for n in names]:
        raise ToolError("material.param_not_found", "the parent has no %s parameter '%s'" % (kind, name),
                        hint="parameters: " + ", ".join(str(n) for n in names))
    if kind == "scalar":
        MEL.set_material_instance_scalar_parameter_value(mi, name, float(value))
        after = MEL.get_material_instance_scalar_parameter_value(mi, name)
    elif kind == "vector":
        MEL.set_material_instance_vector_parameter_value(mi, name, from_json(value, unreal.LinearColor()))
        after = MEL.get_material_instance_vector_parameter_value(mi, name)
    else:
        MEL.set_material_instance_texture_parameter_value(mi, name, load_asset_or_fail(value, unreal.Texture))
        after = MEL.get_material_instance_texture_parameter_value(mi, name)
    MEL.update_material_instance(mi)
    return {"path": path, "name": name, "kind": kind, "value": to_json(after)}
