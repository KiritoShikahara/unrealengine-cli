"""C++ E2E: create a UCLASS in TestProject, apply it (rebuild + reopen), use it from a Blueprint in PIE, then change
the .cpp and apply it with Live Coding. Restores TestProject afterwards. Needs a rendering editor (PIE).
Usage: python scripts/e2e-cpp.py (from the repository root)."""
import json
import os
import subprocess
import sys
import time

CLI = ["./target/debug/unrealengine-cli", "--project", "TestProject", "--output", "json"]
ROOT = "/Game/E2E/C%d" % int(time.time())
CLASS = "CliScoreKeeper"


def run(*a, timeout_ms=None):
    extra = ["--timeout-ms", str(timeout_ms)] if timeout_ms else []
    p = subprocess.run(CLI + extra + list(a), capture_output=True, text=True, encoding="utf-8")
    v = json.loads(p.stdout)
    if not v["ok"]:
        raise RuntimeError("%s -> %s" % (a, json.dumps(v["error"], ensure_ascii=False)[:1500]))
    return v["data"]


created = []
results = {}
try:
    made = run("code", "class-create", CLASS, "--parent", "Actor")
    created = [made["header"], made["source"]]
    header, source = made["header"], made["source"]
    run("code", "edit", header, "--old", "GENERATED_BODY()",
        "--new", "GENERATED_BODY()\n\npublic:\n\tUPROPERTY(BlueprintReadOnly, Category = \"Score\")\n\tint32 Score = 0;\n\n"
                 "\tUFUNCTION(BlueprintCallable, Category = \"Score\")\n\tvoid AddPoints(int32 Points);")
    with open(os.path.join("TestProject", source), "a", encoding="utf-8") as f:
        f.write("\nvoid A%s::AddPoints(int32 Points)\n{\n\tScore += Points;\n}\n" % CLASS)
    plan = run("code", "apply", "--plan")
    results["planAfterHeader"] = plan["action"]
    applied = run("code", "apply", "--force", timeout_ms=1800000)
    results["firstApply"] = applied["action"]

    bp = ROOT + "/BP_ScoreKeeper"
    run("blueprint", "create", bp, "--parent", "/Script/TestProject." + CLASS)
    run("blueprint", "compile", bp)
    run("blueprint", "save", bp)
    run("level", "new", ROOT + "/L_Cpp", "--template", "/Engine/Maps/Templates/Template_Default")
    run("tool", "call", "actor_spawn", "--args", json.dumps({"actorClass": bp, "location": [0, 0, 200], "label": "Keeper"}))

    def points_after_call():
        run("pie", "start")
        run("pie", "call", "Keeper", "AddPoints 5")
        score = run("pie", "get", "Keeper", "Score")["properties"]["Score"]
        run("pie", "stop")
        return score

    results["scoreBeforeLiveCoding"] = points_after_call()

    with open(os.path.join("TestProject", source), encoding="utf-8") as f:
        text = f.read()
    with open(os.path.join("TestProject", source), "w", encoding="utf-8") as f:
        f.write(text.replace("Score += Points;", "Score += Points * 2;"))
    results["planAfterCpp"] = run("code", "apply", "--plan")["action"]
    live = run("code", "apply", "--force", timeout_ms=1800000)
    results["liveApply"] = live["action"]
    if live.get("liveError"):
        results["liveCodingError"] = live["liveError"]
    results["scoreAfterLiveCoding"] = points_after_call()
except RuntimeError as e:
    results["error"] = str(e)
finally:
    for rel in created:
        path = os.path.join("TestProject", rel)
        if os.path.exists(path):
            os.remove(path)
    if created:
        try:
            run("code", "apply", "--rebuild", "--force", timeout_ms=1800000)
        except RuntimeError as e:
            results["cleanupError"] = str(e)

print(json.dumps(results, indent=1, ensure_ascii=False))
checks = {
    "header change needs a rebuild": results.get("planAfterHeader") == "rebuild",
    "rebuild applied": results.get("firstApply") == "rebuild",
    "Blueprint child calls the C++ function": results.get("scoreBeforeLiveCoding") == "5",
    ".cpp change uses Live Coding": results.get("planAfterCpp") == "live",
    ".cpp change applied (Live Coding, or rebuild when it is stuck)": results.get("scoreAfterLiveCoding") == "10",
    "TestProject restored": "cleanupError" not in results,
}
failed = [k for k, v in checks.items() if not v]
print("FAILED: " + ", ".join(failed) if failed else "ALL CHECKS PASSED")
sys.exit(1 if failed else 0)
