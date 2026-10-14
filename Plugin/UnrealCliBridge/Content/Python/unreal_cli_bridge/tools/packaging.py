import os
import subprocess
import sys

import unreal

from .. import ToolError, tool


def _full(path):
    """Absolute path for an engine-relative path (the editor's working directory is the engine's Binaries)."""
    return os.path.normpath(unreal.Paths.convert_relative_path_to_full(path))


def _project_dir():
    return os.path.dirname(_full(unreal.Paths.get_project_file_path()))


def _output_dir(output_dir):
    """Archive directory: relative paths are under the project; never empty or inside the engine install."""
    if not output_dir or not str(output_dir).strip():
        raise ToolError("args.invalid", "outputDir is empty", "pass a folder, e.g. Build/Windows (relative to the project)")
    path = output_dir if os.path.isabs(output_dir) else os.path.join(_project_dir(), output_dir)
    path = os.path.normpath(path)
    # Refuse any Unreal Engine install (not just the running one) and the project's Content folder.
    probe = path
    while True:
        if os.path.isfile(os.path.join(probe, "Engine", "Build", "BatchFiles", "RunUAT.bat")) or \
                os.path.isfile(os.path.join(probe, "Build", "BatchFiles", "RunUAT.bat")):
            raise ToolError("package.bad_output_dir", "'%s' is inside an Unreal Engine install (%s)" % (path, probe),
                            "choose a folder outside the engine, e.g. Build/Windows (relative to the project)")
        parent = os.path.dirname(probe)
        if parent == probe:
            break
        probe = parent
    content = os.path.join(_project_dir(), "Content")
    if os.path.normcase(path + os.sep).startswith(os.path.normcase(content + os.sep)):
        raise ToolError("package.bad_output_dir", "'%s' is inside the project's Content folder" % path,
                        "choose a folder outside Content, e.g. Build/Windows (relative to the project)")
    return path


def _uat():
    engine = _full(unreal.Paths.engine_dir())
    name = "RunUAT.bat" if sys.platform == "win32" else "RunUAT.sh"
    return os.path.join(engine, "Build", "BatchFiles", name)


def _platform():
    return {"win32": "Win64", "darwin": "Mac"}.get(sys.platform, "Linux")


def _start(args, log_name):
    project = _full(unreal.Paths.get_project_file_path())
    log_dir = os.path.join(_full(unreal.Paths.project_saved_dir()), "UnrealCli")
    os.makedirs(log_dir, exist_ok=True)
    log = os.path.join(log_dir, log_name)
    cmd = [_uat(), "BuildCookRun", "-project=" + project, "-noP4", "-unattended", "-utf8output"] + args
    with open(log, "w", encoding="utf-8") as out:
        proc = subprocess.Popen(cmd, stdout=out, stderr=subprocess.STDOUT, cwd=os.path.dirname(project))
    return {"pid": proc.pid, "log": log, "command": " ".join(cmd)}


@tool("package_project", "packaging", "Start BuildCookRun to package the project into outputDir (relative paths "
      "are under the project, e.g. Build/Windows; never an engine install or Content); returns the process id and log "
      "file (poll with package_status). check=true only validates outputDir.",
      {"outputDir": {"type": "string"}, "configuration": {"type": "string", "enum": ["Development", "Shipping", "DebugGame"]},
       "platform": {"type": "string"}, "check": {"type": "boolean"}}, ["outputDir"], mutating=True, undoable=False)
def package_project(outputDir, configuration="Development", platform=None, check=False):
    plat = platform or _platform()
    archive = _output_dir(outputDir)
    if check:
        # Validate only: report where the build would go without starting it.
        return {"outputDir": archive, "platform": plat, "configuration": configuration, "started": False}
    result = _start(["-platform=" + plat, "-clientconfig=" + configuration, "-build", "-cook", "-stage", "-pak",
                     "-archive", "-archivedirectory=" + archive], "package.log")
    result["outputDir"] = archive
    return result


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
