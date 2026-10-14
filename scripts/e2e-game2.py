"""Game E2E 2: a third-person shooter built with unrealengine-cli tool calls only (Enhanced Input with a mapping
context and modifiers, character movement, projectiles, damage, game mode), then play-tested.
Needs a rendering editor on TestProject. Usage: python scripts/e2e-game2.py (from the repository root)."""
import json
import math
import subprocess
import sys
import time

CLI = ["./target/debug/unrealengine-cli", "--project", "TestProject", "--output", "json"]
ROOT = "/Game/CoinGame/S%d" % int(time.time())


def run(*a):
    p = subprocess.run(CLI + list(a), capture_output=True, text=True, encoding="utf-8")
    v = json.loads(p.stdout)
    if not v["ok"]:
        print("FAIL", a, json.dumps(v["error"], ensure_ascii=False)[:800])
        sys.exit(1)
    return v["data"]


def tool(tool_name, **args):
    return run("tool", "call", tool_name, "--args", json.dumps(args))


def node(bp, ty, member=None, graph="EventGraph", **kw):
    args = {"path": bp, "type": ty, "graph": graph, **kw}
    if member:
        args["memberName"] = member
    return tool("bp_add_node", **args)


def link(bp, a, ap, b, bpin, graph="EventGraph"):
    tool("bp_connect", path=bp, graph=graph, **{"from": f"{a['id']}.{ap}", "to": f"{b['id']}.{bpin}"})


def pin(bp, n, name, value, graph="EventGraph"):
    tool("bp_set_pin", path=bp, graph=graph, node=n["id"], pin=name, value=value)


def compile_ok(bp):
    r = run("blueprint", "compile", bp)
    if r["errors"]:
        print("COMPILE ERRORS", bp, json.dumps(r, ensure_ascii=False)[:2000])
        sys.exit(1)
    run("blueprint", "save", bp)


def cls(path):
    return path + "." + path.split("/")[-1] + "_C"


p = lambda n: f"{ROOT}/{n}"
IA_MOVE, IA_FIRE, IMC = p("IA_Move"), p("IA_Fire"), p("IMC_Shooter")
HERO, BULLET, TARGET, GM, LEVEL = p("BP_Hero"), p("BP_Bullet"), p("BP_Target"), p("BP_ShooterGM"), p("L_Shooter")

# --- Input --------------------------------------------------------------------------------------
tool("input_action_create", path=IA_MOVE, valueType="Axis2D")
tool("input_action_create", path=IA_FIRE, valueType="Boolean")
tool("mapping_context_create", path=IMC)
tool("mapping_add", context=IMC, action=IA_MOVE, key="W", modifiers=["Swizzle"])
tool("mapping_add", context=IMC, action=IA_MOVE, key="S", modifiers=["Swizzle", "Negate"])
tool("mapping_add", context=IMC, action=IA_MOVE, key="D")
m = tool("mapping_add", context=IMC, action=IA_MOVE, key="A", modifiers=["Negate"])
assert m["modifiers"] == ["InputModifierNegate"], m
tool("mapping_add", context=IMC, action=IA_FIRE, key="SpaceBar", triggers=["Pressed"])
run("asset", "save", IMC)

# --- Bullet: sphere root + projectile movement; on stop -> damage what it hit, then destroy -------
run("blueprint", "create", BULLET, "--parent", "Actor")
tool("bp_add_component", path=BULLET, **{"class": "SphereComponent"}, name="Ball")
tool("bp_add_component", path=BULLET, **{"class": "ProjectileMovementComponent"}, name="Move")
compile_ok(BULLET)
run("prop", "set", BULLET + ".Ball", "CollisionProfileName", "BlockAllDynamic")
run("prop", "set", BULLET + ".Move", "InitialSpeed", "3000")
run("prop", "set", BULLET + ".Move", "ProjectileGravityScale", "0")
stop = node(BULLET, "ComponentEvent", "Move", delegate="OnProjectileStop")
brk = node(BULLET, "BreakStruct", "HitResult")
link(BULLET, stop, "ImpactResult", brk, "Hit")
dmg = node(BULLET, "CallFunction", "ApplyDamage")
link(BULLET, stop, "then", dmg, "execute")
link(BULLET, brk, "HitActor", dmg, "DamagedActor")
pin(BULLET, dmg, "BaseDamage", "50")
die = node(BULLET, "CallFunction", "K2_DestroyActor")
link(BULLET, dmg, "then", die, "execute")
compile_ok(BULLET)

# --- Target: health, AnyDamage -> subtract -> destroy at 0 ---------------------------------------
run("blueprint", "create", TARGET, "--parent", "Actor")
tool("bp_add_component", path=TARGET, **{"class": "BoxComponent"}, name="Hitbox")
tool("bp_add_variable", path=TARGET, name="Health", type="float", defaultValue="100")
compile_ok(TARGET)
run("prop", "set", TARGET + ".Hitbox", "BoxExtent", "[100, 100, 100]")
run("prop", "set", TARGET + ".Hitbox", "CollisionProfileName", "BlockAllDynamic")
hit = node(TARGET, "Event", "AnyDamage")
get_h = node(TARGET, "VariableGet", "Health")
sub = node(TARGET, "CallFunction", "Subtract_DoubleDouble")
link(TARGET, get_h, "Health", sub, "A")
link(TARGET, hit, "Damage", sub, "B")
set_h = node(TARGET, "VariableSet", "Health")
link(TARGET, hit, "then", set_h, "execute")
link(TARGET, sub, "ReturnValue", set_h, "Health")
le = node(TARGET, "CallFunction", "LessEqual_DoubleDouble")
link(TARGET, set_h, "Health", le, "A")
br = node(TARGET, "Branch")
link(TARGET, set_h, "then", br, "execute")
link(TARGET, le, "ReturnValue", br, "Condition")
say = node(TARGET, "CallFunction", "PrintString")
pin(TARGET, say, "InString", "TARGET_DOWN")
link(TARGET, br, "then", say, "execute")
kill = node(TARGET, "CallFunction", "K2_DestroyActor")
link(TARGET, say, "then", kill, "execute")
compile_ok(TARGET)

