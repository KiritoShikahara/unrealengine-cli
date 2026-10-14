"""Python-side tool registry for UnrealCliBridge.

Each tool is a function decorated with @tool. The C++ bridge reads the catalog
with emit_catalog() and runs a tool with invoke(); both print one marker line
with a JSON payload.
"""
import base64
import json
import traceback

MARKER = "@@UCLI@@"
_TOOLS = {}


class ToolError(Exception):
    def __init__(self, code, message, hint=None, details=None):
        super().__init__(message)
        self.code = code
        self.message = message
        self.hint = hint
        self.details = details


def tool(name, group, summary, properties=None, required=None, mutating=False,
         undoable=True, long_running=False, description=None):
    schema = {"type": "object", "properties": properties or {}}
    if required:
        schema["required"] = list(required)

    def wrap(fn):
        _TOOLS[name] = {
            "fn": fn,
            "meta": {
                "name": name,
                "group": group,
                "summary": summary,
                "description": description or (fn.__doc__ or summary).strip(),
                "inputSchema": schema,
                "mutating": mutating,
                "undoable": mutating and undoable,
                "longRunning": long_running,
            },
        }
        return fn

    return wrap


def _emit(payload):
    print(MARKER + json.dumps(payload, ensure_ascii=True, default=str))


def emit_catalog():
    _emit([t["meta"] for t in _TOOLS.values()])


def invoke(name, args_b64):
    try:
        args = json.loads(base64.b64decode(args_b64).decode("utf-8")) if args_b64 else {}
        entry = _TOOLS.get(name)
        if entry is None:
            raise ToolError("tool.unknown", "unknown python tool '%s'" % name)
        _emit({"ok": True, "data": entry["fn"](**args)})
    except ToolError as e:
        err = {"code": e.code, "message": e.message}
        if e.hint:
            err["hint"] = e.hint
        if e.details is not None:
            err["details"] = e.details
        _emit({"ok": False, "error": err})
    except TypeError as e:
        _emit({"ok": False, "error": {"code": "args.invalid", "message": str(e)}})
    except Exception as e:  # noqa: BLE001 - every failure must come back structured
        _emit({"ok": False, "error": {"code": "python.exception", "message": str(e),
                                      "details": traceback.format_exc()}})


from . import tools  # noqa: E402,F401  (registers all tool modules)
