import os
import subprocess
import sys

import unreal

from .. import ToolError, tool


def _uat():
    engine = os.path.abspath(unreal.Paths.engine_dir())
    name = "RunUAT.bat" if sys.platform == "win32" else "RunUAT.sh"
    return os.path.join(engine, "Build", "BatchFiles", name)


def _platform():
    return {"win32": "Win64", "darwin": "Mac"}.get(sys.platform, "Linux")


def _start(args, log_name):
    project = os.path.abspath(unreal.Paths.get_project_file_path())
    log_dir = os.path.join(os.path.abspath(unreal.Paths.project_saved_dir()), "UnrealCli")
    os.makedirs(log_dir, exist_ok=True)
    log = os.path.join(log_dir, log_name)
    cmd = [_uat(), "BuildCookRun", "-project=" + project, "-noP4", "-unattended", "-utf8output"] + args
    with open(log, "w", encoding="utf-8") as out:
        proc = subprocess.Popen(cmd, stdout=out, stderr=subprocess.STDOUT, cwd=os.path.dirname(project))
    return {"pid": proc.pid, "log": log, "command": " ".join(cmd)}


@tool("package_project", "packaging", "Start BuildCookRun to package the project; returns the process id and log "
      "file (poll with package_status).",
      {"outputDir": {"type": "string"}, "configuration": {"type": "string", "enum": ["Development", "Shipping", "DebugGame"]},
       "platform": {"type": "string"}}, ["outputDir"], mutating=True, undoable=False)
def package_project(outputDir, configuration="Development", platform=None):
    plat = platform or _platform()
    return _start(["-platform=" + plat, "-clientconfig=" + configuration, "-build", "-cook", "-stage", "-pak",
                   "-archive", "-archivedirectory=" + os.path.abspath(outputDir)], "package.log")


@tool("cook_content", "packaging", "Start a content cook for a platform; returns the process id and log file.",
      {"platform": {"type": "string"}}, mutating=True, undoable=False)
def cook_content(platform=None):
    return _start(["-platform=" + (platform or _platform()), "-cook", "-skipstage"], "cook.log")


@tool("package_status", "packaging", "State of a package/cook run from its log file (running, succeeded, failed).",
      {"log": {"type": "string"}, "tail": {"type": "integer"}}, ["log"])
def package_status(log, tail=20):
    if not os.path.isfile(log):
        raise ToolError("packaging.no_log", "no log at '%s'" % log)
    with open(log, encoding="utf-8", errors="replace") as f:
        lines = f.read().splitlines()
    text = "\n".join(lines[-200:])
    state = "running"
    if "BUILD SUCCESSFUL" in text:
        state = "succeeded"
    elif "BUILD FAILED" in text or "AutomationTool exiting with ExitCode=" in text and "ExitCode=0 " not in text:
        state = "failed"
    errors = [l for l in lines if "Error:" in l or "error " in l][-10:]
    return {"state": state, "errors": errors, "tail": lines[-tail:]}