# --- Hero: mapping context at BeginPlay, move with Axis2D, fire spawns a bullet -------------------
run("blueprint", "create", HERO, "--parent", "Character")
begin = node(HERO, "Event", "BeginPlay")
pc = node(HERO, "CallFunction", "GetPlayerController")
sub_node = node(HERO, "Node", "K2Node_GetSubsystemFromPC",
                properties={"CustomClass": "/Script/EnhancedInput.EnhancedInputLocalPlayerSubsystem"})
link(HERO, pc, "ReturnValue", sub_node, "PlayerController")
addctx = node(HERO, "CallFunction", "AddMappingContext", memberParent="EnhancedInputLocalPlayerSubsystem")
link(HERO, begin, "then", addctx, "execute")
link(HERO, sub_node, "ReturnValue", addctx, "self")
pin(HERO, addctx, "MappingContext", IMC)
move = node(HERO, "InputAction", IA_MOVE)
v2 = node(HERO, "BreakStruct", "Vector2D")
link(HERO, move, "ActionValue", v2, "InVec")
fwd = node(HERO, "CallFunction", "GetActorForwardVector")
right = node(HERO, "CallFunction", "GetActorRightVector")
mv1 = node(HERO, "CallFunction", "AddMovementInput")
link(HERO, move, "Triggered", mv1, "execute")
link(HERO, fwd, "ReturnValue", mv1, "WorldDirection")
link(HERO, v2, "Y", mv1, "ScaleValue")
mv2 = node(HERO, "CallFunction", "AddMovementInput")
link(HERO, mv1, "then", mv2, "execute")
link(HERO, right, "ReturnValue", mv2, "WorldDirection")
link(HERO, v2, "X", mv2, "ScaleValue")
fire = node(HERO, "InputAction", IA_FIRE)
spawn = node(HERO, "SpawnActor", BULLET)
link(HERO, fire, "Started", spawn, "execute")
loc = node(HERO, "CallFunction", "K2_GetActorLocation")
fwd2 = node(HERO, "CallFunction", "GetActorForwardVector")
scale = node(HERO, "CallFunction", "Multiply_VectorFloat")
link(HERO, fwd2, "ReturnValue", scale, "A")
pin(HERO, scale, "B", "150")
add = node(HERO, "CallFunction", "Add_VectorVector")
link(HERO, loc, "ReturnValue", add, "A")
link(HERO, scale, "ReturnValue", add, "B")
rot = node(HERO, "CallFunction", "K2_GetActorRotation")
xf = node(HERO, "CallFunction", "MakeTransform")
link(HERO, add, "ReturnValue", xf, "Location")
link(HERO, rot, "ReturnValue", xf, "Rotation")
link(HERO, xf, "ReturnValue", spawn, "SpawnTransform")
compile_ok(HERO)

# --- Game mode and level -------------------------------------------------------------------------
run("blueprint", "create", GM, "--parent", "GameModeBase")
compile_ok(GM)
run("prop", "set", GM, "DefaultPawnClass", cls(HERO))
compile_ok(GM)
run("level", "new", LEVEL, "--template", "/Engine/Maps/Templates/Template_Default")
s = tool("actor_list", classFilter="PlayerStart")[0]["location"]
tool("actor_spawn", actorClass=TARGET, location=[s["x"] + 900, s["y"], s["z"]], label="Target1")
run("prop", "set", "WorldSettings", "DefaultGameMode", cls(GM))
run("level", "save")

# --- Play-test -----------------------------------------------------------------------------------
run("pie", "start")
before = run("pie", "get", "player")
run("pie", "input", "W", "--seconds", "0.6")
run("pie", "wait", "--seconds", "0.5")
after = run("pie", "get", "player")
for _ in range(3):
    run("pie", "input", "SpaceBar", "--seconds", "0.1")
    run("pie", "wait", "--seconds", "0.6")
down = run("pie", "wait", "--seconds", "2", "--until-log", "TARGET_DOWN")
targets = [a for a in run("pie", "actors", "--class", "BP_Target") if a["class"] == "BP_Target_C"]
shot = run("pie", "screenshot")
run("pie", "stop")

loc = lambda r: [r["location"][k] for k in "xyz"]
report = {
    "pawnClass": after["class"],
    "forwardMoved": round(after["location"]["x"] - before["location"]["x"]),
    "targetDownLog": down["endedReason"] == "log",
    "targetsLeft": len(targets),
    "screenshotBlank": shot["blank"],
}
print(json.dumps(report, indent=1))
checks = {
    "player is the hero": report["pawnClass"] == "BP_Hero_C",
    "W moves forward (mapping context + Swizzle)": report["forwardMoved"] > 100,
    "bullets destroyed the target": report["targetsLeft"] == 0,
    "screenshot not blank": not report["screenshotBlank"],
}
failed = [k for k, v in checks.items() if not v]
print("FAILED: " + ", ".join(failed) if failed else "ALL CHECKS PASSED")
sys.exit(1 if failed else 0)
