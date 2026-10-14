import unreal

from .. import tool
from ..util import create_asset


@tool("niagara_system_create", "niagara", "Create an empty Niagara System asset.", {"path": {"type": "string"}},
      ["path"], mutating=True, undoable=False)
def niagara_system_create(path):
    s = create_asset(path, unreal.NiagaraSystem, unreal.NiagaraSystemFactoryNew())
    return {"path": s.get_path_name()}
