import json

import unreal

from .. import ToolError, tool
from ..util import create_asset, load_asset_or_fail, load_class


@tool("input_action_create", "input", "Create an Enhanced Input Action (valueType: Boolean|Axis1D|Axis2D|Axis3D).",
      {"path": {"type": "string"}, "valueType": {"type": "string", "enum": ["Boolean", "Axis1D", "Axis2D", "Axis3D"]}},
      ["path"], mutating=True, undoable=False)
def input_action_create(path, valueType="Boolean"):
    a = create_asset(path, unreal.InputAction, None)
    a.set_editor_property("value_type", getattr(unreal.InputActionValueType, valueType.upper()))
    unreal.EditorAssetLibrary.save_loaded_asset(a, False)
    return {"path": a.get_path_name(), "valueType": valueType}


@tool("mapping_context_create", "input", "Create an Enhanced Input Mapping Context.", {"path": {"type": "string"}},
      ["path"], mutating=True, undoable=False)
def mapping_context_create(path):
    c = create_asset(path, unreal.InputMappingContext, None)
    return {"path": c.get_path_name()}


@tool("mapping_add", "input", "Map a key (SpaceBar, W, Gamepad_FaceButton_Bottom, ...) to an action in a mapping "
      "context.", {"context": {"type": "string"}, "action": {"type": "string"}, "key": {"type": "string"}},
      ["context", "action", "key"], mutating=True)
def mapping_add(context, action, key):
    ctx = load_asset_or_fail(context, unreal.InputMappingContext)
    act = load_asset_or_fail(action, unreal.InputAction)
    k = unreal.Key()
    k.set_editor_property("key_name", key)
    if not unreal.InputLibrary.key_is_valid(k):
        raise ToolError("input.bad_key", "unknown key '%s'" % key, hint="key names: SpaceBar, W, LeftMouseButton, Gamepad_FaceButton_Bottom")
    ctx.modify()
    ctx.map_key(act, k)
    try:
        count = len(ctx.get_editor_property("default_key_mappings").get_editor_property("mappings"))
    except Exception:  # noqa: BLE001 - property layout differs between engine versions
        count = len(ctx.get_editor_property("mappings"))
    return {"context": context, "action": action, "key": key, "mappings": count}


def _struct(path):
    s = unreal.load_object(None, path)
    if s is None:
        raise ToolError("data.struct_not_found", "no struct at '%s'" % path,
                        hint="use a /Script/Module.Struct path or a User Defined Struct asset")
    return s


@tool("datatable_create", "data", "Create a Data Table for a row struct (path of a UserDefinedStruct or /Script "
      "struct).", {"path": {"type": "string"}, "rowStruct": {"type": "string"}}, ["path", "rowStruct"],
      mutating=True, undoable=False)
def datatable_create(path, rowStruct):
    f = unreal.DataTableFactory()
    f.set_editor_property("struct", _struct(rowStruct))
    dt = create_asset(path, unreal.DataTable, f)
    return {"path": dt.get_path_name()}


@tool("datatable_rows_get", "data", "Rows of a Data Table as JSON.", {"path": {"type": "string"}}, ["path"])
def datatable_rows_get(path):
    dt = load_asset_or_fail(path, unreal.DataTable)
    text = unreal.DataTableFunctionLibrary.export_data_table_to_json_string(dt)
    return {"path": path, "rows": json.loads(text) if text else []}


@tool("datatable_rows_set", "data", "Replace a Data Table's rows from a JSON array of objects (each with Name).",
      {"path": {"type": "string"}, "rows": {"type": "array"}}, ["path", "rows"], mutating=True)
def datatable_rows_set(path, rows):
    dt = load_asset_or_fail(path, unreal.DataTable)
    if not all(isinstance(r, dict) and "Name" in r for r in rows):
        raise ToolError("args.invalid", "every row needs a 'Name' field")
    dt.modify()
    if not unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(dt, json.dumps(rows)):
        raise ToolError("data.import_failed", "rows do not match the row struct", hint="check datatable_rows_get for the shape")
    return {"path": path, "rows": len(unreal.DataTableFunctionLibrary.get_data_table_row_names(dt))}


@tool("data_asset_create", "data", "Create a Data Asset of a UDataAsset subclass (class name, /Script or Blueprint "
      "path).", {"path": {"type": "string"}, "class": {"type": "string"}}, ["path", "class"], mutating=True,
      undoable=False)
def data_asset_create(path, **kw):
    cls = load_class(kw["class"])
    try:
        unreal.new_object(cls)
    except Exception:
        raise ToolError("data.abstract_class", "'%s' is abstract; data assets need a concrete class" % kw["class"],
                        "make one: unrealengine-cli blueprint create <path> --parent %s, then pass its path" % kw["class"])
    f = unreal.DataAssetFactory()
    f.set_editor_property("data_asset_class", cls)
    a = create_asset(path, cls, f)
    return {"path": a.get_path_name(), "class": cls.get_name()}
